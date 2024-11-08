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

#include "RixPredefinedStrings.hpp"
#include "XcptErrorCodes.h"

#include "fmt/format.h"

using namespace std;
using namespace IECoreRenderMan;

namespace
{

const RtUString g_domeColorMapUStr( "domeColorMap" );
const RtUString g_intensityUStr( "intensity" );
const RtUString g_intensityMultUStr( "intensityMult" );
const RtUString g_lightingMuteUStr( "lighting:mute" );
const RtUString g_lightColorUStr( "lightColor" );
const RtUString g_lightColorMapUStr( "lightColorMap" );
const RtUString g_portalToDomeUStr( "portalToDome" );
const RtUString g_pxrDomeLightUStr( "PxrDomeLight" );
const RtUString g_pxrPortalLightUStr( "PxrPortalLight" );
const RtUString g_tintUStr( "tint" );

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
		LightShaderInfo &lightShaderInfo = m_domeAndPortalShaders[result.AsUInt32()];
		assert( lightShaderInfo.shaders.empty() ); // ID should be unique.
		lightShaderInfo.shaders.insert( lightShaderInfo.shaders.end(), light.nodes, light.nodes + light.nodeCount );
	}

	return result;
}

void Session::deleteLightShader( riley::LightShaderId lightShaderId )
{
	riley->DeleteLightShader( lightShaderId );
	/// TODO : ERASE IN `linkPortals()`.
	m_domeAndPortalShaders.at( lightShaderId.AsUInt32() ).shaders.clear();
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
	/// TODO : ONLY DO THINGS WHEN ACTUALLY DIRTY

	auto isPortal = [&] ( riley::LightShaderId lightShader ) {
		auto it = m_domeAndPortalShaders.find( lightShader.AsUInt32() );
		if( it != m_domeAndPortalShaders.end() )
		{
			return it->second.shaders.back().name == g_pxrPortalLightUStr;
		}
		return false;
	};

	// Find the dome light.

	const LightInfo *domeLight = nullptr;
	bool havePortals = false;
	size_t numDomes = 0;
	for( const auto &[id, info] : m_domeAndPortalLights )
	{
		if( isPortal( info.lightShader ) )
		{
			havePortals = true;
		}
		else
		{
			numDomes++;
			if( !domeLight )
			{
				domeLight = &info;
			}
		}
	}

	if( havePortals && numDomes > 1 )
	{
		/// \todo To support multiple domes, we need to add a mechanism for
		/// linking them to portals. Perhaps this can be achieved via
		/// `ObjectInterface::link()`?
		IECore::msg( IECore::Msg::Warning, "IECoreRenderMan::Renderer", "PxrPortalLights combined with multiple PxrDomeLights are not yet supported" );
	}

	// Link the lights appropriately.

	RtParamList mutedAttributes;
	mutedAttributes.SetInteger( Rix::k_lighting_mute, 1 );

	for( const auto &[id, info] : m_domeAndPortalLights )
	{
		if( isPortal( info.lightShader ) )
		{
			// Connect portals to dome if we have one,
			// otherwise mute them.
			if( domeLight )
			{
				// Copy parameters from dome to portal, since we want users
				// to control them all in one place, not on each individual portal.
				// Portal lights have all the same parameters as dome lights, so this
				// is easy.
				const RtParamList &domeParams = m_domeAndPortalShaders.at( domeLight->lightShader.AsUInt32() ).shaders.back().params;
				LightShaderInfo &portalShader = m_domeAndPortalShaders.at( info.lightShader.AsUInt32() );
				RtParamList &portalParams = portalShader.shaders.back().params;
				portalParams.Update( domeParams );
				//  Except that `lightColorMap` is unhelpfully renamed to
				// `domeColorMap`, so sort that out.
				portalParams.Remove( g_lightColorMapUStr );
				RtUString colorMap; domeParams.GetString( g_lightColorMapUStr, colorMap );
				portalParams.SetString( g_domeColorMapUStr, colorMap );
				// And of course the portal shader couldn't possibly apply tint
				// etc itself. That is obviously the responsibility of every
				// single bridge project.
				float intensity = 1;
				portalParams.GetFloat( g_intensityUStr, intensity );
				float intensityMult = 1;
				portalParams.GetFloat( g_intensityMultUStr, intensityMult );
				pxrcore::ColorRGB lightColor( 1, 1, 1 );
				portalParams.GetColor( g_lightColorUStr, lightColor );
				pxrcore::ColorRGB tint( 1, 1, 1 );
				portalParams.GetColor( g_tintUStr, tint );
				intensity = intensity * intensityMult;
				portalParams.SetFloat( g_intensityUStr, intensity );
				lightColor = lightColor * tint;
				portalParams.SetColor( g_lightColorUStr, lightColor );

				// We are also responsible for adding a parameter providing the
				// transform between the portal and the dome.
				RtMatrix4x4 domeInverse; domeInverse.Identity();
				domeLight->transform.Inverse( &domeInverse );
				const RtMatrix4x4 portalToDome = info.transform * domeInverse;
				portalShader.shaders.back().params.SetMatrix( g_portalToDomeUStr, portalToDome );

				// Update the light shader. We can modify the existing one in
				// place because we know we're only using it on this one light.
				riley::ShadingNetwork shaders = { (uint32_t)portalShader.shaders.size(), portalShader.shaders.data() };
				riley->ModifyLightShader( info.lightShader, &shaders, /* lightFilter = */ nullptr );
			}
			else
			{
				riley->ModifyLightInstance(
					riley::GeometryPrototypeId(), riley::LightInstanceId( id ),
					nullptr, nullptr, nullptr, nullptr, &mutedAttributes
				);
			}
		}
		else
		{
			// Mute domes if there are portals.
			riley->ModifyLightInstance(
				riley::GeometryPrototypeId(), riley::LightInstanceId( id ),
				nullptr, nullptr, nullptr, nullptr, havePortals ? &mutedAttributes : &info.attributes
			);
		}
	}
}
