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

#include "Light.h"

#include "ShaderNetworkAlgo.h"
#include "Transform.h"

using namespace std;
using namespace Imath;
using namespace IECoreRenderMan;

namespace
{

M44f correctiveTransform( const Attributes *attributes )
{
	if( !attributes->lightShader() )
	{
		return M44f();
	}

	const IECoreScene::Shader *lightShader = attributes->lightShader()->outputShader();
	if( !lightShader )
	{
		return M44f();
	}

	if( lightShader->getName() == "PxrDomeLight" || lightShader->getName() == "PxrEnvDayLight" )
	{
		return M44f().rotate( V3f( -M_PI_2, M_PI_2, 0.0f ) );
	}
	else
	{
		return M44f().scale( V3f( 1, 1, -1 ) );
	}
}

} // namespace

Light::Light( riley::GeometryPrototypeId geometryPrototype, const Attributes *attributes, Session *session )
	:	m_session( session ), m_lightShader( riley::LightShaderId::InvalidId() ),
		m_lightInstance( riley::LightInstanceId::InvalidId() ), m_correctiveTransform( correctiveTransform( attributes ) )
{
	updateLightShader( attributes );
	if( m_lightShader == riley::LightShaderId::InvalidId() )
	{
		// Riley crashes if we try to edit the transform on a light
		// without a shader, so we just don't make such lights.
		return;
	}

	m_lightInstance = m_session->createLightInstance( m_lightShader, StaticTransform(), attributes->instanceAttributes() );
}

Light::~Light()
{
	if( m_session->renderType == IECoreScenePreview::Renderer::Interactive )
	{
		if( m_lightInstance != riley::LightInstanceId::InvalidId() )
		{
			m_session->deleteLightInstance( m_lightInstance );
		}
		if( m_lightShader != riley::LightShaderId::InvalidId() )
		{
			m_session->riley->DeleteLightShader( m_lightShader );
		}
	}
}

void Light::transform( const Imath::M44f &transform )
{
	if( m_lightInstance == riley::LightInstanceId::InvalidId() )
	{
		return;
	}

	const M44f correctedTransform = m_correctiveTransform * transform;
	StaticTransform staticTransform( correctedTransform );

	const riley::LightInstanceResult result = m_session->modifyLightInstance(
		m_lightInstance,
		/* light shader = */ nullptr,
		&staticTransform,
		/* attributes = */ nullptr
	);

	if( result != riley::LightInstanceResult::k_Success )
	{
		IECore::msg( IECore::Msg::Warning, "RenderManLight::transform", "Unexpected edit failure" );
	}
}

void Light::transform( const std::vector<Imath::M44f> &samples, const std::vector<float> &times )
{
	if( m_lightInstance == riley::LightInstanceId::InvalidId() )
	{
		return;
	}

	vector<Imath::M44f> correctedSamples = samples;
	for( auto &m : correctedSamples )
	{
		m = m_correctiveTransform * m;
	}
	AnimatedTransform animatedTransform( correctedSamples, times );

	const riley::LightInstanceResult result = m_session->modifyLightInstance(
		m_lightInstance,
		/* light shader = */ nullptr,
		&animatedTransform,
		/* attributes = */ nullptr
	);

	if( result != riley::LightInstanceResult::k_Success )
	{
		IECore::msg( IECore::Msg::Warning, "RenderManLight::transform", "Unexpected edit failure" );
	}
}

bool Light::attributes( const IECoreScenePreview::Renderer::AttributesInterface *attributes )
{
	auto renderManAttributes = static_cast<const Attributes *>( attributes );
	if( correctiveTransform( renderManAttributes ) != m_correctiveTransform )
	{
		// This can only happen when the light type changes, which is pretty unlikely.
		// We don't know the light's transform, so just request that it be recreated.
		return false;
	}

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
		m_session->deleteLightInstance( m_lightInstance );
		m_lightInstance = riley::LightInstanceId::InvalidId();
		return true;
	}

	const riley::LightInstanceResult result = m_session->modifyLightInstance(
		m_lightInstance,
		/* light shader = */ &m_lightShader,
		/* xform = */ nullptr,
		&renderManAttributes->instanceAttributes()
	);

	if( result != riley::LightInstanceResult::k_Success )
	{
		IECore::msg( IECore::Msg::Warning, "RenderManLight::attributes", "Unexpected edit failure" );
		return false;
	}
	return true;
}

void Light::link( const IECore::InternedString &type, const IECoreScenePreview::Renderer::ConstObjectSetPtr &objects )
{
}

void Light::assignID( uint32_t id )
{
}

void Light::updateLightShader( const Attributes *attributes )
{
	if( m_lightShader != riley::LightShaderId::InvalidId() )
	{
		m_session->deleteLightShader( m_lightShader );
		m_lightShader = riley::LightShaderId::InvalidId();
	}

	/// \todo Could manage light shaders in MaterialCache.
	if( attributes->lightShader() )
	{
		std::vector<riley::ShadingNode> nodes = ShaderNetworkAlgo::convert( attributes->lightShader() );
		m_lightShader = m_session->createLightShader( { (uint32_t)nodes.size(), nodes.data() } );
	}
}
