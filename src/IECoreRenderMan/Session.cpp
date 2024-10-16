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


Session::Session( IECoreScenePreview::Renderer::RenderType renderType, const IECore::MessageHandlerPtr &messageHandler )
	:	riley( nullptr ), renderType( renderType ), m_exceptionHandler( std::make_unique<ExceptionHandler>( messageHandler ) ), m_optionsSet( false )
{
	// `argv[0]==""` prevents RenderMan doing its own signal handling.
	vector<const char *> args = { "" };
	PRManSystemBegin( args.size(), args.data() );
	/// \todo There can only be one PRMan/Riley instance at a time, so we need to
	/// prevent the creation of a second renderer.
	PRManRenderBegin( args.size(), args.data() );

	auto rixXcpt = (RixXcpt *)RixGetContext()->GetRixInterface( k_RixXcpt );
	rixXcpt->Register( m_exceptionHandler.get() );

	auto rileyManager = (RixRileyManager *)RixGetContext()->GetRixInterface( k_RixRileyManager );
	/// \todo What is the `rileyVariant` argument for? XPU?
	riley = rileyManager->CreateRiley( RtUString(), RtParamList() );
}

Session::~Session()
{
	if( !m_optionsSet )
	{
		// Riley crashes if you don't call `SetOptions()` before destruction.
		riley->SetOptions( RtParamList() );
	}

	auto rileyManager = (RixRileyManager *)RixGetContext()->GetRixInterface( k_RixRileyManager );
	rileyManager->DestroyRiley( riley );

	auto rixXcpt = (RixXcpt *)RixGetContext()->GetRixInterface( k_RixXcpt );
	rixXcpt->Unregister( m_exceptionHandler.get() );

	PRManRenderEnd();
	PRManSystemEnd();
}

void Session::setOptions( const RtParamList &options )
{
	riley->SetOptions( options );
	m_optionsSet = true;
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
