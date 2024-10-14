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

#include "Camera.h"

#include "RixPredefinedStrings.hpp"

using namespace std;
using namespace Imath;
using namespace IECoreRenderMan::Renderer;

namespace
{

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

/// TODO : MOVE AND SHARE
struct AnimatedTransform : riley::Transform
{

	AnimatedTransform( const vector<Imath::M44f> &transformSamples, const vector<float> &sampleTimes )
	{
		samples = transformSamples.size();
		matrix = reinterpret_cast<const RtMatrix4x4 *>( transformSamples.data() );
		time = sampleTimes.data();
	}

};

} // namespace

Camera::Camera( const std::string &name, const IECoreScene::Camera *camera, const SessionPtr &session )
	:	m_session( session ), m_name( name )
{
	// Parameters

	RtParamList paramList;
	paramList.SetFloat( Rix::k_nearClip, camera->getClippingPlanes()[0] );
	paramList.SetFloat( Rix::k_farClip, camera->getClippingPlanes()[1] );

	RtParamList projectionParamList;
	projectionParamList.SetFloat( Rix::k_fov, 35.0f ); /// TODO : GET FROM CAMERA

	m_cameraId = m_session->riley->CreateCamera(
		riley::UserId(),
		RtUString( name.c_str() ),
		{
			riley::ShadingNode::Type::k_Projection, RtUString( "PxrCamera" ),
			RtUString( "projection" ), projectionParamList
		},
		StaticTransform(),
		paramList
	);

	m_session->addCamera( name, { m_cameraId, camera } );
}

Camera::~Camera()
{
	if( m_session->renderType == IECoreScenePreview::Renderer::Interactive )
	{
		if( m_cameraId != riley::CameraId::InvalidId() )
		{
			m_session->riley->DeleteCamera( m_cameraId );
		}
		m_session->removeCamera( m_name );
	}
}

void Camera::transform( const Imath::M44f &transform )
{
	transformInternal( { transform }, { 0.0f } );
}

void Camera::transform( const std::vector<Imath::M44f> &samples, const std::vector<float> &times )
{
	transformInternal( samples, times );
}

bool Camera::attributes( const IECoreScenePreview::Renderer::AttributesInterface *attributes )
{
	return true;
}

void Camera::link( const IECore::InternedString &type, const IECoreScenePreview::Renderer::ConstObjectSetPtr &objects )
{
}

void Camera::assignID( uint32_t id )
{
}

void Camera::transformInternal( std::vector<Imath::M44f> samples, const std::vector<float> &times )
{
	for( auto &m : samples )
	{
		m = M44f().scale( V3f( 1, 1, -1 ) ) * m;
	}

	AnimatedTransform transform( samples, times );

	const auto result = m_session->riley->ModifyCamera(
		m_cameraId,
		nullptr,
		&transform,
		nullptr
	);

	if( result != riley::CameraResult::k_Success )
	{
		IECore::msg( IECore::Msg::Warning, "IECoreRenderMan::Camera::transform", "Unexpected edit failure" );
	}
}

void Camera::options( const IECoreScene::Camera *camera, RtParamList &options )
{
	const Imath::V2i resolution = camera->renderResolution();
	options.SetIntegerArray( Rix::k_Ri_FormatResolution, resolution.getValue(), 2 );
	options.SetFloat( Rix::k_Ri_FormatPixelAspectRatio, camera->getPixelAspectRatio() );
	/// \todo Crop window
}
