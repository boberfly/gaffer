//////////////////////////////////////////////////////////////////////////
//
//  Copyright (c) 2024, Cinesite VFX Ltd. All rights reserved.
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

#include "GeometryPrototypeCache.h"

#include "GeometryAlgo.h"

#include "fmt/format.h"

using namespace std;
using namespace IECore;
using namespace IECoreScene;
using namespace IECoreRenderMan;

GeometryPrototypeCache::GeometryPrototypeCache( const Session *session )
	:	m_session( session )
{
}

GeometryPrototypePtr GeometryPrototypeCache::get( const IECore::Object *object, const Attributes *attributes )
{
	if( !object )
	{
		return nullptr;
	}

	auto converter = [] ( const IECore::Object *object, const Attributes *attributes, const Session *session, GeometryPrototypePtr &result ) {

		riley::DisplacementId displacement;
		if( auto d = attributes->displacement() )
		{
			displacement = d->id();
		}

		riley::GeometryPrototypeId id = GeometryAlgo::convert( object, displacement, attributes->prototypeAttributes(), session->riley );
		if( id != riley::GeometryPrototypeId::InvalidId() )
		{
			result = new GeometryPrototype( id, session );
		}

	};

	std::optional<IECore::MurmurHash> attributesHash = attributes->prototypeHash();
	if( !attributesHash )
	{
		// Automatic instancing disabled.
		GeometryPrototypePtr result;
		converter( object, attributes, m_session, result );
		return result;
	}

	IECore::MurmurHash h = *attributesHash;
	object->hash( h );

	auto [it, inserted] = m_cache.emplace(
		std::piecewise_construct, std::forward_as_tuple( h ), std::make_tuple()
	);
	std::call_once( it->second.onceFlag, converter, object, attributes, m_session, it->second.prototype );

	return it->second.prototype;
}

void GeometryPrototypeCache::clearUnused()
{
	for( auto it = m_cache.begin(); it != m_cache.end(); )
	{
		if( it->second.prototype && it->second.prototype->refCount() == 1 )
		{
			// Only one reference - this is ours, so nothing outside of the
			// cache is using the geometry prototype.
			it = m_cache.unsafe_erase( it );
		}
		else
		{
			++it;
		}
	}
}
