##########################################################################
#
#  Copyright (c) 2026, Alex Fuller. All rights reserved.
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

menuShaders = {

	"adjustment" : [

		"colorcorrect",
		"contrast",
		"hsvadjust",
		"hsvtorgb",
		"luminance",
		"range",
		"remap",
		"rgbtohsv",
		"saturate",
		"smoothstep",

	],

	"application" : [

		"frame",
		"time",

	],

	"channel" : [

		"combine2",
		"combine3",
		"combine4",
		"convert",
		"extract",

	],

	"colortransform" : [

		"acescg_to_lin_rec709",
		"adobergb_to_lin_rec709",
		"g18_rec709_to_lin_rec709",
		"g22_ap1_to_lin_rec709",
		"g22_rec709_to_lin_rec709",
		"lin_adobergb_to_lin_rec709",
		"lin_displayp3_to_lin_rec709",
		"rec709_display_to_lin_rec709",
		"srgb_displayp3_to_lin_rec709",
		"srgb_texture_to_lin_rec709",

	],

	"compositing" : [

		"burn",
		"difference",
		"disjointover",
		"dodge",
		"in",
		"inside",
		"mask",
		"matte",
		"minus",
		"mix",
		"out",
		"outside",
		"over",
		"overlay",
		"plus",
		"premult",
		"screen",
		"unpremult",

	],

	"conditional" : [

		"ifequal",
		"ifgreater",
		"ifgreatereq",
		"switch",

	],

	"convolution2d" : [

		"blur",
		"heighttonormal",

	],

	"geometric" : [

		"bitangent",
		"bump",
		"geomcolor",
		"geompropvalue",
		"normal",
		"position",
		"tangent",
		"texcoord",

	],

	"math" : [

		"clamp",
		"crossproduct",
		"determinant",
		"distance",
		"dotproduct",
		"invert",
		"invertmatrix",
		"magnitude",
		"normalize",
		"normalmap",
		"absval",
		"acos",
		"asin",
		"ceil",
		"cos",
		"exp",
		"floor",
		"in",
		"round",
		"sign",
		"sin",
		"sqrt",
		"tan",
		"place2d",
		"reflect",
		"refract",
		"rotate2d",
		"rotate3d",
		"swizzle",
		"transformnormal",
		"transformpoint",
		"transformvector",
		"transformmatrix",
		"transpose",
		"add",
		"atan2",
		"divide",
		"max",
		"min",
		"modulo",
		"multiply",
		"power",
		"safepower",
		"subtract",

	],

	"pbr" : [

		"glossiness_anisotropy",
		"roughness_anisotropy",
		"roughness_dual",
		"lama_add",
		"lama_conductor",
		"lama_dielectric",
		"lama_diffuse",
		"lama_emission",
		"lama_generalized_schlick",
		"lama_iridescence",
		"lama_layer",
		"lama_mix",
		"lama_sss",
		"lama_sheen",
		"lama_surface",
		"lama_translucent",
		"open_pbr_surface",
		"standard_surface",

	],

	"procedural" : [

		"constant",
		"randomfloat",

	],

	"procedural2d" : [

		"cellnoise2d",
		"checkerboard",
		"circle",
		"cloverleaf",
		"crosshatch",
		"fractal3d",
		"grid",
		"hexagon",
		"line",
		"noise2d",
		"ramp4",
		"ramplr",
		"ramptb",
		"splitlr",
		"splittb",
		"tiledcircles",
		"tiledcloverleafs",
		"tiledhexagons",
		"trianglewave",
		"unifiednoise2d",
		"worleynoise2d",

	],

	"procedural3d" : [

		"cellnoise3d",
		"noise3d",
		"randomcolor",
		"unifiednoise3d",
		"worleynoise3d",

	],

	"texture" : [

		"hextiledimage",
		"hextilednormalmap",
		"image",
		"tiledimage",

	],

}

labelNames = {

	"roles" : {

		"adjustment" : "Adjustment",
		"application" : "Application",
		"channel" : "Channel",
		"colortransform" : "Color Transform",
		"compositing" : "Compositing",
		"conditional" : "Conditional",
		"convolution2d" : "Convolution 2D",
		"geometric" : "Geometric",
		"global" : "Global",
		"math" : "Math",
		"pbr" : "Physically-Based Rendering",
		"procedural" : "Procedural",
		"procedural2d" : "Procedural 2D",
		"procedural3d" : "Procedural 3D",
		"texture" : "Texture",

	},

	"labels" : {

		"open_pbr_surface" : "OpenPBR Surface",
		"standard_surface" : "Standard Surface",
		"lama_add" : "Lama Add",
		"lama_conductor" : "Lama Conductor",
		"lama_dielectric" : "Lama Dielectric",
		"lama_diffuse" : "Lama Diffuse",
		"lama_emission" : "Lama Emission",
		"lama_generalized_schlick" : "Lama Generalized Schlick",
		"lama_iridescence" : "Lama Iridescence",
		"lama_layer" : "Lama Layer",
		"lama_mix" : "Lama Mix",
		"lama_sss" : "Lama SSS",
		"lama_sheen" : "Lama Sheen",
		"lama_surface" : "Lama Surface",
		"lama_translucent" : "Lama Translucent",

		"colorcorrect" : "Color Correct",
		"hsvadjust" : "HSV Adjust",
		"hsvtorgb" : "HSV to RGB",
		"rgbtohsv" : "RGB to HSV",
		"smoothstep" : "Smooth Step",

		"combine2" : "Combine 2",
		"combine3": "Combine 3",
		"combine4" : "Combine 4",

		"acescg_to_lin_rec709" : "AcesCG to Linear Rec709",
		"adobergb_to_lin_rec709" : "AdobeRGB To Linear Rec709",
		"g18_rec709_to_lin_rec709" : "Gamma18 Rec709 To Linear Rec709",
		"g22_ap1_to_lin_rec709" : "Gamma22 AP1 To Linear Rec709",
		"g22_rec709_to_lin_rec709" : "Gamma22 Rec709 To Linear Rec709",
		"lin_adobergb_to_lin_rec709" : "Linear AdobeRGB To Linear Rec709",
		"lin_displayp3_to_lin_rec709" : "Linear DisplayP3 To Linear Rec709",
		"rec709_display_to_lin_rec709": "Rec709 Display To Linear Rec709",
		"srgb_displayp3_to_lin_rec709": "sRGB DisplayP3 To Linear Rec709",
		"srgb_texture_to_lin_rec709": "sRGB Texture To Linear Rec709",

		"disjointover" : "Disjoint Over",

		"ifequal": "If Equal",
		"ifgreater": "If Greater",
		"ifgreatereq": "If Greater Or Equal",

		"heighttonormal" : "Height To Normal",

		"bitangent" : "BiTangent",
		"geomcolor" : "Geometry Color",
		"geompropvalue" : "Geometry Property Value",
		"texcoord" : "Texture Coordinate",

		"crossproduct" : "Cross Product",
		"dotproduct" : "Dot Product",
		"invertmatrix" : "Invert Matrix",
		"normalmap" : "Normal Map",
		"absval" : "Absolute Value",
		"acos" : "Arc Cosine",
		"asin" : "Arc Sign",
		"cos" : "Cosine",
		"exp" : "Exponential",
		"sin" : "Sine",
		"sqrt" : "Square Root",
		"tan" : "Math Tangent",
		"place2d" : "Place 2D",
		"rotate2d" : "Rotate 2D",
		"rotate3d" : "Rotate 3D",
		"transformnormal" : "Transform Normal",
		"transformpoint" : "Transform Point",
		"transformvector" : "Trasnsform Vector",
		"transformmatrix" : "Transform Matrix",
		"atan2" : "Arc Tangent 2",
		"safepower" : "Safe Power",

		"glossiness_anisotropy" : "Glossiness Anisotropy",
		"roughness_anisotropy" : 'Roughness Anisotropy',
		"roughness_dual" : "Roughness Dual",

		"randomfloat" : "Random Float",

		"cellnoise2d" : "Cell Noise 2D",
		"cloverleaf" : "Clover Leaf",
		"crosshatch" : "Cross Hatch",
		"fractal3d" : "Fractal 3D",
		"noise2d" : "Noise 2D",
		"ramp4" : "Ramp 4 Directions",
		"ramplr" : "Ramp Left Right",
		"ramptb" : "Ramp Top Bottom",
		"splitlr" : "Split Left Right",
		"splittb" : "Split Top Bottom",
		"tiledcircles" : "Tiled Circles",
		"tiledcloverleafs" : "Tiled Clover Leafs",
		"tiledhexagons" : "Toled Hexagons",
		"trianglewave" : "Triangle Wave",
		"unifiednoise2d" : "Unified Noise 2D",
		"worleynoise2d" : "Worley Noise 2D",

		"cellnoise3d" : "Cell Noise 3D",
		"noise3d" : "Noise 3D",
		"randomcolor" : "Ramp Color",
		"unifiednoise3d" : "Unified Noise 3D",
		"worleynoise3d" : "Worley Noise 3D",

		"hextiledimage" : "Hex Tiled Image",
		"hextilednormalmap" : "Hex Tiled Normal Map",
		"tiledimage" : "Tiled Image",


	},

}

signatures = {

	"colorcorrect" : [ "color3", "color4" ],
	"contrast" : [
		"color3",
		"color3FA",
		"color4",
		"color4FA",
		"float",
		"vector2",
		"vector2FA",
		"vector3",
		"vector3FA",
		"vector4",
		"vector4FA"
	],
	"hsvadjust" : [ "color3", "color4" ],
	"hsvtorgb" : [ "color3", "color4" ],
	"luminance" : [ "color3", "color4" ],
	"range" : [
		"color3",
		"color3FA",
		"color4",
		"color4FA",
		"float",
		"vector2",
		"vector2FA",
		"vector3",
		"vector3FA",
		"vector4",
		"vector4FA"
	],
	"remap" : [
		"color3",
		"color3FA",
		"color4",
		"color4FA",
		"float",
		"vector2",
		"vector2FA",
		"vector3",
		"vector3FA",
		"vector4",
		"vector4FA"
	],
	"rgbtohsv" : [ "color3", "color4" ],
	"saturate" : [ "color3", "color4" ],
	"smoothstep" : [
		"color3",
		"color3FA",
		"color4",
		"color4FA",
		"float",
		"vector2",
		"vector2FA",
		"vector3",
		"vector3FA",
		"vector4",
		"vector4FA"
	],

	"frame" : [ "float" ],
	"time" : [ "float" ],

	"combine2" : [ "color4CF", "vector2", "vector4VF", "vector4VV" ],
	"combine3" : [ "color3", "vector3" ],
	"combine4" : [ "color4", "vector4" ],
	"convert" : [ 
		"boolean_float",
		"color3_color4",
		"color3_vector2",
		"color3_vector3",
		"color3_vector4",
		"color4_color3",
		"color4_vector2",
		"color4_vector3",
		"color4_vector4",
		"float_color3",
		"float_color4",
		"float_vector2",
		"float_vector3",
		"float_vector4",
		"integer_float",
		"vector2_color3",
		"vector2_color4",
		"vector2_vector3",
		"vector2_vector4",
		"vector3_color3",
		"vector3_color4",
		"vector3_vector2",
		"vector3_vector4",
		"vector4_color3",
		"vector4_color4",
		"vector4_vector2",
		"vector4_vector3",
	],
	"extract" : [ "color3", "color4", "vector2", "vector3", "vector4" ],

	"acescg_to_lin_rec709" : [ "color3", "color4" ],
	"adobergb_to_lin_rec709" : [ "color3", "color4" ],
	"g18_rec709_to_lin_rec709" : [ "color3", "color4" ],
	"g22_ap1_to_lin_rec709" : [ "color3", "color4" ],
	"g22_rec709_to_lin_rec709" : [ "color3", "color4" ],
	"lin_adobergb_to_lin_rec709" : [ "color3", "color4" ],
	"lin_displayp3_to_lin_rec709" : [ "color3", "color4" ],
	"rec709_display_to_lin_rec709" : [ "color3", "color4" ],
	"srgb_displayp3_to_lin_rec709" : [ "color3", "color4" ],
	"srgb_texture_to_lin_rec709" : [ "color3", "color4" ],

	"burn" : [ "color3", "color4", "float" ],
	"difference" : [ "color3", "color4", "float" ],
	"disjointover" : [ "color4" ],
	"dodge" : [ "color3", "color4", "float" ],
	"in" : [ "color4" ],
	"inside" : [ "color3", "color4", "float" ],
	"mask" : [ "color4" ],
	"matte" : [ "color4" ],
	"minus" : [ "color3", "color4", "float" ],
	"mix" : [
		"color3",
		"color3_color3",
		"color4",
		"color4_color4",
		"float",
		"vector2",
		"vector2_vector2",
		"vector3",
		"vector3_vector3",
		"vector4",
		"vector4_vector4"
	],
	"out" : [ "color4" ],
	"outside" : [ "color3", "color4", "float" ],
	"over" : [ "color4" ],
	"overlay" : [ "color3", "color4", "float" ],
	"plus" : [ "color3", "color4", "float" ],
	"premult" : [ "color4" ],
	"screen" : [ "color3", "color4", "float" ],
	"unpremult" : [ "color4" ],

	"ifequal" : [
		"color3",
		"color3B",
		"color3I",
		"color4",
		"color4B",
		"color4I",
		"float",
		"floatB",
		"floatI",
		"vector2",
		"vector2B",
		"vector2I",
		"vector3",
		"vector3B",
		"vector3I",
		"vector4",
		"vector4B",
		"vector4I",
	],
	"ifgreater" : [
		"color3",
		"color3I",
		"color4",
		"color4I",
		"float",
		"floatI",
		"vector2",
		"vector2I",
		"vector3",
		"vector3I",
		"vector4",
		"vector4I",
	],
	"ifgreatereq" : [
		"color3",
		"color3I",
		"color4",
		"color4I",
		"float",
		"floatI",
		"vector2",
		"vector2I",
		"vector3",
		"vector3I",
		"vector4",
		"vector4I",
	],
	"switch" : [
		"color3",
		"color3I",
		"color4",
		"color4I",
		"float",
		"floatI",
		"vector2",
		"vector2I",
		"vector3",
		"vector3I",
		"vector4",
		"vector4I",
	],

	"blur" : [ "color3", "color4", "float", "vector2", "vector3", "vector4" ],
	"heighttonormal" : [ "vector3" ],

	"bitangent" : [ "vector3" ],
	"bump" : [ "vector3" ],
	"geomcolor" : [ "color3", "color4", "float" ],
	"geompropvalue" : [
		"boolean",
		"color3",
		"color4",
		"float",
		"integer",
		"string",
		"vector2",
		"vector3",
		"vector4",
	],
	"normal" : [ "vector3" ],
	"position" : [ "vector3" ],
	"tangent" : [ "vector3" ],
	"texcoord" : [ "vector2", "vector3" ],

	"clamp" : [
		"color3",
		"color3FA",
		"color4",
		"color4FA",
		"float",
		"vector2",
		"vector2FA",
		"vector3",
		"vector3FA",
		"vector3",
		"vector3FA",
		"vector4",
		"vector4FA",
	],
	"crossproduct" : [ "vector3" ],
	"determinant" : [ "matrix33", "matrix44" ],
	"distance" : [ "vector2", "vector3", "vector4" ],
	"dotproduct" : [ "vector2", "vector3", "vector4" ],
	"invert" : [
		"color3",
		"color3FA",
		"color4",
		"color4FA",
		"float",
		"vector2",
		"vector2FA",
		"vector3",
		"vector3FA",
		"vector3",
		"vector3FA",
		"vector4",
		"vector4FA",
	],
	"invertmatrix" : [ "matrix33", "matrix44" ],
	"magnitude" : [ "vector2", "vector3", "vector4" ],
	"normalize" : [ "vector2", "vector3", "vector4" ],
	"normalmap" : [ "float", "vector2" ],
	"absval" : [ "color3", "color4", "float", "vector2", "vector3", "vector4" ],
	"acos" : [ "float", "vector2", "vector3", "vector4" ],
	"asin" : [ "float", "vector2", "vector3", "vector4" ],
	"ceil" : [ "color3", "color4", "float", "integer", "vector2", "vector3", "vector4" ],
	"cos" : [ "float", "vector2", "vector3", "vector4" ],
	"exp" : [ "float", "vector2", "vector3", "vector4" ],
	"floor" : [ "color3", "color4", "float", "integer", "vector2", "vector3", "vector4" ],
	"in" : [ "float", "vector2", "vector3", "vector4" ],
	"round" : [ "color3", "color4", "float", "integer", "vector2", "vector3", "vector4" ],
	"sign" : [ "color3", "color4", "float", "vector2", "vector3", "vector4" ],
	"sin" : [ "float", "vector2", "vector3", "vector4" ],
	"sqrt" : [ "float", "vector2", "vector3", "vector4" ],
	"tan" : [ "float", "vector2", "vector3", "vector4" ],
	"place2d" : [ "vector2" ],
	"reflect" : [ "vector3" ],
	"refract" : [ "vector3" ],
	"rotate2d" : [ "vector2" ],
	"rotate3d" : [ "vector3" ],
	"swizzle" : [ "color3_color4", "color4_color3" ],
	"transformnormal" : [ "vector3" ],
	"transformpoint" : [ "vector3" ],
	"transformvector" : [ "vector3" ],
	"transformmatrix" : [ "vector2M3", "vector3", "vector3M4", "vector4" ],
	"transpose" : [ "matrix33", "matrix44" ],
	"add" : [
		"color3",
		"color3FA",
		"color4",
		"color4FA",
		"float",
		"matrix33",
		"matrix33FA",
		"matrix44",
		"matrix44FA",
		"vector2",
		"vector2FA",
		"vector3",
		"vector3FA",
		"vector4",
		"vector4FA",
	],
	"atan2" : [ "float", "vector2", "vector3", "vector4" ],
	"divide" : [
		"color3",
		"color3FA",
		"color4",
		"color4FA",
		"float",
		"matrix33",
		"matrix44",
		"vector2",
		"vector2FA",
		"vector3",
		"vector3FA",
		"vector4",
		"vector4FA",
	],
	"max" : [
		"color3",
		"color3FA",
		"color4",
		"color4FA",
		"float",
		"vector2",
		"vector2FA",
		"vector3",
		"vector3FA",
		"vector4",
		"vector4FA",
	],
	"min" : [
		"color3",
		"color3FA",
		"color4",
		"color4FA",
		"float",
		"vector2",
		"vector2FA",
		"vector3",
		"vector3FA",
		"vector4",
		"vector4FA",
	],
	"modulo" : [
		"color3",
		"color3FA",
		"color4",
		"color4FA",
		"float",
		"vector2",
		"vector2FA",
		"vector3",
		"vector3FA",
		"vector4",
		"vector4FA",
	],
	"multiply" : [
		"color3",
		"color3FA",
		"color4",
		"color4FA",
		"float",
		"matrix33",
		"matrix44",
		"vector2",
		"vector2FA",
		"vector3",
		"vector3FA",
		"vector4",
		"vector4FA",
	],
	"power" : [
		"color3",
		"color3FA",
		"color4",
		"color4FA",
		"float",
		"vector2",
		"vector2FA",
		"vector3",
		"vector3FA",
		"vector4",
		"vector4FA",
	],
	"safepower" : [
		"color3",
		"color3FA",
		"color4",
		"color4FA",
		"float",
		"vector2",
		"vector2FA",
		"vector3",
		"vector3FA",
		"vector4",
		"vector4FA",
	],
	"subtract" : [
		"color3",
		"color3FA",
		"color4",
		"color4FA",
		"float",
		"matrix33",
		"matrix33FA",
		"matrix44",
		"matrix44FA",
		"vector2",
		"vector2FA",
		"vector3",
		"vector3FA",
		"vector4",
		"vector4FA",
	],

	"glossiness_anisotropy" : None,
	"roughness_anisotropy" : None,
	"roughness_dual" : None,
	"lama_add" : [ "bsdf", "edf" ],
	"lama_conductor" : None,
	"lama_dielectric" : None,
	"lama_diffuse" : None,
	"lama_emission" : None,
	"lama_generalized_schlick" : None,
	"lama_iridescence" : None,
	"lama_layer" : [ "bsdf" ],
	"lama_mix" : [ "bsdf", "edf" ],
	"lama_sss" : None,
	"lama_sheen" : None,
	"lama_surface" : None,
	"lama_translucent" : None,
	"open_pbr_surface" : [ "surfaceshader" ],
	"standard_surface" : [ "surfaceshader" ],

	"constant" : [
		"boolean",
		"color3",
		"color4",
		"float",
		"integer",
		"matrix33",
		"matrix44",
		"vector2",
		"vector3",
		"vector4",
	],
	"randomfloat" : [ "float", "integer" ],

	"cellnoise2d" : [ "float" ],
	"checkerboard" : [ "float" ],
	"circle" : [ "float" ],
	"cloverleaf" : [ "float" ],
	"crosshatch" : [ "color3" ],
	"fractal3d" : [
		"color3",
		"color3FA",
		"color4",
		"color4FA",
		"float",
		"vector2",
		"vector2FA",
		"vector3",
		"vector3FA",
		"vector4",
		"vector4FA",
	],
	"grid" : [ "color3" ],
	"hexagon" : [ "float" ],
	"line" : [ "float" ],
	"noise2d" : [
		"color3",
		"color3FA",
		"color4FA",
		"float",
		"vector2",
		"vector2FA",
		"vector3",
		"vector3FA",
		"vector4",
		"vector4FA",
	],
	"ramp4" : [ "color3", "color4", "float", "vector2", "vector3", "vector4" ],
	"ramplr" : [ "color3", "color4", "float", "vector2", "vector3", "vector4" ],
	"ramptb" : [ "color3", "color4", "float", "vector2", "vector3", "vector4" ],
	"splitlr" : [ "color3", "color4", "float", "vector2", "vector3", "vector4" ],
	"splittb" : [ "color3", "color4", "float", "vector2", "vector3", "vector4" ],
	"tiledcircles" : [ "color3" ],
	"tiledcloverleafs" : [ "color3" ],
	"tiledhexagons" : [ "color3" ],
	"trianglewave" : [ "float" ],
	"unifiednoise2d" : [ "float" ],
	"worleynoise2d" : [ "float", "vector2", "vector3" ],

	"cellnoise3d" : [ "float" ],
	"noise3d" : [
		"color3",
		"color3FA",
		"color4",
		"color4FA",
		"float",
		"vector2",
		"vector2FA",
		"vector3",
		"vector3FA",
		"vector4",
		"vector4FA",
	],
	"randomcolor" : [ "float", "integer" ],
	"unifiednoise3d" : [ "float" ],
	"worleynoise3d" : [ "float", "vector2", "vector3" ],

	"hextiledimage" : [ "color3", "color4" ],
	"hextilednormalmap" : [ "vector3" ],
	"image" : [ "color3", "color4", "float", "vector2", "vector3", "vector4" ],
	"tiledimage" : [ "color3", "color4", "float", "vector2", "vector3", "vector4" ],

	# Not in the menu but needed for some renderer backends
	"UsdPreviewSurface" : [ "surfaceshader" ],
	"UsdTransform2d" : None,
	"UsdUVTexture" : None,
	"UsdPrimvarReader" : [ 
		"float", "float2", "float3", "float4", "point", "vector", "normal", "int", "string"
	],
	"anisotropic" : [ "vdf" ],

}

def nodeDefNameFromBaseName( baseName, signature = None ) :

	# Will return the first category found if category is None
	if baseName in signatures :
		sigs = signatures.get( baseName, None )
		if not signature and not sigs :
			return "ND_{}".format( baseName )
		if not signature :
			return "ND_{}_{}".format( baseName, sigs[0] )
		if signature in sigs :
			return "ND_{}_{}".format( baseName, signature )
	return None

def collectShaderNodeDefs() :

	shaderNodeDefs = []

	for baseName, sigs in signatures.items() :
		if sigs == None :
			shaderNodeDefs.append( "ND_{}".format( baseName ) )
		else :
			for signature in sigs :
				shaderNodeDefs.append( "ND_{}_{}".format( baseName, signature ) )
	
	return shaderNodeDefs

def generateOSLCode( buildDir ) :

	import os
	import MaterialX as mx
	import MaterialX.PyMaterialXGenShader as mxGenShader
	import MaterialX.PyMaterialXGenOsl as mxGenOsl

	shaderNodeDefs = collectShaderNodeDefs()

	searchPath = mx.FileSearchPath( os.environ.get( "PXR_MTLX_STDLIB_SEARCH_PATHS", "" ) )
	libraryFolders = [ "libraries" ]

	mxDoc = mx.createDocument()
	mx.loadLibraries( libraryFolders, searchPath, mxDoc )

	generator = mxGenOsl.OslShaderGenerator.create()
	context = mxGenShader.GenContext( generator )
	context.getOptions().addUpstreamDependencies = False
	context.registerSourceCodeSearchPath( searchPath )
	context.getOptions().fileTextureVerticalFlip = True

	mtlxShaders = []

	for mxNodeDef in mxDoc.getNodeDefs() :
		shaderNodeDef = mxNodeDef.getName()
		if not shaderNodeDef in shaderNodeDefs :
			continue
		uniqueName = shaderNodeDef.removeprefix( "ND_" )
		mxNode = mxDoc.addNodeInstance( mxNodeDef, uniqueName )
		try :
			mxShader = generator.generate( shaderNodeDef, mxNode, context )
			code = mxShader.getSourceCode( "pixel" )
			outFile = os.path.join( buildDir, "__" + shaderNodeDef + ".osl" )
			with open( outFile, "w" ) as f:
				f.write(code)
			mtlxShaders.append( outFile )
		except :
			print( "No MaterialX implementation found for {}".format( shaderNodeDef ) )
	
	return mtlxShaders
