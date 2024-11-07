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

#include "Riley.h"

#include "tbb/concurrent_hash_map.h"

namespace IECoreRenderMan
{

/// Owns a Riley instance and provides shared state to facilitate communication
/// between the various Renderer components.
struct Session
{

	/// Options must be provided at construction time, as Riley requires them to
	/// be set before any other operations can take place (and indeed, will crash
	/// if the Riley instance is destroyed without `SetOptions()` being called).
	Session( IECoreScenePreview::Renderer::RenderType renderType, const RtParamList &options, const IECore::MessageHandlerPtr &messageHandler );
	~Session();

	struct CameraInfo
	{
		riley::CameraId id;
		RtParamList options;
	};

	/// \todo Do this by wrapping `CreateCamera()` and `DestroyCamera()` instead.
	/// `Camera` collaborates with `Session` to maintain a map of cameras currently
	/// in existence. This is used by `Globals` when creating the `riley::RenderView`.
	void addCamera( const std::string &name, const CameraInfo &camera );
	/// \todo Should we return `const &`?
	CameraInfo getCamera( const std::string &name ) const;
	void removeCamera( const std::string &name );

	riley::LightShaderId createLightShader( const riley::ShadingNetwork &light );
	void deleteLightShader( riley::LightShaderId lightShaderId );

	riley::LightInstanceId createLightInstance( riley::LightShaderId lightShaderId, const riley::Transform &transform, const RtParamList &attributes );
	riley::LightInstanceResult modifyLightInstance(
		riley::LightInstanceId lightInstanceId, const riley::LightShaderId *lightShaderId, const riley::Transform *transform,
		const RtParamList *attributes
	);
	void deleteLightInstance( riley::LightInstanceId lightInstanceId );

	void linkPortals();

	riley::Riley *riley;
	const IECoreScenePreview::Renderer::RenderType renderType;

	private :

		struct ExceptionHandler;
		std::unique_ptr<ExceptionHandler> m_exceptionHandler;

		using CameraMap = tbb::concurrent_hash_map<std::string, CameraInfo>;
		CameraMap m_cameras;

		struct LightShaderInfo
		{
			std::vector<riley::ShadingNode> shaders;
		};

		// Keys are `riley::LightShaderId`.
		using LightShaderMap = tbb::concurrent_hash_map<uint32_t, LightShaderInfo>;
		LightShaderMap m_domeAndPortalShaders;

		struct LightInfo
		{
			riley::LightShaderId lightShader;
			RtMatrix4x4 transform;
			RtParamList attributes;
		};
		// Keys are `riley::LightInstanceId`.
		using LightInstanceMap = tbb::concurrent_hash_map<uint32_t, LightInfo>;
		LightInstanceMap m_domeAndPortalLights;

};

IE_CORE_DECLAREPTR( Session );

} // namespace IECoreRenderMan
