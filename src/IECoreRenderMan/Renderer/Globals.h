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

#include "Session.h"

#include "IECoreScene/Output.h"
#include "IECoreScene/Shader.h"

#include "boost/noncopyable.hpp"

#include "tbb/spin_mutex.h"

#include <thread>

namespace IECoreRenderMan::Renderer
{

class Globals : public boost::noncopyable
{

	public :

		Globals( const SessionPtr &session );
		~Globals();

		void option( const IECore::InternedString &name, const IECore::Object *value );
		void output( const IECore::InternedString &name, const IECoreScene::Output *output );

		// TODO : UPDATE WHEN YOU FIGURE OUT THE NEW RESTRICTIONS
		//
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
		void ensureWorld();

		void render();
		void pause();

	private :

		bool worldBegun();
		void updateRenderView();
		void deleteRenderView();

		IECoreRenderMan::Renderer::SessionPtr m_session;
		RtParamList m_options;

		std::unordered_map<IECore::InternedString, IECoreScene::ConstOutputPtr> m_outputs;

		IECoreScene::ConstShaderPtr m_integrator; // TODO : CAN WE AVOID STORING THIS?
		riley::IntegratorId m_integratorId;

		std::string m_cameraOption;
		riley::CameraId m_defaultCamera;

		std::vector<riley::RenderOutputId> m_renderOutputs;
		std::vector<riley::DisplayId> m_displays;
		riley::RenderTargetId m_renderTarget;
		Imath::V2i m_renderTargetResolution;
		riley::RenderViewId m_renderView;

		tbb::spin_mutex m_worldBeginMutex;
		std::thread::id m_expectedWorldBeginThreadId;
		bool m_worldBegun;

		std::thread m_interactiveRenderThread;

};




} // namespace IECoreRenderMan::Renderer
