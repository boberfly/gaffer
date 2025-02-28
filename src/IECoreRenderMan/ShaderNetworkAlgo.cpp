//////////////////////////////////////////////////////////////////////////
//
//  Copyright (c) 2024, Cinesite VFX Ltd. All rights reserved.
//
//  Redistribution and use in source and binary forms, with or without
//  modification, are permitted provided that the following conditions are
//  met:
//
//      * Redistributions of source code must retain the above
//        copyright notice, this list of conditions and the following
//        disclaimer.
//
//      * Redistributions in binary form must reproduce the above
//        copyright notice, this list of conditions and the following
//        disclaimer in the documentation and/or other materials provided with
//        the distribution.
//
//      * Neither the name of John Haddon nor the names of
//        any other contributors to this software may be used to endorse or
//        promote products derived from this software without specific prior
//        written permission.
//
//  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
//  IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
//  THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
//  PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
//  CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
//  EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
//  PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
//  PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
//  LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
//  NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
//  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
//////////////////////////////////////////////////////////////////////////

#include "ShaderNetworkAlgo.h"

#include "ParamListAlgo.h"

#include "IECore/DataAlgo.h"
#include "IECore/LRUCache.h"
#include "IECore/MessageHandler.h"
#include "IECore/SearchPath.h"
#include "IECore/SimpleTypedData.h"
#include "IECore/VectorTypedData.h"

#include "IECoreScene/ShaderNetworkAlgo.h"

#include "IECoreMaterialX/ShaderNetworkAlgo.h"

#include "OSL/oslquery.h"

#include "boost/algorithm/string.hpp"
#include "boost/algorithm/string/predicate.hpp"
#include "boost/container/flat_map.hpp"
#include "boost/property_tree/xml_parser.hpp"

#include "fmt/format.h"

#include <unordered_set>

using namespace std;
using namespace Imath;
using namespace IECore;
using namespace IECoreScene;
using namespace IECoreRenderMan;

//////////////////////////////////////////////////////////////////////////
// Internal utilities
//////////////////////////////////////////////////////////////////////////

namespace
{

struct ShaderInfo
{
	riley::ShadingNode::Type type = riley::ShadingNode::Type::k_Invalid;
	using ParameterTypeMap = std::unordered_map<InternedString, pxrcore::DataType>;
	ParameterTypeMap parameterTypes;
};

using ConstShaderInfoPtr = std::shared_ptr<const ShaderInfo>;

void loadParameterTypes( const boost::property_tree::ptree &tree, ShaderInfo::ParameterTypeMap &typeMap )
{
	for( const auto &child : tree )
	{
		if( child.first == "param" )
		{
			const string name = child.second.get<string>( "<xmlattr>.name" );
			const string type = child.second.get<string>( "<xmlattr>.type" );
			if( type == "int" )
			{
				typeMap[name] = pxrcore::DataType::k_integer;
			}
			else if( type == "float" )
			{
				typeMap[name] = pxrcore::DataType::k_float;
			}
			else if( type == "color" )
			{
				typeMap[name] = pxrcore::DataType::k_color;
			}
			else if( type == "point" )
			{
				typeMap[name] = pxrcore::DataType::k_point;
			}
			else if( type == "vector" )
			{
				typeMap[name] = pxrcore::DataType::k_vector;
			}
			else if( type == "normal" )
			{
				typeMap[name] = pxrcore::DataType::k_normal;
			}
			else if( type == "matrix" )
			{
				typeMap[name] = pxrcore::DataType::k_matrix;
			}
			else if( type == "string" )
			{
				typeMap[name] = pxrcore::DataType::k_string;
			}
			else if( type == "bxdf" )
			{
				typeMap[name] = pxrcore::DataType::k_bxdf;
			}
			else if( type == "lightfilter" )
			{
				typeMap[name] = pxrcore::DataType::k_lightfilter;
			}
			else if( type == "samplefilter" )
			{
				typeMap[name] = pxrcore::DataType::k_samplefilter;
			}
			else if( type == "displayfilter" )
			{
				typeMap[name] = pxrcore::DataType::k_displayfilter;
			}
			else if( type == "struct" )
			{
				typeMap[name] = pxrcore::DataType::k_struct;
			}
			else
			{
				IECore::msg( IECore::Msg::Warning, "IECoreRenderMan", fmt::format( "Unknown type `{}` for parameter \"{}\".", type, name ) );
			}
		}
		else if( child.first == "page" )
		{
			loadParameterTypes( child.second, typeMap );
		}
	}
}

ConstShaderInfoPtr shaderInfoFromArgsFile( const boost::filesystem::path file )
{
	std::ifstream argsStream( file.string() );

	boost::property_tree::ptree tree;
	boost::property_tree::read_xml( argsStream, tree );

	auto result = std::make_shared<ShaderInfo>();

	// Get type

	const string shaderType = tree.get<string>( "args.shaderType.tag.<xmlattr>.value" );
	if( shaderType == "pattern" )
	{
		result->type = riley::ShadingNode::Type::k_Pattern;
	}
	else if( shaderType == "bxdf" )
	{
		result->type = riley::ShadingNode::Type::k_Bxdf;
	}
	else if( shaderType == "integrator" )
	{
		result->type = riley::ShadingNode::Type::k_Integrator;
	}
	else if( shaderType == "light" )
	{
		result->type = riley::ShadingNode::Type::k_Light;
	}
	else if( shaderType == "lightfilter" )
	{
		result->type = riley::ShadingNode::Type::k_LightFilter;
	}
	else if( shaderType == "projection" )
	{
		result->type = riley::ShadingNode::Type::k_Projection;
	}
	else if( shaderType == "displacement" )
	{
		result->type = riley::ShadingNode::Type::k_Displacement;
	}
	else if( shaderType == "samplefilter" )
	{
		result->type = riley::ShadingNode::Type::k_SampleFilter;
	}
	else if( shaderType == "displayfilter" )
	{
		result->type = riley::ShadingNode::Type::k_DisplayFilter;
	}

	// Load parameters

	loadParameterTypes( tree.get_child( "args" ), result->parameterTypes );

	return result;
}

ConstShaderInfoPtr shaderInfoFromOSLQuery( OSL::OSLQuery &query )
{
	auto result = std::make_shared<ShaderInfo>();
	result->type = riley::ShadingNode::Type::k_Pattern;

	for( const auto &parameter : query )
	{
		if( parameter.type == OIIO::TypeInt )
		{
			result->parameterTypes[parameter.name.c_str()] = pxrcore::DataType::k_integer;
		}
		else if( parameter.type == OIIO::TypeFloat )
		{
			result->parameterTypes[parameter.name.c_str()] = pxrcore::DataType::k_float;
		}
		else if( parameter.type == OIIO::TypeColor )
		{
			result->parameterTypes[parameter.name.c_str()] = pxrcore::DataType::k_color;
		}
		else if( parameter.type == OIIO::TypePoint )
		{
			result->parameterTypes[parameter.name.c_str()] = pxrcore::DataType::k_point;
		}
		else if( parameter.type == OIIO::TypeVector )
		{
			result->parameterTypes[parameter.name.c_str()] = pxrcore::DataType::k_vector;
		}
		else if( parameter.type == OIIO::TypeNormal )
		{
			result->parameterTypes[parameter.name.c_str()] = pxrcore::DataType::k_normal;
		}
		else if( parameter.type == OIIO::TypeMatrix44 )
		{
			result->parameterTypes[parameter.name.c_str()] = pxrcore::DataType::k_matrix;
		}
		else if( parameter.type == OIIO::TypeString )
		{
			result->parameterTypes[parameter.name.c_str()] = pxrcore::DataType::k_string;
		}
		else if( parameter.isstruct )
		{
			result->parameterTypes[parameter.name.c_str()] = pxrcore::DataType::k_struct;
		}
		else
		{
			IECore::msg(
				IECore::Msg::Warning, "IECoreRenderMan",
				fmt::format(
					"Unknown type `{}` for parameter \"{}\" on shader \"{}\".",
					parameter.type, parameter.name, query.shadername()
				)
			);
		}
	}

	return result;
}

using ShaderInfoCache = IECore::LRUCache<string, ConstShaderInfoPtr>;

ShaderInfoCache g_shaderInfoCache(

	[]( const std::string &shaderName, size_t &cost ) -> ConstShaderInfoPtr {

		cost = 1;

		const char *rixPluginPath = getenv( "RMAN_RIXPLUGINPATH" );
		SearchPath rixSearchPath( rixPluginPath ? rixPluginPath : "" );
		boost::filesystem::path argsFileName = rixSearchPath.find( "Args/" + shaderName + ".args" );
		if( !argsFileName.empty() )
		{
			return shaderInfoFromArgsFile( argsFileName );
		}

		const char *oslSearchPath = getenv( "OSL_SHADER_PATHS" );
		OSL::OSLQuery oslQuery;
		if( oslQuery.open( shaderName, oslSearchPath ? oslSearchPath : "" ) )
		{
			return shaderInfoFromOSLQuery( oslQuery );
		}

		IECore::msg( IECore::Msg::Warning, "IECoreRenderMan", fmt::format( "Unable to find shader \"{}\".", shaderName ) );
		return nullptr;
	},

	/* maxCost = */ 10000

);

void convertConnection( const IECoreScene::ShaderNetwork::Connection &connection, const ShaderInfo *shaderInfo, RtParamList &paramList )
{
	auto it = shaderInfo->parameterTypes.find( connection.destination.name );
	if( it == shaderInfo->parameterTypes.end() )
	{
		IECore::msg(
			IECore::Msg::Warning, "IECoreRenderMan",
			fmt::format(
				"Unable to translate connection to `{}.{}` because its type is not known",
				connection.destination.shader.string(), connection.destination.name.string()
			)
		);
		return;
	}

	std::string reference = connection.source.shader;
	if( !connection.source.name.string().empty() )
	{
		reference += ":" + connection.source.name.string();
	}

	const RtUString referenceU( reference.c_str() );

	RtParamList::ParamInfo const info = {
		RtUString( connection.destination.name.c_str() ),
		it->second,
		pxrcore::DetailType::k_reference,
		1,
		false,
		false,
		false
	};

	paramList.SetParam( info, &referenceU );
}

using HandleSet = std::unordered_set<InternedString>;

void convertShaderNetworkWalk( const ShaderNetwork::Parameter &outputParameter, const IECoreScene::ShaderNetwork *shaderNetwork, vector<riley::ShadingNode> &shadingNodes, HandleSet &visited )
{
	if( !visited.insert( outputParameter.shader ).second )
	{
		return;
	}

	const IECoreScene::Shader *shader = shaderNetwork->getShader( outputParameter.shader );
	ConstShaderInfoPtr shaderInfo = g_shaderInfoCache.get( shader->getName() );
	if( !shaderInfo )
	{
		return;
	}

	const bool isFilterCombiner = (
		(
			shaderInfo->type == riley::ShadingNode::Type::k_DisplayFilter ||
			shaderInfo->type == riley::ShadingNode::Type::k_SampleFilter
		)
		&&
		(
			shader->getName() == "PxrDisplayFilterCombiner" ||
			shader->getName() == "PxrSampleFilterCombiner"
		)
	);

	riley::ShadingNode node = {
		shaderInfo->type,
		RtUString( shader->getName().c_str() ),
		RtUString( outputParameter.shader.c_str() ),
		RtParamList()
	};

	if( !isFilterCombiner )
	{
		IECore::ConstCompoundDataPtr expandedParameters = IECoreScene::ShaderNetworkAlgo::expandSplineParameters(
			shader->parametersData(), shader->getType(), shader->getName()
		);
		ParamListAlgo::convertParameters( expandedParameters->readable(), node.params );
	}

	for( const auto &connection : shaderNetwork->inputConnections( outputParameter.shader ) )
	{
		convertShaderNetworkWalk( connection.source, shaderNetwork, shadingNodes, visited );
		if( !isFilterCombiner )
		{
			convertConnection( connection, shaderInfo.get(), node.params );
		}
	}

	shadingNodes.push_back( node );
}

} // namespace

//////////////////////////////////////////////////////////////////////////
// External API
//////////////////////////////////////////////////////////////////////////

std::vector<riley::ShadingNode> IECoreRenderMan::ShaderNetworkAlgo::convert( const IECoreScene::ShaderNetwork *network )
{
	IECoreScene::ShaderNetworkPtr networkCopy = network->copy();
	IECoreScene::ShaderNetworkAlgo::convertToOSLConventions( networkCopy.get(), 10900 );
	IECoreRenderMan::ShaderNetworkAlgo::convertUSDShaders( networkCopy.get() );
	IECoreMaterialX::ShaderNetworkAlgo::convertToOSLNodes( networkCopy.get(), "renderman" );

	vector<riley::ShadingNode> result;
	result.reserve( networkCopy->size() );

	HandleSet visited;
	convertShaderNetworkWalk( networkCopy->getOutput(), networkCopy.get(), result, visited );

	return result;
}

riley::DisplayFilterId IECoreRenderMan::ShaderNetworkAlgo::convertDisplayFilter( const IECoreScene::ShaderNetwork *network, Session *session )
{
	vector<riley::ShadingNode> shadingNodes;
	shadingNodes.reserve( network->size() );

	vector<RtUString> filterRefs;
	const IECoreScene::Shader *shader = network->outputShader();
	if( shader->getName() == "PxrDisplayFilterCombiner" )
	{
		for( int i = 0; i < shader->parameters().size(); i++ )
		{
			if( auto filter = shader->parameters().find( fmt::format( "filter[{}]", i ) ); filter != shader->parameters().end() )
			{
				filterRefs.push_back( RtUString( network->input( { network->getOutput().shader, filter->first } ).shader.c_str() ) );
			}
		}
	}

	HandleSet visited;
	convertShaderNetworkWalk( network->getOutput(), network, shadingNodes, visited );

	if( filterRefs.size() )
	{
		shadingNodes.back().params.SetDisplayFilterReferenceArray( RtUString( "filter" ), filterRefs.data(), filterRefs.size() );
	}

	return session->riley->CreateDisplayFilter( riley::UserId(), { (uint32_t)shadingNodes.size(), shadingNodes.data() }, RtParamList() );
}

riley::SampleFilterId IECoreRenderMan::ShaderNetworkAlgo::convertSampleFilter( const IECoreScene::ShaderNetwork *network, Session *session )
{
	vector<riley::ShadingNode> shadingNodes;
	shadingNodes.reserve( network->size() );

	vector<RtUString> filterRefs;
	const IECoreScene::Shader *shader = network->outputShader();
	if( shader->getName() == "PxrSampleFilterCombiner" )
	{
		for( int i = 0; i < shader->parameters().size(); i++ )
		{
			if( auto filter = shader->parameters().find( fmt::format( "filter[{}]", i ) ); filter != shader->parameters().end() )
			{
				filterRefs.push_back( RtUString( network->input( { network->getOutput().shader, filter->first } ).shader.c_str() ) );
			}
		}
	}

	HandleSet visited;
	convertShaderNetworkWalk( network->getOutput(), network, shadingNodes, visited );

	if( filterRefs.size() )
	{
		shadingNodes.back().params.SetSampleFilterReferenceArray( RtUString( "filter" ), filterRefs.data(), filterRefs.size() );
	}

	return session->riley->CreateSampleFilter( riley::UserId(), { (uint32_t)shadingNodes.size(), shadingNodes.data() }, RtParamList() );
}

namespace
{

// Traits class to handle the GeometricTypedData fiasco.
template<typename T>
struct DataTraits
{

	using DataType = IECore::TypedData<T>;

};

template<typename T>
struct DataTraits<Vec2<T> >
{

	using DataType = IECore::GeometricTypedData<Vec2<T>>;

};

template<typename T>
struct DataTraits<Vec3<T> >
{

	using DataType = IECore::GeometricTypedData<Vec3<T>>;

};

const InternedString g_outParameter( "out" );
const InternedString g_surfaceParameter( "surface" );
const InternedString g_resultParameter( "result" );

const InternedString g_presenceParameter( "presence" );

const InternedString g_normalParameter( "normal" );
const InternedString g_normalInParameter( "normalIn" );

const InternedString g_emissionParameter( "emission" );
const InternedString g_emissionValueParameter( "emission_value" );

const InternedString g_subsurfaceParameter( "subsurface" );
const InternedString g_subsurfaceValueParameter( "subsurface_value" );

const InternedString g_displacementParameter( "displacement" );
const InternedString g_dispScalarParameter( "dispScalar" );

const InternedString g_diffuseGainParameter( "diffuseGain" );
const InternedString g_diffuseColorParameter( "diffuseColor" );
const InternedString g_diffuseRoughnessParameter( "diffuseRoughness" );
const InternedString g_specularFresnelModeParameter( "specularFresnelMode" );
const InternedString g_specularModelTypeParameter( "specularModelType" );
const InternedString g_specularFaceColorParameter( "specularFaceColor" );
const InternedString g_specularEdgeColorParameter( "specularEdgeColor" );
const InternedString g_specularRoughnessParameter( "specularRoughness" );
const InternedString g_specularIorParameter( "specularIor" );
const InternedString g_specularExtinctionCoeffParameter( "specularExtinctionCoeff" );
const InternedString g_specularAnisotropyParameter( "specularAnisotropy" );
const InternedString g_clearcoatModelTypeParameter( "clearcoatModelType" );
const InternedString g_clearcoatFaceColorParameter( "clearcoatFaceColor" );
const InternedString g_clearcoatEdgeColorParameter( "clearcoatEdgeColor" );
const InternedString g_clearcoatRoughnessParameter( "clearcoatRoughness" );
const InternedString g_clearcoatAnisotropyParameter( "clearcoatAnisotropy" );
const InternedString g_glowGainParameter( "glowGain" );
const InternedString g_glowColorParameter( "glowColor" );
const InternedString g_reflectionGainParameter( "reflectionGain" );
const InternedString g_refractionGainParameter( "refractionGain" );
const InternedString g_refractionColorParameter( "refractionColor" );
const InternedString g_glassIorParameter( "glassIor" );
const InternedString g_glassRoughnessParameter( "glassRoughness" );
const InternedString g_glassAnisotropyParameter( "glassAnisotropy" );
const InternedString g_thinGlassParameter( "thinGlass" );
const InternedString g_ssAlbedoParameter( "ssAlbedo" );
const InternedString g_extinctionParameter( "extinction" );
const InternedString g_g0Parameter( "g0" );
const InternedString g_subsurfaceGainParameter( "subsurfaceGain" );
const InternedString g_subsurfaceColorParameter( "subsurfaceColor" );
const InternedString g_subsurfaceDmfpParameter( "subsurfaceDmfp" );
const InternedString g_subsurfaceDmfpColorParameter( "subsurfaceDmfpColor" );
const InternedString g_subsurfaceDirectionalityParameter( "subsurfaceDirectionality" );
const InternedString g_diffuseTransmitGainParameter( "diffuseTransmitGain" );
const InternedString g_diffuseTransmitColorParameter( "diffuseTransmitColor" );
const InternedString g_diffuseDoubleSidedParameter( "diffuseDoubleSided" );
const InternedString g_fuzzGainParameter( "fuzzGain" );
const InternedString g_fuzzColorParameter( "fuzzColor" );
const InternedString g_fuzzConeAngleParameter( "fuzzConeAngle" );
const InternedString g_iridescenceModeParameter( "iridescenceMode" );
const InternedString g_iridescenceFaceGainParameter( "iridescenceFaceGain" );
const InternedString g_iridescenceEdgeGainParameter( "iridescenceEdgeGain" );
const InternedString g_iridescenceThicknessParameter( "iridescenceThickness" );
const InternedString g_bumpNormalParameter( "bumpNormal" );

const InternedString g_usdPreviewSurfaceShader( "UsdPreviewSurface" );
const InternedString g_mtlxUsdPreviewSurfaceShader( "ND_UsdPreviewSurface_surfaceshader" );
const InternedString g_standardSurfaceShader( "standard_surface" );
const InternedString g_mtlxStandardSurfaceShader( "ND_standard_surface_surfaceshader" );

boost::container::flat_map<InternedString, InternedString> g_previewSurfaceConnectionMap = {
	{ g_diffuseGainParameter, g_diffuseGainParameter.string() + "Out" },
	{ g_diffuseColorParameter, g_diffuseColorParameter.string() + "Out" },
	{ g_specularFaceColorParameter, g_specularFaceColorParameter.string() + "Out" },
	{ g_specularEdgeColorParameter, g_specularEdgeColorParameter.string() + "Out" },
	{ g_specularRoughnessParameter, g_specularRoughnessParameter.string() + "Out" },
	{ g_specularIorParameter, g_specularIorParameter.string() + "Out" },
	{ g_clearcoatFaceColorParameter, g_clearcoatFaceColorParameter.string() + "Out" },
	{ g_clearcoatEdgeColorParameter, g_clearcoatEdgeColorParameter.string() + "Out" },
	{ g_clearcoatRoughnessParameter, g_clearcoatRoughnessParameter.string() + "Out" },
	{ g_glowGainParameter, g_glowGainParameter.string() + "Out" },
	{ g_glowColorParameter, g_glowColorParameter.string() + "Out" },
	{ g_bumpNormalParameter, g_bumpNormalParameter.string() + "Out" },
	{ g_glassIorParameter, g_glassIorParameter.string() + "Out" },
	{ g_glassRoughnessParameter, g_glassRoughnessParameter.string() + "Out" },
	{ g_refractionGainParameter, g_refractionGainParameter.string() + "Out" },
	{ g_presenceParameter, g_presenceParameter.string() + "Out" },
};

boost::container::flat_map<InternedString, InternedString> g_surfaceShaderConnectionMap = {
	{ g_diffuseGainParameter, g_diffuseGainParameter.string() + "Out" },
	{ g_diffuseColorParameter, g_diffuseColorParameter.string() + "Out" },
	{ g_diffuseRoughnessParameter, g_diffuseRoughnessParameter.string() + "Out" },
	{ g_specularFresnelModeParameter, g_specularFresnelModeParameter.string() + "Out" },
	{ g_specularModelTypeParameter, g_specularModelTypeParameter.string() + "Out" },
	{ g_specularFaceColorParameter, g_specularFaceColorParameter.string() + "Out" },
	{ g_specularEdgeColorParameter, g_specularEdgeColorParameter.string() + "Out" },
	{ g_specularRoughnessParameter, g_specularRoughnessParameter.string() + "Out" },
	{ g_specularIorParameter, g_specularIorParameter.string() + "Out" },
	{ g_specularExtinctionCoeffParameter, g_specularExtinctionCoeffParameter.string() + "Out" },
	{ g_specularAnisotropyParameter, g_specularAnisotropyParameter.string() + "Out" },
	{ g_clearcoatModelTypeParameter, g_clearcoatModelTypeParameter.string() + "Out" },
	{ g_clearcoatFaceColorParameter, g_clearcoatFaceColorParameter.string() + "Out" },
	{ g_clearcoatEdgeColorParameter, g_clearcoatEdgeColorParameter.string() + "Out" },
	{ g_clearcoatRoughnessParameter, g_clearcoatRoughnessParameter.string() + "Out" },
	{ g_clearcoatAnisotropyParameter, g_clearcoatAnisotropyParameter.string() + "Out" },
	{ g_glowGainParameter, g_glowGainParameter.string() + "Out" },
	{ g_glowColorParameter, g_glowColorParameter.string() + "Out" },
	{ g_reflectionGainParameter, g_reflectionGainParameter.string() + "Out" },
	{ g_refractionGainParameter, g_refractionGainParameter.string() + "Out" },
	{ g_refractionColorParameter, g_refractionColorParameter.string() + "Out" },
	{ g_glassIorParameter, g_glassIorParameter.string() + "Out" },
	{ g_glassRoughnessParameter, g_glassRoughnessParameter.string() + "Out" },
	{ g_glassAnisotropyParameter, g_glassAnisotropyParameter.string() + "Out" },
	{ g_thinGlassParameter, g_thinGlassParameter.string() + "Out" },
	{ g_ssAlbedoParameter, g_ssAlbedoParameter.string() + "Out" },
	{ g_extinctionParameter, g_extinctionParameter.string() + "Out" },
	{ g_g0Parameter, g_g0Parameter.string() + "Out" },
	{ g_subsurfaceGainParameter, g_subsurfaceGainParameter.string() + "Out" },
	{ g_subsurfaceColorParameter, g_subsurfaceColorParameter.string() + "Out" },
	{ g_subsurfaceDmfpParameter, g_subsurfaceDmfpParameter.string() + "Out" },
	{ g_subsurfaceDmfpColorParameter, g_subsurfaceDmfpColorParameter.string() + "Out" },
	{ g_subsurfaceDirectionalityParameter, g_subsurfaceDirectionalityParameter.string() + "Out" },
	{ g_diffuseTransmitGainParameter, g_diffuseTransmitGainParameter.string() + "Out" },
	{ g_diffuseTransmitColorParameter, g_diffuseTransmitColorParameter.string() + "Out" },
	{ g_diffuseDoubleSidedParameter, g_diffuseDoubleSidedParameter.string() + "Out" },
	{ g_fuzzGainParameter, g_fuzzGainParameter.string() + "Out" },
	{ g_fuzzColorParameter, g_fuzzColorParameter.string() + "Out" },
	{ g_fuzzConeAngleParameter, g_fuzzConeAngleParameter.string() + "Out" },
	{ g_iridescenceModeParameter, g_iridescenceModeParameter.string() + "Out" },
	{ g_iridescenceFaceGainParameter, g_iridescenceFaceGainParameter.string() + "Out" },
	{ g_iridescenceEdgeGainParameter, g_iridescenceEdgeGainParameter.string() + "Out" },
	{ g_iridescenceThicknessParameter, g_iridescenceThicknessParameter.string() + "Out" },
	{ g_bumpNormalParameter, g_bumpNormalParameter.string() + "Out" },
};

boost::container::flat_map<InternedString, string> g_lamaNameMap = {
	{ "ND_lama_surface", "LamaSurface" },
	{ "ND_lama_add", "LamaAdd" },
	{ "ND_lama_mix", "LamaMix" },
	{ "ND_lama_layer", "LamaLayer" },
	{ "ND_lama_conductor", "LamaConductor" },
	{ "ND_lama_dielectric", "LamaDielectric" },
	{ "ND_lama_diffuse", "LamaDiffuse" },
	{ "ND_lama_emission", "LamaEmission" },
	{ "ND_lama_generalized_schlick", "LamaGeneralizedSchlick" },
	{ "ND_lama_iridescence", "LamaIridescence" },
	{ "ND_lama_sheen", "LamaSheen" },
	{ "ND_lama_sss", "LamaSSS" },
	{ "ND_lama_translucent", "LamaTranslucent" },
};

template<typename T>
T parameterValue( const Shader *shader, InternedString parameterName, const T &defaultValue )
{
	if( auto d = shader->parametersData()->member<TypedData<T>>( parameterName ) )
	{
		return d->readable();
	}

	if constexpr( is_same_v<remove_cv_t<T>, Color3f> )
	{
		// Correction for USD files which author `float3` instead of `color3f`.
		// See `ShaderNetworkAlgoTest.testConvertUSDFloat3ToColor3f()`.
		if( auto d = shader->parametersData()->member<V3fData>( parameterName ) )
		{
			return d->readable();
		}
		// Conversion of Color4 to Color3, for cases like converting `UsdUVTexture.scale`
		// to `image.multiply`.
		if( auto d = shader->parametersData()->member<Color4fData>( parameterName ) )
		{
			const Color4f &c = d->readable();
			return Color3f( c[0], c[1], c[2] );
		}
	}
	else if constexpr( is_same_v<remove_cv_t<T>, string> )
	{
		// Support for USD `token`, which will be loaded as `InternedString`, but which
		// we want to translate to `string`.
		if( auto d = shader->parametersData()->member<InternedStringData>( parameterName ) )
		{
			return d->readable().string();
		}
	}

	return defaultValue;
}

template<typename T>
void transferUSDParameter( ShaderNetwork *network, InternedString shaderHandle, const Shader *usdShader, InternedString usdName, Shader *shader, InternedString name, const T &defaultValue )
{
	shader->parameters()[name] = new typename DataTraits<T>::DataType( parameterValue( usdShader, usdName, defaultValue ) );

	if( ShaderNetwork::Parameter input = network->input( { shaderHandle, usdName } ) )
	{
		if( name != usdName )
		{
			network->addConnection( { input, { shaderHandle, name } } );
			network->removeConnection( { input, { shaderHandle, usdName } } );
		}
	}
}

const InternedString remapOutputParameterName( const InternedString name, const InternedString shaderName )
{
	if( shaderName == g_usdPreviewSurfaceShader || shaderName == g_mtlxUsdPreviewSurfaceShader )
	{
		if( name == g_surfaceParameter )
		{
			return g_outParameter;
		}
		else if( name == g_displacementParameter )
		{
			return g_resultParameter;
		}
	}
	return name;
}

void replaceUSDShader( ShaderNetwork *network, InternedString handle, ShaderPtr &&newShader )
{
	const InternedString shaderName = network->getShader( handle )->getName();

	// Replace original shader with the new.
	network->setShader( handle, std::move( newShader ) );

	// When replacing the output shader, remap the network output parameter name.
	ShaderNetwork::Parameter outParameter = network->getOutput();
	if( outParameter.shader == handle )
	{
		outParameter.name = remapOutputParameterName( outParameter.name, shaderName );
		network->setOutput( outParameter );
	}

	// Iterating over a copy because we will modify the range during iteration.
	ShaderNetwork::ConnectionRange range = network->outputConnections( handle );
	vector<ShaderNetwork::Connection> outputConnections( range.begin(), range.end() );
	for( auto &c : outputConnections )
	{
		network->removeConnection( c );
		c.source.name = remapOutputParameterName( c.source.name, shaderName );
		network->addConnection( c );
	}
}

const InternedString g_intensityParameter( "intensity" );
const InternedString g_exposureParameter( "exposure" );
const InternedString g_diffuseParameter( "diffuse" );
const InternedString g_specularParameter( "specular" );

const InternedString g_colorParameter( "color" );
const InternedString g_lightColorParameter( "lightColor" );

const InternedString g_textureFileParameter( "texture:file" );
const InternedString g_lightColorMapParameter( "lightColorMap" );

// No conversion for this one?
const InternedString g_textureFormatParameter( "texture:format" );

const InternedString g_normalizeParameter( "normalize" );
const InternedString g_areaNormalizeParameter( "areaNormalize" );

const InternedString g_angleParameter( "angle" );
const InternedString g_angleExtentParameter( "angleExtent" );

const InternedString g_enableColorTemperatureParameter( "enableColorTemperature" );
const InternedString g_enableTemperatureParameter( "enableTemperature" );

const InternedString g_colorTemperatureParameter( "colorTemperature" );
const InternedString g_temperatureParameter( "temperature" );

const InternedString g_shadowEnableParameter( "shadow:enable" );
const InternedString g_enableShadowsParameter( "enableShadows" );

const InternedString g_usdShadowColorParameter( "shadow:color" );
const InternedString g_shadowColorParameter( "shadowColor" );

const InternedString g_usdShadowDistanceParameter( "shadow:distance" );
const InternedString g_shadowDistanceParameter( "shadowDistance" );

const InternedString g_usdShadowFalloffParameter( "shadow:falloff" );
const InternedString g_shadowFalloffParameter( "shadowFalloff" );

const InternedString g_usdShadowFalloffGammaParameter( "shadow:falloffGamma" );
const InternedString g_shadowFalloffGammaParameter( "shadowFalloffGamma" );

const InternedString g_shapingFocusParameter( "shaping:focus" );
const InternedString g_emissionFocusParameter( "emissionFocus" );

const InternedString g_shapingFocusTintParameter( "shaping:focusTint" );
const InternedString g_emissionFocusTintParameter( "emissionFocusTint" );

const InternedString g_shapingConeAngleParameter( "shaping:cone:angle" );
const InternedString g_coneAngleParameter( "coneAngle" );

const InternedString g_shapingConeSoftnessParameter( "shaping:cone:softness" );
const InternedString g_coneSoftnessParameter( "coneSoftness" );

const InternedString g_shapingIesFileParameter( "shaping:ies:file" );
const InternedString g_iesProfileParameter( "iesProfile" );

const InternedString g_shapingIesAngleScaleParameter( "shaping:ies:angleScale" );
const InternedString g_iesProfileScaleParameter( "iesProfileScale" );

const InternedString g_shapingIesNormalizeParameter( "shaping:ies:normalize" );
const InternedString g_iesProfileNormalizeParameter( "iesProfileNormalize" );

const InternedString g_sphereLight( "SphereLight" );
const InternedString g_diskLight( "DiskLight" );
const InternedString g_cylinderLight( "CylinderLight" );
const InternedString g_distantLight( "DistantLight" );
const InternedString g_domeLight( "DomeLight" );
const InternedString g_rectLight( "RectLight" );

boost::container::flat_map<InternedString, string> g_lightNameMap = {
	{ g_sphereLight, "PxrSphereLight" },
	{ g_diskLight, "PxrDiskLight" },
	{ g_cylinderLight, "PxrCylinderLight" },
	{ g_distantLight, "PxrDistantLight" },
	{ g_domeLight, "PxrDomeLight" },
	{ g_rectLight, "PxrRectLight" },
};

const string g_riLightNamespace( "ri:light:" );

} // namespace

void IECoreRenderMan::ShaderNetworkAlgo::convertUSDShaders( ShaderNetwork *network )
{
	const ShaderNetwork::Parameter &outputParameter = network->getOutput();
	const IECoreScene::Shader *outShader = network->getShader( outputParameter.shader );
	const bool isDisplacement = boost::ends_with( outShader->getType(), "displacement" );

	for( const auto &[handle, shader] : network->shaders() )
	{
		if( shader->getName() == g_usdPreviewSurfaceShader.string() || shader->getName() == g_mtlxUsdPreviewSurfaceShader.string() )
		{
			if( isDisplacement )
			{
				ShaderPtr dispShader = new Shader( "PxrDisplace", "osl:displacement" );
				transferUSDParameter( network, handle, shader.get(), g_displacementParameter, dispShader.get(), g_dispScalarParameter, 0.0f );
				ShaderNetwork::ConnectionRange range = network->inputConnections( handle );
				vector<ShaderNetwork::Connection> inputConnections( range.begin(), range.end() );
				for( const auto &c : inputConnections )
				{
					if( c.destination.name != g_displacementParameter )
					{
						network->removeConnection( c );
					}
				}
				replaceUSDShader( network, handle, std::move( dispShader ) );
				continue;
			}

			ShaderPtr translationShader = new Shader( "__renderman/__UsdPreviewSurfaceParameters.oso", "osl:shader" );

			transferUSDParameter( network, handle, shader.get(), g_normalParameter, translationShader.get(), g_normalInParameter, V3f( 0.0, 0.0, 1.0 ) );

			for( const auto &[name, value] : shader->parameters() )
			{
				if( name == g_normalParameter	)
				{
					continue;
				}

				translationShader->parameters()[name] = value;
			}

			replaceUSDShader( network, handle, std::move( translationShader ) );

			ShaderPtr newShader = new Shader( "PxrSurface", "ri:surface" );
			const InternedString newShaderHandle = network->addShader( handle.string() + "PxrSurface", std::move( newShader ) );

			for( const auto &[destinationName, sourceName] : g_previewSurfaceConnectionMap )
			{
				network->addConnection( ShaderNetwork::Connection( { handle, sourceName }, { newShaderHandle, destinationName } ) );
			}

			network->setOutput( { newShaderHandle, g_outParameter } );
		}
		else if( shader->getName() == g_standardSurfaceShader.string() || shader->getName() == g_mtlxStandardSurfaceShader.string() )
		{
			ShaderPtr translationShader = new Shader( "__renderman/__StandardSurfaceParameters.oso", "osl:shader" );

			transferUSDParameter( network, handle, shader.get(), g_normalParameter, translationShader.get(), g_normalInParameter, V3f( 0.0 ) );
			transferUSDParameter( network, handle, shader.get(), g_emissionParameter, translationShader.get(), g_emissionValueParameter, 0.0f );
			transferUSDParameter( network, handle, shader.get(), g_subsurfaceParameter, translationShader.get(), g_subsurfaceValueParameter, 0.0f );

			for( const auto &[name, value] : shader->parameters() )
			{
				if( name == g_normalParameter ||
					name == g_emissionParameter ||
					name == g_subsurfaceParameter
				)
				{
					continue;
				}

				translationShader->parameters()[name] = value;
			}

			replaceUSDShader( network, handle, std::move( translationShader ) );

			ShaderPtr newShader = new Shader( "PxrSurface", "ri:surface" );
			const InternedString newShaderHandle = network->addShader( handle.string() + "PxrSurface", std::move( newShader ) );

			for( const auto &[destinationName, sourceName] : g_surfaceShaderConnectionMap )
			{
				network->addConnection( ShaderNetwork::Connection( { handle, sourceName }, { newShaderHandle, destinationName } ) );
			}

			network->setOutput( { newShaderHandle, g_outParameter } );
		}
		else if( boost::starts_with( shader->getName(), "ND_lama" ) )
		{
			auto it = g_lamaNameMap.find( shader->getName() );
			if( it != g_lamaNameMap.end() )
			{
				ShaderPtr newShader = new Shader( it->second, "ri:surface" );
				for( const auto &[name, value] : shader->parameters() )
				{
					newShader->parameters()[name] = value;
				}
				replaceUSDShader( network, handle, std::move( newShader ) );
			}
		}
		else if( boost::ends_with( shader->getName(), "Light" ) )
		{
			auto it = g_lightNameMap.find( shader->getName() );
			if( it != g_lightNameMap.end() )
			{
				ShaderPtr newShader = new Shader( it->second, "ri:light" );

				if( it->first == g_distantLight )
				{
					transferUSDParameter( network, handle, shader.get(), g_intensityParameter, newShader.get(), g_intensityParameter, 50000.0f );
					transferUSDParameter( network, handle, shader.get(), g_angleParameter, newShader.get(), g_angleExtentParameter, 0.53f );
				}
				else
				{
					transferUSDParameter( network, handle, shader.get(), g_intensityParameter, newShader.get(), g_intensityParameter, 1.0f );
				}

				transferUSDParameter( network, handle, shader.get(), g_exposureParameter, newShader.get(), g_exposureParameter, 0.0f );
				transferUSDParameter( network, handle, shader.get(), g_diffuseParameter, newShader.get(), g_diffuseParameter, 1.0f );
				transferUSDParameter( network, handle, shader.get(), g_specularParameter, newShader.get(), g_specularParameter, 1.0f );

				transferUSDParameter( network, handle, shader.get(), g_colorParameter, newShader.get(), g_lightColorParameter, Color3f( 1.0 ) );
				transferUSDParameter( network, handle, shader.get(), g_enableColorTemperatureParameter, newShader.get(), g_enableTemperatureParameter, false );
				transferUSDParameter( network, handle, shader.get(), g_colorTemperatureParameter, newShader.get(), g_temperatureParameter, 6500.0f );
				transferUSDParameter( network, handle, shader.get(), g_shadowEnableParameter, newShader.get(), g_enableShadowsParameter, true );
				transferUSDParameter( network, handle, shader.get(), g_usdShadowColorParameter, newShader.get(), g_shadowColorParameter, Color3f( 0 ) );
				transferUSDParameter( network, handle, shader.get(), g_usdShadowDistanceParameter, newShader.get(), g_shadowDistanceParameter, -1.0f );
				transferUSDParameter( network, handle, shader.get(), g_usdShadowFalloffParameter, newShader.get(), g_shadowFalloffParameter, -1.0f );
				transferUSDParameter( network, handle, shader.get(), g_usdShadowFalloffGammaParameter, newShader.get(), g_shadowFalloffGammaParameter, 1.0f );

				if( it->first != g_domeLight )
				{
					transferUSDParameter( network, handle, shader.get(), g_normalizeParameter, newShader.get(), g_areaNormalizeParameter, false );
					transferUSDParameter( network, handle, shader.get(), g_shapingFocusParameter, newShader.get(), g_emissionFocusParameter, 0.0f );
					transferUSDParameter( network, handle, shader.get(), g_shapingFocusTintParameter, newShader.get(), g_emissionFocusTintParameter, Color3f( 0 ) );
				}

				if( it->first == g_rectLight || it->first == g_domeLight )
				{
					transferUSDParameter( network, handle, shader.get(), g_textureFileParameter, newShader.get(), g_lightColorMapParameter, std::string() );
				}

				if( it->first != g_distantLight && it->first != g_domeLight )
				{
					if( auto d = shader->parametersData()->member<FloatData>( g_shapingConeAngleParameter ) )
					{
						transferUSDParameter( network, handle, shader.get(), g_shapingConeAngleParameter, newShader.get(), g_coneAngleParameter, 0.0f );
						transferUSDParameter( network, handle, shader.get(), g_shapingConeSoftnessParameter, newShader.get(), g_coneSoftnessParameter, 0.0f );
					}
					transferUSDParameter( network, handle, shader.get(), g_shapingIesFileParameter, newShader.get(), g_iesProfileParameter, std::string() );
					transferUSDParameter( network, handle, shader.get(), g_shapingIesAngleScaleParameter, newShader.get(), g_iesProfileScaleParameter, 0.0f );
					transferUSDParameter( network, handle, shader.get(), g_shapingIesNormalizeParameter, newShader.get(), g_iesProfileNormalizeParameter, false );
				}

				for( const auto &[name, value] : shader->parameters() )
				{
					if( boost::starts_with( name.string(), g_riLightNamespace ) )
					{
						newShader->parameters()[name.string().substr(g_riLightNamespace.size())] = value;
					}
				}
				replaceUSDShader( network, handle, std::move( newShader ) );
			}
		}
	}
}
