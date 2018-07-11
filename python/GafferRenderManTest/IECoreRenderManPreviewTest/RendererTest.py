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

import os
import unittest
import imath

import IECore
import IECoreImage
import IECoreScene

import GafferTest
import GafferScene
import GafferRenderMan

class RendererTest( GafferTest.TestCase ) :

	def testFactory( self ) :

		self.assertTrue( "RenderMan" in GafferScene.Private.IECoreScenePreview.Renderer.types() )

		r = GafferScene.Private.IECoreScenePreview.Renderer.create( "RenderMan" )
		self.assertTrue( isinstance( r, GafferScene.Private.IECoreScenePreview.Renderer ) )

	def testSceneDescription( self ) :

		with self.assertRaisesRegexp( RuntimeError, "SceneDescription mode not supported" ) :
			GafferScene.Private.IECoreScenePreview.Renderer.create(
				"RenderMan",
				GafferScene.Private.IECoreScenePreview.Renderer.RenderType.SceneDescription,
				self.temporaryDirectory() + "/test.rib"
			)

	def testOutput( self ) :

		r = GafferScene.Private.IECoreScenePreview.Renderer.create(
			"RenderMan",
			GafferScene.Private.IECoreScenePreview.Renderer.RenderType.Batch
		)

		r.output(
			"test",
			IECoreScene.Output(
				self.temporaryDirectory() + "/beauty.exr",
				"exr",
				"rgba",
				{
					"filter" : "gaussian",
					"filterwidth" : imath.V2f( 3.5 ),
				}
			)
		)

		r.render()
		del r

		self.assertTrue( os.path.exists( self.temporaryDirectory() + "/beauty.exr" ) )

	def testObject( self ) :

		renderer = GafferScene.Private.IECoreScenePreview.Renderer.create(
			"RenderMan",
			GafferScene.Private.IECoreScenePreview.Renderer.RenderType.Batch
		)

		renderer.output(
			"test",
			IECoreScene.Output(
				"test",
				"ieDisplay",
				"rgba",
				{
					"driverType" : "ImageDisplayDriver",
					"handle" : "myLovelySphere",
				}
			)
		)

		renderer.object(
			"sphere",
			IECoreScene.SpherePrimitive(),
			renderer.attributes( IECore.CompoundObject() )
		)

		renderer.render()

		image = IECoreImage.ImageDisplayDriver.storedImage( "myLovelySphere" )
		self.assertEqual( max( image["A"] ), 1 )

	def testDestroyObjectAfterRenderer( self ) :

		renderer = GafferScene.Private.IECoreScenePreview.Renderer.create(
			"RenderMan",
			GafferScene.Private.IECoreScenePreview.Renderer.RenderType.Interactive
		)

		renderer.output(
			"test",
			IECoreScene.Output(
				"test",
				"ieDisplay",
				"rgba",
				{
					"driverType" : "ImageDisplayDriver",
					"handle" : "myLovelySphere",
				}
			)
		)

		sphere = renderer.object(
			"sphere",
			IECoreScene.SpherePrimitive(),
			renderer.attributes( IECore.CompoundObject() )
		)

		renderer.render()

		del renderer
		del sphere

if __name__ == "__main__":
	unittest.main()
