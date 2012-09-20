//////////////////////////////////////////////////////////////////////////
//  
//  Copyright (c) 2011-2012, John Haddon. All rights reserved.
//  Copyright (c) 2012, Image Engine Design Inc. All rights reserved.
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

#include "boost/bind.hpp"
#include "boost/bind/placeholders.hpp"

#include "IECore/SimpleTypedData.h"
#include "IECore/WorldBlock.h"
#include "IECore/VectorTypedData.h"

#include "IECoreGL/Renderer.h"
#include "IECoreGL/Scene.h"
#include "IECoreGL/Camera.h"
#include "IECoreGL/State.h"
#include "IECoreGL/Group.h"
#include "IECoreGL/NameStateComponent.h"
#include "IECoreGL/TypedStateComponent.h"

#include "GafferUI/RenderableGadget.h"
#include "GafferUI/ViewportGadget.h"
#include "GafferUI/Style.h"

using namespace GafferUI;
using namespace Imath;
using namespace std;

IE_CORE_DEFINERUNTIMETYPED( RenderableGadget );

RenderableGadget::RenderableGadget( IECore::VisibleRenderablePtr renderable )
	: Gadget( staticTypeName() ),
	  m_renderable( 0 ),
	  m_scene( 0 ),
	  m_baseState( new IECoreGL::State( true ) ),
	  m_selectionColor( new IECoreGL::WireframeColorStateComponent( Color4f( 0.466f, 0.612f, 0.741f, 1.0f ) ) ),
	  m_dragSelecting( false )
{
	// IECoreGL default wireframe colour is far too similar to the blue we use for selection.
	m_baseState->add( new IECoreGL::WireframeColorStateComponent( Color4f( 0.65f, 0.65f, 0.65f, 1.0f ) ) );

	buttonPressSignal().connect( boost::bind( &RenderableGadget::buttonPress, this, ::_1,  ::_2 ) );
	dragBeginSignal().connect( boost::bind( &RenderableGadget::dragBegin, this, ::_1, ::_2 ) );
	dragEnterSignal().connect( boost::bind( &RenderableGadget::dragEnter, this, ::_1, ::_2 ) );
	dragMoveSignal().connect( boost::bind( &RenderableGadget::dragMove, this, ::_1, ::_2 ) );
	dragEndSignal().connect( boost::bind( &RenderableGadget::dragEnd, this, ::_1, ::_2 ) );

	setRenderable( renderable );
}

RenderableGadget::~RenderableGadget()
{
}

Imath::Box3f RenderableGadget::bound() const
{
	if( m_renderable )
	{
		return m_renderable->bound();
	}
	else
	{
		return Imath::Box3f();
	}
}

void RenderableGadget::doRender( const Style *style ) const
{
	if( m_scene )
	{
		m_scene->render( m_baseState );
	}
	
	if( m_dragSelecting )
	{
		const ViewportGadget *viewportGadget = ancestor<ViewportGadget>();
		ViewportGadget::RasterScope rasterScope( viewportGadget );
		
		Box2f b;
		b.extendBy( viewportGadget->gadgetToRasterSpace( m_dragStartPosition, this ) );
		b.extendBy( viewportGadget->gadgetToRasterSpace( m_lastDragPosition, this ) );
		
		style->renderSelectionBox( b );
	}
}

void RenderableGadget::setRenderable( IECore::VisibleRenderablePtr renderable )
{
	if( renderable!=m_renderable )
	{
		m_renderable = renderable;
		m_scene = 0;
		if( m_renderable )
		{
			IECoreGL::RendererPtr renderer = new IECoreGL::Renderer;
			renderer->setOption( "gl:mode", new IECore::StringData( "deferred" ) );
			{
				IECore::WorldBlock world( renderer );
				m_renderable->render( renderer );
			}	
			m_scene = renderer->scene();
			m_scene->setCamera( 0 );	
			applySelection();
		}
		renderRequestSignal()( this );
	}
}

IECore::VisibleRenderablePtr RenderableGadget::getRenderable()
{
	return m_renderable;
}

IECore::ConstVisibleRenderablePtr RenderableGadget::getRenderable() const
{
	return m_renderable;
}

IECoreGL::State *RenderableGadget::baseState()
{
	return m_baseState.get();
}

std::string RenderableGadget::objectAt( const IECore::LineSegment3f &lineInGadgetSpace ) const
{
	std::vector<IECoreGL::HitRecord> selection;
	{
		ViewportGadget::SelectionScope selectionScope( lineInGadgetSpace, this, selection );
		m_scene->render( m_baseState );
	}
	if( !selection.size() )
	{
		return "";
	}
	return selection[0].name.value();
}

RenderableGadget::Selection &RenderableGadget::getSelection()
{
	return m_selection;
}

const RenderableGadget::Selection &RenderableGadget::getSelection() const
{
	return m_selection;
}

void RenderableGadget::setSelection( const std::set<std::string> &selection )
{
	if( selection == m_selection )
	{
		return;
	}
	
	m_selection = selection;
	applySelection();
	m_selectionChangedSignal( this );
	renderRequestSignal()( this );
}

RenderableGadget::SelectionChangedSignal &RenderableGadget::selectionChangedSignal()
{
	return m_selectionChangedSignal;
}

bool RenderableGadget::buttonPress( GadgetPtr gadget, const ButtonEvent &event )
{
	if( event.buttons != ButtonEvent::Left || !m_scene )
	{
		return false;
	}
	
	std::string objectUnderMouse = objectAt( event.line );

	bool shiftHeld = event.modifiers && ButtonEvent::Shift;
	bool selectionChanged = false;
	if( objectUnderMouse == "" )
	{
		// background click - clear the selection unless
		// shift is held in which case we might be starting
		// a drag to add more.
		if( !shiftHeld )
		{
			m_selection.clear();
			selectionChanged = true;
		}
	}
	else
	{
		bool objectSelectedAlready = m_selection.find( objectUnderMouse ) != m_selection.end();
		
		if( objectSelectedAlready )
		{
			if( shiftHeld )
			{
				m_selection.erase( objectUnderMouse );
				selectionChanged = true;
			}
		}
		else
		{
			if( !shiftHeld )
			{
				m_selection.clear();
			}
			m_selection.insert( objectUnderMouse );
			selectionChanged = true;
		}
	}
	
	if( selectionChanged )
	{
		applySelection();
		m_selectionChangedSignal( this );
		renderRequestSignal()( this );
	}
	return true;
}

IECore::RunTimeTypedPtr RenderableGadget::dragBegin( GadgetPtr gadget, const DragDropEvent &event )
{
	if( !m_scene )
	{
		return false;
	}
	
	std::string objectUnderMouse = objectAt( event.line );
	if( objectUnderMouse == "" )
	{
		// drag to select
		m_dragStartPosition = m_lastDragPosition = event.line.p0;
		m_dragSelecting = true;
		renderRequestSignal()( this );
		return this;
	}
	else
	{
		if( m_selection.find( objectUnderMouse ) != m_selection.end() )
		{
			// drag the selection somewhere
			IECore::StringVectorDataPtr dragData = new IECore::StringVectorData();
			dragData->writable().insert( dragData->writable().end(), m_selection.begin(), m_selection.end() );
			return dragData;
		}
	}
	return 0;
}

bool RenderableGadget::dragEnter( GadgetPtr gadget, const DragDropEvent &event )
{
	return event.sourceGadget == this && event.data == this;
}

bool RenderableGadget::dragMove( GadgetPtr gadget, const DragDropEvent &event )
{
	m_lastDragPosition = event.line.p1;
	renderRequestSignal()( this );
	return true;
}

bool RenderableGadget::dragEnd( GadgetPtr gadget, const DragDropEvent &event )
{
	if( !m_dragSelecting )
	{
		return false;
	}
	
	m_dragSelecting = false;

	std::vector<IECoreGL::HitRecord> selection;
	{
		ViewportGadget::SelectionScope selectionScope( m_dragStartPosition, m_lastDragPosition, this, selection );
		m_scene->render( m_baseState );
	}
	
	bool selectionChanged = false;
	for( std::vector<IECoreGL::HitRecord>::const_iterator it = selection.begin(), eIt = selection.end(); it != eIt; it++ )
	{
		const std::string &name = it->name.value();
		if( m_selection.find( name ) == m_selection.end() )
		{
			m_selection.insert( name );
			selectionChanged = true;
		}
	}
	
	if( selectionChanged )
	{
		applySelection();
		m_selectionChangedSignal( this );
	}
	
	renderRequestSignal()( this );
	return true;
}

void RenderableGadget::applySelection( IECoreGL::Group *group )
{
	if( !group )
	{
		if( !m_scene )
		{
			return;
		}
		group = m_scene->root();
	}
	
	IECoreGL::State *state = group->getState();
	IECoreGL::NameStateComponent *nameState = state->get<IECoreGL::NameStateComponent>();
	if( nameState && m_selection.find( nameState->name() ) != m_selection.end() )
	{
		state->add( m_selectionColor );
	}
	else
	{
		state->remove<IECoreGL::WireframeColorStateComponent>();
	}
	
	const IECoreGL::Group::ChildContainer &children = group->children();
	for( IECoreGL::Group::ChildContainer::const_iterator it = children.begin(), eIt = children.end(); it != eIt; it++ )
	{
		IECoreGL::Group *childGroup = IECore::runTimeCast<IECoreGL::Group>( (*it).get() );
		if( childGroup )
		{
			applySelection( childGroup );
		}
	}
}
