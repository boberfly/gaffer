##########################################################################
#  
#  Copyright (c) 2011-2012, John Haddon. All rights reserved.
#  Copyright (c) 2011, Image Engine Design Inc. All rights reserved.
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

import IECore

import Gaffer

class TypedPlugTest( unittest.TestCase ) :

	def testConstructor( self ) :
	
		s = Gaffer.StringPlug()
		self.assertEqual( s.defaultValue(), "" )
		self.assertEqual( s.getName(), "StringPlug" )
		
		s = Gaffer.StringPlug( direction=Gaffer.Plug.Direction.Out, defaultValue = "a" )
			
		self.assertEqual( s.direction(), Gaffer.Plug.Direction.Out )
		self.assertEqual( s.defaultValue(), "a" )
		
		s = Gaffer.StringPlug( defaultValue="b", name="a" )
		self.assertEqual( s.defaultValue(), "b" )
		self.assertEqual( s.getName(), "a" )
		
	def testDisconnection( self ) :
	
		p1 = Gaffer.StringPlug( direction=Gaffer.Plug.Direction.Out )
		p2 = Gaffer.StringPlug( direction=Gaffer.Plug.Direction.In )
		
		p2.setInput( p1 )
		self.assert_( p2.getInput().isSame( p1 ) )
		p2.setInput( None )
		self.assert_( p2.getInput() is None )

	def testAcceptsNoneInput( self ) :
	
		p = Gaffer.StringPlug( "hello" )
		self.failUnless( p.acceptsInput( None ) )
		
	def testRunTimeTyped( self ) :
	
		p = Gaffer.BoolPlug( "b" )
		
		self.assertEqual( p.typeName(), "BoolPlug" )
		self.assertEqual( IECore.RunTimeTyped.typeNameFromTypeId( p.typeId() ), "BoolPlug" )
		self.assertEqual( IECore.RunTimeTyped.baseTypeId( p.typeId() ), Gaffer.ValuePlug.staticTypeId() )

	def testSetToDefault( self ) :
	
		s = Gaffer.StringPlug( "s", defaultValue = "apple" )
		self.assertEqual( s.getValue(), "apple" )
		
		s.setValue( "pear" )
		self.assertEqual( s.getValue(), "pear" )
		
		s.setToDefault()
		self.assertEqual( s.getValue(), "apple" )
						
if __name__ == "__main__":
	unittest.main()
	
