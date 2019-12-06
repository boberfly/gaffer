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

#include "GeometryAlgo.h"
#include "ParamListAlgo.h"

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

#include "tbb/spin_mutex.h"

#include "Riley.h"
#include "RixPredefinedStrings.hpp"

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

static riley::ScopedCoordinateSystem g_emptyCoordinateSystems = { 0, nullptr };

// The various Renderer components all need access to the same
// Riley object, and also need to know the render type because it
// affects whether or not they need to delete resources on destruction.
// Furthermore, we don't want to require all client code to destroy
// all AttributeInterfaces and ObjectInterfaces before destroying the
// renderer - that's too much of a pain, especially in Python. All
// components therefore share ownership of a Session, which provides
// the Riley instance and render type, and is destroyed only when the
// last owner dies.
struct Session : public IECore::RefCounted
{

	Session( IECoreScenePreview::Renderer::RenderType renderType )
		:	riley(
				((RixRileyManager *)RixGetContext()->GetRixInterface( k_RixRileyManager ))->CreateRiley( nullptr )
			),
			renderType( renderType )
	{
	}

	~Session()
	{
		auto m = (RixRileyManager *)RixGetContext()->GetRixInterface( k_RixRileyManager );
		m->DestroyRiley( riley );
	}

	riley::Riley * const riley;
	const IECoreScenePreview::Renderer::RenderType renderType;

};

IE_CORE_DECLAREPTR( Session );

} // namespace

//////////////////////////////////////////////////////////////////////////
// RenderManCamera
//////////////////////////////////////////////////////////////////////////

namespace
{

class RenderManCamera :  public IECoreScenePreview::Renderer::ObjectInterface
{

	public :

		// We deliberately do not pass a `Riley *` here. Trying to make parallel
		// calls of any sort before `RenderManGlobals::ensureWorld()` will trigger
		// RenderMan crashes, so we must bide our time and convert things later.
		RenderManCamera( const IECoreScene::Camera *camera, const std::function<void ()> &destructor = std::function<void ()>() )
			:	m_destructor( destructor )
		{
			// Options

			const V2i resolution = camera->getResolution();
			m_options.SetIntegerArray( Rix::k_Ri_FormatResolution, resolution.getValue(), 2 );
			m_options.SetFloat( Rix::k_Ri_FormatPixelAspectRatio, camera->getPixelAspectRatio() );

			const V2f shutter = camera->getShutter();
			m_options.SetFloatArray( Rix::k_Ri_Shutter, shutter.getValue(), 2 );

			// Parameters

			m_parameters.SetFloat( Rix::k_nearClip, camera->getClippingPlanes()[0] );
			m_parameters.SetFloat( Rix::k_farClip, camera->getClippingPlanes()[1] );

			// Projection

			/// \todo Fill projection from camera
			m_projectionParameters.SetFloat( Rix::k_fov, 35.0f );

			m_projection = {
				riley::ShadingNode::k_Projection, RtUString( "PxrCamera" ),
				RtUString( "projection" ), m_projectionParameters
			};

			transform( M44f() );
		}

		~RenderManCamera()
		{
			if( m_destructor )
			{
				m_destructor();
			}
		}

		const riley::ShadingNode &projection() const
		{
			return m_projection;
		}

		const riley::Transform &cameraToWorldTransform() const
		{
			return m_cameraToWorldTransform;
		}

		const RtParamList &parameters() const
		{
			return m_parameters;
		}

		const RtParamList &options() const
		{
			return m_options;
		}

		// ObjectInterface methods
		// =======================

		void transform( const Imath::M44f &transform ) override
		{
			transformInternal( { transform }, { 0.0f } );
		}

		void transform( const std::vector<Imath::M44f> &samples, const std::vector<float> &times ) override
		{
			transformInternal( samples, times );
		}

		bool attributes( const IECoreScenePreview::Renderer::AttributesInterface *attributes ) override
		{
			return true;
		}

		void link( const IECore::InternedString &type, const IECoreScenePreview::Renderer::ConstObjectSetPtr &objects ) override
		{
		}

	private :

		void transformInternal( const std::vector<Imath::M44f> &samples, const std::vector<float> &times )
		{
			m_transformSamples = samples;
			for( auto &m : m_transformSamples )
			{
				m = M44f().scale( V3f( 1, 1, -1 ) ) * m;
			}

			m_transformTimes = times;

			m_cameraToWorldTransform.samples = m_transformSamples.size();
			m_cameraToWorldTransform.matrix = reinterpret_cast<const RtMatrix4x4 *>( m_transformSamples.data() );
			m_cameraToWorldTransform.time = m_transformTimes.data();
		}

		RtUString m_name;

		RtParamList m_options;

		riley::ShadingNode m_projection;
		RtParamList m_projectionParameters;

		riley::Transform m_cameraToWorldTransform;
		vector<M44f> m_transformSamples;
		vector<float> m_transformTimes;

		RtParamList m_parameters;

		std::function<void ()> m_destructor;

};

IE_CORE_DECLAREPTR( RenderManCamera );

} // namespace

//////////////////////////////////////////////////////////////////////////
// RenderManGlobals
//////////////////////////////////////////////////////////////////////////

namespace
{

const string g_renderManPrefix( "renderman:" );
const IECore::InternedString g_cameraOption( "camera" );
const IECore::InternedString g_sampleMotionOption( "sampleMotion" );
const IECore::InternedString g_frameOption( "frame" );
const IECore::InternedString g_integratorOption( "renderman:integrator" );

class RenderManGlobals : public boost::noncopyable
{

	public :

		RenderManGlobals( const ConstSessionPtr &session )
			:	m_session( session ), m_options( ParamListAlgo::makeParamList() ),
				m_cameraId( riley::CameraId::k_InvalidId ),
				m_expectedWorldBeginThreadId( std::this_thread::get_id() ), m_worldBegun( false ), m_begun( false )
		{
			IECoreScene::ConstCameraPtr defaultCamera = new IECoreScene::Camera();
			m_defaultCamera = new RenderManCamera( defaultCamera.get() );
			m_integrator = new Shader( "PxrPathTracer", "renderman:integrator" );

			if( char *p = getenv( "RMAN_DISPLAYS_PATH" ) )
			{
				string searchPath = string( p ) + ":@";
				m_options.SetString( Rix::k_searchpath_display, RtUString( searchPath.c_str() ) );
			}

			if( char *p = getenv( "OSL_SHADER_PATHS" ) )
			{
				string searchPath = string( p ) + ":@";
				m_options.SetString( Rix::k_searchpath_shader, RtUString( searchPath.c_str() ) );
			}

			if( session->renderType == IECoreScenePreview::Renderer::Interactive )
			{
				m_options.SetInteger( Rix::k_hider_incremental, 1 );
				m_options.SetString( Rix::k_bucket_order, RtUString( "circle" ) );
			}
		}

		~RenderManGlobals()
		{
			pause();
			if( m_begun )
			{
				// No idea why, but we have to call this before destroying
				// the Riley object (in ~RenderManRenderer).
				m_session->riley->End();
			}
		}

		void option( const IECore::InternedString &name, const IECore::Object *value )
		{
			if( worldBegun() && name != g_cameraOption )
			{
				if( name != "frame" ) /// \todo Stop RenderController outputting frame unnecessarily
				{
					msg(
						IECore::Msg::Warning, "RenderManRender::option",
						boost::str(
							boost::format( "Unable to edit option \"%s\" (RenderMan limitation)" ) % name
						)
					);
				}
				return;
			}

			if( name == g_integratorOption )
			{
				if( worldBegun() )
				{
					msg( IECore::Msg::Warning, "RenderManRender::option", "Unable to edit integrator (RenderMan limitation)" );
				}
				else if( auto *network = reportedCast<const ShaderNetwork>( value, "option", name ) )
				{
					m_integrator = network->outputShader();
				}
			}
			else if( name == g_cameraOption )
			{
				if( auto *d = reportedCast<const StringData>( value, "option", name ) )
				{
					m_cameraOption = d->readable();
				}
			}
			else if( name == g_frameOption )
			{
				if( value )
				{
					if( auto *d = reportedCast<const IntData>( value, "option", name ) )
					{
						m_options.SetInteger( RtUString( "Ri:Frame" ), d->readable() );
					}
				}
				else
				{
					m_options.Remove( RtUString( "Ri:Frame" ) );
				}
			}
			else if( name == g_sampleMotionOption )
			{
				if( value )
				{
					if( auto *d = reportedCast<const BoolData>( value, "option", name ) )
					{
						m_options.SetInteger( RtUString( "hider:samplemotion" ), d->readable() );
					}
				}
				else
				{
					m_options.Remove( RtUString( "hider:samplemotion" ) );
				}
			}
			else if( boost::starts_with( name.c_str(), g_renderManPrefix.c_str() ) )
			{
				const RtUString renderManName( name.c_str() + g_renderManPrefix.size() );
				if( value )
				{
					if( auto data = runTimeCast<const Data>( value ) )
					{
						ParamListAlgo::convertParameter( renderManName, data, m_options );
					}
				}
				else
				{
					m_options.Remove( renderManName );
				}
			}
			else if( boost::starts_with( name.c_str(), "user:" ) )
			{
				const RtUString renderManName( name.c_str() );
				if( value )
				{
					if( auto data = runTimeCast<const Data>( value ) )
					{
						ParamListAlgo::convertParameter( renderManName, data, m_options );
					}
				}
				else
				{
					m_options.Remove( renderManName );
				}
			}
		}

		void output( const IECore::InternedString &name, const Output *output )
		{
			if( worldBegun() )
			{
				msg( IECore::Msg::Warning, "RenderManRender::output", "Unable to edit output (RenderMan limitation)" );
				return;
			}

			if( output )
			{
				m_outputs[name] = output->copy();
			}
			else
			{
				m_outputs.erase( name );
			}
		}

		RenderManCameraPtr camera( const std::string &name, const IECoreScene::Camera *camera )
		{
			std::function<void ()> destructor;
			if( m_session->renderType == IECoreScenePreview::Renderer::Interactive )
			{
				destructor = [this, name]{ m_cameras.erase( name ); };
			}

			RenderManCameraPtr result = new RenderManCamera( camera, destructor );
			m_cameras.insert( { name, result } );
			return result;
		}

		// Despite being designed as a modern edit-anything-at-any-time renderer API,
		// in places Riley is still implemented as a veneer over an old RI-like
		// state. Except now you have to guess how the API functions map to
		// state transitions in the backend.
		//
		// It turns out that `SetActiveCamera()` is basically `WorldBegin`,
		// and you must create _all_ cameras before calling it, and you must
		// not create geometry until _after_ calling it. We use `ensureWorld()`
		// to make this transition at the latest possible moment, just before we
		// are given our first geometry. After we've entered the world, we
		// refuse to make any further edits to cameras or outputs.
		//
		// There are further ordering requirements on top of the above. The
		// only workable sequence of operations I've found is this :
		//
		//   1. CreateCamera().
		//   2. CreateIntegrator().
		//   3. SetRenderTargetIds().
		//   4. SetActiveCamera().
		void ensureWorld()
		{
			tbb::spin_mutex::scoped_lock l( m_worldBeginMutex );
			if( m_worldBegun )
			{
				return;
			}

			if( std::this_thread::get_id() != m_expectedWorldBeginThreadId )
			{
				// We are required to make all calls up till `SetActiveCamera()`
				// from the same thread that the `Riley` instance was created on.
				// If we are being driven by a multi-threaded client, our gambit
				// of calling `ensureWorld()` from `Renderer::object()` cannot meet
				// this requirement. The best we can do is provide such clients a
				// nostalgic hoop to jump through.
				IECore::msg(
					Msg::Error,
					"RenderManRenderer",
					"You must call `Renderer::command( \"renderman:worldBegin\" )` before commencing "
					"multithreaded geometry output (RenderMan limitation)."
				);
			}

			updateCamera();
			m_session->riley->SetOptions( m_options );

			// Make integrator

			auto integratorParams = ParamListAlgo::makeParamList();
			ParamListAlgo::convertParameters( m_integrator->parameters(), *integratorParams );

			riley::ShadingNode integratorNode = {
				riley::ShadingNode::k_Integrator,
				RtUString( m_integrator->getName().c_str() ),
				RtUString( "integrator" ),
				integratorParams.get()
			};

			auto integrator = m_session->riley->CreateIntegrator( integratorNode );
			// No use for integrator IDs currently - seems to be there's just one in Riley for now
			(void)integrator;

			createRenderTargets( m_cameraId );

			// WorldBegin! Ho ho ho!
			m_session->riley->SetActiveCamera( m_cameraId );

			m_worldBegun = true;
		}

		void render()
		{
			ensureWorld();

			if( !m_begun )
			{
				// No idea what `Begin()` does, except that we're
				// required to call it before `Render()`.
				m_session->riley->Begin( nullptr );
				m_begun = true;
			}

			updateCamera();

			switch( m_session->renderType )
			{
				case IECoreScenePreview::Renderer::Batch :
					m_session->riley->Render();
					break;
				case IECoreScenePreview::Renderer::Interactive :
					m_interactiveRenderThread = std::thread(
						[this] {
							m_session->riley->Render();
						}
					);
					break;
				case IECoreScenePreview::Renderer::SceneDescription :
					// Protected against in RenderManRenderer constructor
					assert( 0 );
			}
		}

		void pause()
		{
			if( m_interactiveRenderThread.joinable() )
			{
				m_session->riley->Stop();
				m_interactiveRenderThread.join();
			}
		}

	private :

		bool worldBegun()
		{
			tbb::spin_mutex::scoped_lock l( m_worldBeginMutex );
			return m_worldBegun;
		}

		void updateCamera()
		{
			const RenderManCamera *camera = m_defaultCamera.get();
			CameraMap::const_accessor a;
			if( m_cameras.find( a, m_cameraOption ) )
			{
				camera = a->second.get();
			}

			m_options->Update( camera->options() );

			if( m_cameraId == riley::CameraId::k_InvalidId )
			{
				m_cameraId = m_session->riley->CreateCamera(
					RtUString( "ieCoreRenderMan:camera" ),
					camera->projection(),
					camera->cameraToWorldTransform(),
					camera->parameters()
				);
			}
			else
			{
				/// \todo Is there any benefit in sending edits
				/// only for the things that have changed?
				m_session->riley->ModifyCamera(
					m_cameraId,
					&camera->projection(),
					&camera->cameraToWorldTransform(),
					&camera->parameters()
				);
			}
		}

		const vector<riley::DisplayChannelId> &displayChannels( const IECoreScene::Output *output )
		{
			/// \todo Support filter and filter width
			auto inserted = m_displayChannels.insert( { output->getData(), {} } );
			if( !inserted.second )
			{
				return inserted.first->second;
			}

			vector<riley::DisplayChannelId> &result = inserted.first->second;
			RtParamList params;

			if( output->getData() == "rgba" )
			{
				params.SetString( Rix::k_name, RtUString( "Ci" ) );
				params.SetInteger( Rix::k_type, (int)RtDataType::k_color );
				result.push_back( m_session->riley->CreateDisplayChannel( params ) );

				params.SetString( Rix::k_name, RtUString( "a" ) );
				params.SetInteger( Rix::k_type, (int)RtDataType::k_float );
				result.push_back(
					m_session->riley->CreateDisplayChannel( params )
				);
			}
			else if( output->getData() == "rgb" )
			{
				params.SetString( Rix::k_name, RtUString( "Ci" ) );
				params.SetInteger( Rix::k_type, (int)RixDataType::k_color );
				result.push_back( m_session->riley->CreateDisplayChannel( params ) );
			}
			else
			{
				/// \todo Parse `color/vector/float/int name` into a display channel.
				IECore::msg(
					IECore::Msg::Warning, "IECoreRenderMan::Renderer",
					boost::str( boost::format( "Unsupported output data \"%s\"" ) % output->getData() )
				);
			}

			return result;
		}

		void createRenderTargets( riley::CameraId camera )
		{
			vector<riley::RenderTargetId> renderTargetIds;
			for( const auto &output : m_outputs )
			{
				auto params = ParamListAlgo::makeParamList();

				string type = output.second->getType();
				if( type == "exr" )
				{
					type = "openexr";
				}

				params->SetString( Rix::k_Ri_name, RtUString( output.second->getName().c_str() ) );
				params->SetString( Rix::k_Ri_type, RtUString( type.c_str() ) );

				ParamListAlgo::convertParameters( output.second->parameters(), *params );

				const DisplayChannelVector &channels = displayChannels( output.second.get() );
				renderTargetIds.push_back(
					m_session->riley->CreateRenderTarget(
						camera, channels.size(),
						// Cast to work around what I assume to be a const-correctness
						// mistake in the Riley API.
						const_cast<riley::DisplayChannelId *>( channels.data() ),
						*params
					)
				);
			}

			m_session->riley->SetRenderTargetIds( renderTargetIds.size(), renderTargetIds.data() );
		}

		ConstSessionPtr m_session;
		RtParamList m_options;

		std::unordered_map<InternedString, ConstOutputPtr> m_outputs;

		using DisplayChannelVector = vector<riley::DisplayChannelId>;
		using DisplayChannelsMap = unordered_map<string, DisplayChannelVector>;
		DisplayChannelsMap m_displayChannels;

		IECoreScene::ConstShaderPtr m_integrator;

		std::string m_cameraOption;
		using CameraMap = tbb::concurrent_hash_map<std::string, ConstRenderManCameraPtr>;
		CameraMap m_cameras;
		RenderManCameraPtr m_defaultCamera;
		riley::CameraId m_cameraId;

		tbb::spin_mutex m_worldBeginMutex;
		std::thread::id m_expectedWorldBeginThreadId;
		bool m_worldBegun;

		bool m_begun;
		std::thread m_interactiveRenderThread;

};

} // namespace

//////////////////////////////////////////////////////////////////////////
// Shaders
//////////////////////////////////////////////////////////////////////////

namespace
{

using ParameterTypeMap = std::unordered_map<InternedString, RixDataType>;
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
				typeMap[name] = RixDataType::k_float;
			}
			else if( type == "int" )
			{
				typeMap[name] = RixDataType::k_integer;
			}
			else if( type == "point" )
			{
				typeMap[name] = RixDataType::k_point;
			}
			else if( type == "vector" )
			{
				typeMap[name] = RixDataType::k_vector;
			}
			else if( type == "normal" )
			{
				typeMap[name] = RixDataType::k_normal;
			}
			else if( type == "color" )
			{
				typeMap[name] = RixDataType::k_color;
			}
			else if( type == "string" )
			{
				typeMap[name] = RixDataType::k_string;
			}
			else if( type == "struct" )
			{
				typeMap[name] = RixDataType::k_struct;
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

const RixDataType *parameterType( const Shader *shader, IECore::InternedString parameterName )
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
	const RixDataType *type = parameterType( shader, connection.destination.name );
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
		*type, 1,
		RixDetailType::k_reference,
		false,
		false
	};

	paramList.SetParam( info, &referenceU, 0 );
}

void convertShaderNetworkWalk( const ShaderNetwork::Parameter &outputParameter, const IECoreScene::ShaderNetwork *shaderNetwork, vector<riley::ShadingNode> &shadingNodes, HandleSet &visited )
{
	if( !visited.insert( outputParameter.shader ).second )
	{
		return;
	}

	const IECoreScene::Shader *shader = shaderNetwork->getShader( outputParameter.shader );
	riley::ShadingNode node = {
		riley::ShadingNode::k_Pattern,
		RtUString( shader->getName().c_str() ),
		RtUString( outputParameter.shader.c_str() )
	};

	if( shader->getType() == "light" || shader->getType() == "renderman:light" )
	{
		node.type = riley::ShadingNode::k_Light;
	}
	else if( shader->getType() == "surface" || shader->getType() == "renderman:bxdf" )
	{
		node.type = riley::ShadingNode::k_Bxdf;
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

	return riley->CreateMaterial( shadingNodes.data(), shadingNodes.size() );
}

riley::LightShaderId convertLightShaderNetwork( const ShaderNetwork *network, riley::Riley *riley )
{
	vector<riley::ShadingNode> shadingNodes;
	shadingNodes.reserve( network->size() );

	HandleSet visited;
	convertShaderNetworkWalk( network->getOutput(), network, shadingNodes, visited );

	return riley->CreateLightShader(
		shadingNodes.data(), shadingNodes.size(),
		/* filterNodes = */ nullptr, /* numFilterNodes = */ 0
	);
}

riley::MaterialId defaultMaterial( riley::Riley *riley )
{
	vector<riley::ShadingNode> shaders;

	shaders.push_back( { riley::ShadingNode::k_Pattern, RtUString( "PxrFacingRatio" ), RtUString( "facingRatio" ), nullptr } );

	auto toFloat3ParamList = ParamListAlgo::makeParamList();
	toFloat3ParamList->ReferenceFloat( RtUString( "input" ), RtUString( "facingRatio:resultF" ) );
	shaders.push_back( { riley::ShadingNode::k_Pattern, RtUString( "PxrToFloat3" ), RtUString( "toFloat3" ), toFloat3ParamList.get() } );

	auto constantParamList = ParamListAlgo::makeParamList();
	constantParamList->ReferenceColor( RtUString( "emitColor" ), RtUString( "toFloat3:resultRGB" ) );
	shaders.push_back( { riley::ShadingNode::k_Bxdf, RtUString( "PxrConstant" ), RtUString( "constant" ), constantParamList.get() } );

	return riley->CreateMaterial( shaders.data(), shaders.size() );
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

InternedString g_surfaceShaderAttributeName( "renderman:bxdf" );
InternedString g_lightShaderAttributeName( "renderman:light" );

class RenderManAttributes : public IECoreScenePreview::Renderer::AttributesInterface
{

	public :

		// We deliberately do not pass a `Riley *` here. Trying to make parallel
		// calls of any sort before `RenderManGlobals::ensureWorld()` will trigger
		// RenderMan crashes, and RenderManAttributes instances are constructed
		// for use with RenderManCameras. Instead we pass a ShaderCache which
		// allows us to generate materials lazily on demand, when RenderManObjects
		// ask for them.
		RenderManAttributes( const IECore::CompoundObject *attributes, ShaderCachePtr shaderCache )
			:	m_paramList( ParamListAlgo::makeParamList() ), m_shaderCache( shaderCache )
		{
			m_surfaceShader = parameter<ShaderNetwork>( attributes->members(), g_surfaceShaderAttributeName );
			m_lightShader = parameter<ShaderNetwork>( attributes->members(), g_lightShaderAttributeName );

			for( const auto &attribute : attributes->members() )
			{
				if( boost::starts_with( attribute.first.c_str(), g_renderManPrefix.c_str() ) )
				{
					if( auto data = runTimeCast<const Data>( attribute.second.get() ) )
					{
						ParamListAlgo::convertParameter( RtUString( attribute.first.c_str() + g_renderManPrefix.size() ), data, *m_paramList );
					}
				}
				else if( boost::starts_with( attribute.first.c_str(), "user:" ) )
				{
					if( auto data = runTimeCast<const Data>( attribute.second.get() ) )
					{
						ParamListAlgo::convertParameter( RtUString( attribute.first.c_str() ), data, *m_paramList );
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

		RtParamListPtr m_paramList;
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

		RenderManObject( riley::GeometryMasterId geometryMaster, const RenderManAttributes *attributes, const ConstSessionPtr &session )
			:	m_session( session ), m_geometryInstance( riley::GeometryInstanceId::k_InvalidId )
		{
			if( geometryMaster != riley::GeometryMasterId::k_InvalidId )
			{
				m_material = attributes->material();
				m_geometryInstance = m_session->riley->CreateGeometryInstance(
					/* group = */ riley::GeometryMasterId::k_InvalidId,
					geometryMaster,
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
				if( m_geometryInstance != riley::GeometryInstanceId::k_InvalidId )
				{
					m_session->riley->DeleteGeometryInstance( riley::GeometryMasterId::k_InvalidId, m_geometryInstance );
				}
			}
		}

		void transform( const Imath::M44f &transform ) override
		{
			StaticTransform staticTransform( transform );
			const riley::GeometryInstanceResult result = m_session->riley->ModifyGeometryInstance(
				/* group = */ riley::GeometryMasterId::k_InvalidId,
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
				/* group = */ riley::GeometryMasterId::k_InvalidId,
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
				/* group = */ riley::GeometryMasterId::k_InvalidId,
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

		RenderManLight( riley::GeometryMasterId geometryMaster, const ConstRenderManAttributesPtr &attributes, const ConstSessionPtr &session )
			:	m_session( session ), m_lightShader( riley::LightShaderId::k_InvalidId ), m_lightInstance( riley::LightInstanceId::k_InvalidId )
		{
			assignAttributes( attributes );

			m_lightInstance = m_session->riley->CreateLightInstance(
				/* group = */ riley::GeometryMasterId::k_InvalidId,
				geometryMaster,
				riley::MaterialId::k_InvalidId, /// \todo Use `attributes->material()`?
				m_lightShader,
				g_emptyCoordinateSystems,
				StaticTransform(),
				m_attributes->paramList()
			);
		}

		~RenderManLight()
		{
			if( m_session->renderType == IECoreScenePreview::Renderer::Interactive )
			{
				m_session->riley->DeleteLightInstance( riley::GeometryMasterId::k_InvalidId, m_lightInstance );
				if( m_lightShader != riley::LightShaderId::k_InvalidId )
				{
					m_session->riley->DeleteLightShader( m_lightShader );
				}
			}
		}

		void transform( const Imath::M44f &transform ) override
		{
			const M44f flippedTransform = M44f().scale( V3f( 1, 1, -1 ) ) * transform;
			StaticTransform staticTransform( flippedTransform );

			const riley::LightInstanceResult result = m_session->riley->ModifyLightInstance(
				/* group = */ riley::GeometryMasterId::k_InvalidId,
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
			vector<Imath::M44f> flippedSamples = samples;
			for( auto &m : flippedSamples )
			{
				m = M44f().scale( V3f( 1, 1, -1 ) ) * m;
			}
			AnimatedTransform animatedTransform( flippedSamples, times );

			const riley::LightInstanceResult result = m_session->riley->ModifyLightInstance(
				/* group = */ riley::GeometryMasterId::k_InvalidId,
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
			assignAttributes( static_cast<const RenderManAttributes *>( attributes ) );

			const riley::LightInstanceResult result = m_session->riley->ModifyLightInstance(
				/* group = */ riley::GeometryMasterId::k_InvalidId,
				m_lightInstance,
				/* material = */ nullptr,
				/* light shader = */ &m_lightShader,
				/* coordsys = */ nullptr,
				/* xform = */ nullptr,
				&m_attributes->paramList()
			);

			if( result != riley::LightInstanceResult::k_Success )
			{
				IECore::msg( IECore::Msg::Warning, "RenderManLight::attributes", "Unexpected edit failure" );
			}
			return true;
		}

		void link( const IECore::InternedString &type, const IECoreScenePreview::Renderer::ConstObjectSetPtr &objects ) override
		{
		}

	private :

		// Assigns `m_attributes` and updates other members associated with it.
		// Note : This does _not_ modify m_lightInstance.
		void assignAttributes( const ConstRenderManAttributesPtr &attributes )
		{
			if( m_lightShader != riley::LightShaderId::k_InvalidId )
			{
				m_session->riley->DeleteLightShader( m_lightShader );
			}

			m_attributes = attributes;
			if( m_attributes->lightShader() )
			{
				m_lightShader = convertLightShaderNetwork( m_attributes->lightShader(), m_session->riley );
			}
		}

		const ConstSessionPtr m_session;
		riley::LightShaderId m_lightShader;
		riley::LightInstanceId m_lightInstance;
		/// Used to keep material etc alive as long as we need it.
		/// \todo Not sure if this is necessary or not? Perhaps Riley will
		/// extend lifetime anyway? It's not clear if `DeleteMaterial`
		/// actually destroys the material, or just drops a reference
		/// to it. Also, we're not using material at present anyway.
		ConstRenderManAttributesPtr m_attributes;

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

		RenderManRenderer( RenderType renderType, const std::string &fileName )
		{
			if( renderType == SceneDescription )
			{
				throw IECore::Exception( "SceneDescription mode not supported by RenderMan" );
			}

			const char *argv[] = { "ieCoreRenderMan" };
			PRManBegin( 1, (char **)argv );

			m_session = new Session( renderType );
			m_globals = boost::make_unique<RenderManGlobals>( m_session );
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
			RenderManCameraPtr result = m_globals->camera( name, camera );
			result->attributes( attributes );
			return result;
		}

		ObjectInterfacePtr light( const std::string &name, const IECore::Object *object, const AttributesInterface *attributes ) override
		{
			m_globals->ensureWorld();
			riley::GeometryMasterId geometryMaster = riley::GeometryMasterId::k_InvalidId;
			if( object )
			{
				/// \todo Cache geometry masters
				geometryMaster = GeometryAlgo::convert( object, m_session->riley );
			}
			return new RenderManLight( geometryMaster, static_cast<const RenderManAttributes *>( attributes ), m_session );
		}

		ObjectInterfacePtr lightFilter( const std::string &name, const IECore::Object *object, const AttributesInterface *attributes ) override
		{
			return nullptr;
		}

		Renderer::ObjectInterfacePtr object( const std::string &name, const IECore::Object *object, const AttributesInterface *attributes ) override
		{
			m_globals->ensureWorld();
			/// \todo Cache geometry masters
			riley::GeometryMasterId geometryMaster = GeometryAlgo::convert( object, m_session->riley );
			return new RenderManObject( geometryMaster, static_cast<const RenderManAttributes *>( attributes ), m_session );
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

		std::unique_ptr<RenderManGlobals> m_globals;
		ShaderCachePtr m_shaderCache;

		static Renderer::TypeDescription<RenderManRenderer> g_typeDescription;

};

IECoreScenePreview::Renderer::TypeDescription<RenderManRenderer> RenderManRenderer::g_typeDescription( "RenderMan" );

} // namespace
