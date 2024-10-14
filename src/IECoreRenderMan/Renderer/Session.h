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

#pragma once

#include "GafferScene/Private/IECoreScenePreview/Renderer.h"

#include "IECore/RefCounted.h"

#include "Riley.h"

#include "tbb/concurrent_hash_map.h"

namespace IECoreRenderMan::Renderer
{

// The various Renderer components all need access to the same Riley object, and
// also need to know the render type because it affects whether or not they need
// to delete resources on destruction. Furthermore, we don't want to require all
// client code to destroy all AttributeInterfaces and ObjectInterfaces before
// destroying the renderer - that's too much of a pain, especially in Python.
// All components therefore share ownership of a Session, which provides the
// Riley instance and render type, and is destroyed only when the last owner
// dies.
struct Session : public IECore::RefCounted
{

	Session( IECoreScenePreview::Renderer::RenderType renderType );
	~Session();

	void setOptions( const RtParamList &options );

	struct CameraInfo
	{
		riley::CameraId id;
		IECoreScene::ConstCameraPtr source;
	};

	/// `Camera` collaborates with `Session` to maintain a map of cameras currently
	/// in existence. This is used by `Globals` when creating the `riley::RenderView`.
	void addCamera( const std::string &name, const CameraInfo &camera );
	CameraInfo getCamera( const std::string &name ) const;
	void removeCamera( const std::string &name );

	riley::Riley *riley;
	const IECoreScenePreview::Renderer::RenderType renderType;

	private :

		bool m_optionsSet;

		using CameraMap = tbb::concurrent_hash_map<std::string, CameraInfo>;
		CameraMap m_cameras;

};

IE_CORE_DECLAREPTR( Session );

} // namespace IECoreRenderMan::Renderer
