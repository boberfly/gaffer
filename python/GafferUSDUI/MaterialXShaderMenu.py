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

import GafferUI
import GafferUSD
from GafferMaterialX import _MaterialXUtils
import collections
import functools

def __usdShaderCreator( shaderName, label ) :

	node = GafferUSD.USDShader( name = label )
	node.loadShader( shaderName )
	return node

def appendShaders( menuDefinition, prefix="/USD/MaterialX" ) :

	MenuItem = collections.namedtuple( "MenuItem", [ "menuPath", "nodeCreator" ] )
	menuItems = []

	for role, labels in _MaterialXUtils.menuShaders.items() :

		_role = _MaterialXUtils.labelNames["roles"].get( role, role )

		for label in labels :

			shaderName = _MaterialXUtils.nodeDefNameFromBaseName( label )
			_label = _MaterialXUtils.labelNames["labels"].get( label, label.capitalize() )
			menuPath = "{}/{}".format( _role, _label )
			nodeCreator = functools.partial( __usdShaderCreator, shaderName, _label.replace( " ", "" ) )

			menuItems.append( MenuItem( menuPath, nodeCreator ) )

	for menuItem in menuItems :
		menuDefinition.append(
			prefix + "/" + menuItem.menuPath,
			{
				"command" : GafferUI.NodeMenu.nodeCreatorWrapper( menuItem.nodeCreator ),
				"searchText" : "mtlx" + menuItem.menuPath.rpartition( "/" )[2].replace( " ", "" ),
			}
		)
