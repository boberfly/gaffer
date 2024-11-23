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
#include "Session.h"

#include "IECore/DataAlgo.h"
#include "IECore/MessageHandler.h"

#include "boost/container/flat_map.hpp"

#include "fmt/format.h"

#include <unordered_set>

using namespace std;
using namespace IECore;
using namespace IECoreScene;
using namespace IECoreRenderMan;

//////////////////////////////////////////////////////////////////////////
// Internal utilities
//////////////////////////////////////////////////////////////////////////

namespace
{

std::optional<pxrcore::DataType> parameterType( const IECoreScene::Shader *shader, InternedString name )
{
	const Data *value = shader->parametersData()->member( name );
	if( !value )
	{
		return std::nullopt;
	}

	switch( value->typeId() )
	{
		case IntDataTypeId :
			return pxrcore::DataType::k_integer;
		case FloatDataTypeId :
			return pxrcore::DataType::k_float;
		case V3fDataTypeId : {
			switch( static_cast<const V3fData *>( value )->getInterpretation() )
			{
				case GeometricData::Vector :
					return pxrcore::DataType::k_vector;
				case GeometricData::Normal :
					return pxrcore::DataType::k_normal;
				default :
					return pxrcore::DataType::k_point;
			}
		}
		case Color3fDataTypeId :
			return pxrcore::DataType::k_color;
		case StringDataTypeId :
			return pxrcore::DataType::k_string;
		default :
			return std::nullopt;
	}
}

using HandleSet = std::unordered_set<InternedString>;

void convertConnection( const IECoreScene::ShaderNetwork::Connection &connection, const IECoreScene::Shader *shader, RtParamList &paramList )
{
	std::optional<pxrcore::DataType> type = parameterType( shader, connection.destination.name );
	if( !type )
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
		*type,
		pxrcore::DetailType::k_reference,
		1,
		false,
		false,
		false
	};

	paramList.SetParam( info, &referenceU );
}

const boost::container::flat_map<string, RtUString> g_shaderNameConversions = {
	// The lights from UsdLux bear a remarkable resemblance to RenderMan's
	// lights, almost as if they may have been put together rather hastily, with
	// little consideration for standardisation ;) That does at least make
	// conversion easy for _one_ renderer backend though.
	/// \todo This was too optimistic. There are also a bunch of parameter
	/// renames that we need to take into account.
	{ "CylinderLight", RtUString( "PxrCylinderLight" ) },
	{ "DiskLight", RtUString( "PxrDiskLight" ) },
	{ "DistantLight", RtUString( "PxrDistantLight" ) },
	{ "DomeLight", RtUString( "PxrDomeLight" ) },
	{ "RectLight", RtUString( "PxrRectLight" ) },
	{ "SphereLight", RtUString( "PxrSphereLight" ) },
};

void convertShaderNetworkWalk( const ShaderNetwork::Parameter &outputParameter, const IECoreScene::ShaderNetwork *shaderNetwork, vector<riley::ShadingNode> &shadingNodes, HandleSet &visited )
{
	if( !visited.insert( outputParameter.shader ).second )
	{
		return;
	}

	const IECoreScene::Shader *shader = shaderNetwork->getShader( outputParameter.shader );
	auto nameIt = g_shaderNameConversions.find( shader->getName() );
	RtUString shaderName = nameIt != g_shaderNameConversions.end() ? nameIt->second : RtUString( shader->getName().c_str() );

	riley::ShadingNode node = {
		riley::ShadingNode::Type::k_Pattern,
		shaderName,
		RtUString( outputParameter.shader.c_str() ),
		RtParamList()
	};

	bool isFilterCombiner = false;

	if( shader->getType() == "light" || shader->getType() == "ri:light" )
	{
		node.type = riley::ShadingNode::Type::k_Light;
	}
	else if( shader->getType() == "surface" || shader->getType() == "ri:surface" )
	{
		node.type = riley::ShadingNode::Type::k_Bxdf;
	}
	else if( shader->getType() == "displacement" || shader->getType() == "ri:displacement" || shader->getType() == "osl:displacement" )
	{
		node.type = riley::ShadingNode::Type::k_Displacement;
	}
	else if( shader->getType() == "ri:shader" && visited.size() == 1 )
	{
		// Work around failure of IECoreUSD to round-trip surface shader type.
		/// \todo Either fix the round-trip in IECoreUSD, or derive `node.type`
		/// from the `.args` file instead. The latter might be preferable in the
		/// long term, because we're trying to phase out the concept of shader
		/// type.
		node.type = riley::ShadingNode::Type::k_Bxdf;
	}
	else if( shader->getType() == "ri:displayfilter" )
	{
		node.type = riley::ShadingNode::Type::k_DisplayFilter;
		isFilterCombiner = shader->getName() == "PxrDisplayFilterCombiner" ? true : false;
	}
	else if( shader->getType() == "ri:samplefilter" )
	{
		node.type = riley::ShadingNode::Type::k_SampleFilter;
		isFilterCombiner = shader->getName() == "PxrSampleFilterCombiner" ? true : false;
	}

	if( !isFilterCombiner )
	{
		ParamListAlgo::convertParameters( shader->parameters(), node.params );
	}

	for( const auto &connection : shaderNetwork->inputConnections( outputParameter.shader ) )
	{
		convertShaderNetworkWalk( connection.source, shaderNetwork, shadingNodes, visited );
		if( !isFilterCombiner )
		{
			convertConnection( connection, shader, node.params );
		}
	}

	shadingNodes.push_back( node );
}

} // namespace

//////////////////////////////////////////////////////////////////////////
// Public API
//////////////////////////////////////////////////////////////////////////

std::vector<riley::ShadingNode> IECoreRenderMan::ShaderNetworkAlgo::convert( const IECoreScene::ShaderNetwork *network )
{
	vector<riley::ShadingNode> result;
	result.reserve( network->size() );

	HandleSet visited;
	convertShaderNetworkWalk( network->getOutput(), network, result, visited );

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
