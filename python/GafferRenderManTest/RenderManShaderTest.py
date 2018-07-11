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

import unittest

import imath

import Gaffer
import GafferSceneTest
import GafferRenderMan

class RenderManShaderTest( GafferSceneTest.SceneTestCase ) :

	def testBasicLoading( self ) :

		shader = GafferRenderMan.RenderManShader()
		shader.loadShader( "PxrConstant" )

		self.assertEqual( shader["name"].getValue(), "PxrConstant" )
		self.assertEqual( shader["type"].getValue(), "renderman:bxdf" )

		self.assertEqual( shader["parameters"].keys(), [ "emitColor", "presence" ] )

		self.assertIsInstance( shader["parameters"]["emitColor"], Gaffer.Color3fPlug )
		self.assertIsInstance( shader["parameters"]["presence"], Gaffer.FloatPlug )

		self.assertEqual( shader["parameters"]["emitColor"].defaultValue(), imath.Color3f( 1 ) )
		self.assertEqual( shader["parameters"]["presence"].defaultValue(), 1.0 )
		self.assertEqual( shader["parameters"]["presence"].minValue(), 0.0 )
		self.assertEqual( shader["parameters"]["presence"].maxValue(), 1.0 )

	def testLoadParametersInsidePages( self ) :

		shader = GafferRenderMan.RenderManShader()
		shader.loadShader( "PxrDirt" )

		self.assertIn( "occluded", shader["parameters"] )
		self.assertIn( "unoccluded", shader["parameters"] )

	def testLoadRemovesUnnecessaryParameters( self ) :

		for keepExisting in ( True, False ) :

			shader = GafferRenderMan.RenderManShader()
			shader.loadShader( "PxrMix" )
			self.assertIn( "color1", shader["parameters"] )

			shader.loadShader( "PxrConstant", keepExistingValues = keepExisting )
			self.assertNotIn( "color1", shader["parameters"] )
			self.assertIn( "emitColor", shader["parameters"] )

	def testLoadOutputs( self ) :

		shader = GafferRenderMan.RenderManShader()
		shader.loadShader( "PxrBlackBody" )

		self.assertIn( "resultRGB", shader["out"] )
		self.assertIsInstance( shader["out"]["resultRGB"], Gaffer.Color3fPlug )

		self.assertIn( "resultR", shader["out"] )
		self.assertIsInstance( shader["out"]["resultR"], Gaffer.FloatPlug )

		self.assertIn( "resultG", shader["out"] )
		self.assertIsInstance( shader["out"]["resultG"], Gaffer.FloatPlug )

		self.assertIn( "resultB", shader["out"] )
		self.assertIsInstance( shader["out"]["resultB"], Gaffer.FloatPlug )

		shader.loadShader( "PxrPrimvar" )

		self.assertIn( "resultP", shader["out"] )
		self.assertIsInstance( shader["out"]["resultP"], Gaffer.V3fPlug )

if __name__ == "__main__":
	unittest.main()
