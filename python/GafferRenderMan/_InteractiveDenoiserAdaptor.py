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

import inspect

import IECore

import Gaffer
import GafferScene

class _InteractiveDenoiserAdaptor( GafferScene.SceneProcessor ) :

	def __init__( self, name = "_InteractiveDenoiserAdaptor" ) :

		GafferScene.SceneProcessor.__init__( self, name )

		self["out"].setInput( self["in"] )

		self["__globalsExpression"] = Gaffer.Expression()
		self["__globalsExpression"].setExpression( inspect.cleandoc(
			"""
			import GafferRenderMan
			parent["out"]["globals"] = GafferRenderMan._InteractiveDenoiserAdaptor._adaptedGlobals( parent["in"]["globals"] )
			"""
		) )

	@staticmethod
	def _adaptedGlobals( inputGlobals ) :

		enabled = inputGlobals.get( "option:ri:interactiveDenoiser:enabled" )
		if enabled is None or not enabled.value :
			return inputGlobals

		qnParameterPrefix = "option:ri:interactiveDenoiser:"
		qnParameters = {}
		for name, value in inputGlobals.items() :
			if name.startswith( qnParameterPrefix ) :
				name = name[len(qnParameterPrefix):]
				if name != "enabled" :
					qnParameters[name] = value

		requiredOutputs = [
			( "beauty", "rgba", "filter" ),
			( "mse", "rgb", "mse" ),
			( "albedo", "lpe nothruput;noinfinitecheck;noclamp;unoccluded;overwrite;C<.S'passthru'>*((U2L)|O)", "filter" ),
			( "albedo_mse", "lpe nothruput;noinfinitecheck;noclamp;unoccluded;overwrite;C<.S'passthru'>*((U2L)|O)", "mse" ),
			( "diffuse", "lpe C(D[DS]*[LO])|[LO]", "filter" ),
			( "diffuse_mse", "lpe C(D[DS]*[LO])|[LO]", "mse" ),
			( "specular", "lpe CS[DS]*[LO]", "filter" ),
			( "specular_mse", "lpe CS[DS]*[LO]", "mse" ),
			( "normal", "lpe nothruput;noinfinitecheck;noclamp;unoccluded;overwrite;CU6L", "filter" ),
			( "normal_mse", "lpe nothruput;noinfinitecheck;noclamp;unoccluded;overwrite;CU6L", "mse" ),
			( "sampleCount", "float sampleCount", "sum" ),
		]

		dataFound = set()
		templateOutput = None

		outputGlobals = inputGlobals.copy()
		for key, value in inputGlobals.items() :

			if not key.startswith( "output:" ) :
				continue

			if value.getType() != "ieDisplay" :
				continue

			output = value.copy()
			# Give everything the same name, so that all outputs go to a single driver.
			output.setName( "interactiveOutputs" )
			# Redirect output to `quicklyNoiseless` driver, which will do the denoising.
			output.setType( "quicklyNoiseless" )
			# And redirect the output of `quicklyNoiseless` back to our regular driver.
			output.parameters()["dspyDSOPath"] = IECore.StringData(
				str( Gaffer.rootPath() / "renderManPlugins" / "d_ieDisplay.so" )
			)
			# Add parameters for `quicklyNoiseless`.
			output.parameters().update( qnParameters )

			if output.getData() in { "rgb", "rgba" } :
				templateOutput = output.copy()

			dataFound.add( output.getData() ) # TODO : WORRY ABOUT LAYER NAME?
			outputGlobals[key] = output

		if templateOutput is None :
			IECore.msg( IECore.Msg.Warning, "_InteractiveDenoiserAdaptor", "No beauty output found" )
			return inputGlobals

		for layerName, data, accumulationRule in requiredOutputs :

			if data in dataFound :
				continue

			output = templateOutput.copy()
			output.setData( data )
			output.parameters()["layerName"] = IECore.StringData( layerName )
			output.parameters()["ri:accumulationRule"] = IECore.StringData( accumulationRule )

			outputGlobals[f"output:{layerName}"] = output

		return outputGlobals

IECore.registerRunTimeTyped( _InteractiveDenoiserAdaptor, typeName = "GafferRenderMan::_InteractiveDenoiserAdaptor" )

GafferScene.SceneAlgo.registerRenderAdaptor( "InteractiveRenderManDenoiserAdaptor", _InteractiveDenoiserAdaptor, "InteractiveRender", "RenderMan" )

