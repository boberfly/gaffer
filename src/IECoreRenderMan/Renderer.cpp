//////////////////////////////////////////////////////////////////////////
//
//  Copyright (c) 2018, John Haddon. All rights reserved.
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

#include "Camera.h"
#include "GeometryAlgo.h"
#include "Globals.h"
#include "ParamListAlgo.h"
#include "Session.h"

#include "GafferScene/Private/IECoreScenePreview/Renderer.h"

#include "IECoreScene/ShaderNetwork.h"

#include "IECore/DataAlgo.h"
#include "IECore/LRUCache.h"
#include "IECore/MessageHandler.h"
#include "IECore/SearchPath.h"
#include "IECore/SimpleTypedData.h"

#include "boost/algorithm/string.hpp"
#include "boost/algorithm/string/predicate.hpp"
#include "boost/make_unique.hpp"
#include "boost/property_tree/xml_parser.hpp"

#include "tbb/concurrent_hash_map.h"
#include "tbb/spin_mutex.h"

#include "Riley.h"
#include "RixPredefinedStrings.hpp"

#include "fmt/format.h"

#include <thread>
#include <unordered_map>
#include <unordered_set>

using namespace std;
using namespace Imath;
using namespace IECore;
using namespace IECoreScene;
using namespace IECoreRenderMan;

//////////////////////////////////////////////////////////////////////////
// Utilities
//////////////////////////////////////////////////////////////////////////

namespace
{

template<typename T>
T *reportedCast( const IECore::RunTimeTyped *v, const char *type, const IECore::InternedString &name )
{
	T *t = IECore::runTimeCast<T>( v );
	if( t )
	{
		return t;
	}

	IECore::msg( IECore::Msg::Warning, "IECoreRenderMan::Renderer", boost::format( "Expected %s but got %s for %s \"%s\"." ) % T::staticTypeName() % v->typeName() % type % name.c_str() );
	return nullptr;
}

template<typename T, typename MapType>
const T *parameter( const MapType &parameters, const IECore::InternedString &name )
{
	auto it = parameters.find( name );
	if( it == parameters.end() )
	{
		return nullptr;
	}

	return reportedCast<const T>( it->second.get(), "parameter", name );
}

template<typename T>
T parameter( const IECore::CompoundDataMap &parameters, const IECore::InternedString &name, const T &defaultValue )
{
	typedef IECore::TypedData<T> DataType;
	if( const DataType *d = parameter<DataType>( parameters, name ) )
	{
		return d->readable();
	}
	else
	{
		return defaultValue;
	}
}

struct StaticTransform : riley::Transform
{

	StaticTransform( const Imath::M44f &m = Imath::M44f() )
		:	m_time( 0 )
	{
		samples = 1;
		matrix = &reinterpret_cast<const RtMatrix4x4 &>( m );
		time = &m_time;
	}

	private :

		float m_time;

};

struct AnimatedTransform : riley::Transform
{

	AnimatedTransform( const vector<Imath::M44f> &transformSamples, const vector<float> &sampleTimes )
	{
		samples = transformSamples.size();
		matrix = reinterpret_cast<const RtMatrix4x4 *>( transformSamples.data() );
		time = sampleTimes.data();
	}

};

static riley::CoordinateSystemList g_emptyCoordinateSystems = { 0, nullptr };

} // namespace

//////////////////////////////////////////////////////////////////////////
// Shaders
//////////////////////////////////////////////////////////////////////////

namespace
{

using ParameterTypeMap = std::unordered_map<InternedString, pxrcore::DataType>;
using ParameterTypeMapPtr = shared_ptr<ParameterTypeMap>;
using ParameterTypeCache = IECore::LRUCache<string, ParameterTypeMapPtr>;

void loadParameterTypes( const boost::property_tree::ptree &tree, ParameterTypeMap &typeMap )
{
	for( const auto &child : tree )
	{
		if( child.first == "param" )
		{
			const string name = child.second.get<string>( "<xmlattr>.name" );
			const string type = child.second.get<string>( "<xmlattr>.type" );
			if( type == "float" )
			{
				typeMap[name] = pxrcore::DataType::k_float;
			}
			else if( type == "int" )
			{
				typeMap[name] = pxrcore::DataType::k_integer;
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
			else if( type == "color" )
			{
				typeMap[name] = pxrcore::DataType::k_color;
			}
			else if( type == "string" )
			{
				typeMap[name] = pxrcore::DataType::k_string;
			}
			else if( type == "struct" )
			{
				typeMap[name] = pxrcore::DataType::k_struct;
			}
			else
			{
				IECore::msg( IECore::Msg::Warning, "IECoreRenderMan::Renderer", boost::format( "Unknown type %s for parameter \"%s\"." ) % type % name );
			}
		}
		else if( child.first == "page" )
		{
			loadParameterTypes( child.second, typeMap );
		}
	}
}

ParameterTypeCache g_parameterTypeCache(

	[]( const std::string &shaderName, size_t &cost ) {

		const char *pluginPath = getenv( "RMAN_RIXPLUGINPATH" );
		SearchPath searchPath( pluginPath ? pluginPath : "" );

		boost::filesystem::path argsFilename = searchPath.find( "Args/" + shaderName + ".args" );
		if( argsFilename.empty() )
		{
			throw IECore::Exception(
				boost::str(
					boost::format( "Unable to find shader \"%s\" on RMAN_RIXPLUGINPATH" ) % shaderName
				)
			);
		}

		std::ifstream argsStream( argsFilename.string() );

		boost::property_tree::ptree tree;
		boost::property_tree::read_xml( argsStream, tree );

		auto parameterTypes = make_shared<ParameterTypeMap>();
		loadParameterTypes( tree.get_child( "args" ), *parameterTypes );

		cost = 1;
		return parameterTypes;
	},
	/* maxCost = */ 10000

);

const pxrcore::DataType *parameterType( const Shader *shader, IECore::InternedString parameterName )
{
	ParameterTypeMapPtr p = g_parameterTypeCache.get( shader->getName() );
	auto it = p->find( parameterName );
	if( it != p->end() )
	{
		return &it->second;
	}
	return nullptr;
}

using HandleSet = std::unordered_set<InternedString>;

void convertConnection( const IECoreScene::ShaderNetwork::Connection &connection, const IECoreScene::Shader *shader, RtParamList &paramList )
{
	const pxrcore::DataType *type = parameterType( shader, connection.destination.name );
	if( !type )
	{
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

void convertShaderNetworkWalk( const ShaderNetwork::Parameter &outputParameter, const IECoreScene::ShaderNetwork *shaderNetwork, vector<riley::ShadingNode> &shadingNodes, HandleSet &visited )
{
	if( !visited.insert( outputParameter.shader ).second )
	{
		return;
	}

	const IECoreScene::Shader *shader = shaderNetwork->getShader( outputParameter.shader );
	riley::ShadingNode node = {
		riley::ShadingNode::Type::k_Pattern,
		RtUString( shader->getName().c_str() ),
		RtUString( outputParameter.shader.c_str() ),
		RtParamList()
	};

	if( shader->getType() == "light" || shader->getType() == "renderman:light" )
	{
		node.type = riley::ShadingNode::Type::k_Light;
	}
	else if( shader->getType() == "surface" || shader->getType() == "renderman:bxdf" )
	{
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

riley::LightShaderId convertLightShaderNetwork( const ShaderNetwork *network, riley::Riley *riley )
{
	vector<riley::ShadingNode> shadingNodes;
	shadingNodes.reserve( network->size() );

	HandleSet visited;
	convertShaderNetworkWalk( network->getOutput(), network, shadingNodes, visited );

	return riley->CreateLightShader(
		riley::UserId(), { (uint32_t)shadingNodes.size(), shadingNodes.data() },
		/* lightFilter = */ { 0, nullptr }
	);
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

// A reference counted material.
class RenderManMaterial : public IECore::RefCounted
{

	public :

		RenderManMaterial( const IECoreScene::ShaderNetwork *network, const ConstSessionPtr &session )
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

		~RenderManMaterial() override
		{
			if( m_session->renderType == IECoreScenePreview::Renderer::Interactive )
			{
				m_session->riley->DeleteMaterial( m_id );
			}
		}

		const riley::MaterialId &id() const
		{
			return m_id;
		}

	private :

		ConstSessionPtr m_session;
		riley::MaterialId m_id;

};

IE_CORE_DECLAREPTR( RenderManMaterial )

class ShaderCache : public IECore::RefCounted
{

	public :

		ShaderCache( const ConstSessionPtr &session )
			:	m_session( session )
		{
		}

		// Can be called concurrently with other calls to `get()`
		ConstRenderManMaterialPtr get( const IECoreScene::ShaderNetwork *network )
		{
			Cache::accessor a;
			m_cache.insert( a, network ? network->Object::hash() : IECore::MurmurHash() );
			if( !a->second )
			{
				a->second = new RenderManMaterial( network, m_session );
			}
			return a->second;
		}

		// Must not be called concurrently with anything.
		void clearUnused()
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

	private :

		ConstSessionPtr m_session;

		using Cache = tbb::concurrent_hash_map<IECore::MurmurHash, ConstRenderManMaterialPtr>;
		Cache m_cache;

};

IE_CORE_DECLAREPTR( ShaderCache )

} // namespace

//////////////////////////////////////////////////////////////////////////
// RenderManAttributes
//////////////////////////////////////////////////////////////////////////

namespace
{

const string g_renderManPrefix( "renderman:" );
InternedString g_surfaceShaderAttributeName( "renderman:bxdf" );
InternedString g_lightShaderAttributeName( "renderman:light" );

class RenderManAttributes : public IECoreScenePreview::Renderer::AttributesInterface
{

	public :

		// TODO : IS THIS STILL TRUE?
		// We deliberately do not pass a `Riley *` here. Trying to make parallel
		// calls of any sort before `RenderManGlobals::ensureWorld()` will trigger
		// RenderMan crashes, and RenderManAttributes instances are constructed
		// for use with RenderManCameras. Instead we pass a ShaderCache which
		// allows us to generate materials lazily on demand, when RenderManObjects
		// ask for them.
		RenderManAttributes( const IECore::CompoundObject *attributes, ShaderCachePtr shaderCache )
			:	m_shaderCache( shaderCache )
		{
			m_surfaceShader = parameter<ShaderNetwork>( attributes->members(), g_surfaceShaderAttributeName );
			m_lightShader = parameter<ShaderNetwork>( attributes->members(), g_lightShaderAttributeName );

			for( const auto &attribute : attributes->members() )
			{
				if( boost::starts_with( attribute.first.c_str(), g_renderManPrefix.c_str() ) )
				{
					if( auto data = runTimeCast<const Data>( attribute.second.get() ) )
					{
						ParamListAlgo::convertParameter( RtUString( attribute.first.c_str() + g_renderManPrefix.size() ), data, m_paramList );
					}
				}
				else if( boost::starts_with( attribute.first.c_str(), "user:" ) )
				{
					if( auto data = runTimeCast<const Data>( attribute.second.get() ) )
					{
						ParamListAlgo::convertParameter( RtUString( attribute.first.c_str() ), data, m_paramList );
					}
				}
			}
		}

		~RenderManAttributes()
		{
		}

		ConstRenderManMaterialPtr material() const
		{
			return m_shaderCache->get( m_surfaceShader.get() );
		}

		const IECoreScene::ShaderNetwork *lightShader() const
		{
			return m_lightShader.get();
		}

		const RtParamList &paramList() const
		{
			return m_paramList;
		}

	private :

		RtParamList m_paramList;
		IECoreScene::ConstShaderNetworkPtr m_surfaceShader;
		IECoreScene::ConstShaderNetworkPtr m_lightShader;
		ShaderCachePtr m_shaderCache;

};

IE_CORE_DECLAREPTR( RenderManAttributes )

} // namespace

//////////////////////////////////////////////////////////////////////////
// RenderManObject
//////////////////////////////////////////////////////////////////////////

namespace
{

class RenderManObject : public IECoreScenePreview::Renderer::ObjectInterface
{

	public :

		RenderManObject( riley::GeometryPrototypeId geometryPrototype, const RenderManAttributes *attributes, const ConstSessionPtr &session )
			:	m_session( session ), m_geometryInstance( riley::GeometryInstanceId::InvalidId() )
		{
			if( geometryPrototype != riley::GeometryPrototypeId::InvalidId() )
			{
				m_material = attributes->material();
				m_geometryInstance = m_session->riley->CreateGeometryInstance(
					riley::UserId(),
					/* group = */ riley::GeometryPrototypeId::InvalidId(),
					geometryPrototype,
					m_material->id(),
					g_emptyCoordinateSystems,
					StaticTransform(),
					attributes->paramList()
				);
			}
		}

		~RenderManObject()
		{
			if( m_session->renderType == IECoreScenePreview::Renderer::Interactive )
			{
				if( m_geometryInstance != riley::GeometryInstanceId::InvalidId() )
				{
					m_session->riley->DeleteGeometryInstance( riley::GeometryPrototypeId::InvalidId(), m_geometryInstance );
				}
			}
		}

		void transform( const Imath::M44f &transform ) override
		{
			StaticTransform staticTransform( transform );
			const riley::GeometryInstanceResult result = m_session->riley->ModifyGeometryInstance(
				/* group = */ riley::GeometryPrototypeId::InvalidId(),
				m_geometryInstance,
				/* material = */ nullptr,
				/* coordsys = */ nullptr,
				&staticTransform,
				/* attributes = */ nullptr
			);

			if( result != riley::GeometryInstanceResult::k_Success )
			{
				IECore::msg( IECore::Msg::Warning, "RenderManObject::transform", "Unexpected edit failure" );
			}
		}

		void transform( const std::vector<Imath::M44f> &samples, const std::vector<float> &times ) override
		{
			AnimatedTransform animatedTransform( samples, times );
			const riley::GeometryInstanceResult result = m_session->riley->ModifyGeometryInstance(
				/* group = */ riley::GeometryPrototypeId::InvalidId(),
				m_geometryInstance,
				/* material = */ nullptr,
				/* coordsys = */ nullptr,
				&animatedTransform,
				/* attributes = */ nullptr
			);

			if( result != riley::GeometryInstanceResult::k_Success )
			{
				IECore::msg( IECore::Msg::Warning, "RenderManObject::transform", "Unexpected edit failure" );
			}
		}

		bool attributes( const IECoreScenePreview::Renderer::AttributesInterface *attributes ) override
		{
			const auto renderManAttributes = static_cast<const RenderManAttributes *>( attributes );
			m_material = renderManAttributes->material();

			const riley::GeometryInstanceResult result = m_session->riley->ModifyGeometryInstance(
				/* group = */ riley::GeometryPrototypeId::InvalidId(),
				m_geometryInstance,
				&m_material->id(),
				/* coordsys = */ nullptr,
				/* xform = */ nullptr,
				&renderManAttributes->paramList()
			);

			if( result != riley::GeometryInstanceResult::k_Success )
			{
				IECore::msg( IECore::Msg::Warning, "RenderManObject::attributes", "Unexpected edit failure" );
			}
			return true;
		}

		void link( const IECore::InternedString &type, const IECoreScenePreview::Renderer::ConstObjectSetPtr &objects ) override
		{
		}

		void assignID( uint32_t id ) override
		{
		}

	private :

		ConstSessionPtr m_session;
		riley::GeometryInstanceId m_geometryInstance;
		/// Used to keep material etc alive as long as we need it.
		/// \todo Not sure if this is necessary or not? Perhaps Riley will
		/// extend lifetime anyway? It's not clear if `DeleteMaterial`
		/// actually destroys the material, or just drops a reference
		/// to it.
		ConstRenderManMaterialPtr m_material;

};

} // namespace

//////////////////////////////////////////////////////////////////////////
// RenderManLight
//////////////////////////////////////////////////////////////////////////

namespace
{

class RenderManLight : public IECoreScenePreview::Renderer::ObjectInterface
{

	public :

		RenderManLight( riley::GeometryPrototypeId geometryPrototype, const ConstRenderManAttributesPtr &attributes, const ConstSessionPtr &session )
			:	m_session( session ), m_lightShader( riley::LightShaderId::InvalidId() ), m_lightInstance( riley::LightInstanceId::InvalidId() )
		{
			updateLightShader( attributes.get() );
			if( m_lightShader == riley::LightShaderId::InvalidId() )
			{
				// Riley crashes if we try to edit the transform on a light
				// without a shader, so we just don't make such lights.
				return;
			}

			m_lightInstance = m_session->riley->CreateLightInstance(
				riley::UserId(),
				/* group = */ riley::GeometryPrototypeId::InvalidId(),
				geometryPrototype,
				riley::MaterialId::InvalidId(), /// \todo Use `attributes->material()`?
				m_lightShader,
				g_emptyCoordinateSystems,
				StaticTransform(),
				attributes->paramList()
			);
		}

		~RenderManLight()
		{
			if( m_session->renderType == IECoreScenePreview::Renderer::Interactive )
			{
				if( m_lightInstance != riley::LightInstanceId::InvalidId() )
				{
					m_session->riley->DeleteLightInstance( riley::GeometryPrototypeId::InvalidId(), m_lightInstance );
				}
				if( m_lightShader != riley::LightShaderId::InvalidId() )
				{
					m_session->riley->DeleteLightShader( m_lightShader );
				}
			}
		}

		void transform( const Imath::M44f &transform ) override
		{
			if( m_lightInstance == riley::LightInstanceId::InvalidId() )
			{
				return;
			}

			const M44f flippedTransform = M44f().scale( V3f( 1, 1, -1 ) ) * transform;
			StaticTransform staticTransform( flippedTransform );

			const riley::LightInstanceResult result = m_session->riley->ModifyLightInstance(
				/* group = */ riley::GeometryPrototypeId::InvalidId(),
				m_lightInstance,
				/* material = */ nullptr,
				/* light shader = */ nullptr,
				/* coordsys = */ nullptr,
				&staticTransform,
				/* attributes = */ nullptr
			);

			if( result != riley::LightInstanceResult::k_Success )
			{
				IECore::msg( IECore::Msg::Warning, "RenderManLight::transform", "Unexpected edit failure" );
			}
		}

		void transform( const std::vector<Imath::M44f> &samples, const std::vector<float> &times ) override
		{
			if( m_lightInstance == riley::LightInstanceId::InvalidId() )
			{
				return;
			}

			vector<Imath::M44f> flippedSamples = samples;
			for( auto &m : flippedSamples )
			{
				m = M44f().scale( V3f( 1, 1, -1 ) ) * m;
			}
			AnimatedTransform animatedTransform( flippedSamples, times );

			const riley::LightInstanceResult result = m_session->riley->ModifyLightInstance(
				/* group = */ riley::GeometryPrototypeId::InvalidId(),
				m_lightInstance,
				/* material = */ nullptr,
				/* light shader = */ nullptr,
				/* coordsys = */ nullptr,
				&animatedTransform,
				/* attributes = */ nullptr
			);

			if( result != riley::LightInstanceResult::k_Success )
			{
				IECore::msg( IECore::Msg::Warning, "RenderManLight::transform", "Unexpected edit failure" );
			}
		}

		bool attributes( const IECoreScenePreview::Renderer::AttributesInterface *attributes ) override
		{
			auto renderManAttributes = static_cast<const RenderManAttributes *>( attributes );
			updateLightShader( renderManAttributes );

			if( m_lightInstance == riley::LightInstanceId::InvalidId() )
			{
				// Occurs when we were created without a valid shader. We can't
				// magic the light into existence now, even if the new
				// attributes have a valid shader, because we don't know the
				// transform. If we now have a shader, then return false to
				// request that the whole object is sent again from scratch.
				return m_lightShader == riley::LightShaderId::InvalidId();
			}

			if( m_lightShader == riley::LightShaderId::InvalidId() )
			{
				// Riley crashes when a light doesn't have a valid shader, so we delete the light.
				// If we get a valid shader from a later attribute edit, we'll handle that above.
				m_session->riley->DeleteLightInstance( riley::GeometryPrototypeId::InvalidId(), m_lightInstance );
				m_lightInstance = riley::LightInstanceId::InvalidId();
				return true;
			}

			const riley::LightInstanceResult result = m_session->riley->ModifyLightInstance(
				/* group = */ riley::GeometryPrototypeId::InvalidId(),
				m_lightInstance,
				/* material = */ nullptr,
				/* light shader = */ &m_lightShader,
				/* coordsys = */ nullptr,
				/* xform = */ nullptr,
				&renderManAttributes->paramList()
			);

			if( result != riley::LightInstanceResult::k_Success )
			{
				IECore::msg( IECore::Msg::Warning, "RenderManLight::attributes", "Unexpected edit failure" );
				return false;
			}
			return true;
		}

		void link( const IECore::InternedString &type, const IECoreScenePreview::Renderer::ConstObjectSetPtr &objects ) override
		{
		}

		void assignID( uint32_t id ) override
		{
		}

	private :

		void updateLightShader( const RenderManAttributes *attributes )
		{
			if( m_lightShader != riley::LightShaderId::InvalidId() )
			{
				m_session->riley->DeleteLightShader( m_lightShader );
				m_lightShader = riley::LightShaderId::InvalidId();
			}

			if( attributes->lightShader() )
			{
				m_lightShader = convertLightShaderNetwork( attributes->lightShader(), m_session->riley );
			}
		}

		const ConstSessionPtr m_session;
		riley::LightShaderId m_lightShader;
		riley::LightInstanceId m_lightInstance;

};

} // namespace

//////////////////////////////////////////////////////////////////////////
// RenderManRenderer
//////////////////////////////////////////////////////////////////////////

namespace
{

class RenderManRenderer final : public IECoreScenePreview::Renderer
{

	public :

		/// TODO : USE MESSAGEHANDLER
		RenderManRenderer( RenderType renderType, const std::string &fileName, const MessageHandlerPtr &messageHandler )
		{
			if( renderType == SceneDescription )
			{
				throw IECore::Exception( "SceneDescription mode not supported by RenderMan" );
			}

			m_session = new Session( renderType, messageHandler );
			m_globals = boost::make_unique<Globals>( m_session );
			m_shaderCache = new ShaderCache( m_session );
		}

		~RenderManRenderer() override
		{
			m_globals.reset();
		}

		IECore::InternedString name() const override
		{
			return "RenderMan";
		}

		void option( const IECore::InternedString &name, const IECore::Object *value ) override
		{
			m_globals->option( name, value );
		}

		void output( const IECore::InternedString &name, const Output *output ) override
		{
			m_globals->output( name, output );
		}

		Renderer::AttributesInterfacePtr attributes( const IECore::CompoundObject *attributes ) override
		{
			return new RenderManAttributes( attributes, m_shaderCache );
		}

		ObjectInterfacePtr camera( const std::string &name, const IECoreScene::Camera *camera, const AttributesInterface *attributes ) override
		{
			IECoreRenderMan::CameraPtr result = new IECoreRenderMan::Camera( name, camera, m_session );
			result->attributes( attributes );
			return result;
		}

		ObjectInterfacePtr light( const std::string &name, const IECore::Object *object, const AttributesInterface *attributes ) override
		{
			m_globals->ensureWorld();
			riley::GeometryPrototypeId geometryPrototype = riley::GeometryPrototypeId::InvalidId();
			if( object )
			{
				/// \todo Cache geometry masters
				geometryPrototype = GeometryAlgo::convert( object, m_session->riley );
			}
			return new RenderManLight( geometryPrototype, static_cast<const RenderManAttributes *>( attributes ), m_session );
		}

		ObjectInterfacePtr lightFilter( const std::string &name, const IECore::Object *object, const AttributesInterface *attributes ) override
		{
			return nullptr;
		}

		Renderer::ObjectInterfacePtr object( const std::string &name, const IECore::Object *object, const AttributesInterface *attributes ) override
		{
			m_globals->ensureWorld();
			/// \todo Cache geometry masters
			riley::GeometryPrototypeId geometryPrototype = GeometryAlgo::convert( object, m_session->riley );
			return new RenderManObject( geometryPrototype, static_cast<const RenderManAttributes *>( attributes ), m_session );
		}

		ObjectInterfacePtr object( const std::string &name, const std::vector<const IECore::Object *> &samples, const std::vector<float> &times, const AttributesInterface *attributes ) override
		{
			/// \todo Convert all time samples
			return object( name, samples.front(), attributes );
		}

		void render() override
		{
			m_shaderCache->clearUnused();
			m_globals->render();
		}

		void pause() override
		{
			m_globals->pause();
		}

		IECore::DataPtr command( const IECore::InternedString name, const IECore::CompoundDataMap &parameters ) override
		{
			if( name == "renderman:worldBegin" )
			{
				m_globals->ensureWorld();
			}

			return nullptr;
		}

	private :

		SessionPtr m_session;

		std::unique_ptr<Globals> m_globals;
		ShaderCachePtr m_shaderCache;

		static Renderer::TypeDescription<RenderManRenderer> g_typeDescription;

};

IECoreScenePreview::Renderer::TypeDescription<RenderManRenderer> RenderManRenderer::g_typeDescription( "RenderMan" );

} // namespace
