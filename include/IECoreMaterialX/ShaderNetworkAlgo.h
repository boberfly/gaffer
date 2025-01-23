//////////////////////////////////////////////////////////////////////////
//
//  Copyright (c) 2025, Alex Fuller. All rights reserved.
//
//  Redistribution and use in source and binary forms, with or without
//  modification, are permitted provided that the following conditions are
//  met:
//
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//
//     * Redistributions in binary form must reproduce the above copyright
//       notice, this list of conditions and the following disclaimer in the
//       documentation and/or other materials provided with the distribution.
//
//     * Neither the name of Image Engine Design nor the names of any
//       other contributors to this software may be used to endorse or
//       promote products derived from this software without specific prior
//       written permission.
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

#include "IECoreMaterialX/Export.h"

#include "IECoreScene/ShaderNetwork.h"

#include <filesystem>
#include <string>

namespace IECoreMaterialX
{

namespace ShaderNetworkAlgo
{

/// Convert a known and named MaterialX node into an OSL node
/// and return the shader code.
IECOREMATERIALX_API std::string convertToOSL(
	const std::string &shaderName,
	const std::string &mxNodeName
);

/// Convert a known and named MaterialX node into an OSL node
/// and return the path to the compiled OSO bytecode.
IECOREMATERIALX_API std::filesystem::path convertToOSO(
	const std::string &shaderName,
	const std::string &mxNodeName
);

/// Converts all found MaterialX nodes, compiles them to OSL
/// and replaces the MaterialX node with the OSL one.
/// `usdNodes` will translate USD shaders that don't have the mtlx: prefix. Gaffer already translates USD
/// nodes like PreviewSurface to the native renderer's shaders, but this allows it to be processed from
/// MaterialX into a native OSL form instead, or for renderers which don't have an implementation.
/// `addAdapters` will apply the registered MaterialX component connection adapters before translation.
IECOREMATERIALX_API void convertToOSLNodes( IECoreScene::ShaderNetwork *network, const bool usdNodes = true, const bool addAdapters = true );

} // namespace ShaderNetworkAlgo

} // namespace IECoreMaterialX
