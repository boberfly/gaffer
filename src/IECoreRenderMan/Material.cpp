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

#include "Material.h"

#include "ParamListAlgo.h"

#include "IECore/DataAlgo.h"

#include "boost/container/flat_map.hpp"

#include "fmt/format.h"

#include <unordered_set>

using namespace std;
using namespace IECore;
using namespace IECoreScene;
using namespace IECoreRenderMan;

//////////////////////////////////////////////////////////////////////////
// Internal implementation
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

	if( shader->getType() == "light" || shader->getType() == "ri:light" )
	{
		node.type = riley::ShadingNode::Type::k_Light;
	}
	else if( shader->getType() == "surface" || shader->getType() == "ri:surface" )
	{
		node.type = riley::ShadingNode::Type::k_Bxdf;
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

	ParamListAlgo::convertParameters( shader->parameters(), node.params );

	for( const auto &connection : shaderNetwork->inputConnections( outputParameter.shader ) )
	{
		convertShaderNetworkWalk( connection.source, shaderNetwork, shadingNodes, visited );
		convertConnection( connection, shader, node.params );
	}

	shadingNodes.push_back( node );
}

riley::MaterialId convertShaderNetwork( const ShaderNetwork *network, riley::Riley *riley )
{
	vector<riley::ShadingNode> shadingNodes;
	shadingNodes.reserve( network->size() );

	HandleSet visited;
	convertShaderNetworkWalk( network->getOutput(), network, shadingNodes, visited );

	return riley->CreateMaterial( riley::UserId(), { (uint32_t)shadingNodes.size(), shadingNodes.data() }, RtParamList() );
}

riley::MaterialId defaultMaterial( riley::Riley *riley )
{
	vector<riley::ShadingNode> shaders;

	shaders.push_back( { riley::ShadingNode::Type::k_Pattern, RtUString( "PxrFacingRatio" ), RtUString( "facingRatio" ), RtParamList() } );

	RtParamList toFloat3ParamList;
	toFloat3ParamList.SetFloatReference( RtUString( "input" ), RtUString( "facingRatio:resultF" ) );
	shaders.push_back( { riley::ShadingNode::Type::k_Pattern, RtUString( "PxrToFloat3" ), RtUString( "toFloat3" ), toFloat3ParamList } );

	RtParamList constantParamList;
	constantParamList.SetColorReference( RtUString( "emitColor" ), RtUString( "toFloat3:resultRGB" ) );
	shaders.push_back( { riley::ShadingNode::Type::k_Bxdf, RtUString( "PxrConstant" ), RtUString( "constant" ), constantParamList } );

	return riley->CreateMaterial( riley::UserId(), { (uint32_t)shaders.size(), shaders.data() }, RtParamList() );
}

} // namespace

//////////////////////////////////////////////////////////////////////////
// Material
//////////////////////////////////////////////////////////////////////////

Material::Material( const IECoreScene::ShaderNetwork *network, const Session *session )
	:	m_session( session )
{
	if( network )
	{
		m_id = convertShaderNetwork( network, m_session->riley );
	}
	else
	{
		m_id = defaultMaterial( m_session->riley );
	}
}

Material::~Material()
{
	if( m_session->renderType == IECoreScenePreview::Renderer::Interactive )
	{
		m_session->riley->DeleteMaterial( m_id );
	}
}

const riley::MaterialId &Material::id() const
{
	return m_id;
}

riley::LightShaderId IECoreRenderMan::convertLightShaderNetwork( const IECoreScene::ShaderNetwork *network, Session *session )
{
	vector<riley::ShadingNode> shadingNodes;
	shadingNodes.reserve( network->size() );

	HandleSet visited;
	convertShaderNetworkWalk( network->getOutput(), network, shadingNodes, visited );

	return session->createLightShader( { (uint32_t)shadingNodes.size(), shadingNodes.data() } );
}

//////////////////////////////////////////////////////////////////////////
// MaterialCache
//////////////////////////////////////////////////////////////////////////

MaterialCache::MaterialCache( const Session *session )
	:	m_session( session )
{
}

// Can be called concurrently with other calls to `get()`
ConstMaterialPtr MaterialCache::get( const IECoreScene::ShaderNetwork *network )
{
	Cache::accessor a;
	m_cache.insert( a, network ? network->Object::hash() : IECore::MurmurHash() );
	if( !a->second )
	{
		a->second = new Material( network, m_session );
	}
	return a->second;
}

// Must not be called concurrently with anything.
void MaterialCache::clearUnused()
{
	vector<IECore::MurmurHash> toErase;
	for( const auto &m : m_cache )
	{
		if( m.second->refCount() == 1 )
		{
			// Only one reference - this is ours, so
			// nothing outside of the cache is using the
			// shader.
			toErase.push_back( m.first );
		}
	}
	for( const auto &e : toErase )
	{
		m_cache.erase( e );
	}
}
