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

import os
import functools
import string
from xml.etree import cElementTree

import IECore

import Gaffer
import GafferUI

import GafferRenderMan

##########################################################################
# Node menu
##########################################################################

def appendShaders( menuDefinition, prefix = "/RenderMan" ) :

	plugins = __plugins()

	menuDefinition.append(
		prefix + "/Shader",
		{
			"subMenu" : functools.partial( __shadersSubMenu, plugins ),
		}
	)

	menuDefinition.append(
		prefix + "/Light",
		{
			"subMenu" : functools.partial( __lightsSubMenu, plugins ),
		}
	)

def __plugins() :

	result = {}

	searchPaths = IECore.SearchPath( os.environ.get( "RMAN_RIXPLUGINPATH", "" ) )

	pathsVisited = set()
	for path in searchPaths.paths :

		if path in pathsVisited :
			continue
		else :
			pathsVisited.add( path )

		for root, dirs, files in os.walk( path ) :
			for file in [ f for f in files  ] :

				name, extension = os.path.splitext( file )
				if extension != ".args" :
					continue

				plugin = __plugin( os.path.join( root, file ) )
				if plugin is not None :
					result[name] = plugin

	return result

def __plugin( argsFile ) :

	pluginType = None
	classification = ""
	for event, element in cElementTree.iterparse( argsFile, events = ( "start", "end" ) ) :
		if element.tag == "shaderType" and event == "end" :
			tag = element.find( "tag" )
			if tag is not None :
				pluginType = tag.attrib.get( "value" )
		elif element.tag == "rfmdata" :
			classification = element.attrib.get( "classification" )

	if pluginType is None :
		return None

	return {
		"type" : pluginType,
		"classification" : classification
	}

def __loadShader( shaderName, nodeType ) :

	nodeName = os.path.split( shaderName )[-1]
	nodeName = nodeName.translate( string.maketrans( ".-", "__" ) )

	node = nodeType( nodeName )
	node.loadShader( shaderName )

	return node

def __shadersSubMenu( plugins ) :

	result = IECore.MenuDefinition()

	for name, plugin in plugins.items() :

		if plugin["type"] not in { "bxdf", "pattern", "integrator" } :
			continue

		if not plugin["classification"] or not plugin["classification"].startswith( "rendernode/RenderMan" ) :

			path = {
				"bxdf" : "BXDF",
				"pattern" : "Pattern/Other",
				"integrator" : "Integrator",
			}.get( plugin["type"] )

		else :

			path = "/".join( [ x.title() for x in plugin["classification"].split( "/" )[2:] ] )


		result.append(
			"/{0}/{1}".format( path, name ),
			{
				"command" : GafferUI.NodeMenu.nodeCreatorWrapper(
					functools.partial( __loadShader, name, GafferRenderMan.RenderManShader )
				)
			}
		)

	return result

def __lightsSubMenu( plugins ) :

	result = IECore.MenuDefinition()

	for name, plugin in plugins.items() :

		if plugin["type"] != "light" :
			continue

		result.append(
			"/" + name,
			{
				"command" : GafferUI.NodeMenu.nodeCreatorWrapper(
					functools.partial( __loadShader, name, GafferRenderMan.RenderManLight )
				)
			}
		)

	return result

##########################################################################
# Metadata. We register dynamic Gaffer.Metadata entries which are
# implemented as lookups to data queried from .args files.
##########################################################################

__metadataCache = {}

__widgetTypes = {
	"number" : "GafferUI.NumericPlugValueWidget",
	"string" : "GafferUI.StringPlugValueWidget",
	"boolean" : "GafferUI.BoolPlugValueWidget",
	"checkBox" : "GafferUI.BoolPlugValueWidget",
	"popup" : "GafferUI.PresetsPlugValueWidget",
	"mapper" : "GafferUI.PresetsPlugValueWidget",
	"filename" : "GafferUI.PathPlugValueWidget",
	"assetIdInput" : "GafferUI.PathPlugValueWidget",
	"null" : "",
}

def __parsePresets( options, parameter ) :

	presetCreator = {
		"int" : ( IECore.IntVectorData, int ),
		"float" : ( IECore.FloatVectorData, float ),
		"string" : ( IECore.StringVectorData, str ),
	}.get( parameter["__type"] )

	if presetCreator is None :
		return

	presetNames = IECore.StringVectorData()
	presetValues = presetCreator[0]()

	if isinstance( options, str ) :
		for option in options.split( "|" ) :
			try :
				value = presetCreator[1]( option )
			except :
				# Avoid errors triggered by PxrRamp, which has a float
				# parameter containing a bogus "options" attribute containing
				# a string value.
				continue
			presetNames.append( option.title() )
			presetValues.append( value )
	else :
		# Hint dict
		for option in options :
			presetNames.append( option.attrib["name"] )
			presetValues.append( presetCreator[1]( option.attrib["value"] ) )

	parameter["presetNames"] = presetNames
	parameter["presetValues"] = presetValues

def __shaderMetadata( node ) :

	global __metadataCache

	if isinstance( node, GafferRenderMan.RenderManLight ) :
		shaderName = node["__shader"]["name"].getValue()
	else :
		shaderName = node["name"].getValue()

	try :
		return __metadataCache[shaderName]
	except KeyError :
		pass

	result = { "parameters" : {} }

	searchPaths = IECore.SearchPath( os.environ.get( "RMAN_RIXPLUGINPATH", "" ) )
	argsFile = searchPaths.find( "Args/" + shaderName + ".args" )
	if argsFile :

		pageStack = []
		currentParameter = None
		for event, element in cElementTree.iterparse( argsFile, events = ( "start", "end" ) ) :

			if element.tag == "page" :

				if event == "start" :
					pageStack.append( element.attrib["name"] )
				else :
					pageStack.pop()

			elif element.tag == "param" :

				if event == "start" :

					currentParameter = {}
					result["parameters"][element.attrib["name"]] = currentParameter

					currentParameter["__type"] = element.attrib["type"]
					currentParameter["label"] = element.attrib.get( "label" )
					currentParameter["description"] = element.attrib.get( "help" )
					currentParameter["layout:section"] = ".".join( pageStack )
					currentParameter["plugValueWidget:type"] = __widgetTypes.get( element.attrib.get( "widget" ) )
					currentParameter["nodule:type"] = "" if element.attrib.get( "connectable", "true" ).lower() == "false" else None

					if element.attrib.get( "options" ) :
						__parsePresets( element.attrib.get( "options" ), currentParameter )

				elif event == "end" :

					currentParameter = None

			elif element.tag == "help" and event == "end" :

				if currentParameter :
					currentParameter["description"] = element.text
				else :
					result["description"] = element.text

			elif element.tag == "hintdict" and element.attrib.get( "name" ) == "options" :
				if event == "end" :
					__parsePresets( element, currentParameter )

	__metadataCache[shaderName] = result
	return result

def __parameterMetadata( plug, key ) :

	return __shaderMetadata( plug.node() )["parameters"].get( plug.getName(), {} ).get( key )

def __nodeDescription( node ) :

	if isinstance( node, GafferRenderMan.RenderManShader ) :
		defaultDescription = """Loads RenderMan shaders. Use the ShaderAssignment node to assign shaders to objects in the scene."""
	else :
		defaultDescription = """Loads RenderMan lights."""

	metadata = __shaderMetadata( node )
	return metadata.get( "description", defaultDescription )

for nodeType in ( GafferRenderMan.RenderManShader, GafferRenderMan.RenderManLight ) :

	Gaffer.Metadata.registerValue( nodeType, "description", __nodeDescription )

	for key in [
		"label",
		"description",
		"layout:section",
		"plugValueWidget:type",
		"presetNames",
		"presetValues",
		"nodule:type",
	] :

		Gaffer.Metadata.registerValue(
			nodeType, "parameters.*", key,
			functools.partial( __parameterMetadata, key = key )
		)

Gaffer.Metadata.registerValue( GafferRenderMan.RenderManShader, "out", "nodule:type", lambda plug : "GafferUI::CompoundNodule" if len( plug ) else "GafferUI::StandardNodule" )
