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

import unittest

import Gaffer
import GafferSceneTest
import GafferRenderMan

class TagPlugTest( GafferSceneTest.SceneTestCase ) :

	def testConstructor( self ) :

		p = GafferRenderMan.TagPlug( name = "p", direction = Gaffer.Plug.Direction.Out, tags = { "one", "two" }, flags = Gaffer.Plug.Flags.Default | Gaffer.Plug.Flags.Dynamic )
		self.assertEqual( p.getName(), "p" )
		self.assertEqual( p.direction(), Gaffer.Plug.Direction.Out )
		self.assertEqual( p.getFlags(), Gaffer.Plug.Flags.Default | Gaffer.Plug.Flags.Dynamic )
		self.assertEqual( p.tags(), { "one", "two" } )

	def testCreateCounterpart( self ) :

		p = GafferRenderMan.TagPlug( name = "p", direction = Gaffer.Plug.Direction.Out, tags = { "one", "two" }, flags = Gaffer.Plug.Flags.Default | Gaffer.Plug.Flags.Dynamic )
		p2 = p.createCounterpart( "p2", Gaffer.Plug.Direction.In )

		self.assertEqual( p2.getName(), "p2" )
		self.assertEqual( p2.direction(), Gaffer.Plug.Direction.In )
		self.assertEqual( p2.getFlags(), p.getFlags() )
		self.assertEqual( p2.tags(), p.tags() )

	def testAcceptsInput( self ) :

		a = GafferRenderMan.TagPlug( tags = { "a" } )
		b = GafferRenderMan.TagPlug( tags = { "b" } )
		ab = GafferRenderMan.TagPlug( tags = { "a", "b" } )

		self.assertTrue( ab.acceptsInput( a ) )
		self.assertTrue( ab.acceptsInput( b ) )
		self.assertTrue( a.acceptsInput( ab ) )
		self.assertTrue( b.acceptsInput( ab ) )
		self.assertFalse( b.acceptsInput( a ) )
		self.assertFalse( a.acceptsInput( b ) )

if __name__ == "__main__":
	unittest.main()
