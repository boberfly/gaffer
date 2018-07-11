//////////////////////////////////////////////////////////////////////////
//
//  Copyright (c) 2018, John Haddon. All rights reserved.
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

#include "boost/python.hpp"

#include "GafferRenderMan/InteractiveRenderManRender.h"
#include "GafferRenderMan/RenderManAttributes.h"
#include "GafferRenderMan/RenderManIntegrator.h"
#include "GafferRenderMan/RenderManLight.h"
#include "GafferRenderMan/RenderManOptions.h"
#include "GafferRenderMan/RenderManRender.h"
#include "GafferRenderMan/RenderManShader.h"
#include "GafferRenderMan/TagPlug.h"

#include "GafferDispatchBindings/TaskNodeBinding.h"

#include "GafferBindings/PlugBinding.h"

#include "boost/python/suite/indexing/container_utils.hpp"
#include "boost/python/stl_iterator.hpp"

using namespace boost::python;
using namespace Gaffer;
using namespace GafferBindings;
using namespace GafferDispatchBindings;
using namespace GafferRenderMan;

namespace
{

void loadShader( RenderManLight &l, const std::string &shaderName )
{
	IECorePython::ScopedGILRelease gilRelease;
	l.loadShader( shaderName );
}

TagPlugPtr tagPlugConstructor( const std::string &name, Plug::Direction direction, object pythonTags, unsigned flags )
{
	stl_input_iterator<IECore::InternedString> begin( pythonTags ), end;
	TagPlug::Tags tags( begin, end );

	return new TagPlug( name, direction, tags, flags );
}

object tagPlugTags( const TagPlug &p )
{
	list l;
	for( auto &t : p.tags() )
	{
		l.append( t.string() );
	}
	PyObject *set = PySet_New( l.ptr() );
	return object( handle<>( set ) );
}

} // namespace

BOOST_PYTHON_MODULE( _GafferRenderMan )
{
	TaskNodeClass<RenderManRender>();
	NodeClass<InteractiveRenderManRender>();
	GafferBindings::DependencyNodeClass<RenderManShader>();
	GafferBindings::DependencyNodeClass<RenderManLight>()
		.def( "loadShader", &loadShader )
	;
	GafferBindings::DependencyNodeClass<RenderManAttributes>();
	GafferBindings::DependencyNodeClass<RenderManIntegrator>();
	GafferBindings::DependencyNodeClass<RenderManOptions>();

	PlugClass<TagPlug>()
		.def(
			"__init__",
			make_constructor(
				&tagPlugConstructor,
				default_call_policies(),
				(
					arg( "name" ) = Gaffer::GraphComponent::defaultName<TagPlug>(),
					arg( "direction" ) = Gaffer::Plug::In,
					arg( "tags" ) = list(),
					arg( "flags" ) = Gaffer::Plug::Default
				)
			)
		)
		.def( "tags", &tagPlugTags )
	;
}
