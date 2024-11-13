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

# Pull all the option definitions out of RenderMan's `PRManOptions.args` file
# and register them using Gaffer's standard metadata conventions. This is then
# used to populate the RenderManOptions node and the RenderPassEditor etc.

if "RMANTREE" in os.environ :

	rmanTree = pathlib.Path( os.environ["RMANTREE"] )

	GafferRenderMan._ArgsFileAlgo.registerMetadata(
		rmanTree / "lib" / "defaults" / "PRManOptions.args", "option:ri:",
		parametersToIgnore = {
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
			# These aren't documented, and will cause GafferRenderManUITest.DocumentationTest
			# to fail if we load them. We'll let them back in if we determine they are relevant
			# and can come up with a sensible documentation string of our own.
			"limits:gridsize",
			"limits:proceduralbakingclumpsize",
			"limits:ptexturemaxfiles",
			"limits:textureperthreadmemoryratio",
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

	# Add options used by GafferRenderMan._InteractiveDenoiserAdaptor. These don't mean
	# anything to RenderMan, but we still use the "ri:" prefix to keep things consistent
	# for the end user.
	## \todo Should we use a different prefix?

	Gaffer.Metadata.registerValue( "option:ri:interactiveDenoiser:enabled", "defaultValue", False )
	Gaffer.Metadata.registerValue( "option:ri:interactiveDenoiser:enabled", "description",
		"""
		Enables interactive denoising using RenderMan's `quicklyNoiseless` display driver. When on, all
		required denoising AOVs are added to the render automatically.
		"""
	)
	Gaffer.Metadata.registerValue( "option:ri:interactiveDenoiser:enabled", "label", "Enabled" )
	Gaffer.Metadata.registerValue( "option:ri:interactiveDenoiser:enabled", "layout:section", "Interactive Denoiser" )

	Gaffer.Metadata.registerValue( "option:ri:interactiveDenoiser:cheapFirstPass", "defaultValue", True )
	Gaffer.Metadata.registerValue( "option:ri:interactiveDenoiser:cheapFirstPass", "description",
		"""
		When on, the first pass will use a cheaper (slightly faster but lower
		quality) heuristic. This can be useful if rendering something that is
		converging very quickly and you want to prioritize getting a denoised
		result faster.
		"""
	)
	Gaffer.Metadata.registerValue( "option:ri:interactiveDenoiser:cheapFirstPass", "label", "Cheap First Pass" )
	Gaffer.Metadata.registerValue( "option:ri:interactiveDenoiser:cheapFirstPass", "layout:section", "Interactive Denoiser" )

	Gaffer.Metadata.registerValue( "option:ri:interactiveDenoiser:interval", "defaultValue", 4.0 )
	Gaffer.Metadata.registerValue( "option:ri:interactiveDenoiser:interval", "description",
		"""
		The time interval in between denoise runs (in seconds).
		"""
	)
	Gaffer.Metadata.registerValue( "option:ri:interactiveDenoiser:interval", "label", "Interval" )
	Gaffer.Metadata.registerValue( "option:ri:interactiveDenoiser:interval", "layout:section", "Interactive Denoiser" )

	Gaffer.Metadata.registerValue( "option:ri:interactiveDenoiser:minSamples", "defaultValue", 4.0 )
	Gaffer.Metadata.registerValue( "option:ri:interactiveDenoiser:minSamples", "description",
		"""
		The minimum number of average samples per bucket before the interactive denoiser runs for the first time.
		Changing this preference requires the render to be restarted for this option to be respected.
		"""
	)
	Gaffer.Metadata.registerValue( "option:ri:interactiveDenoiser:minSamples", "label", "Min Samples" )
	Gaffer.Metadata.registerValue( "option:ri:interactiveDenoiser:minSamples", "layout:section", "Interactive Denoiser" )
