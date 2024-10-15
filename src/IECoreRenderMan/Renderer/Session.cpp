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

using namespace std;
using namespace IECoreRenderMan::Renderer;

Session::Session( IECoreScenePreview::Renderer::RenderType renderType )
	:	riley( nullptr ), renderType( renderType ), m_optionsSet( false )
{
	/// \todo What is the `rileyVariant` argument for? XPU?
	auto rileyManager = (RixRileyManager *)RixGetContext()->GetRixInterface( k_RixRileyManager );

	// `argv[0]==""`  prevents RenderMan doing its own signal handling.
	vector<const char *> args = { "prman" }; // TODO : REVERT TO "". BUT YOU'RE GETTING SOME USEFUL OUTPUT WITHOUT FOR NOW.
	PRManSystemBegin( args.size(), args.data() );
	// TODO : THERE CAN ONLY BE ONE OF THESE. SO WE'RE GOING TO NEED TO PREVENT
	// THE CREATION OF TWO RENDERERS AT ONCE.
	PRManRenderBegin( args.size(), args.data() );

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
