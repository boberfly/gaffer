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

#include "Attributes.h"
#include "Camera.h"
#include "GeometryAlgo.h"
#include "Globals.h"
#include "Material.h"
#include "ParamListAlgo.h"
#include "Session.h"
#include "Transform.h"

#include "GafferScene/Private/IECoreScenePreview/Renderer.h"

#include "Riley.h"
#include "RixPredefinedStrings.hpp"

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

static riley::CoordinateSystemList g_emptyCoordinateSystems = { 0, nullptr };

} // namespace

//////////////////////////////////////////////////////////////////////////
// RenderManObject
//////////////////////////////////////////////////////////////////////////

namespace
{

class RenderManObject : public IECoreScenePreview::Renderer::ObjectInterface
{

	public :

		RenderManObject( riley::GeometryPrototypeId geometryPrototype, const Attributes *attributes, const ConstSessionPtr &session )
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
			const auto renderManAttributes = static_cast<const Attributes *>( attributes );
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
		ConstMaterialPtr m_material;

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

		RenderManLight( riley::GeometryPrototypeId geometryPrototype, const Attributes *attributes, const ConstSessionPtr &session )
			:	m_session( session ), m_lightShader( riley::LightShaderId::InvalidId() ), m_lightInstance( riley::LightInstanceId::InvalidId() )
		{
			updateLightShader( attributes );
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
			auto renderManAttributes = static_cast<const Attributes *>( attributes );
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

		void updateLightShader( const Attributes *attributes )
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

		RenderManRenderer( RenderType renderType, const std::string &fileName, const MessageHandlerPtr &messageHandler )
		{
			if( renderType == SceneDescription )
			{
				throw IECore::Exception( "SceneDescription mode not supported by RenderMan" );
			}

			m_session = new Session( renderType, messageHandler );
			m_globals = std::make_unique<Globals>( m_session );
			m_materialCache = new MaterialCache( m_session );
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
			m_globals->ensureWorld();
			return new Attributes( attributes, m_materialCache.get() );
		}

		ObjectInterfacePtr camera( const std::string &name, const IECoreScene::Camera *camera, const AttributesInterface *attributes ) override
		{
			m_globals->ensureWorld();
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
			return new RenderManLight( geometryPrototype, static_cast<const Attributes *>( attributes ), m_session );
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
			return new RenderManObject( geometryPrototype, static_cast<const Attributes *>( attributes ), m_session );
		}

		ObjectInterfacePtr object( const std::string &name, const std::vector<const IECore::Object *> &samples, const std::vector<float> &times, const AttributesInterface *attributes ) override
		{
			/// \todo Convert all time samples
			return object( name, samples.front(), attributes );
		}

		void render() override
		{
			m_materialCache->clearUnused();
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
		MaterialCachePtr m_materialCache;

		static Renderer::TypeDescription<RenderManRenderer> g_typeDescription;

};

IECoreScenePreview::Renderer::TypeDescription<RenderManRenderer> RenderManRenderer::g_typeDescription( "RenderMan" );

} // namespace
