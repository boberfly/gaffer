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

#ifndef GAFFERUI_RENDERABLEGADGET_H
#define GAFFERUI_RENDERABLEGADGET_H

#include "IECore/VisibleRenderable.h"

#include "GafferUI/Gadget.h"

namespace IECoreGL
{

IE_CORE_FORWARDDECLARE( Scene )
IE_CORE_FORWARDDECLARE( State )
IE_CORE_FORWARDDECLARE( Group )
IE_CORE_FORWARDDECLARE( StateComponent )

} // namespace IECoreGL

namespace GafferUI
{

IE_CORE_FORWARDDECLARE( RenderableGadget );

/// \todo Should this be defined in GafferSceneUI instead?
class RenderableGadget : public Gadget
{

	public :

		RenderableGadget( IECore::VisibleRenderablePtr renderable );
		virtual ~RenderableGadget();

		IE_CORE_DECLARERUNTIMETYPEDEXTENSION( RenderableGadget, RenderableGadgetTypeId, Gadget );

		virtual Imath::Box3f bound() const;
		
		void setRenderable( IECore::VisibleRenderablePtr renderable );
		IECore::VisibleRenderablePtr getRenderable();
		IECore::ConstVisibleRenderablePtr getRenderable() const;
		
		/// Returns the IECoreGL::State object used as the base display
		/// style for the Renderable. This may be modified freely to
		/// change the display style.
		IECoreGL::State *baseState();
		
		/// Returns the name of the frontmost object intersecting the specified line
		/// through gadget space, or "" if there is no such object.
		std::string objectAt( const IECore::LineSegment3f &lineInGadgetSpace ) const;
		
		/// @name Selection
		/// The RenderableGadget maintains a set of selected object, based
		/// on object name. The user can manipulate the selection with the
		/// mouse, and the selected objects are drawn in a highlighted fashion.
		/// The selection may be queried and set programatically, and the
		/// SelectionChangedSignal can be used to provide notifications of
		/// such changes.
		////////////////////////////////////////////////////////////////////
		//@{
		/// The selection is simply stored as a set of object names.
		typedef std::set<std::string> Selection;
		/// Returns the selection.
		Selection &getSelection();
		const Selection &getSelection() const;
		/// Sets the selection, triggering selectionChangedSignal() if
		/// necessary.
		void setSelection( const std::set<std::string> &selection );
		/// A signal emitted when the selection has changed, either through
		/// a call to setSelection() or through user action.
		typedef boost::signal<void ( RenderableGadgetPtr )> SelectionChangedSignal; 
		SelectionChangedSignal &selectionChangedSignal();
		//@}
		
	protected :
	
		virtual void doRender( const Style *style ) const;
		
	private :
	
		bool buttonPress( GadgetPtr gadget, const ButtonEvent &event );
		IECore::RunTimeTypedPtr dragBegin( GadgetPtr gadget, const DragDropEvent &event );	
		bool dragEnter( GadgetPtr gadget, const DragDropEvent &event );
		bool dragMove( GadgetPtr gadget, const DragDropEvent &event );
		bool dragEnd( GadgetPtr gadget, const DragDropEvent &event );
		
		void applySelection( IECoreGL::Group *group = 0 );
		
		IECore::VisibleRenderablePtr m_renderable;
		IECoreGL::ScenePtr m_scene;
		IECoreGL::StatePtr m_baseState;
		IECoreGL::StateComponentPtr m_selectionColor;
		
		Selection m_selection;
		SelectionChangedSignal m_selectionChangedSignal;
		
		Imath::V3f m_dragStartPosition;
		Imath::V3f m_lastDragPosition;
		bool m_dragSelecting;
		
};

} // namespace GafferUI

#endif // GAFFERUI_RenderableGadget_H
