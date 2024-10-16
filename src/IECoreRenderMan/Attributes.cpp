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

#include "Attributes.h"

#include "ParamListAlgo.h"

#include "IECoreScene/ShaderNetwork.h"

#include "boost/algorithm/string.hpp"
#include "boost/algorithm/string/predicate.hpp"

#include "fmt/format.h"

using namespace std;
using namespace IECore;
using namespace IECoreScene;
using namespace IECoreRenderMan;

namespace
{

const string g_renderManPrefix( "renderman:" );
InternedString g_surfaceShaderAttributeName( "renderman:bxdf" );
InternedString g_lightShaderAttributeName( "renderman:light" );

template<typename T>
T *reportedCast( const IECore::RunTimeTyped *v, const char *type, const IECore::InternedString &name )
{
	T *t = IECore::runTimeCast<T>( v );
	if( t )
	{
		return t;
	}

	IECore::msg( IECore::Msg::Warning, "IECoreRenderMan::Renderer", fmt::format( "Expected {} but got {} for {} \"{}\".", T::staticTypeName(), v->typeName(), type, name.c_str() ) );
	return nullptr;
}

template<typename T, typename MapType>
const T *parameter( const MapType &parameters, const IECore::InternedString &name )
{
	auto it = parameters.find( name );
	if( it == parameters.end() )
	{
		return nullptr;
	}

	return reportedCast<const T>( it->second.get(), "parameter", name );
}

} // namespace

Attributes::Attributes( const IECore::CompoundObject *attributes, MaterialCache *materialCache )
{
	m_material = materialCache->get( parameter<ShaderNetwork>( attributes->members(), g_surfaceShaderAttributeName ) );
	m_lightShader = parameter<ShaderNetwork>( attributes->members(), g_lightShaderAttributeName );

	for( const auto &attribute : attributes->members() )
	{
		if( boost::starts_with( attribute.first.c_str(), g_renderManPrefix.c_str() ) )
		{
			if( auto data = runTimeCast<const Data>( attribute.second.get() ) )
			{
				ParamListAlgo::convertParameter( RtUString( attribute.first.c_str() + g_renderManPrefix.size() ), data, m_paramList );
			}
		}
		else if( boost::starts_with( attribute.first.c_str(), "user:" ) )
		{
			if( auto data = runTimeCast<const Data>( attribute.second.get() ) )
			{
				ParamListAlgo::convertParameter( RtUString( attribute.first.c_str() ), data, m_paramList );
			}
		}
	}
}

Attributes::~Attributes()
{
}

const Material *Attributes::material() const
{
	return m_material.get();
}

const IECoreScene::ShaderNetwork *Attributes::lightShader() const
{
	return m_lightShader.get();
}

const RtParamList &Attributes::paramList() const
{
	return m_paramList;
}
