//////////////////////////////////////////////////////////////////////////
//
//  Copyright (c) 2019, John Haddon. All rights reserved.
//
//  Redistribution and use in source and binary forms, with or without
//  modification, are permitted provided that the following conditions are
//  met:
//
//      * Redistributions of source code must retain the above
//        copyright notice, this list of conditions and the following
//        disclaimer.
//
//      * Redistributions in binary form must reproduce the above
//        copyright notice, this list of conditions and the following
//        disclaimer in the documentation and/or other materials provided with
//        the distribution.
//
//      * Neither the name of John Haddon nor the names of
//        any other contributors to this software may be used to endorse or
//        promote products derived from this software without specific prior
//        written permission.
//
//  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
//  IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
//  THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
//  PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
//  CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
//  EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
//  PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
//  PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
//  LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
//  NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
//  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
//////////////////////////////////////////////////////////////////////////

#include "GafferRenderMan/RenderManOptions.h"

using namespace Imath;
using namespace IECore;
using namespace Gaffer;
using namespace GafferRenderMan;

IE_CORE_DEFINERUNTIMETYPED( RenderManOptions );

RenderManOptions::RenderManOptions( const std::string &name )
	:	GafferScene::Options( name )
{

	Gaffer::CompoundDataPlug *options = optionsPlug();

	options->addChild( new NameValuePlug( "renderman:hider:maxsamples", new IntData( 64 ), false, "hiderMaxSamples" ) );
	options->addChild( new NameValuePlug( "renderman:hider:minsamples", new IntData( -1 ), false, "hiderMinSamples" ) );
	options->addChild( new NameValuePlug( "renderman:Ri:PixelVariance", new FloatPlug( "value", Plug::In, 0.001, 0, 1 ), false, "pixelVariance" ) );
	options->addChild( new NameValuePlug( "renderman:hider:incremental", new BoolData( false ), false, "hiderIncremental" ) );

	options->addChild( new NameValuePlug( "renderman:bucket:order", new StringData( "horizontal" ), false, "bucketOrder" ) );
	options->addChild( new NameValuePlug( "renderman:limits:bucketsize", new V2iData( V2i( 16 ) ), false, "bucketSize" ) );
	options->addChild( new NameValuePlug( "limits:threads", new IntData( 0 ), false, "limitsThreads" ) );

	// Searchpaths. Deliberately omitting shader path because we use OSL_SHADER_PATHS
	// instead, and displays path because we use RMAN_DISPLAYS_PATH instead.

	options->addChild( new NameValuePlug( "renderman:searchpath:texture", new StringData( "" ), false, "searchPathTexture" ) );
	options->addChild( new NameValuePlug( "renderman:searchpath:rixplugin", new StringData( "" ), false, "searchPathRixPlugin" ) );
	options->addChild( new NameValuePlug( "renderman:searchpath:dirmap", new StringData( "" ), false, "searchPathDirMap" ) );

	// Statistics

	options->addChild( new NameValuePlug( "renderman:statistics:level", new BoolData( false ), false, "statisticsLevel" ) );
	options->addChild( new NameValuePlug( "renderman:statistics:filename", new StringData( "" ), false, "statisticsFileName" ) );
	options->addChild( new NameValuePlug( "renderman:statistics:xmlfilename", new StringData( "" ), false, "statisticsXMLFileName" ) );

}

RenderManOptions::~RenderManOptions()
{
}
