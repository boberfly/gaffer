##########################################################################
#
#  Copyright (c) 2011-2012, John Haddon. All rights reserved.
#  Copyright (c) 2013, Image Engine Design Inc. All rights reserved.
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

from __future__ import with_statement

import sys
import collections

import IECore

import Gaffer
import GafferUI

def appendDefinitions( menuDefinition, prefix="" ) :

	menuDefinition.append( prefix + "/Undo", { "command" : undo, "shortCut" : "Ctrl+Z", "active" : __undoAvailable } )
	menuDefinition.append( prefix + "/Redo", { "command" : redo, "shortCut" : "Shift+Ctrl+Z", "active" : __redoAvailable } )
	menuDefinition.append( prefix + "/UndoDivider", { "divider" : True } )

	menuDefinition.append( prefix + "/Cut", { "command" : cut, "shortCut" : "Ctrl+X", "active" : __selectionAvailable } )
	menuDefinition.append( prefix + "/Copy", { "command" : copy, "shortCut" : "Ctrl+C", "active" : __selectionAvailable } )
	menuDefinition.append( prefix + "/Paste", { "command" : paste, "shortCut" : "Ctrl+V", "active" : __pasteAvailable } )
	menuDefinition.append( prefix + "/Delete", { "command" : delete, "shortCut" : "Backspace, Delete", "active" : __selectionAvailable } )
	menuDefinition.append( prefix + "/CutCopyPasteDeleteDivider", { "divider" : True } )

	menuDefinition.append( prefix + "/Find...", { "command" : find, "shortCut" : "Ctrl+F" } )
	menuDefinition.append( prefix + "/FindDivider", { "divider" : True } )

	menuDefinition.append( prefix + "/Arrange", { "command" : arrange, "shortCut" : "Ctrl+L" } )
	menuDefinition.append( prefix + "/ArrangeDivider", { "divider" : True } )

	menuDefinition.append( prefix + "/Select All", { "command" : selectAll, "shortCut" : "Ctrl+A" } )
	menuDefinition.append( prefix + "/Select None", { "command" : selectNone, "shortCut" : "Shift+Ctrl+A", "active" : __selectionAvailable } )

	menuDefinition.append( prefix + "/Select Connected/Inputs", { "command" : selectInputs, "active" : __selectionAvailable } )
	menuDefinition.append( prefix + "/Select Connected/Add Inputs", { "command" : selectAddInputs, "active" : __selectionAvailable } )
	menuDefinition.append( prefix + "/Select Connected/InputsDivider", { "divider" : True } )

	menuDefinition.append( prefix + "/Select Connected/Upstream", { "command" : selectUpstream, "active" : __selectionAvailable } )
	menuDefinition.append( prefix + "/Select Connected/Add Upstream", { "command" : selectAddUpstream, "active" : __selectionAvailable } )
	menuDefinition.append( prefix + "/Select Connected/UpstreamDivider", { "divider" : True } )

	menuDefinition.append( prefix + "/Select Connected/Outputs", { "command" : selectOutputs, "active" : __selectionAvailable } )
	menuDefinition.append( prefix + "/Select Connected/Add Outputs", { "command" : selectAddOutputs, "active" : __selectionAvailable } )
	menuDefinition.append( prefix + "/Select Connected/OutputsDivider", { "divider" : True } )

	menuDefinition.append( prefix + "/Select Connected/Downstream", { "command" : selectDownstream, "active" : __selectionAvailable } )
	menuDefinition.append( prefix + "/Select Connected/Add Downstream", { "command" : selectAddDownstream, "active" : __selectionAvailable } )
	menuDefinition.append( prefix + "/Select Connected/DownstreamDivider", { "divider" : True } )

	menuDefinition.append( prefix + "/Select Connected/Add All", { "command" : selectConnected, "active" : __selectionAvailable } )

__Scope = collections.namedtuple( "Scope", [ "scriptWindow", "script", "parent", "nodeGraph" ] )

## Returns the scope in which an edit menu item should operate. The return
# value has "scriptWindow", "script", "root" and "nodeGraph" attributes.
# The "nodeGraph" attribute may be None if no NodeGraph can be found. Note
# that in many cases user expectation is that an operation will only apply
# to nodes currently visible within the NodeGraph, and that nodes can be
# filtered within the NodeGraph.
def scope( menu ) :

	scriptWindow = menu.ancestor( GafferUI.ScriptWindow )

	nodeGraph = None
	## \todo Add public methods for querying focus.
	focusWidget = GafferUI.Widget._owner( scriptWindow._qtWidget().focusWidget() )
	if focusWidget is not None :
		nodeGraph = focusWidget.ancestor( GafferUI.NodeGraph )

	if nodeGraph is None :
		nodeGraphs = scriptWindow.getLayout().editors( GafferUI.NodeGraph )
		if nodeGraphs :
			nodeGraph = nodeGraphs[0]

	if nodeGraph is not None :
		parent = nodeGraph.graphGadget().getRoot()
	else :
		parent = scriptWindow.scriptNode()

	return __Scope( scriptWindow, scriptWindow.scriptNode(), parent, nodeGraph )

## A function suitable as the command for an Edit/Undo menu item. It must
# be invoked from a menu that has a ScriptWindow in its ancestry.
def undo( menu ) :

	scope( menu ).script.undo()

## A function suitable as the command for an Edit/Redo menu item. It must
# be invoked from a menu that has a ScriptWindow in its ancestry.
def redo( menu ) :

	scope( menu ).script.redo()

## A function suitable as the command for an Edit/Cut menu item. It must
# be invoked from a menu that has a ScriptWindow in its ancestry.
def cut( menu ) :

	s = scope( menu )
	with Gaffer.UndoContext( s.script ) :
		s.script.cut( s.parent, s.script.selection() )

## A function suitable as the command for an Edit/Copy menu item. It must
# be invoked from a menu that has a ScriptWindow in its ancestry.
def copy( menu ) :

	s = scope( menu )
	s.script.copy( s.parent, s.script.selection() )

## A function suitable as the command for an Edit/Paste menu item. It must
# be invoked from a menu that has a ScriptWindow in its ancestry.
def paste( menu ) :

	s = scope( menu )
	originalSelection = Gaffer.StandardSet( iter( s.script.selection() ) )

	with Gaffer.UndoContext( s.script ) :

		s.script.paste( s.parent )

		# try to get the new nodes connected to the original selection
		if s.nodeGraph is None :
			return

		s.nodeGraph.graphGadget().getLayout().connectNodes( s.nodeGraph.graphGadget(), s.script.selection(), originalSelection )

		# position the new nodes sensibly

		bound = s.nodeGraph.bound()
		mousePosition = GafferUI.Widget.mousePosition()
		if bound.intersects( mousePosition ) :
			fallbackPosition = mousePosition - bound.min
		else :
			fallbackPosition = bound.center() - bound.min

		fallbackPosition = s.nodeGraph.graphGadgetWidget().getViewportGadget().rasterToGadgetSpace(
			IECore.V2f( fallbackPosition.x, fallbackPosition.y ),
			gadget = s.nodeGraph.graphGadget()
		).p0
		fallbackPosition = IECore.V2f( fallbackPosition.x, fallbackPosition.y )

		s.nodeGraph.graphGadget().getLayout().positionNodes( s.nodeGraph.graphGadget(), s.script.selection(), fallbackPosition )

		s.nodeGraph.frame( s.script.selection(), extend = True )

## A function suitable as the command for an Edit/Delete menu item. It must
# be invoked from a menu that has a ScriptWindow in its ancestry.
def delete( menu ) :

	s = scope( menu )
	with Gaffer.UndoContext( s.script ) :
		s.script.deleteNodes( s.parent, s.script.selection() )

## A function suitable as the command for an Edit/Find menu item.  It must
# be invoked from a menu that has a ScriptWindow in its ancestry.
def find( menu ) :

	s = scope( menu )

	try :
		findDialogue = s.scriptWindow.__findDialogue
	except AttributeError :
		findDialogue = GafferUI.NodeFinderDialogue( s.parent )
		s.scriptWindow.addChildWindow( findDialogue )
		s.scriptWindow.__findDialogue = findDialogue

	findDialogue.setScope( s.parent )
	findDialogue.setVisible( True )

## A function suitable as the command for an Edit/Arrange menu item.  It must
# be invoked from a menu that has a ScriptWindow in its ancestry.
def arrange( menu ) :

	s = scope( menu )
	if not s.nodeGraph :
		return

	graph = s.nodeGraph.graphGadget()

	nodes = s.script.selection()
	if not nodes :
		nodes = Gaffer.StandardSet( graph.getRoot().children( Gaffer.Node ) )

	with Gaffer.UndoContext( s.script ) :
		graph.getLayout().layoutNodes( graph, nodes )

## A function suitable as the command for an Edit/Select All menu item. It must
# be invoked from a menu that has a ScriptWindow in its ancestry.
def selectAll( menu ) :

	s = scope( menu )
	if s.nodeGraph is None :
		return

	graphGadget = s.nodeGraph.graphGadget()
	for node in s.parent.children( Gaffer.Node ) :
		if graphGadget.nodeGadget( node ) is not None :
			s.script.selection().add( node )

## A function suitable as the command for an Edit/Select None menu item. It must
# be invoked from a menu that has a ScriptWindow in its ancestry.
def selectNone( menu ) :

	scope( menu ).script.selection().clear()

## The command function for the default "Edit/Select Connected/Inputs" menu item. It must
# be invoked from a menu that has a ScriptWindow in its ancestry.
def selectInputs( menu ) :

	__selectConnected( menu, Gaffer.Plug.Direction.In, degreesOfSeparation = 1, add = False )

## The command function for the default "Edit/Select Connected/Add Inputs" menu item. It must
# be invoked from a menu that has a ScriptWindow in its ancestry.
def selectAddInputs( menu ) :

	__selectConnected( menu, Gaffer.Plug.Direction.In, degreesOfSeparation = 1, add = True )

## The command function for the default "Edit/Select Connected/Upstream" menu item. It must
# be invoked from a menu that has a ScriptWindow in its ancestry.
def selectUpstream( menu ) :

	__selectConnected( menu, Gaffer.Plug.Direction.In, degreesOfSeparation = sys.maxint, add = False )

## The command function for the default "Edit/Select Connected/Add Upstream" menu item. It must
# be invoked from a menu that has a ScriptWindow in its ancestry.
def selectAddUpstream( menu ) :

	__selectConnected( menu, Gaffer.Plug.Direction.In, degreesOfSeparation = sys.maxint, add = True )

## The command function for the default "Edit/Select Connected/Outputs" menu item. It must
# be invoked from a menu that has a ScriptWindow in its ancestry.
def selectOutputs( menu ) :

	__selectConnected( menu, Gaffer.Plug.Direction.Out, degreesOfSeparation = 1, add = False )

## The command function for the default "Edit/Select Connected/Add Outputs" menu item. It must
# be invoked from a menu that has a ScriptWindow in its ancestry.
def selectAddOutputs( menu ) :

	__selectConnected( menu, Gaffer.Plug.Direction.Out, degreesOfSeparation = 1, add = True )

## The command function for the default "Edit/Select Connected/Downstream" menu item. It must
# be invoked from a menu that has a ScriptWindow in its ancestry.
def selectDownstream( menu ) :

	__selectConnected( menu, Gaffer.Plug.Direction.Out, degreesOfSeparation = sys.maxint, add = False )

## The command function for the default "Edit/Select Connected/Add Downstream" menu item. It must
# be invoked from a menu that has a ScriptWindow in its ancestry.
def selectAddDownstream( menu ) :

	__selectConnected( menu, Gaffer.Plug.Direction.Out, degreesOfSeparation = sys.maxint, add = True )

## The command function for the default "Edit/Select Connected/Add All" menu item. It must
# be invoked from a menu that has a ScriptWindow in its ancestry.
def selectConnected( menu ) :

	__selectConnected( menu, Gaffer.Plug.Direction.Invalid, degreesOfSeparation = sys.maxint, add = True )


def __selectConnected( menu, direction, degreesOfSeparation, add ) :

	s = scope( menu )
	if s.nodeGraph is None :
		return

	connected = Gaffer.StandardSet()
	for node in s.script.selection() :
		connected.add( [ g.node() for g in s.nodeGraph.graphGadget().connectedNodeGadgets( node, direction, degreesOfSeparation ) ] )

	selection = s.script.selection()
	if not add :
		selection.clear()
	selection.add( connected )

def __selectionAvailable( menu ) :

	return True if scope( menu ).script.selection().size() else False

def __pasteAvailable( menu ) :

	root = scope( menu ).script.ancestor( Gaffer.ApplicationRoot )
	return isinstance( root.getClipboardContents(), IECore.StringData )

def __undoAvailable( menu ) :

	return scope( menu ).script.undoAvailable()

def __redoAvailable( menu ) :

	return scope( menu ).script.redoAvailable()
