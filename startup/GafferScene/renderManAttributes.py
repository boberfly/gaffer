##########################################################################
#
#  Copyright (c) 2024, Cinesite VFX Ltd. All rights reserved.
#
#  Redistribution and use in source and binary forms, with or without
#  modification, are permitted provided that the following conditions are
#  met:
#
#      * Redistributions of source code must retain the above
#        copyright notice, this list of conditions and the following
#        disclaimer.
#
#      * Redistributions in binary form must reproduce the above
#        copyright notice, this list of conditions and the following
#        disclaimer in the documentation and/or other materials provided with
#        the distribution.
#
#      * Neither the name of John Haddon nor the names of
#        any other contributors to this software may be used to endorse or
#        promote products derived from this software without specific prior
#        written permission.
#
#  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
#  IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
#  THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
#  PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
#  CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
#  EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
#  PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
#  PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
#  LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
#  NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
#  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#
##########################################################################

import os
import pathlib

import GafferRenderMan

# Pull all the attribute definitions out of RenderMan's `PRManAttributes.args` file
# and register them using Gaffer's standard metadata conventions. This is then
# used to populate the RenderManAttributes node and the AttributeEditor etc.

if "RMANTREE" in os.environ :

	rmanTree = pathlib.Path( os.environ["RMANTREE"] )

	GafferRenderMan._ArgsFileAlgo.registerMetadata(
		rmanTree / "lib" / "defaults" / "PRManAttributes.args", "attribute:ri:",
		parametersToIgnore = {
			# Things that Gaffer has renderer-agnostic attributes for already.
			"Ri:Sides",
			"lighting:mute",
			# Things that we might want to use internally in the Renderer class.
			"identifier:id",
			"identifier:id2",
			"identifier:name",
			"Ri:ReverseOrientation",
			# Things that we probably want to expose, but which will require
			# additional plumbing before they will be useful.
			"lightfilter:subset",
			"lighting:excludesubset",
			"lighting:subset",
			"trace:reflectexcludesubset",
			"trace:reflectsubset",
			"trace:shadowexcludesubset",
			"trace:shadowsubset",
			"trace:transmitexcludesubset",
			"trace:transmitsubset",
			# Might it be better if we populate this automatically based on set
			# memberships and/or light links? Don't expose it until we know.
			"grouping:membership",
		}
	)
