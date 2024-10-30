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

from xml.etree import cElementTree

import IECore

## Parses a RenderMan `.args` file, converting it to a dictionary
# using Gaffer's standard metadata conventions :
#
# ```
# {
#	"description" : ...,
#	"parameters" : {
# 		"parameter1" : {
#       	"description" : ...
#       	"plugValueWidget:type" : ...
#           ...
#		}
#   }
# }
# ```
def parseMetadata( argsFile ) :

	result = { "parameters" : {} }

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

	return result

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
			value = presetCreator[1]( option )
			presetNames.append( option.title() )
			presetValues.append( value )
	else :
		# Hint dict
		for option in options :
			presetNames.append( option.attrib["name"] )
			presetValues.append( presetCreator[1]( option.attrib["value"] ) )

	parameter["presetNames"] = presetNames
	parameter["presetValues"] = presetValues
