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

import IECore

import Gaffer
import GafferRenderMan

# Pull all the option and attribute definitions out of RenderMan's `.args`
# files and register them using Gaffer's standard metadata conventions.
# This is then used to populate the RenderManOptions node and the RenderPassEditor
# etc.

def __registerMetadata( path, prefix, ignore ) :

	metadata = GafferRenderMan._ArgsFileAlgo.parseMetadata( path )
	for name, values in metadata["parameters"].items() :

		if name in ignore :
			continue

		target = f"{prefix}{name}"
		for key, value in values.items() :
			Gaffer.Metadata.registerValue( target, key, value )

if "RMANTREE" in os.environ :

	rmanTree = pathlib.Path( os.environ["RMANTREE"] )

	__registerMetadata(
		rmanTree / "lib" / "defaults" / "PRManOptions.args", "option:ri:",
		ignore = {
			# Gaffer handles all of these in a renderer-agnostic manner.
			"Ri:Frame",
			"Ri:FrameAspectRatio",
			"Ri:ScreenWindow",
			"Ri:CropWindow",
			"Ri:FormatPixelAspectRatio",
			"Ri:FormatResolution",
			"Ri:Shutter",
			"hider:samplemotion",
			# These are for back-compatibility with a time before Gaffer
			# supported RenderMan, so we don't need them. The fewer settings
			# people have to wrestle with, the better.
			"statistics:displace_ratios",
			"statistics:filename",
			"statistics:level",
			"statistics:maxdispwarnings",
			"statistics:shaderprofile",
			"statistics:stylesheet",
			"statistics:texturestatslevel",
			"statistics:xmlfilename",
			"trace:incorrectCurveBias",
			"shade:chiangCompatibilityVersion",
			"shade:subsurfaceTypeDefaultFromVersion24",
			# https://rmanwiki-26.pixar.com/space/REN26/19661831/Sampling+Modes#Adaptive-Sampling-Error-Metrics
			# implies that this is only used by the obsolete adaptive metrics.
			"hider:darkfalloff",
			# These are XPU-only, and we don't yet support XPU. They also
			# sound somewhat fudgy.
			"interactive:displacementupdatemode",
			"interactive:displacementupdatedebug",
			# These just don't make much sense in Gaffer.
			"ribparse:varsubst",
		}
	)

	# Omit obsolete adaptive metrics.

	for key in [ "presetNames", "presetValues" ] :
		Gaffer.Metadata.registerValue(
			"option:ri:hider:adaptivemetric", key,
			IECore.StringVectorData( [
				x for x in Gaffer.Metadata.value( "option:ri:hider:adaptivemetric", key )
				if "v22" not in x
			] )
		)

	# Move some stray options into a more logical section of the layout.

	for option in [
		"shade:debug",
		"shade:roughnessmollification",
		"shade:shadowBumpTerminator",
	] :
		Gaffer.Metadata.registerValue( f"option:ri:{option}", "layout:section", "Shading" )
