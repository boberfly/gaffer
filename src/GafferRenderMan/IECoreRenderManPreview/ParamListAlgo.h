//////////////////////////////////////////////////////////////////////////
//
//  Copyright (c) 2019, John Haddon. All rights reserved.
//
//  Redistribution and use in source and binary forms, with or without
//  modification, are permitted provided that the following conditions are
//  met:
//
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//
//     * Redistributions in binary form must reproduce the above copyright
//       notice, this list of conditions and the following disclaimer in the
//       documentation and/or other materials provided with the distribution.
//
//     * Neither the name of Image Engine Design nor the names of any
//       other contributors to this software may be used to endorse or
//       promote products derived from this software without specific prior
//       written permission.
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

#ifndef IECORERENDERMAN_PARAMLISTALGO_H
#define IECORERENDERMAN_PARAMLISTALGO_H

#include "IECore/CompoundData.h"

#include "Riley.h"
#include "RixParamList.h"

#include <memory>

namespace IECoreRenderMan
{

namespace ParamListAlgo
{

namespace Detail
{

struct ParamListDeleter
{

	ParamListDeleter( RixRileyManager *manager )
		:	m_manager( manager )
	{
	}
	void operator()( RixParamList *p ) const
	{
		m_manager->DestroyRixParamList( p );
	}

	private :

		RixRileyManager *m_manager;

};

} // namespace Detail

using RixParamListPtr = std::unique_ptr<RixParamList, Detail::ParamListDeleter>;

/// Convenience function to make a scoped parameter list
inline RixParamListPtr makeParamList( RixRileyManager *manager = nullptr )
{
	if( !manager )
	{
		manager = (RixRileyManager *)RixGetContext()->GetRixInterface( k_RixRileyManager );
	}
	return RixParamListPtr( manager->CreateRixParamList(), Detail::ParamListDeleter( manager ) );
}

inline RixParamListPtr makePrimitiveVariableList( size_t numUniform, size_t numVertex, size_t numVarying, size_t numFaceVarying, RixRileyManager *manager = nullptr )
{
	if( !manager )
	{
		manager = (RixRileyManager *)RixGetContext()->GetRixInterface( k_RixRileyManager );
	}
	return RixParamListPtr(
		manager->CreateRixParamList( numUniform, numVertex, numVarying, numFaceVarying ),
		Detail::ParamListDeleter( manager )
	);
}

void convertParameter( const RtUString &name, const IECore::Data *data, RixParamList &paramList );
void convertParameters( const IECore::CompoundDataMap &parameters, RixParamList &paramList );

} // namespace ParamListAlgo

} // namespace IECoreRenderMan

#endif // IECORERENDERMAN_PARAMLISTALGO_H
