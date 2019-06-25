##########################################################################
#
#  Copyright (c) 2018, John Haddon. All rights reserved.
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

import glob
import os
from xml.etree import cElementTree

import IECore

import Gaffer
import GafferUITest

import GafferRenderMan
import GafferRenderManUI

class RenderManShaderUITest( GafferUITest.TestCase ) :

	def testMetadata( self ) :

		n = GafferRenderMan.RenderManShader()
		n.loadShader( "PxrSurface" )

		self.assertEqual(
			Gaffer.Metadata.value( n["parameters"]["diffuseGain"], "layout:section" ),
			"Diffuse"
		)

		self.assertEqual(
			Gaffer.Metadata.value( n["parameters"]["diffuseExponent"], "layout:section" ),
			"Diffuse.Advanced"
		)

		self.assertEqual(
			Gaffer.Metadata.value( n["parameters"]["diffuseGain"], "label" ),
			"Gain"
		)

		self.assertEqual(
			Gaffer.Metadata.value( n["parameters"]["continuationRayMode"], "presetNames" ),
			IECore.StringVectorData( [ "Off", "Last Hit", "All Hits" ] )
		)

		self.assertEqual(
			Gaffer.Metadata.value( n["parameters"]["continuationRayMode"], "presetValues" ),
			IECore.IntVectorData( [ 0, 1, 2 ] )
		)

	def testLoadAllStandardShaders( self ) :

		def __shaderType( argsFile ) :

			for event, element in cElementTree.iterparse( argsFile, events = ( "start", "end" ) ) :
				if element.tag == "shaderType" and event == "end" :
					tag = element.find( "tag" )
					return tag.attrib.get( "value" ) if tag is not None else None

			return None

		shadersLoaded = set()
		for argsFile in glob.glob( os.path.expandvars( "$RMANTREE/lib/plugins/Args/*.args" ) ) :

			if __shaderType( argsFile ) not in ( "pattern", "bxdf", "integrator" ) :
				continue

			node = GafferRenderMan.RenderManShader()
			shaderName = os.path.basename( os.path.splitext( argsFile )[0] )
			node.loadShader( shaderName )

			# Trigger metadata parsing and ensure there are no errors
			Gaffer.Metadata.value( node, "description" )

			shadersLoaded.add( shaderName )

		# Guard against shaders being moved and this test therefore not
		# loading anything.
		self.assertIn( "PxrSurface", shadersLoaded )

if __name__ == "__main__":
	unittest.main()
