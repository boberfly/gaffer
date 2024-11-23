##########################################################################
#
#  Copyright (c) 2024, Alex Fuller. All rights reserved.
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

	GafferRenderMan.RenderManSampleFilter,

	"description",
	"""
	Assigns a Sample Filter. This is stored as an `ri:samplefilter` option in
	Gaffer's globals, and applied to all render outputs.
	""",

	plugs = {

		"samplefilter" : [

			"description",
			"""
			The Sample Filter to be assigned. The output of a RenderManShader node
			holding a Sample Filter should be connected here.
			""",

			"noduleLayout:section", "left",
			"nodule:type", "GafferUI::StandardNodule",

		],

		"mode" : [

			"description",
			"""
			The mode used to combine the `samplefilter` input with any display
			filters that already exist in the globals.

			- Replace : Removes all pre-existing Sample Filters, and replaces them with
			  the new ones.
			- InsertFirst : Inserts the new Sample Filter so that they will be run before
			  any pre-existing Sample Filters.
			- InsertLast : Inserts the new Sample Filter so that they will be run after
			  any pre-existing Sample Filters.
			""",

			"preset:Replace", GafferRenderMan.RenderManSampleFilter.Mode.Replace,
			"preset:InsertFirst", GafferRenderMan.RenderManSampleFilter.Mode.InsertFirst,
			"preset:InsertLast", GafferRenderMan.RenderManSampleFilter.Mode.InsertLast,
			"plugValueWidget:type", "GafferUI.PresetsPlugValueWidget",

		],

	}

)
