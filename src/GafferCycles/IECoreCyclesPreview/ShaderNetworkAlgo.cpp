//////////////////////////////////////////////////////////////////////////
//
//  Copyright (c) 2018, Alex Fuller, Image Engine Design Inc.
//  All rights reserved.
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

#include "GafferCycles/IECoreCyclesPreview/ShaderNetworkAlgo.h"

#include "GafferCycles/IECoreCyclesPreview/SocketAlgo.h"

#include "SceneAlgo.h"

#include "IECoreScene/Shader.h"
#include "IECoreScene/ShaderNetworkAlgo.h"

#include "IECoreMaterialX/ShaderNetworkAlgo.h"

#include "IECore/AngleConversion.h"
#include "IECore/LRUCache.h"
#include "IECore/MessageHandler.h"
#include "IECore/SearchPath.h"
#include "IECore/SimpleTypedData.h"
#include "IECore/RampData.h"
#include "IECore/VectorTypedData.h"

#include "boost/algorithm/string.hpp"
#include "boost/algorithm/string/predicate.hpp"
#include "boost/lexical_cast.hpp"
#include "boost/unordered_map.hpp"

// Cycles
IECORE_PUSH_DEFAULT_VISIBILITY
#include "kernel/types.h"
#include "scene/shader_nodes.h"
#include "scene/object.h"
#include "scene/osl.h"
#include "util/path.h"
#include "util/version.h"
IECORE_POP_DEFAULT_VISIBILITY

#include "fmt/format.h"

#include <filesystem>
#include <unordered_map>

using namespace std;
using namespace Imath;
using namespace IECore;
using namespace IECoreScene;
using namespace IECoreCycles;

namespace
{

std::string shaderCacheGetter( const std::string &shaderName, size_t &cost )
{
	cost = 1;
	const char *oslShaderPaths = getenv( "OSL_SHADER_PATHS" );
	SearchPath searchPath( oslShaderPaths ? oslShaderPaths : "" );
	boost::filesystem::path path = searchPath.find( shaderName + ".oso" );
	if( path.empty() )
	{
		return shaderName;
	}
	else
	{
		return path.generic_string();
	}
}

using ShaderSearchPathCache = IECore::LRUCache<std::string, std::string>;
ShaderSearchPathCache g_shaderSearchPathCache( shaderCacheGetter, 10000 );

ccl::SocketType::Type getSocketType( const std::string &name )
{
	if( name == "float" ) return ccl::SocketType::Type::FLOAT;
	if( name == "int" ) return ccl::SocketType::Type::INT;
	if( name == "color" ) return ccl::SocketType::Type::COLOR;
	if( name == "vector" ) return ccl::SocketType::Type::VECTOR;
	if( name == "point" ) return ccl::SocketType::Type::POINT;
	if( name == "normal" ) return ccl::SocketType::Type::NORMAL;
	if( name == "closure" ) return ccl::SocketType::Type::CLOSURE;
	if( name == "string" ) return ccl::SocketType::Type::STRING;
	return ccl::SocketType::Type::UNDEFINED;
}

using ShaderMap = boost::unordered_map<ShaderNetwork::Parameter, ccl::ShaderNode *>;

ccl::ShaderNode *convertWalk( const ShaderNetwork::Parameter &outputParameter, const IECoreScene::ShaderNetwork *shaderNetwork, const std::string &namePrefix, ccl::Scene *scene, ccl::ShaderGraph *shaderGraph, ShaderMap &converted )
{
	// Reuse previously created node if we can.
	const IECoreScene::Shader *shader = shaderNetwork->getShader( outputParameter.shader );
	ccl::ShaderNode *&node = converted[outputParameter.shader];
	if( node )
	{
		return node;
	}

	// Create node for shader.

	const bool isOSLShader = boost::starts_with( shader->getType(), "osl:" );

	if( isOSLShader )
	{
		if( scene->shader_manager->use_osl() )
		{
			std::string shaderFileName = g_shaderSearchPathCache.get( shader->getName() );
			node = ccl::OSLShaderManager::osl_node( shaderGraph, scene, shaderFileName.c_str() );
		}
		else
		{
			msg(
				Msg::Warning, "IECoreCycles::ShaderNetworkAlgo",
				fmt::format( "Couldn't load OSL shader \"{}\" as the shading system is not set to OSL.", shader->getName() )
			);
			return node;
		}
	}
	else if( boost::starts_with( shader->getName(), "convert" ) )
	{
		/// \todo Why can't this be handled by the generic case below? There are NodeTypes
		/// registered for each of these conversions, so `NodeType::find()` does work. The
		/// only difference I can see is that this way we pass `autoconvert = true` to the
		/// ConvertNode constructor, but it's not clear what benefit that has.
		vector<string> split;
		boost::split( split, shader->getName(), boost::is_any_of( "_" ) );
		if( split.size() >= 4 ) // should be 4 eg. "convert, X, to, Y"
		{
			node = shaderGraph->create_node<ccl::ConvertNode>( getSocketType( split[1] ), getSocketType( split[3] ), true );
		}
	}
	else if( const ccl::NodeType *nodeType = ccl::NodeType::find( ccl::ustring( shader->getName() ) ) )
	{
		if( nodeType->type == ccl::NodeType::SHADER && nodeType->create )
		{
			node = shaderGraph->create_node( nodeType );
		}
	}

	if( !node )
	{
		msg( Msg::Warning, "IECoreCycles::ShaderNetworkAlgo", fmt::format( "Couldn't load shader \"{}\"", shader->getName() ) );
		return node;
	}

	node->name = ccl::ustring( namePrefix + outputParameter.shader.string() );

	// Set the shader parameters

	const bool isImageTexture = shader->getName() == "image_texture";

	for( const auto &namedParameter : shader->parameters() )
	{
		// We needed to change any "." found in the socket input names to
		// "__", revert that change here.
		string parameterName = boost::replace_first_copy( namedParameter.first.string(), "__", "." );

		if( const RampffData *rampData = runTimeCast<const RampffData>( namedParameter.second.get() ) )
		{
			// For OSL, ramps are handled by convertToOSLConventions
			assert( !isOSLShader );

			if( const ccl::SocketType *socket = node->type->find_input( ccl::ustring( parameterName.c_str() ) ) )
			{
				SocketAlgo::setRampSocket( node, socket, rampData->readable() );
			}
		}
		else if( const RampfColor3fData *rampData = runTimeCast<const RampfColor3fData>( namedParameter.second.get() ) )
		{
			// For OSL, ramps are handled by convertToOSLConventions
			assert( !isOSLShader );

			if( const ccl::SocketType *socket = node->type->find_input( ccl::ustring( parameterName.c_str() ) ) )
			{
				SocketAlgo::setRampSocket( node, socket, rampData->readable() );
			}
		}
		else if( isImageTexture && parameterName == "filename" )
		{
			if( const StringData *stringData = runTimeCast<const StringData>( namedParameter.second.get() ) )
			{
				string pathFileName( stringData->readable() );
				string fileName = ccl::path_filename( pathFileName );
				size_t offset = fileName.find( "<UDIM>" );
				ccl::ImageTextureNode *imgTexNode = (ccl::ImageTextureNode*)node;
				if( offset != string::npos )
				{
					// Workaround to find all available tiles
					string baseFileName = fileName.substr( 0, offset );
					vector<string> files;
					const std::filesystem::path path( ccl::path_dirname( pathFileName ) );
					for( const auto &d : std::filesystem::directory_iterator( path ) )
					{
						if( std::filesystem::is_regular_file( d.status() ) || std::filesystem::is_symlink( d.status() ) )
						{
							string foundFile = d.path().stem().string();
							if( baseFileName == ( foundFile.substr( 0, offset ) ) )
							{
								files.push_back( foundFile );
							}
						}
					}

					ccl::array<int> tiles;
					for( string file : files )
					{
						tiles.push_back_slow( atoi( file.substr( offset, offset+3 ).c_str() ) );
					}
					imgTexNode->set_tiles( tiles );
				}
				imgTexNode->set_filename( ccl::ustring( pathFileName ) );
			}
		}
		else
		{
			SocketAlgo::setSocket( node, parameterName, namedParameter.second.get() );
		}
	}

	// Recurse through input connections

	for( const auto &connection : shaderNetwork->inputConnections( outputParameter.shader ) )
	{
		ccl::ShaderNode *sourceNode = convertWalk( connection.source, shaderNetwork, namePrefix, scene, shaderGraph, converted );
		if( !sourceNode )
		{
			continue;
		}

		// We needed to change any "." found in the socket input names to
		// "__", revert that change here.
		string parameterName = boost::replace_first_copy( connection.destination.name.string(), "__", "." );

		InternedString sourceName = connection.source.name;

		if( ccl::ShaderOutput *shaderOutput = IECoreCycles::ShaderNetworkAlgo::output( sourceNode, sourceName ) )
		{
			if( ccl::ShaderInput *shaderInput = IECoreCycles::ShaderNetworkAlgo::input( node, parameterName ) )
			{
				shaderGraph->connect( shaderOutput, shaderInput );
			}
		}
	}

	return node;
}

template<typename T>
T parameterValue( const IECore::Data *data, const IECore::InternedString &name, const T &defaultValue )
{
	using DataType = IECore::TypedData<T>;
	if( auto d = runTimeCast<const DataType>( data ) )
	{
		return d->readable();
	}

	IECore::msg(
		IECore::Msg::Warning, "IECoreCycles::ShaderNetworkAlgo",
		fmt::format( "Expected {} but got {} for parameter \"{}\".", DataType::staticTypeName(), data->typeName(), name.c_str() )
	);
	return defaultValue;
}

template<typename T>
T parameterValue( const IECore::CompoundDataMap &parameters, const IECore::InternedString &name, const T &defaultValue )
{
	auto it = parameters.find( name );
	if( it != parameters.end() )
	{
		return parameterValue<T>( it->second.get(), name, defaultValue );
	}

	return defaultValue;
}

const bool g_disableMaterialxUSDShaders = []() -> bool {
	const char *c = getenv( "GAFFERCYCLES_DISABLE_MATERIALX_USD_SHADERS" );
	if( !c )
	{
		return false;
	}
	return strcmp( c, "0" );
}();

const bool g_useLegacyLights = []() -> bool {
	const char *c = getenv( "GAFFERCYCLES_USE_LEGACY_LIGHTS" );
	if( !c )
	{
		return false;
	}
	return strcmp( c, "0" );
}();

// Cycles lights just have a single `strength` parameter which
// we want to present as separate "virtual" parameters for
// intensity, color, exposure.
bool contributesToLightStrength( InternedString parameterName )
{
	return
		parameterName == "intensity" ||
		parameterName == "color" ||
		parameterName == "exposure"
	;
}

Imath::Color3f constantLightStrength( const IECoreScene::ShaderNetwork *light )
{
	const IECoreScene::Shader *lightShader = light->outputShader();

	Imath::Color3f strength( 1 );
	if( !light->input( { light->getOutput().shader, "intensity" } ) )
	{
		strength *= parameterValue<float>( lightShader->parameters(), "intensity", 1.0f );
	}

	if( !light->input( { light->getOutput().shader, "color" } ) )
	{
		strength *= parameterValue<Imath::Color3f>( lightShader->parameters(), "color", Imath::Color3f( 1.0f ) );
	}

	// We don't support input connections to exposure - it seems unlikely that
	// you'd want to texture that.
	strength *= powf( 2.0f, parameterValue<float>( lightShader->parameters(), "exposure", 0.0f ) );

	if(
		!g_useLegacyLights &&
		(
			lightShader->getName() == "disk_light" ||
			lightShader->getName() == "point_light" ||
			lightShader->getName() == "quad_light" ||
			lightShader->getName() == "spot_light"
		)
	)
	{
		// Blender/hdCycles scale by `pi` to convert intensity to radiant flux.
		// We do the same here to ensure lights exported from Blender to USD render
		// with an equivalent strength. This also causes these lights to match
		// the equivalent Arnold light at the same intensity.
		//
		// Though all is not perfect and we still have the following quirks :
		//
		// Blender scales `distant_light` strength down by 4 when exporting intensity
		// to USD and scales intensity up by 4 when importing USD or rendering with hdCycles.
		// For lack of clarity in the UsdLuxLight specification, this value appears to have
		// been chosen as it approximately matches Karma. Given the lack of clarity here, we
		// do not apply the same scaling factor. As a result, distant lights render with 4
		// times less strength than Blender/hdCycles but match Arnold.
		//
		// Cycles and Arnold normalize point (sphere) and spotlights differently,
		// with Arnold lights rendering 4 times brighter than Cycles at the same intensity,
		// though they match when not normalized. For lack of a definition of standard
		// behaviour in UsdLuxLight, we don't attempt to match normalization between the two.
		strength *= M_PI;
	}

	return strength;
}

const InternedString g_empty( "" );
const InternedString g_out( "out" );

} // namespace

namespace IECoreCycles
{

namespace ShaderNetworkAlgo
{

// These functions do exist in Cycles, however they check the 'ui_name' and not
// the true 'name' which is really annoying, so we check the 'name' with these.
// We might as well use IECore::InternedString here also for a cleaner API.

ccl::ShaderInput *input( ccl::ShaderNode *node, IECore::InternedString name )
{
	ccl::ustring cname = ccl::ustring( name.c_str() );
	for( ccl::ShaderInput *socket : node->inputs )
	{
		if( socket->socket_type.name == cname )
			return socket;
	}

	msg(
		Msg::Warning, "IECoreCycles::ShaderNetworkAlgo",
		fmt::format( "Couldn't find socket input \"{}\" on shaderNode \"{}\"", name.string(), node->name.c_str() )
	);
	return nullptr;
}

ccl::ShaderOutput *output( ccl::ShaderNode *node, IECore::InternedString name )
{
	const ccl::ustring cname = ccl::ustring( name.c_str() );
	for( ccl::ShaderOutput *socket : node->outputs )
	{
		if( socket->socket_type.name == cname )
			return socket;
	}

	// Workaround for degenerate ShaderNetworks in which the output
	// parameter name is not specified correctly. These are currently
	// produced by the ShaderView - see `ShaderView::g_viewDescription`.
	if( name == g_empty || name == g_out )
	{
		if( node->outputs.size() )
		{
			return node->outputs[0];
		}
	}

	msg(
		Msg::Warning, "IECoreCycles::ShaderNetworkAlgo",
		fmt::format( "Couldn't find socket output \"{}\" on shaderNode \"{}\"", name.string(), node->name.c_str() )
	);
	return nullptr;
}

std::unique_ptr<ccl::ShaderGraph> convertGraph( const IECoreScene::ShaderNetwork *surfaceShader,
								const IECoreScene::ShaderNetwork *displacementShader,
								const IECoreScene::ShaderNetwork *volumeShader,
								ccl::Scene *scene,
								const std::string &namePrefix )
{
	std::unique_ptr<ccl::ShaderGraph> graph = std::make_unique<ccl::ShaderGraph>();

	using NamedNetwork = std::pair<std::string, const IECoreScene::ShaderNetwork *>;
	for( const auto &[name, network] : { NamedNetwork( "surface", surfaceShader ), NamedNetwork( "displacement", displacementShader ), NamedNetwork( "volume", volumeShader ) } )
	{
		if( !network )
		{
			continue;
		}
		if( network->getOutput().shader.string().empty() )
		{
			msg( Msg::Warning, "IECoreCycles::ShaderNetworkAlgo", "Shader has no output" );
			continue;
		}

		ShaderNetworkPtr toConvert = network->copy();

		/// Hardcoded to the old OSL version to indicate that component connection adapters are
		/// required - even though OSL now supports component connections, the Cycles API AFAIK doesn't.
		IECoreScene::ShaderNetworkAlgo::convertToOSLConventions( toConvert.get(), 10900 );
		// Convert MaterialX nodes that are better suited to native Cycles shaders eg.
		// ND_geomparamvalue_*/ND_image_*.
		IECoreCycles::ShaderNetworkAlgo::convertMtlxShaders( toConvert.get() );
		// Convert all MaterialX nodes to OSL nodes, component connection adapters have already been added above.
		// For the OSL backend, use the MaterialX OSL implementations of UsdPreviewSurface/UsdUVTexture/etc.
		// as these would be a better match to the specification, and then pass to convertUSDShaders for the rest.
		// GAFFERCYCLES_DISABLE_MATERIALX_USD_SHADERS environment variable disables this and uses the same
		// conversion that the SVM backend uses.
		const bool usdNodes = scene->shader_manager.get() && scene->shader_manager->use_osl() && !g_disableMaterialxUSDShaders;
		if( scene->shader_manager->use_osl() )
		{
			IECoreMaterialX::ShaderNetworkAlgo::convertToOSLNodes( toConvert.get(), "cycles", /* usdNodes */ usdNodes, /* addAdapters */ false );
		}
		IECoreCycles::ShaderNetworkAlgo::convertUSDShaders( toConvert.get() );
		// The above only added component connection adaptors for OSL. Now add them for native
		// Cycles shaders, as well as MaterialX ones.
		IECoreScene::ShaderNetworkAlgo::addComponentConnectionAdapters( toConvert.get() );
		ShaderMap converted;
		ccl::ShaderNode *node = convertWalk( toConvert->getOutput(), toConvert.get(), namePrefix, scene, graph.get(), converted );

		if( node )
		{
			// Connect to the main output node of the cycles shader graph, either
			// surface, displacement or volume.
			if( ccl::ShaderOutput *shaderOutput = output( node, toConvert->getOutput().name ) )
			{
				graph->connect( shaderOutput, input( (ccl::ShaderNode *)graph->output(), name ) );
			}
		}
	}

	return graph;
}

void convertAOV( const IECoreScene::ShaderNetwork *shaderNetwork, ccl::ShaderGraph *graph, ccl::Scene *scene, const std::string &namePrefix )
{
	ShaderMap converted;
	convertWalk( shaderNetwork->getOutput(), shaderNetwork, namePrefix, scene, graph, converted );
}

void setSingleSided( ccl::ShaderGraph *graph )
{
	// Cycles doesn't natively support setting single-sided on objects, however we can build
	// a shader which does it for us by checking for backfaces and using a transparentBSDF
	// to emulate the effect.
	ccl::ShaderNode *mixClosure = graph->create_node<ccl::MixClosureNode>();
	ccl::ShaderNode *transparentBSDF = graph->create_node<ccl::TransparentBsdfNode>();
	ccl::ShaderNode *geometry = graph->create_node<ccl::GeometryNode>();

	if( ccl::ShaderOutput *shaderOutput = ShaderNetworkAlgo::output( geometry, "backfacing" ) )
		if( ccl::ShaderInput *shaderInput = ShaderNetworkAlgo::input( mixClosure, "fac" ) )
			graph->connect( shaderOutput, shaderInput );

	if( ccl::ShaderOutput *shaderOutput = ShaderNetworkAlgo::output( transparentBSDF, "BSDF" ) )
		if( ccl::ShaderInput *shaderInput = ShaderNetworkAlgo::input( mixClosure, "closure2" ) )
			graph->connect( shaderOutput, shaderInput );

	ccl::OutputNode *output = graph->output();

	if( ccl::ShaderInput *shaderInput = ShaderNetworkAlgo::input( (ccl::ShaderNode*)output, "surface" ) )
	{
		ccl::ShaderOutput *shaderOutput = shaderInput->link;
		if( shaderOutput )
		{
			shaderInput->disconnect();
			if( ccl::ShaderInput *shaderInput2 = ShaderNetworkAlgo::input( mixClosure, "closure1" ) )
				graph->connect( shaderOutput, shaderInput2 );

			if( ccl::ShaderOutput *shaderOutput2 = ShaderNetworkAlgo::output( mixClosure, "closure" ) )
				graph->connect( shaderOutput2, shaderInput );
		}
	}
}

bool hasOSL( const ccl::Shader *cshader )
{
	for( ccl::ShaderNode *snode : cshader->graph->nodes )
	{
		if( snode->special_type == ccl::SHADER_SPECIAL_TYPE_OSL )
			return true;
	}
	return false;
}

bool compatibleLightType( const IECoreScene::ShaderNetwork *light, const ccl::Light *cyclesLight )
{
	if( !light || !cyclesLight )
	{
		return false;
	}

	assert( light->typeId() == IECoreScene::ShaderNetwork::staticTypeId() );

	const IECoreScene::Shader *lightShader = light->outputShader();

	if( !lightShader )
	{
		return false;
	}

	if( lightShader->getName() == "spot_light" )
	{
		return ( cyclesLight && cyclesLight->is_spot_light() );
	}
	else if( lightShader->getName() == "distant_light" )
	{
		return ( cyclesLight && ( cyclesLight->is_sun_light() || cyclesLight->is_distant_light() ) );
	}
	else if( lightShader->getName() == "background_light" )
	{
		return ( cyclesLight && cyclesLight->is_background_light() );
	}
	else if(
		lightShader->getName() == "quad_light" ||
		lightShader->getName() == "portal" ||
		lightShader->getName() == "disk_light"
	)
	{
		return ( cyclesLight && cyclesLight->is_area_light() );
	}
	else
	{
		return ( cyclesLight && cyclesLight->is_point_light() );
	}

	return false;
}

ccl::Light *convertLight( const IECoreScene::ShaderNetwork *light, ccl::Scene *scene, ccl::Light *prevCyclesLight )
{
	ccl::Light *cyclesLight = nullptr;
	if( !light )
	{
		msg( Msg::Warning, "IECoreCycles::ShaderNetworkAlgo::convertLight", "No ShaderNetwork" );
		if( !prevCyclesLight )
		{
			// If no previous cycles light was passed, create a dummy disabled one.
			cyclesLight = static_cast<ccl::Light*>( SceneAlgo::createNodeWithLock<ccl::PointLight>( scene ) );
			cyclesLight->set_is_enabled( false );
			return cyclesLight;
		}

		prevCyclesLight->set_is_enabled( false );
		return prevCyclesLight;
	}

	assert( light->typeId() == IECoreScene::ShaderNetwork::staticTypeId() );

	const IECoreScene::Shader *lightShader = light->outputShader();
	if( !lightShader )
	{
		msg( Msg::Warning, "IECoreCycles::ShaderNetworkAlgo::convertLight", "ShaderNetwork has no output shader" );
		if( !prevCyclesLight )
		{
			// If no previous cycles light was passed, create a dummy disabled one.
			cyclesLight = static_cast<ccl::Light*>( SceneAlgo::createNodeWithLock<ccl::PointLight>( scene ) );
			cyclesLight->set_is_enabled( false );
			return cyclesLight;
		}

		prevCyclesLight->set_is_enabled( false );
		return prevCyclesLight;
	}

	if( lightShader->getName() == "spot_light" )
	{
		if( prevCyclesLight && !prevCyclesLight->is_spot_light() )
		{
			msg( Msg::Warning, "IECoreCycles::ShaderNetworkAlgo::convertLight", "ShaderNetwork type does not match Cycles light type" );
			prevCyclesLight->set_is_enabled( false );
			return prevCyclesLight;
		}

		ccl::SpotLight *spotLight = prevCyclesLight ? static_cast<ccl::SpotLight*>( prevCyclesLight ) : SceneAlgo::createNodeWithLock<ccl::SpotLight>( scene );

		for( const auto &[name, value] : lightShader->parameters() )
		{
			if( contributesToLightStrength( name ) )
			{
				continue;
			}

			// Convert angle-based parameters, where we use degrees and Cycles uses radians.
			if( name == "spot_angle" )
			{
				spotLight->set_angle( IECore::degreesToRadians( parameterValue<float>( value.get(), name, 45.0f ) ) );
			}
			// Convert generic parameters.
			else
			{
				SocketAlgo::setSocket( spotLight, name, value.get() );
			}
		}

		cyclesLight = spotLight;
	}
	else if( lightShader->getName() == "distant_light" )
	{
		if( prevCyclesLight && ( !prevCyclesLight->is_sun_light() && !prevCyclesLight->is_distant_light() ) )
		{
			msg( Msg::Warning, "IECoreCycles::ShaderNetworkAlgo::convertLight", "ShaderNetwork type does not match Cycles light type" );
			prevCyclesLight->set_is_enabled( false );
			return prevCyclesLight;
		}

		ccl::SunLight *sunLight = prevCyclesLight ? static_cast<ccl::SunLight*>( prevCyclesLight ) : SceneAlgo::createNodeWithLock<ccl::SunLight>( scene );

		for( const auto &[name, value] : lightShader->parameters() )
		{
			if( contributesToLightStrength( name ) )
			{
				continue;
			}

			// Convert generic parameters.
			else
			{
				SocketAlgo::setSocket( sunLight, name, value.get() );
			}
		}

		cyclesLight = sunLight;
	}
	else if( lightShader->getName() == "background_light" )
	{
		if( prevCyclesLight && !prevCyclesLight->is_background_light() )
		{
			msg( Msg::Warning, "IECoreCycles::ShaderNetworkAlgo::convertLight", "ShaderNetwork type does not match Cycles light type" );
			prevCyclesLight->set_is_enabled( false );
			return prevCyclesLight;
		}

		ccl::BackgroundLight *backgroundLight = prevCyclesLight ? static_cast<ccl::BackgroundLight*>( prevCyclesLight ) : SceneAlgo::createNodeWithLock<ccl::BackgroundLight>( scene );

		for( const auto &[name, value] : lightShader->parameters() )
		{
			if( contributesToLightStrength( name ) )
			{
				continue;
			}

			// Convert generic parameters.
			else
			{
				SocketAlgo::setSocket( backgroundLight, name, value.get() );
			}
		}

        cyclesLight = backgroundLight;
	}
	else if(
		lightShader->getName() == "quad_light" ||
		lightShader->getName() == "portal" ||
		lightShader->getName() == "disk_light"
	)
	{
		if( prevCyclesLight && !prevCyclesLight->is_area_light() )
		{
			msg( Msg::Warning, "IECoreCycles::ShaderNetworkAlgo::convertLight", "ShaderNetwork type does not match Cycles light type" );
			prevCyclesLight->set_is_enabled( false );
			return prevCyclesLight;
		}

		ccl::AreaLight *areaLight = prevCyclesLight ? static_cast<ccl::AreaLight*>( prevCyclesLight ) : SceneAlgo::createNodeWithLock<ccl::AreaLight>( scene );
		areaLight->set_sizeu( 2.0f );
		areaLight->set_sizev( 2.0f );

        areaLight->set_is_portal( lightShader->getName() == "portal" ? true : false );
		areaLight->set_ellipse( lightShader->getName() == "disk_light" ? true : false );

		for( const auto &[name, value] : lightShader->parameters() )
		{
			if( contributesToLightStrength( name ) )
			{
				continue;
			}

			if( name == "spread" )
			{
				areaLight->set_spread( IECore::degreesToRadians( parameterValue<float>( value.get(), name, 180.0f ) ) );
			}
			else if( name == "width" )
			{
				areaLight->set_sizeu( parameterValue<float>( value.get(), name, 2.0f ) );
				// No oval support yet, just apply width to height.
				if( lightShader->getName() == "disk_light" )
				{
					areaLight->set_sizev( parameterValue<float>( value.get(), name, 2.0f ) );
				}
			}
			else if( name == "height" )
			{
				areaLight->set_sizev( parameterValue<float>( value.get(), name, 2.0f ) );
			}
			// Convert generic parameters.
			else
			{
				SocketAlgo::setSocket( areaLight, name, value.get() );
			}
		}
		
        cyclesLight = areaLight;
	}
	else
	{
		if( prevCyclesLight && !prevCyclesLight->is_point_light() )
		{
			msg( Msg::Warning, "IECoreCycles::ShaderNetworkAlgo::convertLight", "ShaderNetwork type does not match Cycles light type" );
			prevCyclesLight->set_is_enabled( false );
			return prevCyclesLight;
		}

        ccl::PointLight *pointLight = prevCyclesLight ? static_cast<ccl::PointLight*>( prevCyclesLight ) : SceneAlgo::createNodeWithLock<ccl::PointLight>( scene );

		for( const auto &[name, value] : lightShader->parameters() )
		{
			if( contributesToLightStrength( name ) )
			{
				continue;
			}
			// Convert generic parameters.
			else
			{
				SocketAlgo::setSocket( pointLight, name, value.get() );
			}
		}
		
        cyclesLight = pointLight;
	}

	// Convert "virtual" parameters to strength. We can't do this for background
	// lights because Cycles will ignore it - we deal with that in
	// `convertLightShader()` instead.
	if( !cyclesLight->is_background_light() )
	{
		const Imath::Color3f strength = constantLightStrength( light );
		cyclesLight->set_strength( ccl::make_float3( strength[0], strength[1], strength[2] ) );
	}
	else
	{
		cyclesLight->set_strength( ccl::one_float3() );
	}

	return cyclesLight;
}

IECoreScene::ShaderNetworkPtr convertLightShader( const IECoreScene::ShaderNetwork *light )
{
	// Take a copy and replace the output shader (the light itself) with a
	// Cycles emission or background shader as appropriate.

	ShaderNetworkPtr result = light->copy();

	result->removeShader( result->getOutput().shader );

	IECoreScene::ShaderPtr outputShader;
	if( light->outputShader()->getName() == "background_light" )
	{
		outputShader = new IECoreScene::Shader( "background_shader", "cycles:surface" );
	}
	else
	{
		outputShader = new IECoreScene::Shader( "emission", "cycles:surface" );
	}

	outputShader->parameters()["color"] = new Color3fData( Imath::Color3f( 1.0f ) );
	outputShader->parameters()["strength"] = new FloatData( 1.0f );
	InternedString outputHandle = result->addShader( "output", std::move( outputShader ) );
	result->setOutput( outputHandle );

	// Connect up intensity and color to the emission shader if necessary.

	if( auto intensityInput = light->input( { light->getOutput().shader, "intensity" } ) )
	{
		result->addConnection( { intensityInput, { outputHandle, "strength" } } );
	}

	const auto colorInput = light->input( { light->getOutput().shader, "color" } );
	if( colorInput )
	{
		result->addConnection( { colorInput, { outputHandle, "color" } } );
	}

	// Workaround for Cycles ignoring strength for background lights - insert a
	// shader to multiply it into the input `color`. Hopefully we can remove
	// this at some point.
	if( light->outputShader()->getName() == "background_light" )
	{
		const Imath::Color3f strength = constantLightStrength( light );
		if( strength != Imath::Color3f( 1 ) )
		{
			if( colorInput )
			{
				IECoreScene::ShaderPtr tintShader = new IECoreScene::Shader( "vector_math", "cycles:shader" );
				tintShader->parameters()["math_type"] = new StringData( "multiply" );
				tintShader->parameters()["vector2"] = new V3fData( strength );
				const IECore::InternedString tintHandle = result->addShader( "tint", std::move( tintShader ) );
				result->addConnection( { colorInput, { tintHandle, "vector1" } } );
				result->removeConnection( { colorInput, { outputHandle, "color" } } );
				result->addConnection( { { tintHandle, "vector" }, { outputHandle, "color" } } );
			}
			else
			{
				outputShader = result->getShader( outputHandle )->copy();
				outputShader->parameters()["color"] = new Color3fData( strength );
				result->setShader( outputHandle, std::move( outputShader ) );
			}
		}
	}

	return result;
}

} // namespace ShaderNetworkAlgo

} // namespace IECoreCycles


//////////////////////////////////////////////////////////////////////////
// USD conversion code
//////////////////////////////////////////////////////////////////////////

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

Color3f blackbody( float kelvins )
{
	// Table borrowed from `UsdLuxBlackbodyTemperatureAsRgb()`, which in
	// turn is borrowed from Colour Rendering of Spectra by John Walker.
	static SplinefColor3f g_spline(
		CubicBasisf::catmullRom(),
		{
			{  1000.0f, Color3f( 1.000000f, 0.027490f, 0.000000f ) },
			{  1000.0f, Color3f( 1.000000f, 0.027490f, 0.000000f ) },
			{  1500.0f, Color3f( 1.000000f, 0.149664f, 0.000000f ) },
			{  2000.0f, Color3f( 1.000000f, 0.256644f, 0.008095f ) },
			{  2500.0f, Color3f( 1.000000f, 0.372033f, 0.067450f ) },
			{  3000.0f, Color3f( 1.000000f, 0.476725f, 0.153601f ) },
			{  3500.0f, Color3f( 1.000000f, 0.570376f, 0.259196f ) },
			{  4000.0f, Color3f( 1.000000f, 0.653480f, 0.377155f ) },
			{  4500.0f, Color3f( 1.000000f, 0.726878f, 0.501606f ) },
			{  5000.0f, Color3f( 1.000000f, 0.791543f, 0.628050f ) },
			{  5500.0f, Color3f( 1.000000f, 0.848462f, 0.753228f ) },
			{  6000.0f, Color3f( 1.000000f, 0.898581f, 0.874905f ) },
			{  6500.0f, Color3f( 1.000000f, 0.942771f, 0.991642f ) },
			{  7000.0f, Color3f( 0.906947f, 0.890456f, 1.000000f ) },
			{  7500.0f, Color3f( 0.828247f, 0.841838f, 1.000000f ) },
			{  8000.0f, Color3f( 0.765791f, 0.801896f, 1.000000f ) },
			{  8500.0f, Color3f( 0.715255f, 0.768579f, 1.000000f ) },
			{  9000.0f, Color3f( 0.673683f, 0.740423f, 1.000000f ) },
			{  9500.0f, Color3f( 0.638992f, 0.716359f, 1.000000f ) },
			{ 10000.0f, Color3f( 0.609681f, 0.695588f, 1.000000f ) },
			{ 10000.0f, Color3f( 0.609681f, 0.695588f, 1.000000f ) },
		}
	);

	Color3f c = g_spline( kelvins );
	c /= c.dot( V3f( 0.2126f, 0.7152f, 0.0722f ) ); // Normalise luminance
	return Color3f( max( c[0], 0.0f ), max( c[1], 0.0f ), max( c[2], 0.0f ) );
}

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
	else if constexpr( is_same_v<remove_cv_t<T>, V3f> )
	{
		// Convert V2f to V3f, UVs from MaterialX are often V2f but Cycles expects V3f.
		if( auto d = shader->parametersData()->member<V2fData>( parameterName ) )
		{
			return V3f( d->readable()[0], d->readable()[1], 0.0f );
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

const InternedString g_aParameter( "a" );
const InternedString g_alphaParameter( "alpha" );
const InternedString g_angleParameter( "angle" );
const InternedString g_attributeParameter( "attribute" );
const InternedString g_baseColorParameter( "base_color" );
const InternedString g_bParameter( "b" );
const InternedString g_biasParameter( "bias" );
const InternedString g_BSDFParameter( "BSDF" );
const InternedString g_castShadowParameter( "cast_shadow" );
const InternedString g_clearcoatParameter( "clearcoat" );
const InternedString g_clearcoatRoughnessParameter( "clearcoatRoughness" );
const InternedString g_coatRoughnessParameter( "coat_roughness" );
const InternedString g_coatWeightParameter( "coat_weight" );
const InternedString g_colorParameter( "color" );
const InternedString g_colorRParameter( "color.r" );
const InternedString g_colorGParameter( "color.g" );
const InternedString g_colorBParameter( "color.b" );
const InternedString g_colorspaceParameter( "colorspace" );
const InternedString g_colorTemperatureParameter( "colorTemperature" );
const InternedString g_diffuseParameter( "diffuse" );
const InternedString g_diffuseColorParameter( "diffuseColor" );
const InternedString g_emissionColorParameter( "emission_color" );
const InternedString g_emissionStrengthParameter( "emission_strength" );
const InternedString g_emissiveColorParameter( "emissiveColor" );
const InternedString g_enableColorTemperatureParameter( "enableColorTemperature" );
const InternedString g_exposureParameter( "exposure" );
const InternedString g_extensionParameter( "extension" );
const InternedString g_facParameter( "fac" );
const InternedString g_fileParameter( "file" );
const InternedString g_fileColorspaceParameter( "file_colorspace" );
const InternedString g_filenameParameter( "filename" );
const InternedString g_gParameter( "g" );
const InternedString g_heightParameter( "height" );
const InternedString g_inParameter( "in" );
const InternedString g_intensityParameter( "intensity" );
const InternedString g_iorParameter( "ior" );
const InternedString g_lengthParameter( "length" );
const InternedString g_locationParameter( "location" );
const InternedString g_mappingTypeParameter( "mapping_type" );
const InternedString g_mathTypeParameter( "math_type" );
const InternedString g_metallicParameter( "metallic" );
const InternedString g_normalParameter( "normal" );
const InternedString g_normalizeParameter( "normalize" );
const InternedString g_occlusionParameter( "occlusion" );
const InternedString g_opacityParameter( "opacity" );
const InternedString g_opacityModeParameter( "opacityMode" );
const InternedString g_opacityThresholdParameter( "opacityThreshold" );
const InternedString g_parametricParameter( "parametric" );
const InternedString g_positionParameter( "position");
const InternedString g_projectionParameter( "projection");
const InternedString g_rParameter( "r" );
const InternedString g_radiusParameter( "radius" );
const InternedString g_resultParameter( "result" );
const InternedString g_rgbParameter( "rgb" );
const InternedString g_rotationParameter( "rotation" );
const InternedString g_scaleParameter( "scale" );
const InternedString g_roughnessParameter( "roughness" );
const InternedString g_shadowEnableParameter( "shadow:enable" );
const InternedString g_shapingConeAngleParameter( "shaping:cone:angle" );
const InternedString g_shapingConeSoftnessParameter( "shaping:cone:softness" );
const InternedString g_sizeParameter( "size" );
const InternedString g_sourceColorSpaceParameter( "sourceColorSpace" );
const InternedString g_specularParameter( "specular" );
const InternedString g_specularColorParameter( "specularColor" );
const InternedString g_specularTintParameter( "specular_tint" );
const InternedString g_specularIORLevelParameter( "specular_ior_level" );
const InternedString g_spotAngleParameter( "spot_angle" );
const InternedString g_spotSmoothParameter( "spot_smooth" );
const InternedString g_stParameter( "st" );
const InternedString g_surfaceParameter( "surface" );
const InternedString g_textureFileParameter( "texture:file" );
const InternedString g_textureFormatParameter( "texture:format" );
const InternedString g_texMappingScaleParameter( "tex_mapping__scale" );
const InternedString g_texMappingYMappingParameter( "tex_mapping__y_mapping" );
const InternedString g_texMappingZMappingParameter( "tex_mapping__z_mapping" );
const InternedString g_translationParameter( "translation" );
const InternedString g_transmissionWeightParameter( "transmission_weight" );
const InternedString g_treatAsPointParameter( "treatAsPoint" );
const InternedString g_useClampParameter( "use_clamp" );
const InternedString g_useMISParameter( "use_mis" );
const InternedString g_useSpecularWorkflowParameter( "useSpecularWorkflow" );
const InternedString g_UVParameter( "UV" );
const InternedString g_valueParameter( "value" );
const InternedString g_value1Parameter( "value1" );
const InternedString g_value2Parameter( "value2" );
const InternedString g_value3Parameter( "value3" );
const InternedString g_varnameParameter( "varname" );
const InternedString g_vectorParameter( "vector" );
const InternedString g_vectorXParameter( "vector.x" );
const InternedString g_vectorYParameter( "vector.y" );
const InternedString g_vectorZParameter( "vector.z" );
const InternedString g_vector1Parameter( "vector1" );
const InternedString g_vector2Parameter( "vector2" );
const InternedString g_vector3Parameter( "vector3" );
const InternedString g_widthParameter( "width" );
const InternedString g_wrapSParameter( "wrapS" );
const InternedString g_wrapTParameter( "wrapT" );
const InternedString g_USDRayVisibilityBlindDataKey( "__USDRayVisibility" );

const InternedString g_interpolationParameter( "interpolation" );
const InternedString g_filterTypeParameter( "filterType" );
const InternedString g_uAddressModeParameter( "uaddressmode" );
const InternedString g_vAddressModeParameter( "vaddressmode" );
const InternedString g_texcoordParameter( "texcoord" );
const InternedString g_defaultParameter( "default" );
const InternedString g_geompropParameter( "geomprop" );
const InternedString g_indexParameter( "index" );
const InternedString g_strengthParameter( "strength" );
const InternedString g_tangentParameter( "tangent" );
const InternedString g_bitangentParameter( "bitangent" );

const string g_cyclesNamespace( "cycles:" );

void transferUSDLightParameters( ShaderNetwork *network, InternedString shaderHandle, const Shader *usdShader, Shader *shader )
{
	Color3f color = parameterValue( usdShader, g_colorParameter, Color3f( 1 ) );
	if( parameterValue( usdShader, g_enableColorTemperatureParameter, false ) )
	{
		color *= blackbody( parameterValue( usdShader, g_colorTemperatureParameter, 6500.0f ) );
	}
	shader->parameters()[g_colorParameter] = new Color3fData( color );

	transferUSDParameter( network, shaderHandle, usdShader, g_exposureParameter, shader, g_exposureParameter, 0.0f );
	transferUSDParameter( network, shaderHandle, usdShader, g_intensityParameter, shader, g_intensityParameter, 1.0f );
	transferUSDParameter( network, shaderHandle, usdShader, g_normalizeParameter, shader, g_normalizeParameter, false );
	transferUSDParameter( network, shaderHandle, usdShader, g_shadowEnableParameter, shader, g_castShadowParameter, true );

	int visibility = (int)ccl::PATH_RAY_ALL_VISIBILITY;
	if( parameterValue( usdShader, g_diffuseParameter, 1.0f ) == 0.0f )
	{
		visibility &= ~(int)ccl::PATH_RAY_DIFFUSE;
	}
	if( parameterValue( usdShader, g_specularParameter, 1.0f ) == 0.0f )
	{
		visibility &= ~(int)ccl::PATH_RAY_GLOSSY;
	}
	shader->blindData()->writable()[g_USDRayVisibilityBlindDataKey] = new IntData( visibility );

	shader->parameters()[g_useMISParameter] = new BoolData( true );

	for( const auto &[name, value] : usdShader->parameters() )
	{
		if( boost::starts_with( name.string(), g_cyclesNamespace ) )
		{
			shader->parameters()[name.string().substr(g_cyclesNamespace.size())] = value;
		}
	}
}

void transferUSDShapingParameters( ShaderNetwork *network, InternedString shaderHandle, const Shader *usdShader, Shader *shader )
{
	if( auto d = usdShader->parametersData()->member<FloatData>( g_shapingConeAngleParameter ) )
	{
		shader->setName( "spot_light" );
		shader->parameters()[g_spotAngleParameter] = new FloatData( d->readable() * 2.0f );
		/// \todo This logic is copied from `IECoreArnold::ShaderNetworkAlgo` for reasons
		/// documented there, but I suspect things have moved on a bit and we can probably
		/// take this at face value now.
		if( parameterValue( usdShader, g_shapingConeSoftnessParameter, 0.0f ) > 1.0 )
		{
			IECore::msg( IECore::Msg::Warning, "transferUSDShapingParameters", "Ignoring `shaping:cone:softness` as it is greater than 1" );
		}
		else
		{
			transferUSDParameter( network, shaderHandle, usdShader, g_shapingConeSoftnessParameter, shader, g_spotSmoothParameter, 0.0f );
		}
	}
}

// Should be called after `transferUSDLightParameters()`, as it needs to examine
// the transferred `color` parameter.
void transferUSDTextureFile( ShaderNetwork *network, InternedString shaderHandle, const Shader *usdShader, const Shader *shader )
{
	const string textureFile = parameterValue( usdShader, g_textureFileParameter, string() );
	if( textureFile.empty() )
	{
		return;
	}

	ShaderPtr imageShader;
	if( usdShader->getName() == "DomeLight" )
	{
		imageShader = new Shader( "environment_texture", "cycles:shader" );
		string format = parameterValue( usdShader, g_textureFormatParameter, string( "equirectangular" ) );
		if( format == "mirroredBall" )
		{
			format = "mirror_ball";
		}
		else if( format == "latlong" )
		{
			format = "equirectangular";
		}
		else
		{
			IECore::msg( IECore::Msg::Warning, "convertUSDShaders", fmt::format( "Unsupported value \"{}\" for DomeLight.format", format ) );
			format = "equirectangular";
		}
		imageShader->parameters()[g_projectionParameter] = new StringData( format );
		imageShader->parameters()[g_texMappingScaleParameter] = new V3fData( V3f( -1.0f, 1.0f, 1.0f ) );
		imageShader->parameters()[g_texMappingYMappingParameter] = new StringData( "z" );
		imageShader->parameters()[g_texMappingZMappingParameter] = new StringData( "y" );
	}
	else
	{
		// RectLight
		imageShader = new Shader( "image_texture", "cycles:shader" );
		imageShader->parameters()[g_texMappingScaleParameter] = new V3fData( V3f( -1.0f, 1.0f, 1.0f ) );
	}

	imageShader->parameters()[g_filenameParameter] = new StringData( textureFile );
	const InternedString imageHandle = network->addShader( "ColorImage", std::move( imageShader ) );

	const Color3f color = parameterValue( shader, g_colorParameter, Color3f( 1 ) );
	if( color != Color3f( 1 ) )
	{
		// Multiply image with color
		ShaderPtr multiplyShader = new Shader( "vector_math" );
		multiplyShader->parameters()[g_mathTypeParameter] = new StringData( "multiply" );
		multiplyShader->parameters()[g_vector2Parameter] = new Color3fData( color );
		const InternedString multiplyHandle = network->addShader( shaderHandle.string() + "Multiply", std::move( multiplyShader ) );
		network->addConnection( ShaderNetwork::Connection( { multiplyHandle, g_vectorParameter }, { shaderHandle, g_colorParameter } ) );
		network->addConnection( ShaderNetwork::Connection( { imageHandle, g_colorParameter }, { multiplyHandle, g_vector1Parameter } ) );
	}
	else
	{
		// Connect image directly
		network->addConnection( ShaderNetwork::Connection( { imageHandle, g_colorParameter }, { shaderHandle, g_colorParameter } ) );
	}

	if( shader->getName() == "quad_light" )
	{
		// For the correct coordinate mapping for quad lights, a geometry
		// shader with the parametric output is needed.
		ShaderPtr geometryShader = new Shader( "geometry" );
		const InternedString geometryHandle = network->addShader( shaderHandle.string() + "Geometry", std::move( geometryShader ) );
		network->addConnection( ShaderNetwork::Connection( { geometryHandle, g_parametricParameter }, { imageHandle, g_vectorParameter } ) );
	}
	else if( shader->getName() == "background_light" )
	{
		// We need a 90 degree rotation about Y on the map, which we could
		// easily add as a `tex_mapping.rotation` parameter value on
		// `imageShader`. Except that it would get clobbered by
		// `ShaderCache::updateShaders()` in `Renderer.cpp` - see additional
		// comments there. So instead we insert a `mapping` shader to do that
		// separately.
		ShaderPtr mappingShader = new Shader( "mapping" );
		mappingShader->parameters()[g_rotationParameter] = new V3fData( V3f( 0, -M_PI / 2.0, 0 ) );
		const InternedString mappingHandle = network->addShader( shaderHandle.string() + "Mapping", std::move( mappingShader ) );
		network->addConnection( ShaderNetwork::Connection( { mappingHandle, g_vectorParameter }, { imageHandle, g_vectorParameter } ) );
		ShaderPtr geometryShader = new Shader( "geometry" );
		const InternedString geometryHandle = network->addShader( shaderHandle.string() + "Geometry", std::move( geometryShader ) );
		network->addConnection( ShaderNetwork::Connection( { geometryHandle, g_positionParameter }, { mappingHandle, g_vectorParameter } ) );
	}
}

// Map of USD shader output parameter names to their equivalent Cycles parameter name.
const std::unordered_map<InternedString, InternedString> g_outputParameterMap = {
	{ g_surfaceParameter, g_BSDFParameter },
	{ g_rgbParameter, g_colorParameter },
	{ g_rParameter, g_colorRParameter },
	{ g_gParameter, g_colorGParameter},
	{ g_bParameter, g_colorBParameter },
	{ g_aParameter, g_alphaParameter },
};

// Map of USD shaders with `result` parameters to the output of their equivalent Cycles shader.
const std::unordered_map<std::string, InternedString> g_resultParameterMap = {
	{ "UsdPrimvarReader_int", g_facParameter },
	{ "UsdPrimvarReader_float", g_facParameter },
	{ "UsdPrimvarReader_float2", g_UVParameter },
	{ "UsdPrimvarReader_float3", g_colorParameter },
	{ "UsdPrimvarReader_float4", g_colorParameter },
	{ "UsdPrimvarReader_normal", g_vectorParameter },
	{ "UsdPrimvarReader_point", g_vectorParameter },
	{ "UsdPrimvarReader_vector", g_vectorParameter },
	{ "UsdTransform2d", g_vectorParameter },
	{ "ND_geompropvalue_boolean", g_facParameter },
	{ "ND_geompropvalue_float", g_facParameter },
	{ "ND_geompropvalue_integer", g_facParameter },
	{ "ND_geompropvalue_color3", g_colorParameter },
	{ "ND_geompropvalue_color4", g_colorParameter },
	{ "ND_geompropvalue_vector2", g_UVParameter },
	{ "ND_geompropvalue_vector3", g_colorParameter },
	{ "ND_geompropvalue_vector4", g_colorParameter },
	{ "ND_normalmap", g_normalParameter },
	{ "ND_normalmap_float", g_normalParameter },
	{ "ND_normalmap_vector2", g_normalParameter },
};

const InternedString remapOutputParameterName( const InternedString name, const InternedString shaderName )
{
	if( name == g_resultParameter )
	{
		// `result` parameters are remapped based on the shader name
		const auto it = g_resultParameterMap.find( shaderName );
		if( it != g_resultParameterMap.end() )
		{
			return it->second;
		}
	}
	else if( boost::starts_with( shaderName.string(), "ND_image" ) )
	{
		return g_colorParameter;
	}
	else
	{
		// other parameters can be remapped from their name directly
		const auto it = g_outputParameterMap.find( name );
		if( it != g_outputParameterMap.end() )
		{
			return it->second;
		}
	}

	return name;
}

void removeInput( ShaderNetwork *network, const ShaderNetwork::Parameter &parameter )
{
	if( auto i = network->input( parameter ) )
	{
		network->removeConnection( { i, parameter } );
	}
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

void convertUSDUVTextures( ShaderNetwork *network )
{
	for( const auto &[handle, shader] : network->shaders() )
	{
		if( shader->getName() != "UsdUVTexture" )
		{
			continue;
		}

		ShaderPtr imageShader = new Shader( "image_texture", "cycles:shader" );
		transferUSDParameter( network, handle, shader.get(), g_fileParameter, imageShader.get(), g_filenameParameter, string() );
		transferUSDParameter( network, handle, shader.get(), g_sourceColorSpaceParameter, imageShader.get(), g_colorspaceParameter, string() );

		// Cycles has a single "extension" parameter for wrapping a texture in
		// both directions, so we take the first wrap parameter with a value.
		string mode = parameterValue( shader.get(), g_wrapSParameter, string() );
		if( mode.empty() )
		{
			mode = parameterValue( shader.get(), g_wrapTParameter, string( "useMetadata" ) );
		}
		if( mode == "useMetadata" )
		{
			// Cycles has no mode matching "useMetadata" so the UsdPreviewSurface spec
			// says this should fall back to "black".
			mode = "black";
		}
		else if( mode == "repeat" )
		{
			mode = "periodic";
		}
		imageShader->parameters()[g_extensionParameter] = new StringData( mode );

		if( auto input = network->input( { handle, g_stParameter } ) )
		{
			transferUSDParameter( network, handle, shader.get(), g_stParameter, imageShader.get(), g_vectorParameter, V3f( 0 ) );
		}

		// The Cycles image_texture shader provides no parameters for colour correction,
		// so we insert additional shaders to handle any scale and bias adjustments.
		const Color4f scale = parameterValue( shader.get(), g_scaleParameter, Color4f( 1 ) );
		const Color4f bias = parameterValue( shader.get(), g_biasParameter, Color4f( 0 ) );
		if( scale != Color4f( 1 ) || bias != Color4f( 0 ) )
		{
			ShaderPtr multiplyAddShader = new Shader( "vector_math", "cycles:shader" );
			multiplyAddShader->parameters()[g_mathTypeParameter] = new StringData( "multiply_add" );
			multiplyAddShader->parameters()[g_vector2Parameter] = new Color3fData( Color3f( scale.r, scale.g, scale.b ) );
			multiplyAddShader->parameters()[g_vector3Parameter] = new Color3fData( Color3f( bias.r, bias.g, bias.b ) );
			const InternedString multiplyAddHandle = network->addShader( handle.string() + "MultiplyAdd", std::move( multiplyAddShader ) );

			ShaderPtr alphaMultiplyAddShader = new Shader( "math", "cycles:shader" );
			alphaMultiplyAddShader->parameters()[g_mathTypeParameter] = new StringData( "multiply_add" );
			alphaMultiplyAddShader->parameters()[g_value2Parameter] = new FloatData( scale.a );
			alphaMultiplyAddShader->parameters()[g_value3Parameter] = new FloatData( bias.a );
			const InternedString alphaMultiplyAddHandle = network->addShader( handle.string() + "AlphaMultiplyAdd", std::move( alphaMultiplyAddShader ) );

			ShaderNetwork::ConnectionRange range = network->outputConnections( handle );
			vector<ShaderNetwork::Connection> outputConnections( range.begin(), range.end() );
			for( auto &c : outputConnections )
			{
				InternedString sourceHandle = multiplyAddHandle;
				InternedString sourceParameter = g_vectorParameter;
				if( c.source.name == g_rParameter )
				{
					sourceParameter = g_vectorXParameter;
				}
				else if( c.source.name == g_gParameter )
				{
					sourceParameter = g_vectorYParameter;
				}
				else if( c.source.name == g_bParameter )
				{
					sourceParameter = g_vectorZParameter;
				}
				else if( c.source.name == g_aParameter )
				{
					sourceHandle = alphaMultiplyAddHandle;
					sourceParameter = g_valueParameter;
				}

				network->removeConnection( c );
				network->addConnection( ShaderNetwork::Connection( { sourceHandle, sourceParameter }, c.destination ) );
			}

			network->addConnection( ShaderNetwork::Connection( { handle, g_colorParameter }, { multiplyAddHandle, g_vector1Parameter } ) );
			network->addConnection( ShaderNetwork::Connection( { handle, g_alphaParameter }, { alphaMultiplyAddHandle, g_value1Parameter } ) );
		}

		replaceUSDShader( network, handle, std::move( imageShader ) );
	}
}

void convertMtlxTextures( ShaderNetwork *network )
{
	for( const auto &[handle, shader] : network->shaders() )
	{
		if( !boost::starts_with( shader->getName(), "ND_image" ) )
		{
			continue;
		}

		ShaderPtr imageShader = new Shader( "image_texture", "cycles:shader" );
		transferUSDParameter( network, handle, shader.get(), g_fileParameter, imageShader.get(), g_filenameParameter, string() );
		transferUSDParameter( network, handle, shader.get(), g_fileColorspaceParameter, imageShader.get(), g_colorspaceParameter, string() );

		// ND_image has the same matching names as Cycles for `closest`, `linear`, `cubic`
		imageShader->parameters()[g_interpolationParameter] = new StringData( parameterValue( shader.get(), g_filterTypeParameter, string( "linear" ) ) );

		// Cycles has a single "extension" parameter for wrapping a texture in
		// both directions, so we take the first wrap parameter with a value.
		// All seem to match Cycles except `constant`, which we will map to `black`.
		string mode = parameterValue( shader.get(), g_uAddressModeParameter, string() );
		if( mode.empty() )
		{
			mode = parameterValue( shader.get(), g_vAddressModeParameter, string( "periodic" ) );
		}
		if( mode == "constant" )
		{
			mode = "black";
		}
		imageShader->parameters()[g_extensionParameter] = new StringData( mode );

		transferUSDParameter( network, handle, shader.get(), g_texcoordParameter, imageShader.get(), g_vectorParameter, V3f( 0.0f ) );

		replaceUSDShader( network, handle, std::move( imageShader ) );
	}
}

} // namespace

void IECoreCycles::ShaderNetworkAlgo::convertUSDShaders( ShaderNetwork *shaderNetwork )
{
	// Must convert these first, before we convert the connected
	// UsdPrimvarReader inputs.
	convertUSDUVTextures( shaderNetwork );

	for( const auto &[handle, shader] : shaderNetwork->shaders() )
	{
		ShaderPtr newShader;
		if( shader->getName() == "UsdPreviewSurface" )
		{
			newShader = new Shader( "principled_bsdf", "cycles:surface" );

			// Easy stuff with a one-to-one correspondence between `UsdPreviewSurface` and `principled_bsdf`.

			transferUSDParameter( shaderNetwork, handle, shader.get(), g_diffuseColorParameter, newShader.get(), g_baseColorParameter, Color3f( 0.18 ) );
			transferUSDParameter( shaderNetwork, handle, shader.get(), g_roughnessParameter, newShader.get(), g_roughnessParameter, 0.5f );
			transferUSDParameter( shaderNetwork, handle, shader.get(), g_clearcoatParameter, newShader.get(), g_coatWeightParameter, 0.0f );
			transferUSDParameter( shaderNetwork, handle, shader.get(), g_clearcoatRoughnessParameter, newShader.get(), g_coatRoughnessParameter, 0.01f );
			transferUSDParameter( shaderNetwork, handle, shader.get(), g_iorParameter, newShader.get(), g_iorParameter, 1.5f );

			// Emission. UsdPreviewSurface only has `emissiveColor`, which we transfer to `emission_color`. But then
			// we need to turn on Cycles' `emission_strength` to that the `emission_color` is actually used.

			transferUSDParameter( shaderNetwork, handle, shader.get(), g_emissiveColorParameter, newShader.get(), g_emissionColorParameter, Color3f( 0 ) );
			const bool hasEmission =
				shaderNetwork->input( { handle, g_emissionColorParameter } ) ||
				parameterValue( newShader.get(), g_emissionColorParameter, Color3f( 0 ) ) != Color3f( 0 );
			;
			newShader->parameters()[g_emissionStrengthParameter] = new FloatData( hasEmission ? 1.0f : 0.0f );

			// Specular.

			// Cycles' default value of `specular_ior_level` is 0, we need to set it to 0.5 to enable specular.
			newShader->parameters()[g_specularIORLevelParameter] = new FloatData( 0.5f );
			if( parameterValue<int>( shader.get(), g_useSpecularWorkflowParameter, 0 ) )
			{
				transferUSDParameter( shaderNetwork, handle, shader.get(), g_specularColorParameter, newShader.get(), g_specularTintParameter, Color3f( 0.0f ) );
				removeInput( shaderNetwork, { handle, g_metallicParameter } );
			}
			else
			{
				transferUSDParameter( shaderNetwork, handle, shader.get(), g_metallicParameter, newShader.get(), g_metallicParameter, 0.0f );
			}

			removeInput( shaderNetwork, { handle, g_specularColorParameter } );

			// Opacity. USD has a funky `opacityThreshold` thing, that we need to implement
			// with a little compare/multiply network.

			float opacity = parameterValue( shader.get(), g_opacityParameter, 1.0f );
			const string opacityMode = parameterValue( shader.get(), g_opacityModeParameter, string( "transparent" ) );
			const float opacityThreshold = parameterValue( shader.get(), g_opacityThresholdParameter, 0.0f );
			if( const ShaderNetwork::Parameter opacityInput = shaderNetwork->input( { handle, g_opacityParameter } ) )
			{
				if( opacityThreshold != 0.0f )
				{
					ShaderPtr compareShader = new Shader( "math", "cycles:shader" );
					compareShader->parameters()[g_value2Parameter] = new FloatData( opacityThreshold );
					compareShader->parameters()[g_mathTypeParameter] = new StringData( "greater_than" );
					const InternedString compareHandle = shaderNetwork->addShader( handle.string() + "OpacityCompare", std::move( compareShader ) );
					shaderNetwork->addConnection( ShaderNetwork::Connection( opacityInput, { compareHandle, g_value1Parameter } ) );
					ShaderPtr multiplyShader = new Shader( "math", "cycles:shader" );
					multiplyShader->parameters()[g_mathTypeParameter] = new StringData( "multiply" );
					const InternedString multiplyHandle = shaderNetwork->addShader( handle.string() + "OpacityMultiply", std::move( multiplyShader ) );
					shaderNetwork->addConnection( ShaderNetwork::Connection( opacityInput, { multiplyHandle, g_value1Parameter } ) );
					shaderNetwork->addConnection( ShaderNetwork::Connection( { compareHandle, g_valueParameter }, { multiplyHandle, g_value2Parameter } ) );
					shaderNetwork->removeConnection( ShaderNetwork::Connection( opacityInput, { handle, g_opacityParameter } ) );
					shaderNetwork->addConnection( ShaderNetwork::Connection( { multiplyHandle, g_valueParameter }, { handle, g_alphaParameter } ) );
				}
				else if( opacityMode == string( "presence" ) )
				{
					transferUSDParameter( shaderNetwork, handle, shader.get(), g_opacityParameter, newShader.get(), g_alphaParameter, 1.0f );
				}
				else
				{
					shaderNetwork->removeConnection( ShaderNetwork::Connection( opacityInput, { handle, g_opacityParameter } ) );
				}

				if( opacityMode == string( "transparent" ) )
				{
					ShaderPtr invertShader = new Shader( "math", "cycles:shader" );
					invertShader->parameters()[g_value1Parameter] = new FloatData( 1.0f );
					invertShader->parameters()[g_mathTypeParameter] = new StringData( "subtract" );
					invertShader->parameters()[g_useClampParameter] = new BoolData( true );
					const InternedString invertHandle = shaderNetwork->addShader( handle.string() + "OpacityInvert", std::move( invertShader ) );
					shaderNetwork->addConnection( ShaderNetwork::Connection( opacityInput, { invertHandle, g_value2Parameter } ) );
					shaderNetwork->addConnection( ShaderNetwork::Connection( { invertHandle, g_valueParameter }, { handle, g_transmissionWeightParameter } ) );
				}
			}
			else if( opacityMode == string( "transparent" ) )
			{
				newShader->parameters()[g_transmissionWeightParameter] = new FloatData( 1.0f - opacity );
			}
			else
			{
				opacity = opacity > opacityThreshold ? opacity : 0.0f;
				newShader->parameters()[g_alphaParameter] = new FloatData( opacity );
			}

			// Normal.
			if( const ShaderNetwork::Parameter normalInput = shaderNetwork->input( { handle, g_normalParameter } ) )
			{
				ShaderPtr normalmapShader = new Shader( "normal_map", "cycles:shader" );
				const InternedString normalmapHandle = shaderNetwork->addShader( handle.string() + "NormalMap", std::move( normalmapShader ) );
				ShaderPtr normalizeShader = new Shader( "vector_math", "cycles:shader" );
				normalizeShader->parameters()[g_mathTypeParameter] = new StringData( "normalize" );
				const InternedString normalizeHandle = shaderNetwork->addShader( handle.string() + "Normalize", std::move( normalizeShader ) );
				shaderNetwork->addConnection( ShaderNetwork::Connection( normalInput, { normalmapHandle, g_colorParameter } ) );
				shaderNetwork->addConnection( ShaderNetwork::Connection( { normalmapHandle, g_normalParameter }, { normalizeHandle, g_vector1Parameter } ) );
				shaderNetwork->removeConnection( ShaderNetwork::Connection( normalInput, { handle, g_normalParameter } ) );
				shaderNetwork->addConnection( ShaderNetwork::Connection( { normalizeHandle, g_vectorParameter }, { handle, g_normalParameter } ) );
			}

			// Remove occlusion.
			removeInput( shaderNetwork, { handle, g_occlusionParameter } );
		}
		else if( shader->getName() == "UsdTransform2d" )
		{
			newShader = new Shader( "mapping", "cycles:shader" );
			transferUSDParameter( shaderNetwork, handle, shader.get(), g_inParameter, newShader.get(), g_vectorParameter, V3f( 0 ) );
			const V2f t = parameterValue( shader.get(), g_translationParameter, V2f( 0 ) );
			const float r = parameterValue( shader.get(), g_rotationParameter, 0.0f );
			const V2f s = parameterValue( shader.get(), g_scaleParameter, V2f( 1 ) );
			// The `mapping` shader has a "texture" mapping type, but it is not useful in this case as we are
			// transforming the texture coordinates, not the textures themselves.
			newShader->parameters()[g_mappingTypeParameter] = new StringData( "point" );
			newShader->parameters()[g_locationParameter] = new V3fData( V3f( t.x, t.y, 0.0f ) );
			newShader->parameters()[g_rotationParameter] = new V3fData( V3f( 0.0f, 0.0f, IECore::degreesToRadians( r ) ) );
			newShader->parameters()[g_scaleParameter] = new V3fData( V3f( s.x, s.y, 1.0f ) );
		}
		else if( shader->getName() == "UsdPrimvarReader_float2" )
		{
			newShader = new Shader( "uvmap", "cycles:shader" );

			if( parameterValue<string>( shader.get(), g_varnameParameter, "" ) == "st" )
			{
				newShader->parameters()[g_attributeParameter] = new StringData( "uv" );
			}
			else
			{
				transferUSDParameter( shaderNetwork, handle, shader.get(), g_varnameParameter, newShader.get(), g_attributeParameter, string() );
			}
		}
		else if(
			shader->getName() == "UsdPrimvarReader_float" ||
			shader->getName() == "UsdPrimvarReader_float3" ||
			shader->getName() == "UsdPrimvarReader_float4" ||
			shader->getName() == "UsdPrimvarReader_normal" ||
			shader->getName() == "UsdPrimvarReader_point" ||
			shader->getName() == "UsdPrimvarReader_vector" ||
			shader->getName() == "UsdPrimvarReader_int" ||
			shader->getName() == "UsdPrimvarReader_string"
		)
		{
			newShader = new Shader( "attribute", "cycles:shader" );
			transferUSDParameter( shaderNetwork, handle, shader.get(), g_varnameParameter, newShader.get(), g_attributeParameter, string() );
		}
		else if( shader->getName() == "SphereLight" )
		{
			newShader = new Shader( "point_light", "cycles:light" );
			transferUSDLightParameters( shaderNetwork, handle, shader.get(), newShader.get() );
			transferUSDShapingParameters( shaderNetwork, handle, shader.get(), newShader.get() );
			transferUSDParameter( shaderNetwork, handle, shader.get(), g_radiusParameter, newShader.get(), g_sizeParameter, 0.5f );
			if( parameterValue( shader.get(), g_treatAsPointParameter, false ) )
			{
				newShader->parameters()[g_sizeParameter] = new FloatData( 0.0 );
				newShader->parameters()[g_normalizeParameter] = new BoolData( true );
			}
		}
		else if( shader->getName() == "DiskLight" )
		{
			newShader = new Shader( "disk_light", "cycles:light" );
			transferUSDLightParameters( shaderNetwork, handle, shader.get(), newShader.get() );
			transferUSDShapingParameters( shaderNetwork, handle, shader.get(), newShader.get() );
			const float size = parameterValue( shader.get(), g_radiusParameter, 1.0f ) * 2.0f;
			newShader->parameters()[g_widthParameter] = new FloatData( size );
		}
		else if( shader->getName() == "CylinderLight" )
		{
			// No cylinder light in Cycles, so convert to a point light of vaguely the right size.
			newShader = new Shader( "point_light", "cycles:light" );
			transferUSDLightParameters( shaderNetwork, handle, shader.get(), newShader.get() );
			transferUSDShapingParameters( shaderNetwork, handle, shader.get(), newShader.get() );
			const float radius = parameterValue( shader.get(), g_radiusParameter, 0.5f );
			const float length = parameterValue( shader.get(), g_lengthParameter, 1.0f );
			newShader->parameters()[g_sizeParameter] = new FloatData( std::max( radius, length / 2.0f ) );
			IECore::msg( IECore::Msg::Warning, "ShaderNetworkAlgo", "Converting USD CylinderLight to Cycles point light" );
		}
		else if( shader->getName() == "DistantLight" )
		{
			newShader = new Shader( "distant_light", "cycles:light" );
			transferUSDLightParameters( shaderNetwork, handle, shader.get(), newShader.get() );
			transferUSDShapingParameters( shaderNetwork, handle, shader.get(), newShader.get() );
			transferUSDParameter( shaderNetwork, handle, shader.get(), g_angleParameter, newShader.get(), g_angleParameter, 0.53f );
		}
		else if( shader->getName() == "DomeLight" )
		{
			newShader = new Shader( "background_light", "cycles:light" );
			transferUSDLightParameters( shaderNetwork, handle, shader.get(), newShader.get() );
			transferUSDTextureFile( shaderNetwork, handle, shader.get(), newShader.get() );
			/// \todo This brings performance into line with a default-created `background_light`
			/// using the CyclesLight node, which will define a resolution of 1024 to override
			/// the auto-resolution-from-map behaviour of Cycles. If we don't do this, viewer
			/// update gets very choppy when using high resolution maps. I suspect this may
			/// indicate a problem whereby we are causing Cycles to rebuild importance maps
			/// unnecessarily when moving the free camera.
			newShader->parameters()["map_resolution"] = new IntData( 1024 );
		}
		else if( shader->getName() == "RectLight" )
		{
			newShader = new Shader( "quad_light", "cycles:light" );
			transferUSDLightParameters( shaderNetwork, handle, shader.get(), newShader.get() );
			transferUSDParameter( shaderNetwork, handle, shader.get(), g_widthParameter, newShader.get(), g_widthParameter, 1.0f );
			transferUSDParameter( shaderNetwork, handle, shader.get(), g_heightParameter, newShader.get(), g_heightParameter, 1.0f );
			transferUSDTextureFile( shaderNetwork, handle, shader.get(), newShader.get() );
		}

		if( newShader )
		{
			replaceUSDShader( shaderNetwork, handle, std::move( newShader ) );
		}
	}

	IECoreScene::ShaderNetworkAlgo::removeUnusedShaders( shaderNetwork );
}

void IECoreCycles::ShaderNetworkAlgo::convertMtlxShaders( ShaderNetwork *shaderNetwork )
{
	// Must convert these first.
	// Cycles doesn't allow us to use UDIMs on any other shaders except
	// the built-in `image_texture` node, so we convert those.
	// Unfortunately the biggest drawback here is there's no `default`
	// or `missingColor` fallback so those cases will fail...
	convertMtlxTextures( shaderNetwork );

	for( const auto &[handle, shader] : shaderNetwork->shaders() )
	{
		ShaderPtr newShader;
		if( shader->getName() == "ND_geompropvalue_vector2" )
		{
			// Cycles seems to do some extra plumbing to get UV reading to work that
			// free-form calls to `getattribute` in OSL would fail on, so we swap it
			// in the Cycles `uvmap` node.
			newShader = new Shader( "uvmap", "cycles:shader" );

			if( parameterValue<string>( shader.get(), g_geompropParameter, "" ) == "st" )
			{
				newShader->parameters()[g_attributeParameter] = new StringData( "uv" );
			}
			else
			{
				transferUSDParameter( shaderNetwork, handle, shader.get(), g_geompropParameter, newShader.get(), g_attributeParameter, string() );
			}
		}
		else if( boost::starts_with( shader->getName(), "ND_geompropvalue" ) )
		{
			// Same story for geompropvalue, we swap these for a Cycles `attribute`.
			newShader = new Shader( "attribute", "cycles:shader" );
			transferUSDParameter( shaderNetwork, handle, shader.get(), g_geompropParameter, newShader.get(), g_attributeParameter, string() );
		}
		else if( boost::starts_with( shader->getName(), "ND_texcoord" ) )
		{
			// This shader is an index, most likely for the GLSL backend of MaterialX
			// which GenOSL essentially just maps u and v builtins of OSL and ignores
			// the index number entirely. This isn't quite what we expect, so just default
			// to the default UVs of the model and ignore the index number.
			// Maya's LookdevX will make a similar warning that the node isn't supported
			// for name-based renderers.
			newShader = new Shader( "uvmap", "cycles:shader" );
			newShader->parameters()[g_attributeParameter] = new StringData( "uv" );

			if( parameterValue<int>( shader.get(), g_indexParameter, 0 ) != 0 )
			{
				msg(
					Msg::Warning, "IECoreCycles::ShaderNetworkAlgo",
					fmt::format( "MaterialX node \"{}\" is not supported in a name-based renderer and will use the default UVs.", shader->getName() )
				);
			}
		}
		else if( boost::starts_with( shader->getName(), "ND_geomcolor" ) )
		{
			msg(
				Msg::Warning, "IECoreCycles::ShaderNetworkAlgo",
				fmt::format( "MaterialX node \"{}\" is not supported in a name-based renderer.", shader->getName() )
			);
		}
		/*
		else if( boost::starts_with( shader->getName(), "ND_normalmap" ) )
		{
			newShader = new Shader( "normal_map", "cycles:shader" );

			transferUSDParameter( shaderNetwork, handle, shader.get(), g_inParameter, newShader.get(), g_colorParameter, V3f( 0.5f, 0.5, 1.0f ) );
			transferUSDParameter( shaderNetwork, handle, shader.get(), g_scaleParameter, newShader.get(), g_strengthParameter, 1.0f );

			if( const ShaderNetwork::Parameter tangentInput = shaderNetwork->input( { handle, g_tangentParameter } ) )
			{
				const Shader *inShader = shaderNetwork->getShader( tangentInput.shader );
				if( boost::starts_with( inShader->getName(), "ND_tangent" ) )
				{
					IECore::msg(
						IECore::Msg::Warning,
						"IECoreCycles",
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
						"IECoreCycles",
						fmt::format( "MaterialX node `{}` is not supported in a name-based renderer.",
						inShader->getName() ) );
				}
				shaderNetwork->removeConnection( { bitangentInput, { handle, g_bitangentParameter } } );
			}
		}
		*/

		for( const auto &c : shaderNetwork->inputConnections( handle ) )
		{
			const Shader *inShader = shaderNetwork->getShader( c.source.shader );
			if( boost::starts_with( shader->getName(), "ND_geompropvalueuniform" ) )
			{
				const std::string paramValue = parameterValue<string>( inShader, g_geompropParameter, std::string() );
				if( !paramValue.empty() )
				{
					newShader->parameters()[c.destination.name] = new StringData( fmt::format( "<attr:{}>", paramValue ) );
					msg(
						Msg::Debug, "IECoreCycles::ShaderNetworkAlgo",
						fmt::format( "MaterialX node \"{}\" of shader \"{}\" has been converted to a string substitution onto \"{}.{}\" but will ignore the default fallback value.",
						inShader->getName(), c.source.shader.string(), handle.string(), c.destination.name.string() )
					);
				}
				shaderNetwork->removeConnection( c );
			}
		}

		if( newShader )
		{
			replaceUSDShader( shaderNetwork, handle, std::move( newShader ) );
		}
	}

	IECoreScene::ShaderNetworkAlgo::removeUnusedShaders( shaderNetwork );
}
