//////////////////////////////////////////////////////////////////////////
//
//  Copyright (c) 2025, Alex Fuller. All rights reserved.
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

#include "IECoreMaterialX/ShaderNetworkAlgo.h"

#include "IECoreMaterialX/CompileAlgo.h"

#include "IECore/LRUCache.h"
#include "IECore/MessageHandler.h"
#include "IECore/SearchPath.h"
#include "IECore/SimpleTypedData.h"
#include "IECore/VectorTypedData.h"

#include "IECoreScene/ShaderNetworkAlgo.h"

#include "OSL/oslquery.h"

#include "MaterialXCore/Node.h"
#include "MaterialXCore/Document.h"
#include "MaterialXFormat/Environ.h"
#include "MaterialXFormat/Util.h"
#include "MaterialXFormat/XmlIo.h"
#include "MaterialXGenShader/Shader.h"
#include "MaterialXGenShader/Util.h"
#include "MaterialXGenOsl/OslShaderGenerator.h"
#include "MaterialXRender/Util.h"

#include "boost/algorithm/string.hpp"
#include "boost/algorithm/string/predicate.hpp"
#include "boost/container/flat_map.hpp"

#include "fmt/format.h"

#include <array>

using namespace std;
using namespace IECore;
using namespace IECoreScene;
using namespace IECoreMaterialX;

namespace mx = MaterialX;
namespace fs = std::filesystem;
namespace
{

const InternedString g_arnoldRendererName( "arnold" );
const InternedString g_cyclesRendererName( "cycles" );
const InternedString g_rendermanRendererName( "renderman" );

const InternedString g_inParameter( "in" );
const InternedString g_in1Parameter( "in1" );
const InternedString g_in2Parameter( "in2" );
const InternedString g_in3Parameter( "in3" );
const InternedString g_in4Parameter( "in4" );

const InternedString g_outParameter( "out" );
const InternedString g_outrParameter( "outr" );
const InternedString g_outgParameter( "outg" );
const InternedString g_outbParameter( "outb" );
const InternedString g_outDotrgbParameter( "out.rgb" );
const InternedString g_outDotaParameter( "out.a" );
const InternedString g_outxParameter( "outx" );
const InternedString g_outyParameter( "outy" );
const InternedString g_outDotxParameter( "out.x" );
const InternedString g_outDotyParameter( "out.y" );
const InternedString g_outDotzParameter( "out.z" );
const InternedString g_outDotwParameter( "out.w" );

const InternedString g_resultParameter( "result" );
const InternedString g_surfaceParameter( "surface" );
const InternedString g_opacityModeParameter( "opacityMode" );

const InternedString g_separateColorShaderName( "separate_color" );
const InternedString g_separateVectorShaderName( "separate_vector" );
const InternedString g_combineColorShaderName( "combine_color" );
const InternedString g_combineVectorShaderName( "combine_vector" );

const InternedString g_usdPreviewSurfaceShader( "UsdPreviewSurface" );
const InternedString g_mtlxUsdPreviewSurfaceShader( "ND_UsdPreviewSurface_surfaceshader" );

// TODO: Remove this once we know why base value of 0.0f crashes cycles!
const InternedString g_baseParameter( "base" );
const InternedString g_mtlxStandardSurfaceShader( "ND_standard_surface_surfaceshader" );

const InternedString g_mtlxUVTextureShader( "ND_UsdUVTexture" );
const InternedString g_sourceColorSpaceParameter( "sourceColorSpace" );
const InternedString g_fileColorspaceParameter( "file_colorspace" );

const InternedString g_mtlxVector2Type( "vector2" );
const InternedString g_mtlxVector4Type( "vector4" );
const InternedString g_mtlxColor4Type( "color4" );

// For backwards-compatibility translation
const InternedString g_inxParameter( "inx" );
const InternedString g_inyParameter( "iny" );
const InternedString g_mtlxAtan2FloatShader( "ND_atan2_float" );
const InternedString g_mtlxAtan2Vector2Shader( "ND_atan2_vector2" );
const InternedString g_mtlxAtan2Vector3Shader( "ND_atan2_vector3" );
const InternedString g_mtlxAtan2Vector4Shader( "ND_atan2_vector4" );

const string g_usdNamePrefix( "Usd" );
const string g_mtlxNDNamePrefix( "ND_" );
//const string g_mtlxNDImageNamePrefix( "ND_image" );
//const string g_mtlxNDTexcoordNamePrefix( "ND_texcoord" );
//const string g_mtlxNDGeompropvaluePrefix( "ND_geompropvalue" );

const string g_mtlxPrefix( "mtlx:" );
const string g_mtlxVector2Suffix( "vector2" );
const string g_mtlxVector4Suffix( "vector4" );
const string g_mtlxColor4Suffix( "color4" );
const string g_oslShader( "osl:shader" );

const string g_mtlxUsdTransform2dShader( "ND_UsdTransform2d" );
//const string g_mtlxGeompropvalueVector2Shader( "ND_geompropvalue_vector2" );

// Use these converter adapters to smooth out rough edging
// for vector2/vector4/color4 structs are used when MaterialX
// translates these to OSL inputs and outputs.
const string g_mtlxCombine3Color3Shader( "ND_combine3_color3" );
const string g_mtlxCombine3Vector3Shader( "ND_combine3_vector3" );
const string g_mtlxSeparate3Color3Shader( "ND_separate3_color3" );
const string g_mtlxSeparate3Vector3Shader( "ND_separate3_vector3" );

// TODO: Our way of detecting the type by checking the suffix name color4
// fails miserably sometimes, need to find a better way.
const string g_mtlxGltfImageColor4Shader( "ND_gltf_image_color4_color4_1_0" );

const string g_usdTransform2dShader( "UsdTransform2d" );
const string g_usdPrimvarReaderFloat2Shader( "UsdPrimvarReader_float2" );
const string g_usdPrimvarReaderFloat4Shader( "UsdPrimvarReader_float4" );

const string g_presenceName( "presence" );

// Convert any USD shader nodes into their named equivalents in MaterialX
boost::container::flat_map<string, string> g_nameOverrides = {
	{ "UsdPreviewSurface", "ND_UsdPreviewSurface_surfaceshader" },
	{ "UsdTransform2d", "ND_UsdTransform2d" },
	{ "UsdUVTexture", "ND_UsdUVTexture" },
	{ "UsdPrimvarReader_float", "ND_UsdPrimvarReader_float" },
	{ "UsdPrimvarReader_float2", "ND_UsdPrimvarReader_vector2" },
	{ "UsdPrimvarReader_float3", "ND_UsdPrimvarReader_vector3" },
	{ "UsdPrimvarReader_float4", "ND_UsdPrimvarReader_vector4" },
	{ "UsdPrimvarReader_point", "ND_UsdPrimvarReader_vector3" },
	{ "UsdPrimvarReader_vector", "ND_UsdPrimvarReader_vector3" },
	{ "UsdPrimvarReader_normal", "ND_UsdPrimvarReader_vector3" },
	{ "UsdPrimvarReader_int", "ND_UsdPrimvarReader_integer" },
	{ "UsdPrimvarReader_string", "ND_UsdPrimvarReader_string" },
#if MATERIALX_MAJOR_VERSION == 1 && MATERIALX_MINOR_VERSION >= 39
	// TODO: Look into seeing if there's an automatic way to detect old nodes
	// and upgrade them to their newer names? Might be something for usdMtlx to address...
	// https://github.com/AcademySoftwareFoundation/MaterialX/releases/tag/v1.39.0
	{ "ND_normalmap", "ND_normalmap_float"},
#endif
};

array<InternedString, 4> g_inComponents = { { g_in1Parameter, g_in2Parameter, g_in3Parameter, g_in4Parameter } };

using ParamMap = std::map<InternedString, vector<const OSL::OSLQuery::Parameter*>>;
using StructParamMap = std::map<InternedString, const OSL::OSLQuery::Parameter*>;

fs::path shaderCacheGetter( const std::string &shaderName, size_t &cost )
{
	cost = 1;
	const char *oslShaderPaths = getenv( "OSL_SHADER_PATHS" );
	SearchPath searchPath( oslShaderPaths ? oslShaderPaths : "" );
	std::string lookupName = fmt::format( "__mtlx/__{}", shaderName );
	boost::filesystem::path path = searchPath.find( lookupName + ".oso" );
	if( path.empty() )
	{
		try
		{
			return IECoreMaterialX::ShaderNetworkAlgo::convertToOSO( lookupName, shaderName );
		}
		catch( ... )
		{
			msg(
				Msg::Warning, "IECoreMaterialX::ShaderNetworkAlgo",
				fmt::format( "Couldn't convert MaterialX shader \"{}\".", lookupName )
			);
			return {};
		}
	}
	else
	{
		return path.generic_string();
	}
}

typedef IECore::LRUCache<std::string, fs::path> ShaderSearchPathCache;
ShaderSearchPathCache g_shaderSearchPathCache( shaderCacheGetter, 10000 );

const InternedString remapOutputParameterName( const InternedString name, const InternedString shaderName )
{
	if( shaderName == g_usdPreviewSurfaceShader && name == g_surfaceParameter )
	{
		return g_outParameter;
	}
	else if( name == g_resultParameter )
	{
		auto typeIt = g_nameOverrides.find( shaderName.string() );
		if( typeIt != g_nameOverrides.end() )
		{
			return g_outParameter;
		}
	}

	return name;
}

// For 'upgrading' parameter names eg. USD files with older published MaterialX files
// TODO: Can MaterialX itself or usdMtlx do the upgrading when loading in a USD file?
const InternedString getParameterName( const InternedString &shaderName, const InternedString &parameterName )
{
#if MATERIALX_MAJOR_VERSION == 1 && MATERIALX_MINOR_VERSION >= 39
	if( shaderName == g_mtlxAtan2FloatShader ||
		shaderName == g_mtlxAtan2Vector2Shader ||
		shaderName == g_mtlxAtan2Vector3Shader ||
		shaderName == g_mtlxAtan2Vector4Shader )
	{
		if( parameterName == g_in1Parameter )
		{
			return g_inyParameter;
		}
		if( parameterName == g_in2Parameter )
		{
			return g_inxParameter;
		}
	}
#endif
	if( shaderName == g_mtlxUVTextureShader && parameterName == g_sourceColorSpaceParameter )
	{
		return g_fileColorspaceParameter;
	}
	return parameterName;
}

void replaceMtlxShader( ShaderNetwork *network, InternedString handle, ShaderPtr &&newShader, ParamMap &outParamMap )
{
	const InternedString shaderName = network->getShader( handle )->getName();

	// Replace original shader with the new.
	network->setShader( handle, std::move( newShader ) );

	// When replacing the output shader, remap the network output parameter name.
	ShaderNetwork::Parameter outParameter = network->getOutput();
	if( outParameter.shader == handle )
	{
		// If the output is a vector2 or vector4, these are structs with separated components
		// that we need to take the first 2 or 3 float values and connect them into a combiner.
		InternedString parameterName = remapOutputParameterName( outParameter.name, shaderName );
		auto typeIt = outParamMap.find( parameterName );
		if( typeIt != outParamMap.end() )
		{
			// color4 has out.rgb + out.a form, so just use out.rgb
			if( typeIt->second.size() == 2 && InternedString( typeIt->second[0]->name.c_str() ) == g_outDotrgbParameter )
			{
				outParameter.name = g_outDotrgbParameter;
			}
			else
			{
				fs::path combinePath = g_shaderSearchPathCache.get( g_mtlxCombine3Color3Shader );
				ShaderPtr combine = new Shader( combinePath.replace_extension().generic_string(), g_oslShader );
				InternedString combineHandle = network->addShader( g_combineColorShaderName, std::move( combine ) );
				for( size_t i = 0; i < typeIt->second.size() && i < g_inComponents.size(); ++i )
				{
					network->addConnection( { { outParameter.shader, typeIt->second[i]->name.c_str() }, { combineHandle, g_inComponents[i] } } );
				}
				outParameter.shader = combineHandle;
				outParameter.name = g_outParameter;
			}
		}
		else
		{
			outParameter.name = parameterName;
		}
		network->setOutput( outParameter );
	}

	// Iterating over a copy because we will modify the range during iteration.
	ShaderNetwork::ConnectionRange range = network->outputConnections( handle );
	vector<ShaderNetwork::Connection> outputConnections( range.begin(), range.end() );
	for( auto &c : outputConnections )
	{
		InternedString parameterName = remapOutputParameterName( c.source.name, shaderName );
		auto typeIt = outParamMap.find( parameterName );
		if( typeIt != outParamMap.end() )
		{
			// color4 has out.rgb + out.a form, so just use out.rgb (unless it has been done already)
			if( c.source.name != g_outDotrgbParameter &&
				typeIt->second.size() == 2 &&
				InternedString( typeIt->second[0]->name.c_str() ) == g_outDotrgbParameter )
			{
				IECore::msg(
					IECore::Msg::Debug, "IECoreMaterialX::ShaderNetworkAlgo",
					fmt::format(
						"Remapped color4 struct connection `{}.{}` -> `{}.{}` into `{}->{}`, but skipping `{}`",
						c.source.shader.string(), c.source.name.string(), c.destination.shader.string(), c.destination.name.string(),
						g_outDotrgbParameter.string(),  c.destination.name.string(), g_outDotaParameter.string()
					)
				);

				// Unfortunately Renderman seems to not resolve color4 outputs eg. out.rgb so this
				// needs further investigation, or avoid any color4/vector4 output nodes?
				network->removeConnection( c );
				c.source.name = g_outDotrgbParameter;
				network->addConnection( c );
			}
			else if( typeIt->second.size() == 2 && InternedString( typeIt->second[0]->name.c_str() ) == g_outDotxParameter )
			{
				// vector2 .x/y -> vector3
				fs::path combinePath = g_shaderSearchPathCache.get( g_mtlxCombine3Vector3Shader ) ;
				ShaderPtr combine = new Shader( combinePath.replace_extension().generic_string(), g_oslShader );
				InternedString combineHandle = network->addShader( g_combineVectorShaderName, std::move( combine ) );
				for( size_t i = 0; i < typeIt->second.size() && i < g_inComponents.size(); ++i )
				{
					network->addConnection( { { outParameter.shader, typeIt->second[i]->name.c_str() }, { combineHandle, g_inComponents[i] } } );
				}
				network->removeConnection( c );
				c.source.shader = combineHandle;
				network->addConnection( c );

				IECore::msg(
					IECore::Msg::Debug, "IECoreMaterialX::ShaderNetworkAlgo",
					fmt::format(
						"Remapped vector2 struct connection `{}.{}` -> `{}.{}` using adapter into `.{}|.{}->in1|in2->{}.{}->{}`",
						c.source.shader.string(), c.source.name.string(), c.destination.shader.string(), c.destination.name.string(),
						g_outDotxParameter.string(), g_outDotyParameter.string(), g_combineColorShaderName.string(), g_outParameter.string(), c.destination.name.string()
					)
				);
			}
		}
	}
	IECore::msg(
		IECore::Msg::Debug, "IECoreMaterialX::ShaderNetworkAlgo",
		fmt::format(
			"Replaced MaterialX node `{}` with an OSL implementation.",
			shaderName
		)
	);
}

std::vector<InternedString> getMtlxNodeParameterNames( const std::string &mxNodeName )
{
	mx::FileSearchPath searchPath = mx::FileSearchPath( getenv( "PXR_MTLX_STDLIB_SEARCH_PATHS" ) );
	mx::FilePathVec libraryFolders = { "libraries" };

	mx::DocumentPtr mxDoc = mx::createDocument();
	mx::loadLibraries( libraryFolders, searchPath, mxDoc );
	for( const mx::NodeDefPtr& nodeDef : mxDoc->getNodeDefs() )
	{
		if( nodeDef->getName() == mxNodeName )
		{
			vector<mx::InputPtr> mxNodeInputs = nodeDef->getActiveInputs();
			std::vector<InternedString> inputs;
			inputs.reserve( mxNodeInputs.size() );
			for( const mx::InputPtr& input : mxNodeInputs )
			{
				inputs.push_back( input->getName() );
			}
			return inputs;
		}
	}

	msg( Msg::Warning, "IECoreMaterialX::ShaderNetworkAlgo", fmt::format( "Node definition '{}' not found in MaterialX Library.", mxNodeName ) );
	std::vector<InternedString> empty;
	return empty;
}

}; // namespace

std::string IECoreMaterialX::ShaderNetworkAlgo::convertToOSL( const std::string &shaderName, const std::string &mxNodeName )
{
	mx::FileSearchPath searchPath = mx::FileSearchPath( getenv( "PXR_MTLX_STDLIB_SEARCH_PATHS" ) );
	mx::FilePathVec libraryFolders = { "libraries" };
	mx::ShaderGeneratorPtr generator = mx::OslShaderGenerator::create();
	mx::GenContext context( generator );
	context.getOptions().addUpstreamDependencies = false;
	context.registerSourceCodeSearchPath( searchPath );
	context.getOptions().fileTextureVerticalFlip = true;

	mx::DocumentPtr mxDoc = mx::createDocument();
	mx::loadLibraries( libraryFolders, searchPath, mxDoc );
	mx::NodeDefPtr mxDef;
	for( const mx::NodeDefPtr& nodeDef : mxDoc->getNodeDefs() )
	{
		if( nodeDef->getName() == mxNodeName )
		{
			mxDef = nodeDef;
			break;
		}
	}

	if( !mxDef )
	{
		msg( Msg::Warning, "IECoreMaterialX::ShaderNetworkAlgo", fmt::format( "Node definition '{}' not found in MaterialX Library.", mxNodeName ) );
		return mx::EMPTY_STRING;
	}

	mx::NodePtr mxNode = mxDoc->addNodeInstance( mxDef, shaderName );

	if( !mxNode )
	{
		msg( Msg::Warning, "IECoreMaterialX::ShaderNetworkAlgo", fmt::format( "Node '{}' not found in MaterialX Library.", shaderName ) );
		return mx::EMPTY_STRING;
	}

	// Generate the OSL Shader for the Node
	msg( Msg::Info, "IECoreMaterialX::ShaderNetworkAlgo", fmt::format( "Generate a MaterialX OSL shader for '{}' node.", shaderName ) );
	mx::ShaderPtr mxShader;

	try
	{
		mxShader = generator->generate( mxNode->getName(), mxNode, context );
	}
	catch( mx::Exception& exception )
	{
		msg( Msg::Warning, "IECoreMaterialX::ShaderNetworkAlgo", fmt::format( "Unable to create OSL Shader '{}' from MaterialX node '{}'.\nMxException: {}", shaderName, mxNodeName, exception.what() ) );
		return mx::EMPTY_STRING;
	}

	if( !mxShader )
	{
		msg( Msg::Warning, "IECoreMaterialX::ShaderNetworkAlgo", fmt::format( "Unable to create OSL Shader '{}' for node '{}'.", shaderName, mxNodeName ) );
		return mx::EMPTY_STRING;
	}

	return mxShader->getSourceCode();

}

fs::path IECoreMaterialX::ShaderNetworkAlgo::convertToOSO( const std::string &shaderName, const std::string &mxNodeName )
{
	return IECoreMaterialX::CompileAlgo::compile( mxNodeName, convertToOSL( shaderName, mxNodeName ) );
}

void IECoreMaterialX::ShaderNetworkAlgo::convertToOSLNodes( ShaderNetwork *network, const InternedString renderer, const bool usdNodes, const bool addAdapters )
{
	if( addAdapters )
	{
		IECoreScene::ShaderNetworkAlgo::addComponentConnectionAdapters( network, g_mtlxPrefix );
	}

	// Make a const copy to reference from as we change the network coming in
	ConstShaderNetworkPtr originalNetwork = network->copy();

	for( const auto &[handle, shader] : originalNetwork->shaders() )
	{
		const bool isUsdPrefix = boost::starts_with( shader->getName(), g_usdNamePrefix );
		if( !usdNodes && isUsdPrefix )
		{
			// Skip USD node conversion early if usdNodes isn't set and has a USD prefix.
			continue;
		}

		// Unfortunately, we don't have a `mtlx:` prefix on the shader type out in the wild
		// from other authored sources of MaterialX shaders, so the next best thing is to
		// look out for the `ND_` prefix for `surface`/`displacement`/`volume` types.
		const bool isMtlxTypePrefix = boost::starts_with( shader->getType(), g_mtlxPrefix );
		const bool isNDPrefix = boost::starts_with( shader->getName(), g_mtlxNDNamePrefix );
		if( !isNDPrefix && !isUsdPrefix && !isMtlxTypePrefix )
		{
			// No Usd or ND prefix name, no mtlx: type, skip
			continue;
		}

		IECore::msg(
			IECore::Msg::Debug, "IECoreMaterialX::ShaderNetworkAlgo",
			fmt::format(
				"Attempting to replace MaterialX node `{}` with an OSL implementation.",
				shader->getName()
			)
		);

		auto typeIt = g_nameOverrides.find( shader->getName() );
		const string shaderName = typeIt != g_nameOverrides.end() ? typeIt->second : shader->getName();

		// Can't find this particular USD Shader
		if( usdNodes && isUsdPrefix && typeIt == g_nameOverrides.end() )
		{
			msg(
				Msg::Warning, "IECoreMaterialX::ShaderNetworkAlgo",
				fmt::format( "Couldn't find USD shader \"{}\" for shader \"{}\".", shaderName, handle.string() )
			);
			continue;
		}

		fs::path path = g_shaderSearchPathCache.get( shaderName );
		if( path.empty() )
		{
			continue;
		}

		std::vector<std::string> split;
		boost::split( split, shader->getType(), boost::is_any_of( ":" ) );
		ShaderPtr oslShader = new Shader( path.replace_extension().generic_string(), fmt::format( "osl:{}", split.back() ) );

		OSL::OSLQuery query;
		if( !query.open( path.generic_string() ) )
		{
			msg(
				Msg::Warning, "IECoreMaterialX::ShaderNetworkAlgo",
				fmt::format( "Couldn't open OSL shader \"{}\".", handle.string() )
			);
			continue;
		}

		// vector as one parameter can be split into multiple parameters
		ParamMap paramMap;
		ParamMap outParamMap;
		StructParamMap structParamMap;
		vector<InternedString> paramNames = getMtlxNodeParameterNames( shaderName );

		for( const auto &namedParameter : paramNames )
		{
			const OSL::OSLQuery::Parameter *parameter = query.getparam( namedParameter.string() );
			if( parameter && parameter->isstruct )
			{
				structParamMap[namedParameter] = parameter;
				continue;
			}

			if( !parameter )
			{
				// The parameter used a reserved name and MaterialX will make a "1" suffix
				parameter = query.getparam( fmt::format( "{}1", namedParameter.string() ) );

				if( parameter && parameter->isstruct )
				{
					structParamMap[namedParameter] = parameter;
					continue;
				}

				if( parameter )
				{
					IECore::msg(
						IECore::Msg::Debug, "IECoreMaterialX::ShaderNetworkAlgo",
						fmt::format(
							"Detected suffix on OSL shader `{}` implementation with parameter `{}` and remapping to `{}1`.",
							shader->getName(), namedParameter.string(), namedParameter.string()
						)
					);
					paramMap[namedParameter].push_back( parameter );
				}
			}
		}

		// Find any output structs with split-out parameters
		for( size_t i = 0; i < query.nparams(); ++i )
		{
			const OSL::OSLQuery::Parameter *parameter = query.getparam( i );
			if( parameter->isstruct && parameter->isoutput )
			{
				structParamMap[parameter->name.c_str()] = parameter;
			}
		}

		// Now get all the parameters that are a part of a struct
		for( const auto &[name, structParam] : structParamMap )
		{
			// Get all the component parameters and map to the original parameter name
			for( size_t i = 0; i < query.nparams(); ++i )
			{
				const OSL::OSLQuery::Parameter *parameter = query.getparam( i );

				// Cannot do a simple starts_with check here as for example, the switch
				// node has in1 and in10 and will all resolve to in1...
				std::vector<std::string> split;
				boost::split( split, parameter->name.c_str(), boost::is_any_of( "." ) );

				if( parameter->name != structParam->name &&
					split.front() == structParam->name.c_str() )
				{
					structParam->isoutput ? outParamMap[name].push_back( parameter ) :
											paramMap[name].push_back( parameter );
				}
			}
		}

		// Arnold seems to allow direct assignment of parameter values
		// and connections for vector2, vector4 and color4 - this makes things
		// a lot simpler to translate, we just need to make sure the suffixed
		// parameters remap and not worry about direct-mapping of struct components.
		if( renderer == g_arnoldRendererName )
		{
			// Parameters
			for( const auto &[name, value] : shader->parameters() )
			{
				InternedString parameterName = getParameterName( shaderName, name );
				StructParamMap::const_iterator structIt = structParamMap.find( parameterName );
				if( structIt != structParamMap.end() )
				{
					// Just use the struct name directly
					oslShader->parameters()[structIt->second->name.c_str()] = value;
				}
				else
				{
					ParamMap::const_iterator paramIt = paramMap.find( parameterName );
					if( paramIt != paramMap.end() )
					{
						oslShader->parameters()[paramIt->second.front()->name.c_str()] = value;
					}
					else
					{
						oslShader->parameters()[parameterName] = value;
					}
				}
			}

			// Connections
			for( const auto &c : originalNetwork->inputConnections( handle ) )
			{
				InternedString destinationName = getParameterName( shaderName, c.destination.name );
				StructParamMap::const_iterator structIt = structParamMap.find( destinationName );
				if( structIt != structParamMap.end() )
				{
					// We have all structs here and not just the suffix rename ones, so only
					// replace the connection if the names don't match
					InternedString structName = structIt->second->name.c_str();
					if( destinationName != structName )
					{
						network->addConnection( { c.source, { handle, structName } } );
						network->removeConnection( c );
					}
				}
				else
				{
					ParamMap::const_iterator paramIt = paramMap.find( destinationName );
					if( paramIt != paramMap.end() )
					{
						// The parameter is a suffixed one so we need to replace the connection
						network->addConnection( { c.source, { handle, paramIt->second.front()->name.c_str() } } );
						network->removeConnection( c );
					}
					else if( destinationName != c.destination.name )
					{
						// If the parameter has been renamed due to updating a legacy shader definition,
						// make sure we use this new parameter name for any connections.
						network->addConnection( { c.source, { handle, destinationName } } );
						network->removeConnection( c );
					}
				}
			}

			network->setShader( handle, std::move( oslShader ) );
			IECore::msg(
				IECore::Msg::Debug, "IECoreMaterialX::ShaderNetworkAlgo",
				fmt::format(
					"Replaced MaterialX node `{}` with an OSL implementation.",
					shader->getName()
				)
			);
			continue;
		}

		// From the limited testing so far, both Cycles and Renderman need a helping-hand
		// to map OSL structs by their float components for vector2, vector4 and color4 which would
		// be vector2 .x + .y, vector4 .x + .y + .z + .w and color4 .rgb + .a for both parameter
		// data and the connections (with helper adapters where needed).

		for( const auto &namedParameter : shader->parameters() )
		{
			InternedString parameterName = getParameterName( shaderName, namedParameter.first );
			IECore::TypeId type = namedParameter.second->typeId();
			ParamMap::const_iterator it = paramMap.find( parameterName );
			if( it != paramMap.end() )
			{
				if( it->second.size() == 2 )
				{
					if( type == IECore::V2fDataTypeId )
					{
						oslShader->parameters()[it->second[0]->name.c_str()] =
							new IECore::FloatData( static_cast<const IECore::V2fData *>( namedParameter.second.get() )->readable()[0] );
						oslShader->parameters()[it->second[1]->name.c_str()] =
							new IECore::FloatData( static_cast<const IECore::V2fData *>( namedParameter.second.get() )->readable()[1] );
					}
					else if( type == IECore::Color4fDataTypeId )
					{
						Imath::Color4f color = static_cast<const IECore::Color4fData *>( namedParameter.second.get() )->readable();
						oslShader->parameters()[it->second[0]->name.c_str()] =
							new IECore::Color3fData( Imath::Color3f( color[0], color[1], color[2] ) );
						oslShader->parameters()[it->second[1]->name.c_str()] =
							new IECore::FloatData( color[3] );
					}
					else
					{
						IECore::msg(
							IECore::Msg::Warning, "IECoreMaterialX::ShaderNetworkAlgo",
							fmt::format(
								"Unsupported parameter `{}` on shader node `{}`",
								parameterName.string(), handle.string()
							)
						);
					}
				}
				else if( it->second.size() == 4 && type == IECore::Color4fDataTypeId )
				{
					Imath::Color4f color = static_cast<const IECore::Color4fData *>( namedParameter.second.get() )->readable();
					oslShader->parameters()[it->second[0]->name.c_str()] = new IECore::FloatData( color[0] );
					oslShader->parameters()[it->second[1]->name.c_str()] = new IECore::FloatData( color[1] );
					oslShader->parameters()[it->second[2]->name.c_str()] = new IECore::FloatData( color[2] );
					oslShader->parameters()[it->second[3]->name.c_str()] = new IECore::FloatData( color[3] );
				}
				else if( it->second.size() > 1 )
				{
					IECore::msg(
						IECore::Msg::Warning, "IECoreMaterialX::ShaderNetworkAlgo",
						fmt::format(
							"Unsupported parameter `{}` on shader node `{}`",
							parameterName.string(), handle.string()
						)
					);
				}
				else
				{
					oslShader->parameters()[it->second.front()->name.c_str()] = namedParameter.second;
				}
			}
			else if( usdNodes &&
				parameterName == g_opacityModeParameter &&
				shader->getName() == g_usdPreviewSurfaceShader.string() )
			{
				// Gaffer represents the opacityMode as a string from its original enum/token form, but
				// MaterialX takes in an integer index of the enum. Do the basic conversion here.
				const string mode = static_cast<const IECore::StringData *>( namedParameter.second.get() )->readable();
				if( mode == g_presenceName )
				{
					oslShader->parameters()[parameterName] = new IntData( 1 );
				}
				else
				{
					oslShader->parameters()[parameterName] = new IntData( 0 );
				}
			}
			else
			{
				// TODO: Remove this once we know why base value of 0.0f crashes cycles!
				if( renderer == g_cyclesRendererName && parameterName == g_baseParameter && shader->getName() == g_mtlxStandardSurfaceShader.string() )
				{
					const float base = static_cast<const IECore::FloatData *>( namedParameter.second.get() )->readable();
					if( base <= 0.0f )
					{
						oslShader->parameters()[parameterName] = new FloatData( 0.0001f );
					}
					else
					{
						oslShader->parameters()[parameterName] = namedParameter.second;
					}
				}
				else
				{
					oslShader->parameters()[parameterName] = namedParameter.second;
				}
			}
		}

		// Now do all the connections, adding conversion helpers when needed for vector2/vector4/color4
		for( const auto &c : originalNetwork->inputConnections( handle ) )
		{
			InternedString destinationName = getParameterName( shaderName, c.destination.name );
			ParamMap::const_iterator it = paramMap.find( destinationName );
			if( it != paramMap.end() )
			{
				IECore::TypeId type = IECore::InvalidTypeId;
				StructParamMap::const_iterator structIt = structParamMap.find( destinationName );
				if( structIt != structParamMap.end() )
				{
					InternedString typeName( structIt->second->structname.c_str() );
					if( typeName == g_mtlxVector2Type )
					{
						type = IECore::V2fDataTypeId;
					}
					else if( typeName == g_mtlxVector4Type )
					{
						type = IECore::Color4fDataTypeId;
					}
					else if( typeName == g_mtlxColor4Type )
					{
						type = IECore::Color4fDataTypeId;
					}
					else
					{
						IECore::msg(
							IECore::Msg::Warning, "IECoreMaterialX::ShaderNetworkAlgo",
							fmt::format(
								"Unknown struct type `{}` for parameter `{}` on `{}`",
								typeName.string(), destinationName.string(), c.destination.shader.string()
							)
						);
						network->removeConnection( c );
						continue;
					}
				}

				if( it->second.size() == 2 )
				{
					if( type == IECore::V2fDataTypeId )
					{
						const Shader *inShader = originalNetwork->getShader( c.source.shader );
						if( inShader->getName() == g_mtlxUsdTransform2dShader ||
							( usdNodes && ( inShader->getName() == g_usdTransform2dShader || inShader->getName() == g_usdPrimvarReaderFloat2Shader ) ) ||
							( isNDPrefix && boost::ends_with( inShader->getName(), g_mtlxVector2Suffix ) ) )
						{
							network->addConnection( { { c.source.shader, g_outDotxParameter }, { handle, it->second[0]->name.c_str() } } );
							network->addConnection( { { c.source.shader, g_outDotyParameter }, { handle, it->second[1]->name.c_str() } } );

							IECore::msg(
								IECore::Msg::Debug, "IECoreMaterialX::ShaderNetworkAlgo",
								fmt::format(
									"Remapped vector2 struct connection `{}.{}` -> `{}.{}` into `{}->{}` and `{}->{}`",
									c.source.shader.string(), c.source.name.string(), c.destination.shader.string(), destinationName.string(),
									g_outDotxParameter.string(),  it->second[0]->name.c_str(), g_outDotyParameter.string(), it->second[1]->name.c_str()
								)
							);
						}
						else
						{
							// Make a separate vec2 helper
							fs::path separatePath = g_shaderSearchPathCache.get( g_mtlxSeparate3Vector3Shader );
							ShaderPtr separate = new Shader( separatePath.replace_extension().generic_string(), g_oslShader );
							InternedString separateHandle = network->addShader( g_separateVectorShaderName, std::move( separate ) );
							network->addConnection( { c.source, { separateHandle, g_inParameter } } );
							network->addConnection( { { separateHandle, g_outxParameter }, { handle, it->second[0]->name.c_str() } } );
							network->addConnection( { { separateHandle, g_outyParameter }, { handle, it->second[1]->name.c_str() } } );

							IECore::msg(
								IECore::Msg::Debug, "IECoreMaterialX::ShaderNetworkAlgo",
								fmt::format(
									"Remapped vector2 struct connection `{}.{}` -> `{}.{}` with adapter `-> {}.{}` `{}->{}` and `{}->{}`",
									c.source.shader.string(), c.source.name.string(), c.destination.shader.string(), destinationName.string(),
									separateHandle.string(), g_inParameter.string(),
									g_outxParameter.string(), it->second[0]->name.c_str(), g_outyParameter.string(), it->second[1]->name.c_str()
								)
							);
						}
						network->removeConnection( c );
					}
					else if( type == IECore::Color4fDataTypeId )
					{
						const Shader *inShader = originalNetwork->getShader( c.source.shader );
						if( boost::ends_with( inShader->getName(), g_mtlxColor4Suffix ) || inShader->getName() == g_mtlxGltfImageColor4Shader )
						{
							// .out.rgb/.out.a
							network->addConnection( { { c.source.shader, g_outDotrgbParameter }, { handle, it->second[0]->name.c_str() } } );
							network->addConnection( { { c.source.shader, g_outDotaParameter }, { handle, it->second[1]->name.c_str() } } );

							IECore::msg(
								IECore::Msg::Debug, "IECoreMaterialX::ShaderNetworkAlgo",
								fmt::format(
									"Remapped color4 struct connection `{}.{}` -> `{}.{}` into `{}->{}` and `{}->{}`",
									c.source.shader.string(), c.source.name.string(), c.destination.shader.string(), destinationName.string(),
									g_outDotrgbParameter.string(),  it->second[0]->name.c_str(), g_outDotaParameter.string(), it->second[1]->name.c_str()
								)
							);
						}
						else if( ( usdNodes && inShader->getName() == g_usdPrimvarReaderFloat4Shader ) ||
							boost::ends_with( inShader->getName(), g_mtlxVector4Suffix ) )
						{
							fs::path combinePath = g_shaderSearchPathCache.get( g_mtlxCombine3Color3Shader );
							ShaderPtr combine = new Shader( combinePath.replace_extension().generic_string(), g_oslShader );
							InternedString combineHandle = network->addShader( g_combineColorShaderName, std::move( combine ) );
							network->addConnection( { { c.source.shader, g_outDotxParameter }, { combineHandle, g_in1Parameter } } );
							network->addConnection( { { c.source.shader, g_outDotyParameter }, { combineHandle, g_in2Parameter } } );
							network->addConnection( { { c.source.shader, g_outDotzParameter }, { combineHandle, g_in3Parameter } } );
							network->addConnection( { { combineHandle, g_outParameter }, { handle, it->second[0]->name.c_str() } } );
							network->addConnection( { { c.source.shader, g_outDotwParameter }, { handle, it->second[1]->name.c_str() } } );

							IECore::msg(
								IECore::Msg::Debug, "IECoreMaterialX::ShaderNetworkAlgo",
								fmt::format(
									"Remapped vector4 struct connection `{}.{}` -> `{}.{}` with adapter `-> {}.{}+{}+{}` `{}->{}` and `{}->{}`",
									c.source.shader.string(), c.source.name.string(), c.destination.shader.string(), destinationName.string(),
									combineHandle.string(), g_in1Parameter.string(), g_in2Parameter.string(), g_in3Parameter.string(),
									g_outParameter.string(),  it->second[0]->name.c_str(), g_outDotwParameter.string(), it->second[1]->name.c_str()
								)
							);
						}
						else
						{
							IECore::msg(
								IECore::Msg::Warning, "IECoreMaterialX::ShaderNetworkAlgo",
								fmt::format(
									"Cannot translate connection between `{}.{}` and `{}.{}`",
									c.source.shader.string(), c.source.name.string(), c.destination.shader.string(), destinationName.string()
								)
							);
						}
						IECore::msg(
							IECore::Msg::Debug, "IECoreMaterialX::ShaderNetworkAlgo",
							fmt::format(
								"Remapped connections `{}.{}` to `{}.{}`",
								c.source.shader.string(), c.source.name.string(), c.destination.shader.string(), destinationName.string()
							)
						);
						network->removeConnection( c );
					}
					else
					{
						IECore::msg(
							IECore::Msg::Warning, "IECoreMaterialX::ShaderNetworkAlgo",
							fmt::format(
								"Unsupported parameter `{}` on shader node `{}`",
								destinationName.string(), handle.string()
							)
						);
						IECore::msg(
							IECore::Msg::Debug, "IECoreMaterialX::ShaderNetworkAlgo",
							fmt::format(
								"Remapped connections `{}.{}` to `{}.{}`",
								c.source.shader.string(), c.source.name.string(), c.destination.shader.string(), destinationName.string()
							)
						);
						network->removeConnection( c );
					}
				}
				else if( it->second.size() == 4 && type == IECore::Color4fDataTypeId )
				{
					const Shader *inShader = originalNetwork->getShader( c.source.shader );
					if( boost::ends_with( inShader->getName(), g_mtlxVector4Suffix ) )
					{
						// .out.x/.out.y/.out.z/.out.w
						network->addConnection( { { c.source.shader, g_outDotxParameter }, { handle, it->second[0]->name.c_str() } } );
						network->addConnection( { { c.source.shader, g_outDotyParameter }, { handle, it->second[1]->name.c_str() } } );
						network->addConnection( { { c.source.shader, g_outDotzParameter }, { handle, it->second[2]->name.c_str() } } );
						network->addConnection( { { c.source.shader, g_outDotwParameter }, { handle, it->second[3]->name.c_str() } } );
					}
					else if( boost::ends_with( inShader->getName(), g_mtlxColor4Suffix ) )
					{
						// .out.rgb/.out.a
						fs::path separatePath = g_shaderSearchPathCache.get( g_mtlxSeparate3Color3Shader );
						ShaderPtr separate = new Shader( separatePath.replace_extension().generic_string(), g_oslShader );
						InternedString separateHandle = network->addShader( g_separateColorShaderName, std::move( separate ) );
						network->addConnection( { { c.source.shader, g_outDotrgbParameter }, { separateHandle, g_inParameter } } );
						network->addConnection( { { separateHandle, g_outrParameter }, { handle, it->second[0]->name.c_str() } } );
						network->addConnection( { { separateHandle, g_outgParameter }, { handle, it->second[1]->name.c_str() } } );
						network->addConnection( { { separateHandle, g_outbParameter }, { handle, it->second[2]->name.c_str() } } );
						network->addConnection( { { c.source.shader, g_outDotaParameter }, { handle, it->second[3]->name.c_str() } } );
					}
					else
					{
						IECore::msg(
							IECore::Msg::Warning, "IECoreMaterialX::ShaderNetworkAlgo",
							fmt::format(
								"Cannot translate connection between `{}.{}` and `{}.{}`",
								c.source.shader.string(), c.source.name.string(), c.destination.shader.string(), destinationName.string()
							)
						);
					}
					IECore::msg(
						IECore::Msg::Debug, "IECoreMaterialX::ShaderNetworkAlgo",
						fmt::format(
							"Remapped connections `{}.{}` to `{}.{}`",
							c.source.shader.string(), c.source.name.string(), c.destination.shader.string(), destinationName.string()
						)
					);
					network->removeConnection( c );
				}
				else
				{
					IECore::msg(
						IECore::Msg::Debug, "IECoreMaterialX::ShaderNetworkAlgo",
						fmt::format(
							"Renamed connection parameter `{}` on `{}` to `{}`",
							c.destination.name.string(), c.destination.shader.string(), it->second.front()->name.c_str()
						)
					);
					network->addConnection( { c.source, { handle, it->second.front()->name.c_str() } } );
					network->removeConnection( c );
				}
			}
			else if( destinationName != c.destination.name )
			{
				// If the parameter has been renamed due to updating a legacy shader definition,
				// make sure we use this new parameter name for any connections.
				network->addConnection( { c.source, { handle, destinationName } } );
				network->removeConnection( c );
			}
		}

		replaceMtlxShader( network, handle, std::move( oslShader ), outParamMap );
	}
}
