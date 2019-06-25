##########################################################################
#
#  Copyright (c) 2019, John Haddon. All rights reserved.
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

import Gaffer
import GafferRenderMan

Gaffer.Metadata.registerNode(

	GafferRenderMan.RenderManOptions,

	"description",
	"""
	Sets global scene options applicable to the RenderMan
	renderer. Use the StandardOptions node to set
	global options applicable to all renderers.
	""",

	plugs = {

		"options.bucketOrder" : [

			"description",
			"""
			The order that the buckets (image tiles) are rendered in.
			""",

			"layout:section", "Rendering",

		],

		"options.bucketOrder.value" : [


			"preset:Horizontal", "horizontal",
			"preset:Vertical", "vertical",
			"preset:ZigZagX", "zigzag-x",
			"preset:ZigZagY", "zigzag-y",
			"preset:SpaceFill", "spacefill",
			"preset:Random", "random",
			"preset:Spiral", "spiral",
			"preset:Circle", "circle",

			"plugValueWidget:type", "GafferUI.PresetsPlugValueWidget",
			"layout:section", "Rendering",

		],

		"options.bucketSize" : [

			"description",
			"""
			The size of the buckets (image tiles).
			""",

			"layout:section", "Rendering",

		],

		"options.limitsThreads" : [

			"description",
			"""
			The number of threads to use for rendering. A value
			of 0 means that all CPUs are used. A value of `-N` means
			that all except `N` CPUs are used.
			""",

			"label", "Threads",
			"layout:section", "Rendering",

		],

		"options.hiderMaxSamples" : [


			"description",
			"""
			The maximum number of camera samples to take per pixel.
			""",

			"label", "Max Samples",
			"layout:section", "Sampling",

		],

		"options.hiderMinSamples" : [


			"description",
			"""
			The minimum number of camera samples to take per pixel.
			This is used in combination with pixel variance to control
			adaptive sampling. The default means that the minimum number
			of samples will be automatically set to the square root of
			the maximum number of samples.
			""",

			"label", "Min Samples",
			"layout:section", "Sampling",

		],

		"options.pixelVariance" : [


			"description",
			"""
			Sets a limit on the estimated variance from the true pixel
			value. Used in conjunction with the minimum sampling limit
			to control adaptive sampling.
			""",

			"label", "Variance",
			"layout:section", "Sampling",

		],

		"options.hiderIncremental" : [


			"description",
			"""
			Turns on incremental image output, so that a noisy image
			is displayed quickly and refined to produce the final image.
			When this is off, final buckets are written out one-by-one
			instead.
			""",

			"label", "Incremental",
			"layout:section", "Sampling",

		],

		"options.statisticsLevel" : [

			"description",
			"""
			Enables statistics reporting.
			""",

			"label", "Level",
			"layout:section", "Statistics",

		],

		"options.statisticsFileName" : [

			"description",
			"""
			File where a human-readable summary of the statistics will
			be written.
			""",

			"label", "File",
			"layout:section", "Statistics",

		],

		"options.statisticsXMLFileName" : [

			"description",
			"""
			File where comprehensive XML statistics will be written.
			""",

			"label", "XML File",
			"layout:section", "Statistics",

		],

		"options.searchPathTexture" : [

			"description",
			"""
			List of directories to search for textures on. Directories should
			be separated by a ':' character. Use '@' to include RenderMan's
			standard directories.
			""",

			"label", "Textures",
			"layout:section", "Search Paths",

		],

		"options.searchPathRixPlugin" : [

			"description",
			"""
			List of directories to search for textures on. Directories should
			be separated by a ':' character. Use '@' to include RenderMan's
			standard directories.
			""",

			"label", "Plugins",
			"layout:section", "Search Paths",

		],

		"options.searchPathDirMap" : [

			"description",
			"""
			A directory mapping applied to the absolute paths that RenderMan
			uses to look up resources such as shaders and textures. See
			RenderMan documentation for more details.
			""",

			"label", "Directory Mapping",
			"layout:section", "Search Paths",

		],

	}

)
