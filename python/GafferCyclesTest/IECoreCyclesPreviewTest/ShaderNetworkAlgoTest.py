##########################################################################
#
#  Copyright (c) 2022, Alex Fuller. All rights reserved.
#  Copyright (c) 2022, Image Engine Design Inc. All rights reserved.
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
#      * Neither the name of Alex Fuller nor the names of
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
import math


import imath

import IECore
import IECoreScene
import IECoreCycles

class ShaderNetworkAlgoTest( unittest.TestCase ) :

	def testConvertUSDPreviewSurfaceEmission( self ) :

		for emissiveColor in ( imath.Color3f( 1 ), imath.Color3f( 0 ), None ) :

			parameters = {}
			if emissiveColor is not None :
				parameters["emissiveColor"] = IECore.Color3fData( emissiveColor )

			network = IECoreScene.ShaderNetwork(
				shaders = {
					"previewSurface" : IECoreScene.Shader(
						"UsdPreviewSurface", "surface", parameters
					)
				},
				output = "previewSurface",
			)

			convertedNetwork = network.copy()
			IECoreCycles.ShaderNetworkAlgo.convertUSDShaders( convertedNetwork, False )

			convertedShader = convertedNetwork.getShader( "previewSurface" )
			self.assertEqual( convertedShader.name, "principled_bsdf" )
			self.assertEqual(
				convertedShader.parameters["emission"].value,
				emissiveColor if emissiveColor is not None else imath.Color3f( 0 )
			)

			if emissiveColor is not None and emissiveColor != imath.Color3f( 0 ) :
				self.assertEqual( convertedShader.parameters["emission_strength"], IECore.FloatData( 1 ) )
			else :
				self.assertEqual( convertedShader.parameters["emission_strength"], IECore.FloatData( 0 ) )

			# Repeat, but with an input connection as well as the parameter value

			network.addShader( "texture", IECoreScene.Shader( "UsdUVTexture" ) )
			network.addConnection( ( ( "texture", "rgb" ), ( "previewSurface", "emissiveColor" ) ) )

			convertedNetwork = network.copy()
			IECoreCycles.ShaderNetworkAlgo.convertUSDShaders( convertedNetwork, False )

			convertedShader = convertedNetwork.getShader( "previewSurface" )
			self.assertEqual( convertedShader.name, "principled_bsdf" )

			self.assertEqual(
				convertedNetwork.input( ( "previewSurface", "emission" ) ),
				( "texture", "color" ),
			)
			self.assertEqual( convertedShader.parameters["emission_strength"], IECore.FloatData( 1 ) )

	def testConvertUSDFloat3ToColor3f( self ) :

		# Although UsdPreviewSurface parameters are defined to be `color3f` in the spec,
		# some USD files seem to provide `float3` values instead. For example :
		#
		# https://github.com/usd-wg/assets/blob/64ebce19c9a6c795862548066bc1070bf0f7f955/test_assets/AlphaBlendModeTest/AlphaBlendModeTest.usd#L27
		#
		# Make sure that we convert these to colours for consumption by Cycles.

		network = IECoreScene.ShaderNetwork(
			shaders = {
				"previewSurface" : IECoreScene.Shader(
					"UsdPreviewSurface", "surface",
					{
						"diffuseColor" : imath.V3f( 0, 0.25, 0.5 ),
					}
				)
			},
			output = "previewSurface",
		)

		IECoreCycles.ShaderNetworkAlgo.convertUSDShaders( network, False )
		self.assertEqual(
			network.getShader( "previewSurface" ).parameters["base_color"],
			IECore.Color3fData( imath.Color3f( 0, 0.25, 0.5 ) )
		)

	def testConvertUSDOpacity( self ) :

		# The results of this type of conversion may also be verified visually using
		# the AlphaBlendModeTest asset found here :
		#
		# https://github.com/usd-wg/assets/tree/main/test_assets/AlphaBlendModeTest

		for opacity in ( 0.25, 1.0, None ) :
			for opacityThreshold in ( 0.0, 0.5, None ) :

				parameters = {}
				if opacity is not None :
					parameters["opacity"] = IECore.FloatData( opacity )
				if opacityThreshold is not None :
					parameters["opacityThreshold"] = IECore.FloatData( opacityThreshold )

				network = IECoreScene.ShaderNetwork(
					shaders = {
						"previewSurface" : IECoreScene.Shader(
							"UsdPreviewSurface", "surface", parameters
						)
					},
					output = "previewSurface",
				)

				convertedNetwork = network.copy()
				IECoreCycles.ShaderNetworkAlgo.convertUSDShaders( convertedNetwork, False )

				convertedShader = convertedNetwork.getShader( "previewSurface" )
				expectedOpacity = opacity if opacity is not None else 1.0
				if opacityThreshold is not None :
					expectedOpacity = expectedOpacity if expectedOpacity > opacityThreshold else 0

				self.assertEqual(
					convertedShader.parameters["alpha"].value,
					expectedOpacity
				)

				# Repeat, but with an input connection as well as the parameter value

				network.addShader( "texture", IECoreScene.Shader( "UsdUVTexture" ) )
				network.addConnection( ( ( "texture", "a" ), ( "previewSurface", "opacity" ) ) )

				convertedNetwork = network.copy()
				IECoreCycles.ShaderNetworkAlgo.convertUSDShaders( convertedNetwork, False )

				if opacityThreshold :

					self.assertEqual( len( convertedNetwork ), 4 )
					opacityInput = convertedNetwork.input( ( "previewSurface", "alpha" ) )
					self.assertEqual( opacityInput, ( "previewSurfaceOpacityMultiply", "value" ) )
					self.assertEqual( convertedNetwork.getShader( opacityInput.shader ).name, "math" )
					self.assertEqual(
						convertedNetwork.input( ( "previewSurfaceOpacityMultiply", "value1" ) ),
						( "texture", "alpha" )
					)
					self.assertEqual(
						convertedNetwork.input( ( "previewSurfaceOpacityMultiply", "value2" ) ),
						( "previewSurfaceOpacityCompare", "value" )
					)

					self.assertEqual(
						convertedNetwork.input( ( "previewSurfaceOpacityCompare", "value1" ) ),
						( "texture", "alpha" )
					)

					compareShader = convertedNetwork.getShader( "previewSurfaceOpacityCompare" )
					self.assertEqual( compareShader.parameters["math_type"].value, "greater_than" )
					self.assertEqual( compareShader.parameters["value2"].value, opacityThreshold )

				else :

					self.assertEqual( len( convertedNetwork ), 2 )
					self.assertEqual(
						convertedNetwork.input( ( "previewSurface", "alpha" ) ),
						( "texture", "alpha" )
					)

	def testConvertUSDSpecular( self ) :

		for useSpecularWorkflow in ( True, False ) :

			network = IECoreScene.ShaderNetwork(
				shaders = {
					"previewSurface" : IECoreScene.Shader(
						"UsdPreviewSurface", "surface",
						{
							"specularColor" : imath.V3f( 1, 0.25, 0.5 ),
							"metallic" : 0.5,
							"useSpecularWorkflow" : int( useSpecularWorkflow ),
						}
					)
				},
				output = "previewSurface",
			)

			convertedNetwork = network.copy()
			IECoreCycles.ShaderNetworkAlgo.convertUSDShaders( convertedNetwork, False )
			convertedShader = convertedNetwork.getShader( "previewSurface" )

            # Specular in the principled_bsdf is a float and not color, a workaround
            # for now is to plug the red channel into specular.
            # todo: revisit when principled v2 comes out.

			if useSpecularWorkflow :
				self.assertEqual( convertedShader.parameters["specular"].value, 1 )
				self.assertNotIn( "metallic", convertedShader.parameters )
			else :
				self.assertEqual( convertedShader.parameters["metallic"].value, 0.5 )
				self.assertNotIn( "specular", convertedShader.parameters )

			# Repeat with input connections

			network.addShader( "texture", IECoreScene.Shader( "UsdUVTexture" ) )
			network.addConnection( ( ( "texture", "rgb" ), ( "previewSurface", "specularColor" ) ) )
			network.addConnection( ( ( "texture", "r" ), ( "previewSurface", "metallic" ) ) )

			convertedNetwork = network.copy()
			IECoreCycles.ShaderNetworkAlgo.convertUSDShaders( convertedNetwork, False )

			if useSpecularWorkflow :
				self.assertEqual(
					convertedNetwork.input( ( "previewSurface", "specular" ) ),
					( "texture", "color.r" ),
				)
				self.assertFalse( convertedNetwork.input( ( "previewSurface", "metallic" ) ) )
			else :
				self.assertEqual(
					convertedNetwork.input( ( "previewSurface", "metallic" ) ),
					( "texture", "color.r" ),
				)
				self.assertFalse( convertedNetwork.input( ( "previewSurface", "specular" ) ) )

			self.assertFalse( convertedNetwork.input( ( "previewSurface", "specularColor" ) ) )

	def testConvertUSDClearcoat( self ) :

		network = IECoreScene.ShaderNetwork(
			shaders = {
				"previewSurface" : IECoreScene.Shader(
					"UsdPreviewSurface", "surface",
					{
						"clearcoat" : 0.75,
						"clearcoatRoughness" : 0.25,
					}
				)
			},
			output = "previewSurface",
		)

		convertedNetwork = network.copy()
		IECoreCycles.ShaderNetworkAlgo.convertUSDShaders( convertedNetwork, False )
		convertedShader = convertedNetwork.getShader( "previewSurface" )

		self.assertEqual( convertedShader.parameters["clearcoat"].value, 0.75 )
		self.assertEqual( convertedShader.parameters["clearcoat_roughness"].value, 0.25 )

		# Repeat with input connections

		network.addShader( "texture", IECoreScene.Shader( "UsdUVTexture" ) )
		network.addConnection( ( ( "texture", "r" ), ( "previewSurface", "clearcoat" ) ) )
		network.addConnection( ( ( "texture", "g" ), ( "previewSurface", "clearcoatRoughness" ) ) )

		convertedNetwork = network.copy()
		IECoreCycles.ShaderNetworkAlgo.convertUSDShaders( convertedNetwork, False )

		self.assertEqual(
			convertedNetwork.input( ( "previewSurface", "clearcoat" ) ),
			( "texture", "color.r" ),
		)
		self.assertEqual(
			convertedNetwork.input( ( "previewSurface", "clearcoat_roughness" ) ),
			( "texture", "color.g" ),
		)

	def testConvertSimpleUSDUVTexture( self ) :

		for uvPrimvar in ( "st", "customUV" ) :

			network = IECoreScene.ShaderNetwork(
				shaders = {
					"previewSurface" : IECoreScene.Shader( "UsdPreviewSurface" ),
					"texture" : IECoreScene.Shader(
						"UsdUVTexture", "shader",
						{
							"file" : "test.png",
							"wrapS" : "repeat",
							"wrapT" : "repeat",
							"sourceColorSpace" : "auto",
						}
					),
					"uvReader" : IECoreScene.Shader(
						"UsdPrimvarReader_float2", "shader",
						{
							"varname" : uvPrimvar,
						}
					),
				},
				connections = [
					( ( "uvReader", "result" ), ( "texture", "st" ) ),
					( ( "texture", "rgb" ), ( "previewSurface", "diffuseColor" ) ),
				],
				output = "previewSurface",
			)

			IECoreCycles.ShaderNetworkAlgo.convertUSDShaders( network, False )

			self.assertEqual( network.input( ( "previewSurface", "base_color" ) ), ( "texture", "color" ) )

			texture = network.getShader( "texture" )
			self.assertEqual( texture.name, "image_texture" )
			self.assertEqual( texture.parameters["filename"].value, "test.png" )
			self.assertEqual( texture.parameters["colorspace"].value, "auto" )
			self.assertEqual( texture.parameters["extension"].value, "periodic" )


			uvReader = network.getShader( "uvReader" )
			self.assertEqual( uvReader.name, "uvmap" )
			self.assertEqual( uvReader.parameters["attribute"].value, uvPrimvar if uvPrimvar != "st" else "uv" )

	def testConvertTransformedUSDUVTexture( self ) :

		# The results of this type of conversion may also be verified visually using
		# the TextureTransformTest asset found here :
		#
		# https://github.com/usd-wg/assets/tree/main/test_assets/TextureTransformTest

		network = IECoreScene.ShaderNetwork(
			shaders = {
				"previewSurface" : IECoreScene.Shader( "UsdPreviewSurface" ),
				"texture" : IECoreScene.Shader(
					"UsdUVTexture", "shader",
					{
						"file" : "test.png",
						"wrapS" : "repeat",
						"wrapT" : "repeat",
						"sourceColorSpace" : "auto",
					}
				),
				"transform" : IECoreScene.Shader(
					"UsdTransform2d", "shader",
					{
						"rotation" : 90.0,
					}
				),
				"uvReader" : IECoreScene.Shader(
					"UsdPrimvarReader_float2", "shader",
					{
						"varname" : "st",
					}
				),
			},
			connections = [
				( ( "uvReader", "result" ), ( "transform", "in" ) ),
				( ( "transform", "result" ), ( "texture", "st" ) ),
				( ( "texture", "rgb" ), ( "previewSurface", "diffuseColor" ) ),
			],
			output = "previewSurface",
		)

		convertedNetwork = network.copy()
		IECoreCycles.ShaderNetworkAlgo.convertUSDShaders( convertedNetwork, False )

		texture = convertedNetwork.getShader( "texture" )
		self.assertEqual( texture.name, "image_texture" )
		self.assertEqual( texture.parameters["filename"].value, "test.png" )
		self.assertEqual( texture.parameters["colorspace"].value, "auto" )
		self.assertEqual( texture.parameters["extension"].value, "periodic" )

		uvReader = convertedNetwork.getShader( "uvReader" )
		self.assertEqual( uvReader.name, "uvmap" )

		transform = convertedNetwork.getShader( "transform" )
		self.assertEqual( transform.name, "mapping" )
		self.assertEqual( transform.parameters["rotation"].value, imath.V3f( 0, 0, math.radians( 90 ) ) )

		self.assertEqual( convertedNetwork.input( ( "transform", "vector" ) ), ( "uvReader", "UV" ) )
		self.assertEqual( convertedNetwork.input( ( "texture", "vector" ) ), ( "transform", "vector" ) )
		self.assertEqual( convertedNetwork.input( ( "previewSurface", "base_color" ) ), ( "texture", "color" ) )

	def testConvertUSDPrimvarReader( self ) :

        # Cycles hasn't got a fallback value

		for usdDataType, cyclesShaderType in [
			( "float", "attribute" ),
			( "float2", "uvmap" ),
			( "float3", "attribute" ),
			( "normal", "attribute" ),
			( "point", "attribute" ),
			( "vector", "attribute" ),
			( "float4", "attribute" ),
			( "int", "attribute" ),
		] :

			network = IECoreScene.ShaderNetwork(
				shaders = {
					"reader" : IECoreScene.Shader(
						"UsdPrimvarReader_{}".format( usdDataType ), "shader",
						{
							"varname" : "test",
						}
					),
				},
				output = "reader",
			)

			IECoreCycles.ShaderNetworkAlgo.convertUSDShaders( network, False )

			reader = network.getShader( "reader" )
			self.assertEqual( reader.name, cyclesShaderType )
			self.assertEqual( len( reader.parameters ), 1 )
			self.assertEqual( reader.parameters["attribute"].value, "test" )

	def testConvertUSDSphereLight( self ) :

		network = IECoreScene.ShaderNetwork(
			shaders = {
				"light" : IECoreScene.Shader(
					"SphereLight", "light",
					{
						"exposure" : 1.0,
						"intensity" : 2.0,
						"color" : imath.Color3f( 1, 2, 3 ),
						"normalize" : True,
						"radius" : 0.25,
						"unknown" : "unknown",
					}
				),
			},
			output = "light",
		)

		IECoreCycles.ShaderNetworkAlgo.convertUSDShaders( network, False )

		light = network.getShader( "light" )
		self.assertEqual( light.name, "point_light" )
		self.assertEqual( light.parameters["exposure"].value, 1.0 )
		self.assertEqual( light.parameters["intensity"].value, 2.0 )
		self.assertEqual( light.parameters["color"].value, imath.Color3f( 1, 2, 3 ) )
		self.assertEqual( light.parameters["normalize"].value, True )
		self.assertEqual( light.parameters["size"].value, 0.25 )
		self.assertNotIn( "unknown", light.parameters )

	## \todo Register this via `unittest.addTypeEqualityFunc()`, once we've
	# ditched Python 2.
	def __assertShadersEqual( self, shader1, shader2, message = None ) :

		self.assertEqual( shader1.name, shader2.name, message )
		self.assertEqual( shader1.parameters.keys(), shader2.parameters.keys(), message )
		for k in shader1.parameters.keys() :
			self.assertEqual(
				shader1.parameters[k], shader2.parameters[k],
				"{}(Parameter = {})".format( message or "", k )
			)

	def testConvertUSDLights( self ) :

		def expectedLightParameters( parameters ) :

			# Start with defaults
			result = {
				"intensity" : 1.0,
				"exposure" : 0.0,
				"color" : imath.Color3f( 1, 1, 1 ),
				"normalize" : False,
			}
			result.update( parameters )
			return result

		for testName, shaders in {

			# Basic SphereLight -> point_light conversion

			"sphereLightToPointLight" : [

				IECoreScene.Shader(
					"SphereLight", "light",
					{
						"intensity" : 2.5,
						"exposure" : 1.1,
						"color" : imath.Color3f( 1, 2, 3 ),
						"normalize" : True,
						"radius" : 1.0,
					}
				),

				IECoreScene.Shader(
					"point_light", "light",
					expectedLightParameters( {
						"intensity" : 2.5,
						"exposure" : 1.1,
						"color" : imath.Color3f( 1, 2, 3 ),
						"normalize" : True,
						"size" : 1.0,
					} )
				),

			],

			# Basic SphereLight -> point_light conversion, testing default values

			"defaultParameters" : [

				IECoreScene.Shader( "SphereLight", "light", {} ),

				IECoreScene.Shader(
					"point_light", "light",
					expectedLightParameters( {
						"size" : 0.5,
					} )
				),

			],

			# SphereLight with `treatAsPoint = true`. We must normalize these
			# otherwise we lose all the energy in Arnold.

			"treatAsPoint" : [

				IECoreScene.Shader(
					"SphereLight", "light",
					{
						"treatAsPoint" : True,
					}
				),

				IECoreScene.Shader(
					"point_light", "light",
					expectedLightParameters( {
						"size" : 0.0,
						"normalize" : True,
					} )
				),

			],

			# SphereLight (with shaping) -> spot_light conversion

			"sphereLightToSpotLight" : [

				IECoreScene.Shader(
					"SphereLight", "light",
					{
						"shaping:cone:angle" : 20.0,
						"shaping:cone:softness" : 0.5,
					}
				),

				IECoreScene.Shader(
					"spot_light", "light",
					expectedLightParameters( {
						"spot_angle" : 40.0,
						"spot_smooth" : 0.5,
						"size" : 0.5,
					} )
				),

			],

			# SphereLight (with bogus out-of-range Houdini softness)

			"houdiniPenumbra" : [

				IECoreScene.Shader(
					"SphereLight", "light",
					{
						"shaping:cone:angle" : 20.0,
						"shaping:cone:softness" : 60.0,
					}
				),

				IECoreScene.Shader(
					"spot_light", "light",
					expectedLightParameters( {
						"spot_angle" : 40.0,
						"size" : 0.5,
					} )
				),

			],

			# RectLight -> quad_light

			"rectLight" : [

				IECoreScene.Shader(
					"RectLight", "light",
					{
						"width" : 20.0,
						"height" : 60.0,
					}
				),

				IECoreScene.Shader(
					"quad_light", "light",
					expectedLightParameters( {
						"sizeu" : 40.0,
                        "sizev" : 120.0,
					} )
				),

			],

			# DistantLight -> distant_light

			"distantLight" : [

				IECoreScene.Shader(
					"DistantLight", "light",
					{
						"angle" : 1.0,
					}
				),

				IECoreScene.Shader(
					"distant_light", "light",
					expectedLightParameters( {
						"angle" : 1.0
					} )
				),

			],


		}.items() :

			network = IECoreScene.ShaderNetwork(
				shaders = {
					"light" : shaders[0],
				},
				output = "light",
			)

			IECoreCycles.ShaderNetworkAlgo.convertUSDShaders( network, False )

			light = network.getShader( "light" )
			self.__assertShadersEqual( network.getShader( "light" ), shaders[1], "Testing {}".format( testName ) )

	def testConvertUSDRectLightTexture( self ) :

		network = IECoreScene.ShaderNetwork(
			shaders = {
				"light" : IECoreScene.Shader(
					"RectLight", "light",
					{
						"texture:file" : "myFile.tx"
					}
				)
			},
			output = "light"
		)

		IECoreCycles.ShaderNetworkAlgo.convertUSDShaders( network, False )

		output = network.outputShader()
		self.assertEqual( output.name, "quad_light" )

		colorInput = network.input( ( "light", "color" ) )
		texture = network.getShader( colorInput.shader )
		self.assertEqual( texture.name, "image_texture" )
		self.assertEqual( texture.parameters["filename"].value, "myFile.tx" )
		textureInput = network.input( ( colorInput.shader, "vector" ) )
		self.assertEqual( textureInput.name, "parametric" )
		geometry = network.getShader( textureInput.shader )
		self.assertEqual( geometry.name, "geometry" )

	def testConvertUSDDomeLightTexture( self ) :

		network = IECoreScene.ShaderNetwork(
			shaders = {
				"light" : IECoreScene.Shader(
					"DomeLight", "light",
					{
						"texture:file" : "myFile.tx",
						"texture:format" : "mirroredBall",
					}
				)
			},
			output = "light"
		)

		IECoreCycles.ShaderNetworkAlgo.convertUSDShaders( network, False )

		output = network.outputShader()
		self.assertEqual( output.name, "background_light" )

		colorInput = network.input( ( "light", "color" ) )
		texture = network.getShader( colorInput.shader )
		self.assertEqual( texture.name, "environment_texture" )
		self.assertEqual( texture.parameters["filename"].value, "myFile.tx" )
		self.assertEqual( texture.parameters["projection"].value, "mirror_ball" )

	def testConvertUSDRectLightTextureWithColor( self ) :

		network = IECoreScene.ShaderNetwork(
			shaders = {
				"light" : IECoreScene.Shader(
					"RectLight", "light",
					{
						"texture:file" : "myFile.tx",
						"color" : imath.Color3f( 1, 2, 3 ),
					}
				)
			},
			output = "light"
		)

		IECoreCycles.ShaderNetworkAlgo.convertUSDShaders( network, False )

		output = network.outputShader()
		self.assertEqual( output.name, "quad_light" )

		# When using a colour and a texture, we need to multiply
		# them together using a shader in Arnold.

		colorInput = network.input( ( "light", "color" ) )
		colorInputShader = network.getShader( colorInput.shader )
		self.assertEqual( colorInputShader.name, "vector_math" )
		self.assertEqual( colorInputShader.parameters["vector2"].value, imath.Color3f( 1, 2, 3 ) )

		colorInput1 = network.input( ( colorInput.shader, "vector1" ) )
		texture = network.getShader( colorInput1.shader )
		self.assertEqual( texture.name, "image_texture" )
		self.assertEqual( texture.parameters["filename"].value, "myFile.tx" )

if __name__ == "__main__":
	unittest.main()
