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

#include "Session.h"

#include "XcptErrorCodes.h"

#include "fmt/format.h"

using namespace std;
using namespace IECoreRenderMan;

namespace
{

const RtUString g_pxrDomeLightUStr( "PxrDomeLight" );
const RtUString g_pxrPortalLightUStr( "PxrPortalLight" );
const riley::CoordinateSystemList g_emptyCoordinateSystems = { 0, nullptr };

} // namespace

struct Session::ExceptionHandler : public RixXcpt::XcptHandler
{

	ExceptionHandler( const IECore::MessageHandlerPtr &messageHandler )
		:	m_messageHandler( messageHandler )
	{
	}

	void HandleXcpt( int code, int severity, const char *message ) override
	{
		IECore::Msg::Level level;
		switch( severity )
		{
			case RIE_INFO :
				level = IECore::Msg::Level::Info;
				break;
			case RIE_WARNING :
				level = IECore::Msg::Level::Warning;
				break;
			default :
				level = IECore::Msg::Level::Error;
				break;
		}

		m_messageHandler->handle( level, "RenderMan", message );
	}

	void HandleExitRequest( int code ) override
	{
		/// \todo Not sure how best to handle this. We don't want
		/// to exit the application, but perhaps we want to prevent
		/// any further attempt to interact with the renderer?
	}

	private :

		IECore::MessageHandlerPtr m_messageHandler;

};


Session::Session( IECoreScenePreview::Renderer::RenderType renderType, const RtParamList &options, const IECore::MessageHandlerPtr &messageHandler )
	:	riley( nullptr ), renderType( renderType )
{
	// `argv[0]==""` prevents RenderMan doing its own signal handling.
	vector<const char *> args = { "" };
	PRManSystemBegin( args.size(), args.data() );
	/// \todo There can only be one PRMan/Riley instance at a time, so we need to
	/// prevent the creation of a second renderer.
	PRManRenderBegin( args.size(), args.data() );

	if( messageHandler )
	{
		m_exceptionHandler = std::make_unique<ExceptionHandler>( messageHandler );
		auto rixXcpt = (RixXcpt *)RixGetContext()->GetRixInterface( k_RixXcpt );
		rixXcpt->Register( m_exceptionHandler.get() );
	}

	auto rileyManager = (RixRileyManager *)RixGetContext()->GetRixInterface( k_RixRileyManager );
	/// \todo What is the `rileyVariant` argument for? XPU?
	riley = rileyManager->CreateRiley( RtUString(), RtParamList() );

	riley->SetOptions( options );
}

Session::~Session()
{
	auto rileyManager = (RixRileyManager *)RixGetContext()->GetRixInterface( k_RixRileyManager );
	rileyManager->DestroyRiley( riley );

	if( m_exceptionHandler )
	{
		auto rixXcpt = (RixXcpt *)RixGetContext()->GetRixInterface( k_RixXcpt );
		rixXcpt->Unregister( m_exceptionHandler.get() );
	}

	PRManRenderEnd();
	PRManSystemEnd();
}

void Session::addCamera( const std::string &name, const CameraInfo &camera )
{
	CameraMap::accessor a;
	m_cameras.insert( a, name );
	a->second = camera;
}

Session::CameraInfo Session::getCamera( const std::string &name ) const
{
	CameraMap::const_accessor a;
	if( m_cameras.find( a, name ) )
	{
		return a->second;
	}

	return { riley::CameraId::InvalidId(), RtParamList() };
}

void Session::removeCamera( const std::string &name )
{
	m_cameras.erase( name );
}

riley::LightShaderId Session::createLightShader( const riley::ShadingNetwork &light )
{
	riley::LightShaderId result = riley->CreateLightShader( riley::UserId(), light, { 0, nullptr } );
	RtUString type = light.nodeCount ? light.nodes[light.nodeCount-1].name : RtUString();
	if( type == g_pxrDomeLightUStr || type == g_pxrPortalLightUStr )
	{
		LightShaderMap::accessor a;
		[[maybe_unused]] bool inserted = m_domeAndPortalShaders.insert( a, result.AsUInt32() );
		assert( inserted ); // ID should be unique.
		a->second.shaders.insert( a->second.shaders.end(), light.nodes, light.nodes + light.nodeCount );
	}

	return result;
}

void Session::deleteLightShader( riley::LightShaderId lightShaderId )
{
	riley->DeleteLightShader( lightShaderId );
	m_domeAndPortalShaders.erase( lightShaderId.AsUInt32() );
}

riley::LightInstanceId Session::createLightInstance( riley::LightShaderId lightShaderId, const riley::Transform &transform, const RtParamList &attributes )
{
	riley::LightInstanceId result = riley->CreateLightInstance(
		riley::UserId(), riley::GeometryPrototypeId(), riley::GeometryPrototypeId(),
		riley::MaterialId(), lightShaderId,
		g_emptyCoordinateSystems, transform, attributes
	);

	if( m_domeAndPortalShaders.count( lightShaderId.AsUInt32() ) )
	{
		LightInstanceMap::accessor a;
		[[maybe_unused]] bool inserted = m_domeAndPortalLights.insert( a, result.AsUInt32() );
		assert( inserted ); // ID should be unique.
		a->second.lightShader = lightShaderId;
		a->second.transform.Identity();
		a->second.attributes = attributes;
	}

	return result;
}

riley::LightInstanceResult Session::modifyLightInstance(
	riley::LightInstanceId lightInstanceId, const riley::LightShaderId *lightShaderId, const riley::Transform *transform,
	const RtParamList *attributes
)
{
	riley::LightInstanceResult result = riley->ModifyLightInstance(
		riley::GeometryPrototypeId(), lightInstanceId,
		nullptr, lightShaderId, nullptr, transform, attributes
	);

	/// \todo Consider the possibility of a non-portal/dome turning
	/// into a portal/dome. We'll have incomplete information, so
	/// perhaps should fail the edit, and cause the controller to
	/// re-send.

	LightInstanceMap::accessor a;
	if( m_domeAndPortalLights.find( a, lightInstanceId.AsUInt32() ) )
	{
		if( lightShaderId )
		{
			a->second.lightShader = *lightShaderId;
		}
		if( transform )
		{
			a->second.transform = *(transform->matrix);
		}
		if( attributes )
		{
			a->second.attributes = *attributes;
		}
	}

	return result;
}

void Session::deleteLightInstance( riley::LightInstanceId lightInstanceId )
{
	riley->DeleteLightInstance( riley::GeometryPrototypeId(), lightInstanceId );
	m_domeAndPortalLights.erase( lightInstanceId.AsUInt32() );
}

void Session::linkPortals()
{
	auto isPortal = [&] ( uint32_t lightShader ) {
		LightShaderMap::const_accessor a;
		if( m_domeAndPortalShaders.find( a, lightShader ) )
		{
			return a->second.shaders.back().name == g_pxrPortalLightUStr;
		}
	};

	// Find the dome light.

	const LightInfo *domeLight = nullptr;
	bool havePortals = false;
	for( const auto &[id, info] : m_domeAndPortalLights )
	{
		if( isPortal( id ) )
		{
			havePortals = true;
		}
		else
		{
			if( !domeLight )
			{
				domeLight = &info;
			}
			else
			{
				// To support multiple domes, we need to add a mechanism for
				// linking them to portals. Perhaps this can be achieved via
				// `ObjectInterface::link()`?
				IECore::msg( IECore::Msg::Warning, "IECoreRenderMan::Renderer", "Multiple PxrDomeLights are not yet supported" );
			}
		}
	}

	// Link the lights appropriately.

	for( const auto &[id, info] : m_domeAndPortalLights )
	{
		if( isPortal( id ) )
		{

		}
		else
		{

		}
	}
}
