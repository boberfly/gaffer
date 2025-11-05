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

#include "IECoreRenderMan/ShaderNetworkAlgo.h"

#include "ParamListAlgo.h"

#include "IECoreScene/ShaderNetworkAlgo.h"

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

#include <regex>
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
	using ParameterTypeMap = std::unordered_map<RtUString, pxrcore::DataType>;
	ParameterTypeMap parameterTypes;
};

using ConstShaderInfoPtr = std::shared_ptr<const ShaderInfo>;

void loadParameterTypes( const boost::property_tree::ptree &tree, ShaderInfo::ParameterTypeMap &typeMap )
{
	for( const auto &child : tree )
	{
		if( child.first == "param" )
		{
			const RtUString name( child.second.get<string>( "<xmlattr>.name" ).c_str() );
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
				IECore::msg( IECore::Msg::Warning, "IECoreRenderMan", fmt::format( "Unknown type `{}` for parameter \"{}\".", type, name.CStr() ) );
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
		const RtUString name( parameter.name.c_str() );
		OIIO::TypeDesc type = parameter.type;
		type.unarray();
		if( type == OIIO::TypeInt )
		{
			result->parameterTypes[name] = pxrcore::DataType::k_integer;
		}
		else if( type == OIIO::TypeFloat )
		{
			result->parameterTypes[name] = pxrcore::DataType::k_float;
		}
		else if( type == OIIO::TypeColor )
		{
			result->parameterTypes[name] = pxrcore::DataType::k_color;
		}
		else if( type == OIIO::TypePoint )
		{
			result->parameterTypes[name] = pxrcore::DataType::k_point;
		}
		else if( type == OIIO::TypeVector )
		{
			result->parameterTypes[name] = pxrcore::DataType::k_vector;
		}
		else if( type == OIIO::TypeNormal )
		{
			result->parameterTypes[name] = pxrcore::DataType::k_normal;
		}
		else if( type == OIIO::TypeMatrix44 )
		{
			result->parameterTypes[name] = pxrcore::DataType::k_matrix;
		}
		else if( type == OIIO::TypeString )
		{
			result->parameterTypes[name] = pxrcore::DataType::k_string;
		}
		else if( parameter.isstruct )
		{
			result->parameterTypes[name] = pxrcore::DataType::k_struct;
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

using ArrayConnections = std::unordered_map<RtUString, vector<RtUString>>;
const std::regex g_arrayIndexRegex( R"((\w+)\[([0-9]+)\])" );

void convertConnection( const IECoreScene::ShaderNetwork::Connection &connection, const ShaderInfo *shaderInfo, RtParamList &paramList, ArrayConnections &arrayConnections )
{
	RtUString destination;
	std::optional<size_t> destinationIndex;

	std::smatch arrayIndexMatch;
	if( std::regex_match( connection.destination.name.string(), arrayIndexMatch, g_arrayIndexRegex ) )
	{
		destination = RtUString( arrayIndexMatch.str( 1 ).c_str() );
		destinationIndex = std::stoi( arrayIndexMatch.str( 2 ) );
	}
	else
	{
		destination = RtUString( connection.destination.name.c_str() );
	}

	auto typeIt = shaderInfo->parameterTypes.find( destination );
	if( typeIt == shaderInfo->parameterTypes.end() )
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
	if(
		connection.source.name.string().size() &&
		// Several node types don't have named outputs, and
		// connections will silently fail if we include a name.
		typeIt->second != pxrcore::DataType::k_displayfilter &&
		typeIt->second != pxrcore::DataType::k_samplefilter &&
		typeIt->second != pxrcore::DataType::k_bxdf &&
		typeIt->second != pxrcore::DataType::k_lightfilter
	)
	{
		reference += ":" + connection.source.name.string();
	}
	const RtUString referenceU( reference.c_str() );

	if( !destinationIndex )
	{
		RtParamList::ParamInfo const info = {
			destination,
			typeIt->second,
			pxrcore::DetailType::k_reference,
			1,
			false,
			false,
			false
		};

		paramList.SetParam( info, &referenceU );
	}
	else
	{
		// We must connect all array elements at once. Buffer up for
		// later connection.
		auto &array = arrayConnections[destination];
		array.resize( max( array.size(), *destinationIndex + 1 ) );
		array[*destinationIndex] = referenceU;
	}
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

	riley::ShadingNode node = {
		shaderInfo->type,
		RtUString( shader->getName().c_str() ),
		RtUString( outputParameter.shader.c_str() ),
		RtParamList()
	};

	IECore::ConstCompoundDataPtr expandedParameters = IECoreScene::ShaderNetworkAlgo::expandSplineParameters(
		shader->parametersData(), shader->getType(), shader->getName()
	);

	for( const auto &[parameterName, parameterValue] : expandedParameters->readable() )
	{
		if( std::regex_match( parameterName.string(), g_arrayIndexRegex ) )
		{
			// Ignore array element values for now. Gaffer generates these when
			// we use ArrayPlugs to represent shader parameters, but RenderMan
			// only accepts whole arrays as values. In practice, we're only using
			// ArrayPlugs where RenderMan is only interested in connections and not
			// values anyway, so we're not losing anything.
		}
		else
		{
			ParamListAlgo::convertParameter( RtUString( parameterName.c_str() ), parameterValue.get(), node.params );
		}
	}

	ArrayConnections arrayConnections;
	for( const auto &connection : shaderNetwork->inputConnections( outputParameter.shader ) )
	{
		convertShaderNetworkWalk( connection.source, shaderNetwork, shadingNodes, visited );
		convertConnection( connection, shaderInfo.get(), node.params, arrayConnections );
	}

	for( const auto &[destination, references] : arrayConnections )
	{
		RtParamList::ParamInfo const info = {
			destination,
			shaderInfo->parameterTypes.at( destination ),
			pxrcore::DetailType::k_reference,
			(uint32_t)references.size(),
			true,
			false,
			false
		};

		node.params.SetParam( info, references.data() );
	}

	shadingNodes.push_back( node );
}

//////////////////////////////////////////////////////////////////////////
// USD conversion code
//////////////////////////////////////////////////////////////////////////

const InternedString g_filterTypeParameter( "filterType" );
const InternedString g_closestFilterName( "closest" );
const InternedString g_cubicFilterName( "cubic" );
const InternedString g_linearFilterName( "linear" );

template<typename T>
T parameterValue( const Shader *shader, InternedString parameterName, const T &defaultValue )
{
	if( auto d = shader->parametersData()->member<TypedData<T>>( parameterName ) )
	{
		return d->readable();
	}

	if constexpr( is_same_v<remove_cv_t<T>, Color3f > )
	{
		// Correction for USD files which author `float3` instead of `color3f`.
		// See `ShaderNetworkAlgoTest.testConvertUSDFloat3ToColor3f()`.
		if( auto d = shader->parametersData()->member<V3fData>( parameterName ) )
		{
			return d->readable();
		}
		// Conversion of Color4 to Color3, for cases like converting `UsdUVTexture.scale`
		// to `PxrTexture.colorScale`.
		if( auto d = shader->parametersData()->member<Color4fData>( parameterName ) )
		{
			const Color4f &c = d->readable();
			return Color3f( c[0], c[1], c[2] );
		}
	}
	else if constexpr( is_same_v<remove_cv_t<T>, V3f > )
	{
		// Conversion of V2f to V3f, for cases like converting `UsdPrimvarReader_float2.fallback`
		// to `PxrPrimvar.defaultFloat3`
		if( auto d = shader->parametersData()->member<V2fData>( parameterName ) )
		{
			const V2f &v = d->readable();
			return V3f( v[0], v[1], 0.f );
		}
	}
	else if constexpr( is_same_v<remove_cv_t<T>, std::string> )
	{
		// Support for USD `token`, which will be loaded as `InternedString`, but which
		// we want to translate to `string`.
		if( auto d = shader->parametersData()->member<InternedStringData>( parameterName ) )
		{
			return d->readable().string();
		}
	}
	else if constexpr( is_same_v<remove_cv_t<T>, int > )
	{
		// String to Enum for special-case `ND_image.filterType` to `PxrTexture.filter`.
		// PxrTexture's filter enum numbers: 0 == closest, 1 == cubic, 2 == linear.
		if( boost::starts_with( shader->getName(), "ND_image" ) && parameterName == g_filterTypeParameter )
		{
			if( auto d = shader->parametersData()->member<InternedStringData>( parameterName ) )
			{
				const InternedString &s = d->readable();
				if( s == g_closestFilterName )
				{
					return 0;
				}
				else if( s == g_cubicFilterName )
				{
					return 1;
				}
				else if( s == g_linearFilterName )
				{
					return 2;
				}
			}
		}
	}
	else if constexpr( is_same_v<remove_cv_t<T>, float > )
	{
		// Conversion of V2f to the first value eg. `ND_normalmap_vector2`
		if( auto d = shader->parametersData()->member<V2fData>( parameterName ) )
		{
			const V2f &v = d->readable();
			return v[0];
		}
	}

	return defaultValue;
}

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

const InternedString g_angleParameter( "angle" );
const InternedString g_angleExtentParameter( "angleExtent" );
const InternedString g_areaNormalizeParameter( "areaNormalize" );
const InternedString g_bumpNormalParameter( "bumpNormal" );
const InternedString g_clearcoatDoubleSidedParameter( "clearcoatDoubleSided" );
const InternedString g_clearcoatFaceColorParameter( "clearcoatFaceColor" );
const InternedString g_clearcoatEdgeColorParameter( "clearcoatEdgeColor" );
const InternedString g_clearcoatRoughnessParameter( "clearcoatRoughness" );
const InternedString g_colorParameter( "color" );
const InternedString g_colorTemperatureParameter( "colorTemperature" );
const InternedString g_coneAngleParameter( "coneAngle" );
const InternedString g_coneSoftnessParameter( "coneSoftness" );
const InternedString g_defaultFloatParameter( "defaultFloat" );
const InternedString g_defaultFloat3Parameter( "defaultFloat3" );
const InternedString g_defaultIntParameter( "defaultInt" );
const InternedString g_diffuseParameter( "diffuse" );
const InternedString g_diffuseColorParameter( "diffuseColor" );
const InternedString g_diffuseDoubleSidedParameter( "diffuseDoubleSided" );
const InternedString g_diffuseGainParameter( "diffuseGain") ;
const InternedString g_emissionFocusParameter( "emissionFocus" );
const InternedString g_emissionFocusTintParameter( "emissionFocusTint" );
const InternedString g_enableColorTemperatureParameter( "enableColorTemperature" );
const InternedString g_enableShadowsParameter( "enableShadows" );
const InternedString g_enableTemperatureParameter( "enableTemperature" );
const InternedString g_exposureParameter( "exposure" );
const InternedString g_fallbackParameter( "fallback" );
const InternedString g_glassIorParameter( "glassIor" );
const InternedString g_glassRoughnessParameter( "glassRoughness" );
const InternedString g_glowColorParameter( "glowColor" );
const InternedString g_glowGainParameter( "glowGain" );
const InternedString g_heightParameter( "height" );
const InternedString g_iesProfileParameter( "iesProfile" );
const InternedString g_iesProfileScaleParameter( "iesProfileScale" );
const InternedString g_iesProfileNormalizeParameter( "iesProfileNormalize" );
const InternedString g_intensityParameter( "intensity" );
const InternedString g_lengthParameter( "length" );
const InternedString g_lightColorParameter( "lightColor" );
const InternedString g_lightColorMapParameter( "lightColorMap" );
const InternedString g_normalParameter( "normal" );
const InternedString g_normalInParameter( "normalIn" );
const InternedString g_normalizeParameter( "normalize" );
const InternedString g_presenceParameter( "presence" );
const InternedString g_radiusParameter( "radius" );
const InternedString g_refractionGainParameter( "refractionGain" );
const InternedString g_resultFParameter( "resultF" );
const InternedString g_resultIParameter( "resultI" );
const InternedString g_resultRGBParameter( "resultRGB" );
const InternedString g_roughSpecularDoubleSidedParameter( "roughSpecularDoubleSided" );
const InternedString g_shadowColorParameter( "shadowColor" );
const InternedString g_shadowColorUSDParameter( "shadow:color" );
const InternedString g_shadowDistanceParameter( "shadowDistance" );
const InternedString g_shadowDistanceUSDParameter( "shadow:distance" );
const InternedString g_shadowEnableParameter( "shadow:enable" );
const InternedString g_shadowFalloffParameter( "shadowFalloff" );
const InternedString g_shadowFalloffUSDParameter( "shadow:falloff" );
const InternedString g_shadowFalloffGammaParameter( "shadowFalloffGamma");
const InternedString g_shadowFalloffGammaUSDParameter( "shadow:falloffGamma" );
const InternedString g_shapingConeAngleParameter( "shaping:cone:angle" );
const InternedString g_shapingConeSoftnessParameter( "shaping:cone:softness" );
const InternedString g_shapingFocusParameter( "shaping:focus" );
const InternedString g_shapingFocusTintParameter( "shaping:focusTint" );
const InternedString g_shapingIesFileParameter( "shaping:ies:file" );
const InternedString g_shapingIesAngleScaleParameter( "shaping:ies:angleScale" );
const InternedString g_shapingIesNormalizeParameter( "shaping:ies:normalize" );
const InternedString g_specularParameter( "specular" );
const InternedString g_specularDoubleSidedParameter( "specularDoubleSided" );
const InternedString g_specularEdgeColorParameter( "specularEdgeColor" );
const InternedString g_specularFaceColorParameter( "specularFaceColor" );
const InternedString g_specularIorParameter( "specularIor" );
const InternedString g_specularModelTypeParameter( "specularModelType" );
const InternedString g_specularRoughnessParameter( "specularRoughness" );
const InternedString g_sunDirectionParameter( "sunDirection" );
const InternedString g_temperatureParameter( "temperature" );
const InternedString g_textureFileParameter( "texture:file" );
const InternedString g_textureFormatParameter( "texture:format" );
const InternedString g_treatAsPointParameter( "treatAsPoint" );
const InternedString g_treatAsLineParameter( "treatAsLine" );
const InternedString g_typeParameter( "type" );
const InternedString g_usdPrimvarReaderIntShaderName( "UsdPrimvarReader_int" );
const InternedString g_usdPrimvarReaderFloatShaderName( "UsdPrimvarReader_float" );
const InternedString g_varnameParameter( "varname" );
const InternedString g_widthParameter( "width" );

const std::string g_renderManLightNamespace( "ri:light:" );

const std::vector<InternedString> g_pxrSurfaceParameters = {
	g_diffuseGainParameter,
	g_diffuseColorParameter,
	g_specularFaceColorParameter,
	g_specularEdgeColorParameter,
	g_specularRoughnessParameter,
	g_specularIorParameter,
	g_clearcoatFaceColorParameter,
	g_clearcoatEdgeColorParameter,
	g_clearcoatRoughnessParameter,
	g_glowGainParameter,
	g_glowColorParameter,
	g_bumpNormalParameter,
	g_glassIorParameter,
	g_glassRoughnessParameter,
	g_refractionGainParameter,
	g_presenceParameter
};

const std::unordered_map<std::string, std::tuple<std::string, InternedString, std::variant<float, V3f, int>>> g_primVarMap = {
	{ "UsdPrimvarReader_float", { "float", g_defaultFloatParameter, 0.f } },
	{ "UsdPrimvarReader_float2", { "float2", g_defaultFloat3Parameter, V3f( 0.f ) } },
	{ "UsdPrimvarReader_float3", { "vector", g_defaultFloat3Parameter, V3f( 0.f ) } },
	{ "UsdPrimvarReader_normal", { "normal", g_defaultFloat3Parameter, V3f( 0.f ) } },
	{ "UsdPrimvarReader_point", { "point", g_defaultFloat3Parameter, V3f( 0.f ) } },
	{ "UsdPrimvarReader_vector", { "vector", g_defaultFloat3Parameter, V3f( 0.f ) } },
	{ "UsdPrimvarReader_int", { "int", g_defaultIntParameter, 0 } }
};

void transferUSDLightParameters( ShaderNetwork *network, InternedString shaderHandle, const Shader *usdShader, Shader *shader, const float defaultIntensity = 1.f )
{
	transferUSDParameter( network, shaderHandle, usdShader, g_colorParameter, shader, g_lightColorParameter, Color3f( 1.f, 1.f, 1.f ) );
	transferUSDParameter( network, shaderHandle, usdShader, g_diffuseParameter, shader, g_diffuseParameter, 1.0f );
	transferUSDParameter( network, shaderHandle, usdShader, g_exposureParameter, shader, g_exposureParameter, 0.0f );
	transferUSDParameter( network, shaderHandle, usdShader, g_intensityParameter, shader, g_intensityParameter, defaultIntensity );
	transferUSDParameter( network, shaderHandle, usdShader, g_specularParameter, shader, g_specularParameter, 1.0f );
	transferUSDParameter( network, shaderHandle, usdShader, g_enableColorTemperatureParameter, shader, g_enableTemperatureParameter, false );
	transferUSDParameter( network, shaderHandle, usdShader, g_colorTemperatureParameter, shader, g_temperatureParameter, 6500.f );

	transferUSDParameter( network, shaderHandle, usdShader, g_shadowEnableParameter, shader, g_enableShadowsParameter, true );
	transferUSDParameter( network, shaderHandle, usdShader, g_shadowColorUSDParameter, shader, g_shadowColorParameter, Color3f( 0 ) );
	transferUSDParameter( network, shaderHandle, usdShader, g_shadowDistanceUSDParameter, shader, g_shadowDistanceParameter, -1.f );
	transferUSDParameter( network, shaderHandle, usdShader, g_shadowFalloffUSDParameter, shader, g_shadowFalloffParameter, -1.f );
	transferUSDParameter( network, shaderHandle, usdShader, g_shadowFalloffGammaUSDParameter, shader, g_shadowFalloffGammaParameter, 1.f );

	for( const auto &[name, value] : usdShader->parameters() )
	{
		if( boost::starts_with( name.string(), g_renderManLightNamespace ) )
		{
			shader->parameters()[name.string().substr(g_renderManLightNamespace.size())] = value;
		}
	}
}

void transferUSDShapingParameters( ShaderNetwork *network, InternedString shaderHandle, const Shader *usdShader, Shader *shader )
{
	if( auto dFile = usdShader->parametersData()->member<StringData>( g_shapingIesFileParameter ) )
	{
		if( !dFile->readable().empty() )
		{
			shader->parameters()[g_iesProfileParameter] = new StringData( dFile->readable() );
			transferUSDParameter( network, shaderHandle, usdShader, g_shapingIesAngleScaleParameter, shader, g_iesProfileScaleParameter, 0.f );
			transferUSDParameter( network, shaderHandle, usdShader, g_shapingIesNormalizeParameter, shader, g_iesProfileNormalizeParameter, false );
		}
	}

	if( auto dAngle = usdShader->parametersData()->member<FloatData>( g_shapingConeAngleParameter ) )
	{
		shader->parameters()[g_coneAngleParameter] = new FloatData( dAngle->readable() );
		const float softness = parameterValue( usdShader, g_shapingConeSoftnessParameter, 0.f );
		shader->parameters()[g_coneSoftnessParameter] = new FloatData( softness );
	}

	if( auto dFocus = usdShader->parametersData()->member<FloatData>( g_shapingFocusParameter ) )
	{
		shader->parameters()[g_emissionFocusParameter] = new FloatData( dFocus->readable() );
		const Color3f tint = parameterValue( usdShader, g_shapingFocusTintParameter, Color3f( 0.f, 0.f, 0.f ) );
		shader->parameters()[g_emissionFocusTintParameter] = new Color3fData( tint );
	}
}

// standard_surface

const InternedString g_emissionParameter( "emission" );
const InternedString g_emissionValueParameter( "emission_value" );

const InternedString g_subsurfaceParameter( "subsurface" );
const InternedString g_subsurfaceValueParameter( "subsurface_value" );

const InternedString g_diffuseRoughnessParameter( "diffuseRoughness" );
const InternedString g_specularFresnelModeParameter( "specularFresnelMode" );
const InternedString g_specularExtinctionCoeffParameter( "specularExtinctionCoeff" );
const InternedString g_specularAnisotropyParameter( "specularAnisotropy" );
const InternedString g_clearcoatModelTypeParameter( "clearcoatModelType" );
const InternedString g_clearcoatAnisotropyParameter( "clearcoatAnisotropy" );
const InternedString g_reflectionGainParameter( "reflectionGain" );
const InternedString g_refractionColorParameter( "refractionColor" );
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
const InternedString g_fuzzGainParameter( "fuzzGain" );
const InternedString g_fuzzColorParameter( "fuzzColor" );
const InternedString g_fuzzConeAngleParameter( "fuzzConeAngle" );
const InternedString g_iridescenceModeParameter( "iridescenceMode" );
const InternedString g_iridescenceFaceGainParameter( "iridescenceFaceGain" );
const InternedString g_iridescenceEdgeGainParameter( "iridescenceEdgeGain" );
const InternedString g_iridescenceThicknessParameter( "iridescenceThickness" );

const std::vector<InternedString> g_standardSurfaceParameters = {
	g_diffuseGainParameter,
	g_diffuseColorParameter,
	g_diffuseRoughnessParameter,
	g_specularFresnelModeParameter,
	g_specularModelTypeParameter,
	g_specularFaceColorParameter,
	g_specularEdgeColorParameter,
	g_specularRoughnessParameter,
	g_specularIorParameter,
	g_specularExtinctionCoeffParameter,
	g_specularAnisotropyParameter,
	g_clearcoatModelTypeParameter,
	g_clearcoatFaceColorParameter,
	g_clearcoatEdgeColorParameter,
	g_clearcoatRoughnessParameter,
	g_clearcoatAnisotropyParameter,
	g_glowGainParameter,
	g_glowColorParameter,
	g_reflectionGainParameter,
	g_refractionGainParameter,
	g_refractionColorParameter,
	g_glassIorParameter,
	g_glassRoughnessParameter,
	g_glassAnisotropyParameter,
	g_thinGlassParameter,
	g_ssAlbedoParameter,
	g_extinctionParameter,
	g_g0Parameter,
	g_subsurfaceGainParameter,
	g_subsurfaceColorParameter,
	g_subsurfaceDmfpParameter,
	g_subsurfaceDmfpColorParameter,
	g_subsurfaceDirectionalityParameter,
	g_diffuseTransmitGainParameter,
	g_diffuseTransmitColorParameter,
	g_diffuseDoubleSidedParameter,
	g_fuzzGainParameter,
	g_fuzzColorParameter,
	g_fuzzConeAngleParameter,
	g_iridescenceModeParameter,
	g_iridescenceFaceGainParameter,
	g_iridescenceEdgeGainParameter,
	g_iridescenceThicknessParameter,
	g_bumpNormalParameter,
};

// Lama

const std::unordered_map<std::string, std::string> g_lamaNameMap = {
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

// image

const InternedString g_mtlxImageFloatShader( "ND_image_float" );
const InternedString g_mtlxImageColor3Shader( "ND_image_color3" );
const InternedString g_mtlxImageColor4Shader( "ND_image_color4" );
const InternedString g_mtlxImageVector2Shader( "ND_image_vector2" );
const InternedString g_mtlxImageVector3Shader( "ND_image_vector3" );
const InternedString g_mtlxImageVector4Shader( "ND_image_vector4" );

const InternedString g_resultAParameter( "resultA" );
const InternedString g_resultNGParameter( "resultNG" );

const InternedString g_fileParameter( "file" );
const InternedString g_filenameParameter( "filename" );
const InternedString g_defaultParameter( "default" );
const InternedString g_missingColorParameter( "missingColor" );
const InternedString g_filterParameter( "filter" );
const InternedString g_missingAlphaParameter( "missingAlpha" );
const InternedString g_texcoordParameter( "texcoord" );
const InternedString g_manifoldParameter( "manifold" );
const InternedString g_manifoldQParameter( "manifold.Q" );
const InternedString g_nameUvSetParameter( "name_uvSet" );

const InternedString g_resultParameter( "result" );
const InternedString g_resultNParameter( "resultN" );

const InternedString g_inParameter( "in" );
const InternedString g_inputRGBParameter( "inputRGB" );
const InternedString g_scaleParameter( "scale" );
const InternedString g_bumpScaleParameter( "bumpScale" );

const InternedString g_tangentParameter( "tangent" );
const InternedString g_bitangentParameter( "bitangent" );
const InternedString g_flipYParameter( "flipY" );

const std::vector<std::string> g_mtlxImageShaders = {
	g_mtlxImageFloatShader.string(),
	g_mtlxImageColor3Shader.string(),
	g_mtlxImageColor4Shader.string(),
	g_mtlxImageVector2Shader.string(),
	g_mtlxImageVector3Shader.string(),
	g_mtlxImageVector4Shader.string(),
};

const InternedString remapOutputParameterName( const InternedString name, const InternedString shaderName )
{
	if( shaderName == g_mtlxImageFloatShader )
	{
		return g_resultAParameter;
	}
	else if( shaderName == g_mtlxImageColor3Shader )
	{
		return g_resultRGBParameter;
	}
	else if( shaderName == g_mtlxImageColor4Shader )
	{
		return g_resultRGBParameter;
	}
	else if( shaderName == g_mtlxImageVector2Shader ||
				shaderName == g_mtlxImageVector3Shader ||
				shaderName == g_mtlxImageVector4Shader )
	{
		// There is resultNG but not suitable from my testing eg.
		// PxrTexture -> PxrNormalMap is a better fit.
		return g_resultRGBParameter;
	}
	else if( boost::starts_with( shaderName.string(), "UsdPrimvarReader" ) )
	{
		if( shaderName == g_usdPrimvarReaderFloatShaderName )
		{
			return g_resultFParameter;
		}
		else if( shaderName == g_usdPrimvarReaderIntShaderName )
		{
			return g_resultIParameter;
		}
		else
		{
			return g_resultRGBParameter;
		}
	}
	else if( boost::starts_with( shaderName.string(), "ND_texcoord" ) )
	{
		return g_resultParameter;
	}
	else if( boost::starts_with( shaderName.string(), "ND_normalmap" ) )
	{
		return g_resultNParameter;
	}

	return name;
}

void replaceUSDShader( ShaderNetwork *network, InternedString handle, ShaderPtr &&newShader )
{
	const InternedString shaderName = network->getShader( handle )->getName();

	// Replace original shader with the new.
	network->setShader( handle, std::move( newShader ) );

	// Iterating over a copy because we will modify the range during iteration
	ShaderNetwork::ConnectionRange range = network->outputConnections( handle );
	std::vector<ShaderNetwork::Connection> outputConnections( range.begin(), range.end() );
	for( auto &c : outputConnections )
	{
		const InternedString remappedName = remapOutputParameterName( c.source.name, shaderName );
		if( remappedName != c.source.name )
		{
			network->removeConnection( c );
			c.source.name = remapOutputParameterName( c.source.name, shaderName );
			network->addConnection( c );
		}
	}
}

void correctParameters( ShaderNetwork *network )
{
	const Shader *shader = network->outputShader();
	if( shader && shader->getName() == "PxrEnvDayLight" )
	{
		ShaderPtr newShader = shader->copy();

		// The incoming object-space coordinates of `sunDirection` is in our Y-up coordinate system.
		// But RenderMan's orientation is Z-up, so we transform from our coordinate system to RenderMan's
		// so the parameter is intuitive to work with and the appearance of the shader matches expectations.
		const V3f direction = parameterValue( newShader.get(), g_sunDirectionParameter, V3f( 0.f, 1.f, 0.f ) );
		newShader->parameters()[g_sunDirectionParameter] = new V3fData( V3f( direction.x, -direction.z, direction.y ) );

		network->setShader( network->getOutput().shader, std::move( newShader ) );
	}
}

ShaderNetworkPtr preprocessedNetwork( const IECoreScene::ShaderNetwork *shaderNetwork )
{
	ShaderNetworkPtr result = shaderNetwork->copy();

	correctParameters( result.get() );

	IECoreScene::ShaderNetworkAlgo::expandSplines( result.get() );

	IECoreRenderMan::ShaderNetworkAlgo::convertUSDShaders( result.get() );
	IECoreMaterialX::ShaderNetworkAlgo::convertToOSLNodes( result.get(), "renderman" );

	return result;
}

} // namespace

//////////////////////////////////////////////////////////////////////////
// External API
//////////////////////////////////////////////////////////////////////////

namespace IECoreRenderMan::ShaderNetworkAlgo
{

std::vector<riley::ShadingNode> convert( const IECoreScene::ShaderNetwork *network )
{
	ConstShaderNetworkPtr preprocessedNetwork = ::preprocessedNetwork( network );
	vector<riley::ShadingNode> result;
	result.reserve( preprocessedNetwork->size() );

	HandleSet visited;
	convertShaderNetworkWalk( preprocessedNetwork->getOutput(), preprocessedNetwork.get(), result, visited );

	return result;
}

IECoreScene::ConstShaderNetworkPtr combineLightFilters( const std::vector<const IECoreScene::ShaderNetwork *> networks )
{
	if( networks.empty() )
	{
		return nullptr;
	}

	if( networks.size() == 1 )
	{
		return networks[0];
	}

	unordered_map<string, size_t> numConnections;

	ShaderNetworkPtr combinedNetwork = new ShaderNetwork;
	auto combinerHandle = combinedNetwork->addShader(
		"combiner", new Shader( "PxrCombinerLightFilter", "lightFilter" )
	);
	combinedNetwork->setOutput( { combinerHandle, "out" } );

	for( auto network : networks )
	{
		const Shader *outputShader = network->outputShader();
		if( !outputShader )
		{
			continue;
		}

		string combineMode = "mult";
		if( auto combineModeData = outputShader->parametersData()->member<StringData>( "combineMode" ) )
		{
			combineMode = combineModeData->readable();
		}

		ShaderNetwork::Parameter filterHandle = IECoreScene::ShaderNetworkAlgo::addShaders( combinedNetwork.get(), network );

		const size_t connectionIndex = numConnections[combineMode]++;
		combinedNetwork->addConnection(
			ShaderNetwork::Connection( filterHandle, { combinerHandle, fmt::format( "{}[{}]", combineMode, connectionIndex ) } )
		);
	}

	return combinedNetwork;
}

void convertUSDShaders( ShaderNetwork *shaderNetwork )
{
	for( const auto &[handle, shader] : shaderNetwork->shaders() )
	{
		ShaderPtr newShader;
		if( shader->getName() == "UsdPreviewSurface" || shader->getName() == "ND_UsdPreviewSurface_surfaceshader" )
		{
			newShader = new Shader( "__usd/__UsdPreviewSurfaceParameters", "osl:shader" );

			// `UsdPreviewSurface` and `UsdPreviewSurfaceParameters` match except for `normal` -> `normalIn`.
			for( const auto &[p, v] : shader->parameters() )
			{
				newShader->parameters()[p != g_normalParameter ? p : g_normalInParameter] = v;
			}

			ShaderPtr pxrSurfaceShader = new Shader( "PxrSurface", "ri:surface" );
			// Use GGX instead of Beckman specular model.
			pxrSurfaceShader->parameters()[g_specularModelTypeParameter] = new IECore::IntData( 1 );
			pxrSurfaceShader->parameters()[g_diffuseDoubleSidedParameter] = new IECore::IntData( 1 );
			pxrSurfaceShader->parameters()[g_specularDoubleSidedParameter] = new IECore::IntData( 1 );
			pxrSurfaceShader->parameters()[g_roughSpecularDoubleSidedParameter] = new IECore::IntData( 1 );
			pxrSurfaceShader->parameters()[g_clearcoatDoubleSidedParameter] = new IECore::IntData( 1 );

			const InternedString pxrSurfaceHandle = shaderNetwork->addShader( handle.string() + "PxrSurface", std::move( pxrSurfaceShader ) );

			for( const auto &p : g_pxrSurfaceParameters )
			{
				shaderNetwork->addConnection( ShaderNetwork::Connection( { handle, InternedString( p.string() + "Out" ) }, { pxrSurfaceHandle, p } ) );
			}

			shaderNetwork->setOutput( { pxrSurfaceHandle, "" } );
		}
		else if( shader->getName() == "SphereLight" )
		{
			newShader = new Shader( "PxrSphereLight", "ri:light" );
			transferUSDLightParameters( shaderNetwork, handle, shader.get(), newShader.get() );
			transferUSDShapingParameters( shaderNetwork, handle, shader.get(), newShader.get() );
			transferUSDParameter( shaderNetwork, handle, shader.get(), g_normalizeParameter, newShader.get(), g_areaNormalizeParameter, false );

			if( parameterValue( shader.get(), g_treatAsPointParameter, false ) )
			{
				newShader->parameters()[g_areaNormalizeParameter] = new BoolData( true );
			}
		}
		else if( shader->getName() == "DiskLight" )
		{
			newShader = new Shader( "PxrDiskLight", "ri:light" );
			transferUSDLightParameters( shaderNetwork, handle, shader.get(), newShader.get() );
			transferUSDShapingParameters( shaderNetwork, handle, shader.get(), newShader.get() );
			transferUSDParameter( shaderNetwork, handle, shader.get(), g_normalizeParameter, newShader.get(), g_areaNormalizeParameter, false );
		}
		else if( shader->getName() == "RectLight" )
		{
			newShader = new Shader( "PxrRectLight", "ri:light" );
			transferUSDLightParameters( shaderNetwork, handle, shader.get(), newShader.get() );
			transferUSDShapingParameters( shaderNetwork, handle, shader.get(), newShader.get() );
			transferUSDParameter( shaderNetwork, handle, shader.get(), g_normalizeParameter, newShader.get(), g_areaNormalizeParameter, false );

			const std::string textureFile = parameterValue( shader.get(), g_textureFileParameter, std::string() );
			if( !textureFile.empty() )
			{
				newShader->parameters()[g_lightColorMapParameter] = new StringData( textureFile );
			}
		}
		else if( shader->getName() == "DistantLight" )
		{
			newShader = new Shader( "PxrDistantLight", "ri:light" );
			transferUSDLightParameters( shaderNetwork, handle, shader.get(), newShader.get(), 50000.f );
			transferUSDShapingParameters( shaderNetwork, handle, shader.get(), newShader.get() );
			transferUSDParameter( shaderNetwork, handle, shader.get(), g_normalizeParameter, newShader.get(), g_areaNormalizeParameter, false );

			const float angle = parameterValue( shader.get(), g_angleParameter, 0.53f );
			newShader->parameters()[g_angleExtentParameter] = new FloatData( angle );
		}
		else if( shader->getName() == "DomeLight" )
		{
			newShader = new Shader( "PxrDomeLight", "ri:light" );
			transferUSDLightParameters( shaderNetwork, handle, shader.get(), newShader.get() );

			const std::string textureFile = parameterValue( shader.get(), g_textureFileParameter, std::string() );
			if( !textureFile.empty() )
			{
				newShader->parameters()[g_lightColorMapParameter] = new StringData( textureFile );
			}

			const std::string textureFormat = parameterValue( shader.get(), g_textureFormatParameter, std::string() );
			if( textureFormat != "automatic" )
			{
				IECore::msg(
					IECore::Msg::Warning,
					"convertUSDShaders",
					fmt::format( "Unsupported value \"{}\" for DomeLight.format. Only \"automatic\" is supported. Format will be read from texture file.", textureFormat )
				);
			}
		}
		else if( shader->getName() == "CylinderLight" )
		{
			newShader = new Shader( "PxrCylinderLight", "ri:light" );
			transferUSDLightParameters( shaderNetwork, handle, shader.get(), newShader.get() );
			transferUSDShapingParameters( shaderNetwork, handle, shader.get(), newShader.get() );
			transferUSDParameter( shaderNetwork, handle, shader.get(), g_normalizeParameter, newShader.get(), g_areaNormalizeParameter, false );

			if( parameterValue( shader.get(), g_treatAsLineParameter, false ) )
			{
				newShader->parameters()[g_areaNormalizeParameter] = new BoolData( true );
			}
		}

		const auto it = g_primVarMap.find( shader->getName() );
		if( it != g_primVarMap.end() )
		{
			newShader = new Shader( "PxrAttribute", "osl:shader" );
			const auto &[typeName, defaultParameter, defaultValue] = it->second;

			newShader->parameters()[g_typeParameter] = new StringData( typeName );
			transferUSDParameter( shaderNetwork, handle, shader.get(), g_varnameParameter, newShader.get(), g_varnameParameter, string() );
			std::visit(
				[&shaderNetwork, &handle=handle, &shader=shader, &newShader, &defaultParameter=defaultParameter]( auto &&v )
				{
					transferUSDParameter( shaderNetwork, handle, shader.get(), g_fallbackParameter, newShader.get(), defaultParameter, v );
				},
				defaultValue
			);
		}

		if( shader->getName() == "standard_surface" || shader->getName() == "ND_standard_surface_surfaceshader" )
		{
			newShader = new Shader( "__renderman/__StandardSurfaceParameters.oso", "osl:shader" );

			transferUSDParameter( shaderNetwork, handle, shader.get(), g_normalParameter, newShader.get(), g_normalInParameter, V3f( 0.0 ) );
			transferUSDParameter( shaderNetwork, handle, shader.get(), g_emissionParameter, newShader.get(), g_emissionValueParameter, 0.0f );
			transferUSDParameter( shaderNetwork, handle, shader.get(), g_subsurfaceParameter, newShader.get(), g_subsurfaceValueParameter, 0.0f );

			for( const auto &[name, value] : shader->parameters() )
			{
				if( name == g_normalParameter ||
					name == g_emissionParameter ||
					name == g_subsurfaceParameter
				)
				{
					continue;
				}

				newShader->parameters()[name] = value;
			}

			ShaderPtr pxrSurfaceShader = new Shader( "PxrSurface", "ri:surface" );
			// Use GGX instead of Beckman specular model.
			pxrSurfaceShader->parameters()[g_specularModelTypeParameter] = new IECore::IntData( 1 );
			pxrSurfaceShader->parameters()[g_diffuseDoubleSidedParameter] = new IECore::IntData( 1 );
			pxrSurfaceShader->parameters()[g_specularDoubleSidedParameter] = new IECore::IntData( 1 );
			pxrSurfaceShader->parameters()[g_roughSpecularDoubleSidedParameter] = new IECore::IntData( 1 );
			pxrSurfaceShader->parameters()[g_clearcoatDoubleSidedParameter] = new IECore::IntData( 1 );

			const InternedString pxrSurfaceHandle = shaderNetwork->addShader( handle.string() + "PxrSurface", std::move( pxrSurfaceShader ) );

			for( const auto &p : g_standardSurfaceParameters )
			{
				shaderNetwork->addConnection( ShaderNetwork::Connection( { handle, InternedString( p.string() + "Out" ) }, { pxrSurfaceHandle, p } ) );
			}

			shaderNetwork->setOutput( { pxrSurfaceHandle, "" } );
		}

		const auto lamaIt = g_lamaNameMap.find( shader->getName() );
		if( lamaIt != g_lamaNameMap.end() )
		{
			newShader = new Shader( lamaIt->second, "ri:surface" );
			for( const auto &[name, value] : shader->parameters() )
			{
				newShader->parameters()[name] = value;
			}
		}

		// Currently `file_colorspace` MaterialX GenOSL will error out, so we just swap
		// them with native PxrTexture. Possible future solution:
		// https://github.com/AcademySoftwareFoundation/MaterialX/pull/2263
		for( const std::string &imageShaderName : g_mtlxImageShaders )
		{
			if( shader->getName() == imageShaderName )
			{
				newShader = new Shader( "PxrTexture", "osl:shader" );

				transferUSDParameter( shaderNetwork, handle, shader.get(), g_fileParameter, newShader.get(), g_filenameParameter, std::string() );
				//transferUSDParameterRGBA( shaderNetwork, handle, shader.get(), g_defaultParameter, newShader.get(), g_missingColorParameter, g_missingAlphaParameter, Color4f( 0.0f, 0.0f, 0.0f, 1.0f ) );
				transferUSDParameter( shaderNetwork, handle, shader.get(), g_defaultParameter, newShader.get(), g_missingColorParameter, Color3f( 0.0f ) );
				transferUSDParameter( shaderNetwork, handle, shader.get(), g_filterTypeParameter, newShader.get(), g_filterParameter, 2 );
				//transferUSDParameter( shaderNetwork, handle, shader.get(), g_defaultParameter, newShader.get(), g_missingAlphaParameter, 1.0f );
				transferUSDParameter( shaderNetwork, handle, shader.get(), g_texcoordParameter, newShader.get(), g_manifoldParameter, V3f( 0.0f ) );
			}
		}

		if( boost::starts_with( shader->getName(), "ND_texcoord" ) )
		{
			newShader = new Shader( "PxrManifold2D", "osl:shader" );
			newShader->parameters()[g_nameUvSetParameter] = new StringData( "st" );
		}

		if( boost::starts_with( shader->getName(), "ND_normalmap" ) )
		{
			newShader = new Shader( "PxrNormalMap", "osl:shader" );

			transferUSDParameter( shaderNetwork, handle, shader.get(), g_inParameter, newShader.get(), g_inputRGBParameter, V3f( 0.0f ) );
			transferUSDParameter( shaderNetwork, handle, shader.get(), g_scaleParameter, newShader.get(), g_bumpScaleParameter, 1.0f );
			newShader->parameters()[g_flipYParameter] = new IntData( 1 );

			if( const ShaderNetwork::Parameter tangentInput = shaderNetwork->input( { handle, g_tangentParameter } ) )
			{
				const Shader *inShader = shaderNetwork->getShader( tangentInput.shader );
				if( boost::starts_with( inShader->getName(), "ND_tangent" ) )
				{
					IECore::msg(
						IECore::Msg::Warning,
						"IECoreRenderMan",
						fmt::format( "MaterialX node `{}` is not supported in a name-based renderer.",
						inShader->getName() ) );
				}
				shaderNetwork->removeConnection( { tangentInput, { handle, g_tangentParameter } } );
			}

			if( const ShaderNetwork::Parameter bitangentInput = shaderNetwork->input( { handle, g_bitangentParameter } ) )
			{
				const Shader *inShader = shaderNetwork->getShader( bitangentInput.shader );
				if( boost::starts_with( inShader->getName(), "ND_bitangent" ) )
				{
					IECore::msg(
						IECore::Msg::Warning,
						"IECoreRenderMan",
						fmt::format( "MaterialX node `{}` is not supported in a name-based renderer.",
						inShader->getName() ) );
				}
				shaderNetwork->removeConnection( { bitangentInput, { handle, g_bitangentParameter } } );
			}
		}

		if( newShader )
		{
			replaceUSDShader( shaderNetwork, handle, std::move( newShader ) );
		}
	}
	IECoreScene::ShaderNetworkAlgo::removeUnusedShaders( shaderNetwork );
}

M44f usdLightTransform( const Shader *lightShader )
{
	assert( lightShader );

	if( lightShader->getName() == "SphereLight" )
	{
		const float radius = !parameterValue( lightShader, g_treatAsPointParameter, false ) ?
			parameterValue( lightShader, g_radiusParameter, 0.5f ) :
			0.001f
		;
		return M44f().scale( V3f( radius * 2.f ) );
	}
	else if( lightShader->getName() == "DiskLight" )
	{
		const float radius = parameterValue( lightShader, g_radiusParameter, 0.5f );
		return M44f().scale( V3f( radius * 2.f ) );
	}
	else if( lightShader->getName() == "RectLight" )
	{
		const float width = parameterValue( lightShader, g_widthParameter, 1.f );
		const float height = parameterValue( lightShader, g_heightParameter, 1.f );
		return M44f().scale( V3f( width, height, 1.f ) );
	}
	else if( lightShader->getName() == "CylinderLight" )
	{
		const float length = parameterValue( lightShader, g_lengthParameter, 1.f );
		const float radius = !parameterValue( lightShader, g_treatAsLineParameter, false ) ?
			parameterValue( lightShader, g_radiusParameter, 0.5f ) :
			0.001f
		;

		return M44f().scale( V3f( length, radius * 2.f, radius * 2.f ) );
	}

	return M44f();
}

} // namespace IECoreRenderMan::ShaderNetworkAlgo
