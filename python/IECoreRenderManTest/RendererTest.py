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

import math
import time
import unittest

import imath

import OpenImageIO

import IECore
import IECoreImage
import IECoreScene

import GafferTest
import GafferScene

class RendererTest( GafferTest.TestCase ) :

	def testFactory( self ) :

		self.assertTrue( "RenderMan" in GafferScene.Private.IECoreScenePreview.Renderer.types() )

		r = GafferScene.Private.IECoreScenePreview.Renderer.create( "RenderMan" )
		self.assertTrue( isinstance( r, GafferScene.Private.IECoreScenePreview.Renderer ) )

	def testSceneDescription( self ) :

		with self.assertRaisesRegex( RuntimeError, "SceneDescription mode not supported" ) :
			GafferScene.Private.IECoreScenePreview.Renderer.create(
				"RenderMan",
				GafferScene.Private.IECoreScenePreview.Renderer.RenderType.SceneDescription,
				( self.temporaryDirectory() / "test.rib" ).as_posix()
			)

	def testOutput( self ) :

		r = GafferScene.Private.IECoreScenePreview.Renderer.create(
			"RenderMan",
			GafferScene.Private.IECoreScenePreview.Renderer.RenderType.Batch
		)

		r.output(
			"testRGB",
			IECoreScene.Output(
				( self.temporaryDirectory() / "rgb.exr" ).as_posix(),
				"exr",
				"rgb",
				{
				}
			)
		)

		r.output(
			"testRGBA",
			IECoreScene.Output(
				( self.temporaryDirectory() / "rgba.exr" ).as_posix(),
				"exr",
				"rgba",
				{
				}
			)
		)

		r.render()
		del r

		self.assertTrue( ( self.temporaryDirectory() / "rgb.exr" ).is_file() )
		imageSpec = OpenImageIO.ImageInput.open( str( self.temporaryDirectory() / "rgb.exr" ) ).spec()
		self.assertEqual( imageSpec.nchannels, 3 )
		self.assertEqual( imageSpec.channelnames, ( "R", "G", "B" ) )

		self.assertTrue( ( self.temporaryDirectory() / "rgba.exr" ).is_file() )
		imageSpec = OpenImageIO.ImageInput.open( str( self.temporaryDirectory() / "rgba.exr" ) ).spec()
		self.assertEqual( imageSpec.nchannels, 4 )
		self.assertEqual( imageSpec.channelnames, ( "R", "G", "B", "A" ) )

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

	def testMissingLightShader( self ) :

		renderer = GafferScene.Private.IECoreScenePreview.Renderer.create(
			"RenderMan",
			GafferScene.Private.IECoreScenePreview.Renderer.RenderType.Interactive
		)

		lightShader = IECoreScene.ShaderNetwork( { "light" : IECoreScene.Shader( "BadShader", "ri:light" ), }, output = ( "light", "out" ) )
		lightAttributes = renderer.attributes(
			IECore.CompoundObject( { "ri:light" : lightShader } )
		)

		# Exercises our workarounds for crashes in Riley when a light
		# doesn't have a valid shader.
		light = renderer.light( "/light", None, lightAttributes )
		light.transform( imath.M44f().translate( imath.V3f( 1, 2, 3 ) ) )

		del light
		del renderer

	def testIntegratorEdit( self ) :

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

		object = renderer.object(
			"sphere",
			IECoreScene.SpherePrimitive(),
			renderer.attributes( IECore.CompoundObject() )
		)

		renderer.render()
		time.sleep( 1 )
		renderer.pause()

		image = IECoreImage.ImageDisplayDriver.storedImage( "myLovelySphere" )
		self.assertEqual( self.__colorAtUV( image, imath.V2i( 0.5 ) ), imath.Color4f( 1 ) )

		renderer.option(
			"ri:integrator",
			IECoreScene.ShaderNetwork(
				shaders = {
					"integrator" : IECoreScene.Shader(
						"PxrVisualizer", "ri:integrator",
						{
							"style" : "objectnormals",
							"wireframe" : False,
						}
					),
				},
				output = "integrator"
			)
		)

		renderer.render()
		time.sleep( 1 )

		image = IECoreImage.ImageDisplayDriver.storedImage( "myLovelySphere" )
		self.assertNotEqual( self.__colorAtUV( image, imath.V2i( 0.5 ) ), imath.Color4f( 0, 0.514117, 0.515205, 1 ) )

		del object
		del renderer

	def testUSDLight( self ) :

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
					"handle" : "test",
				}
			)
		)

		renderer.object(
			"sphere",
			IECoreScene.SpherePrimitive(),
			renderer.attributes( IECore.CompoundObject( {
				"ri:surface" : IECoreScene.ShaderNetwork(
					shaders = {
						"output" : IECoreScene.Shader( "PxrDiffuse" )
					},
					output = "output",
				)
			} ) )
		).transform( imath.M44f().translate( imath.V3f( 0, 0, -3 ) ) )

		renderer.light(
			"light",
			None,
			renderer.attributes( IECore.CompoundObject( {
				"light" : IECoreScene.ShaderNetwork(
					shaders = {
						"output" : IECoreScene.Shader( "DomeLight", "light" )
					},
					output = "output",
				)
			} ) )
		)

		renderer.render()

		image = IECoreImage.ImageDisplayDriver.storedImage( "test" )
		c = self.__colorAtUV( image, imath.V2f( 0.5 ) )
		for i in range( 0, 3 ) :
			self.assertAlmostEqual( c[i], 0.5, delta = 0.01 )
		self.assertEqual( c[3], 1.0 )

	def testEXRLayerNames( self ) :

		renderer = GafferScene.Private.IECoreScenePreview.Renderer.create(
			"RenderMan",
			GafferScene.Private.IECoreScenePreview.Renderer.RenderType.Batch
		)

		outputs = [
			# Data source, layer name, expected EXR channel names.
			( "rgb", None, ( "R", "G", "B" ) ),
			( "rgba", None, ( "R", "G", "B", "A" ) ),
			( "float z", "Z", ( "Z", ) ),
			( "lpe C<RD>[<L.>O]", None, ( "R", "G", "B" ) ),
			# Really we want the "rgb" suffixes to be capitalised to match
			# the EXR specification, but that's not what RenderMan does.
			# Gaffer's ImageReader will correct for it on loading though.
			( "lpe C<RD>[<L.>O]", "directDiffuse", ( "directDiffuse.r", "directDiffuse.g", "directDiffuse.b" ) ),
		]

		for i, output in enumerate( outputs ) :

			parameters = {}
			if output[1] is not None :
				parameters["layerName"] = output[1]

			renderer.output(
				f"test{i}",
				IECoreScene.Output(
					str( self.temporaryDirectory() / f"test{i}.exr" ),
					"exr",
					output[0],
					parameters
				)
			)

		renderer.render()
		del renderer

		for i, output in enumerate( outputs ) :
			with self.subTest( source = output[0], layerName = output[1] ) :
				image = OpenImageIO.ImageBuf( str( self.temporaryDirectory() / f"test{i}.exr" ) )
				self.assertEqual( image.spec().channelnames, output[2] )

	def testOutputAccumulationRule( self ) :

		renderer = GafferScene.Private.IECoreScenePreview.Renderer.create(
			"RenderMan",
			GafferScene.Private.IECoreScenePreview.Renderer.RenderType.Batch
		)

		renderer.option( "ri:hider:maxsamples", IECore.IntData( 8 ) )
		renderer.option( "ri:hider:minsamples", IECore.IntData( 8 ) )

		fileName = str( self.temporaryDirectory() / "test.exr" )
		renderer.output(
			"test",
			IECoreScene.Output(
				fileName,
				"exr",
				"float sampleCount",
				{
					"ri:accumulationRule" : "sum",
				},
			)
		)

		renderer.object(
			"sphere",
			IECoreScene.SpherePrimitive(),
			renderer.attributes( IECore.CompoundObject( {
				"ri:surface" : IECoreScene.ShaderNetwork(
					shaders = {
						"output" : IECoreScene.Shader( "PxrDiffuse" )
					},
					output = "output",
				)
			} ) )
		).transform( imath.M44f().translate( imath.V3f( 0, 0, -3 ) ) )

		renderer.render()
		del renderer

		image = OpenImageIO.ImageBuf( fileName )
		self.assertEqual( image.getpixel( 320, 240, 0 ), ( 8.0, ) )

	def testEXRHeaderMetadata( self ) :

		renderer = GafferScene.Private.IECoreScenePreview.Renderer.create(
			"RenderMan",
			GafferScene.Private.IECoreScenePreview.Renderer.RenderType.Batch
		)

		fileName = str( self.temporaryDirectory() / "test.exr" )
		renderer.output(
			"test",
			IECoreScene.Output(
				fileName,
				"exr",
				"rgba",
				{
					"header:testInt" : 1,
					"header:testFloat" : 2.0,
					"header:testString" : "foo",
					"header:testBool" : True,
					"header:testV2i" : imath.V2i( 1, 2 ),
				},
			)
		)

		renderer.render()
		del renderer

		image = OpenImageIO.ImageBuf( fileName )
		self.assertEqual( image.spec().get_int_attribute( "testInt" ), 1 )
		self.assertEqual( image.spec().get_float_attribute( "testFloat" ), 2.0 )
		self.assertEqual( image.spec().get_string_attribute( "testString" ), "foo" )
		self.assertEqual( image.spec().get_int_attribute( "testBool" ), 1 )
		self.assertEqual( image.spec().getattribute( "testV2i" ), ( 1, 2 ) )

	def testUserAttribute( self ) :

		renderer = GafferScene.Private.IECoreScenePreview.Renderer.create(
			"RenderMan",
			GafferScene.Private.IECoreScenePreview.Renderer.RenderType.Batch
		)

		fileName = str( self.temporaryDirectory() / "test.exr" )
		renderer.output(
			"test",
			IECoreScene.Output(
				fileName,
				"exr",
				"rgba",
				{
				},
			)
		)

		renderer.object(
			"sphere",
			IECoreScene.SpherePrimitive(),
			renderer.attributes( IECore.CompoundObject( {
				"ri:surface" : IECoreScene.ShaderNetwork(
					shaders = {
						"attribute" : IECoreScene.Shader(
							"PxrAttribute", "osl:shader", {
								"varname" : "user:myColor",
								"type" : "color",
							}
						),
						"output" : IECoreScene.Shader( "PxrConstant" ),
					},
					connections = [
						( ( "attribute", "resultRGB" ), ( "output", "emitColor" ) )
					],
					output = "output",
				),
				"user:myColor" : IECore.Color3fData( imath.Color3f( 1, 0.5, 0.25 ) ),
			} ) )
		).transform( imath.M44f().translate( imath.V3f( 0, 0, -3 ) ) )

		renderer.render()
		del renderer

		image = OpenImageIO.ImageBuf( fileName )
		self.assertEqual( image.getpixel( 320, 240, 0 ), ( 1.0, 0.5, 0.25, 1.0 ) )

	def testPortalLight( self ) :

		# Render with a dome light on its own.

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
			renderer.attributes( IECore.CompoundObject( {
				"ri:surface" : IECoreScene.ShaderNetwork(
					shaders = {
						"output" : IECoreScene.Shader( "PxrDiffuse" )
					},
					output = "output",
				)
			} ) )
		)
		sphere.transform( imath.M44f().translate( imath.V3f( 0, 0, -2 ) ) )

		dome = renderer.light(
			"dome",
			None,
			renderer.attributes( IECore.CompoundObject( {
				"ri:light" : IECoreScene.ShaderNetwork(
					shaders = {
						"output" : IECoreScene.Shader( "PxrDomeLight", "ri:light", { "exposure" : 2.0 } ),
					},
					output = "output",
				),
				"ri:visibility:camera" : IECore.BoolData( False ),
			} ) )
		)

		renderer.render()
		time.sleep( 1 )
		renderer.pause()

		# We should be illuminating the whole sphere.

		image = IECoreImage.ImageDisplayDriver.storedImage( "myLovelySphere" )
		self.assertGreater( self.__colorAtUV( image, imath.V2f( 0.3, 0.5 ) )[0], 0.5 )
		self.assertGreater( self.__colorAtUV( image, imath.V2f( 0.6, 0.5 ) )[0], 0.5 )

		# Add a portal light.

		portal = renderer.light(
			"portal",
			None,
			renderer.attributes( IECore.CompoundObject( {
				"ri:light" : IECoreScene.ShaderNetwork(
					shaders = {
						"output" : IECoreScene.Shader( "PxrPortalLight", "ri:light", { "exposure" : 4.0 } ),
					},
					output = "output",
				),
				"ri:visibility:camera" : IECore.BoolData( False ),
			} ) )
		)
		portal.transform( imath.M44f().translate( imath.V3f( 1, 0, -1 ) ).rotate( imath.V3f( 0, math.pi / 2, 0 ) ) )

		renderer.render()
		time.sleep( 1 )
		renderer.pause()

		# We should only be illuminating the side the portal is on.

		image = IECoreImage.ImageDisplayDriver.storedImage( "myLovelySphere" )
		self.assertEqual( self.__colorAtUV( image, imath.V2f( 0.3, 0.5 ) )[0], 0 )
		self.assertGreater( self.__colorAtUV( image, imath.V2f( 0.6, 0.5 ) )[0], 0.5 )

		# Delete the portal light. We should be back to illuminating
		# on both sides.

		del portal
		renderer.render()
		time.sleep( 1 )
		renderer.pause()

		image = IECoreImage.ImageDisplayDriver.storedImage( "myLovelySphere" )
		self.assertGreater( self.__colorAtUV( image, imath.V2f( 0.4, 0.5 ) )[0], 0.5 )
		self.assertGreater( self.__colorAtUV( image, imath.V2f( 0.6, 0.5 ) )[0], 0.5 )

		# Recreate the portal light. We should only be illuminating on
		# one side again.

		portal = renderer.light(
			"portal",
			None,
			renderer.attributes( IECore.CompoundObject( {
				"ri:light" : IECoreScene.ShaderNetwork(
					shaders = {
						"output" : IECoreScene.Shader( "PxrPortalLight", "ri:light", { "exposure" : 4.0 } ),
					},
					output = "output",
				),
				"ri:visibility:camera" : IECore.BoolData( False ),
			} ) )
		)
		portal.transform( imath.M44f().translate( imath.V3f( 1, 0, -1 ) ).rotate( imath.V3f( 0, math.pi / 2, 0 ) ) )

		renderer.render()
		time.sleep( 1 )
		renderer.pause()

		image = IECoreImage.ImageDisplayDriver.storedImage( "myLovelySphere" )
		self.assertEqual( self.__colorAtUV( image, imath.V2f( 0.3, 0.5 ) )[0], 0 )
		self.assertGreater( self.__colorAtUV( image, imath.V2f( 0.6, 0.5 ) )[0], 0.5 )

		# Now delete the dome light. We should get no illumination at all.

		del dome
		renderer.render()
		time.sleep( 1 )
		renderer.pause()

		image = IECoreImage.ImageDisplayDriver.storedImage( "myLovelySphere" )
		self.assertEqual( self.__colorAtUV( image, imath.V2f( 0.3, 0.5 ) )[0], 0 )
		self.assertEqual( self.__colorAtUV( image, imath.V2f( 0.6, 0.5 ) )[0], 0 )

		del sphere, portal
		del renderer

	def __colorAtUV( self, image, uv ) :

		dimensions = image.dataWindow.size() + imath.V2i( 1 )

		ix = int( uv.x * ( dimensions.x - 1 ) )
		iy = int( uv.y * ( dimensions.y - 1 ) )
		i = iy * dimensions.x + ix

		return imath.Color4f( image["R"][i], image["G"][i], image["B"][i], image["A"][i] if "A" in image.keys() else 0.0 )

if __name__ == "__main__":
	unittest.main()
