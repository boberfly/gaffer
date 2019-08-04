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

#include "GafferRenderMan/RenderManShader.h"

#include "GafferRenderMan/TagPlug.h"

#include "Gaffer/CompoundNumericPlug.h"
#include "Gaffer/NumericPlug.h"
#include "Gaffer/PlugAlgo.h"
#include "Gaffer/StringPlug.h"

#include "IECore/SearchPath.h"
#include "IECore/MessageHandler.h"

#include "boost/lexical_cast.hpp"
#include "boost/property_tree/xml_parser.hpp"

#include <unordered_set>

using namespace std;
using namespace Imath;
using namespace IECore;
using namespace IECoreScene;
using namespace Gaffer;
using namespace GafferScene;
using namespace GafferRenderMan;

//////////////////////////////////////////////////////////////////////////
// RenderManShader
//////////////////////////////////////////////////////////////////////////

IE_CORE_DEFINERUNTIMETYPED( RenderManShader );

RenderManShader::RenderManShader( const std::string &name )
	:	GafferScene::Shader( name )
{
	/// \todo It would be better if the Shader base class added this
	/// output plug, but that means changing ArnoldShader.
	addChild( new Plug( "out", Plug::Out ) );
}

RenderManShader::~RenderManShader()
{
}

//////////////////////////////////////////////////////////////////////////
// Shader loading code
//////////////////////////////////////////////////////////////////////////

namespace
{

template<typename PlugType>
Plug *loadNumericParameter( const boost::property_tree::ptree &parameter, IECore::InternedString name, Plug *parent )
{
	typedef typename PlugType::ValueType ValueType;

	const ValueType defaultValue = parameter.get( "<xmlattr>.default", ValueType( 0 ) );
	const ValueType minValue = parameter.get( "<xmlattr>.min", limits<ValueType>::min() );
	const ValueType maxValue = parameter.get( "<xmlattr>.max", limits<ValueType>::max() );

	PlugType *existingPlug = parent->template getChild<PlugType>( name );
	if(
		existingPlug &&
		existingPlug->defaultValue() == defaultValue &&
		existingPlug->minValue() == minValue &&
		existingPlug->maxValue() == maxValue
	)
	{
		return existingPlug;
	}

	typename PlugType::Ptr plug = new PlugType( name, parent->direction(), defaultValue, minValue, maxValue );

	if( existingPlug )
	{
		PlugAlgo::replacePlug( parent, plug );
	}
	else
	{
		parent->setChild( name, plug );
	}

	return plug.get();
}

template<typename T>
T parseCompoundNumericValue( const string &s )
{
	typedef typename T::BaseType BaseType;
	typedef boost::tokenizer<boost::char_separator<char> > Tokenizer;

	T result( 0 );
	unsigned int i = 0;
	for( auto token : Tokenizer( s, boost::char_separator<char>( " " ) ) )
	{
		if( i >= T::dimensions() )
		{
			break;
		}
		result[i++] = boost::lexical_cast<BaseType>( token );
	}

	return result;
}

template<typename PlugType>
Plug *loadCompoundNumericParameter( const boost::property_tree::ptree &parameter, IECore::InternedString name, IECore::GeometricData::Interpretation interpretation, Plug *parent )
{
	typedef typename PlugType::ValueType ValueType;
	typedef typename ValueType::BaseType BaseType;

	const ValueType defaultValue = parseCompoundNumericValue<ValueType>( parameter.get( "<xmlattr>.default", "0 0 0" ) );
	const ValueType minValue( limits<BaseType>::min() );
	const ValueType maxValue( limits<BaseType>::max() );

	PlugType *existingPlug = parent->template getChild<PlugType>( name );
	if(
		existingPlug &&
		existingPlug->defaultValue() == defaultValue &&
		existingPlug->minValue() == minValue &&
		existingPlug->maxValue() == maxValue &&
		existingPlug->interpretation() == interpretation
	)
	{
		return existingPlug;
	}

	typename PlugType::Ptr plug = new PlugType( name, parent->direction(), defaultValue, minValue, maxValue, Plug::Default, interpretation );

	if( existingPlug )
	{
		PlugAlgo::replacePlug( parent, plug );
	}
	else
	{
		parent->setChild( name, plug );
	}

	return plug.get();
}

Plug *loadStringParameter( const boost::property_tree::ptree &parameter, IECore::InternedString name, Plug *parent )
{
	const string defaultValue = parameter.get( "<xmlattr>.default", "" );

	StringPlug *existingPlug = parent->getChild<StringPlug>( name );
	if(
		existingPlug &&
		existingPlug->defaultValue() == defaultValue
	)
	{
		return existingPlug;
	}

	PlugPtr plug = new StringPlug( name, parent->direction(), defaultValue );

	if( existingPlug )
	{
		PlugAlgo::replacePlug( parent, plug );
	}
	else
	{
		parent->setChild( name, plug );
	}

	return plug.get();
}

Plug *loadTagParameter( const TagPlug::Tags &tags, IECore::InternedString name, Plug *parent )
{
	TagPlug *existingPlug = parent->getChild<TagPlug>( name );
	if( existingPlug && existingPlug->tags() == tags )
	{
		return existingPlug;
	}

	PlugPtr plug = new TagPlug( name, parent->direction(), tags );
	if( existingPlug )
	{
		PlugAlgo::replacePlug( parent, plug );
	}
	else
	{
		parent->setChild( name, plug );
	}

	return plug.get();
}

Plug *loadTagParameter( const boost::property_tree::ptree &parameter, IECore::InternedString name, Plug *parent )
{
	TagPlug::Tags tags;
	for( const auto &tag : parameter.get_child( "tags" ) )
	{
		tags.insert( tag.second.get<string>( "<xmlattr>.value" ) );
	}

	return loadTagParameter( tags, name, parent );
}

Gaffer::Plug *loadParameter( const boost::property_tree::ptree &parameter, Plug *parent )
{
	if( parameter.get<string>( "<xmlattr>.omitFromRender", "False" ) == "True" )
	{
		// Ignore those pesky "notes" parameters
		return nullptr;
	}

	const string name = parameter.get<string>( "<xmlattr>.name" );
	const string type = parameter.get<string>( "<xmlattr>.type" );
	if( type == "float" )
	{
		return loadNumericParameter<FloatPlug>( parameter, name, parent );
	}
	else if( type == "int" )
	{
		return loadNumericParameter<IntPlug>( parameter, name, parent );
	}
	else if( type == "point" )
	{
		return loadCompoundNumericParameter<V3fPlug>( parameter, name, GeometricData::Point, parent );
	}
	else if( type == "vector" )
	{
		return loadCompoundNumericParameter<V3fPlug>( parameter, name, GeometricData::Vector, parent );
	}
	else if( type == "normal" )
	{
		return loadCompoundNumericParameter<V3fPlug>( parameter, name, GeometricData::Normal, parent );
	}
	else if( type == "color" )
	{
		return loadCompoundNumericParameter<Color3fPlug>( parameter, name, GeometricData::None, parent );
	}
	else if( type == "string" )
	{
		return loadStringParameter( parameter, name, parent );
	}
	else if( type == "struct" )
	{
		return loadTagParameter( parameter, name, parent );
	}
	else
	{
		msg(
			IECore::Msg::Warning, "RenderManShader::loadShader",
			boost::format( "Parameter \"%s\" has unsupported type \"%s\"" ) % name % type
		);
		return nullptr;
	}
}

void loadParameters( const boost::property_tree::ptree &tree, Plug *parent, std::unordered_set<const Plug *> &validPlugs )
{
	for( const auto &child : tree )
	{
		if( child.first == "param" )
		{
			if( Plug *p = loadParameter( child.second, parent ) )
			{
				validPlugs.insert( p );
			}
		}
		else if( child.first == "page" )
		{
			loadParameters( child.second, parent, validPlugs );
		}
	}
}

void loadParameters( const boost::property_tree::ptree &tree, Plug *parent )
{
	// Load all the parameters

	std::unordered_set<const Plug *> validPlugs;
	loadParameters( tree, parent, validPlugs );

	// Remove any old plugs which it turned out we didn't need.

	for( int i = parent->children().size() - 1; i >= 0; --i )
	{
		Plug *child = parent->getChild<Plug>( i );
		if( validPlugs.find( child ) == validPlugs.end() )
		{
			parent->removeChild( child );
		}
	}
}

template<typename T>
Plug *loadTypedOutput( const IECore::InternedString &name, Plug *parent )
{
	T *existingPlug = parent->getChild<T>( name );
	if( existingPlug )
	{
		return existingPlug;
	}

	PlugPtr plug = new T( name, Plug::Out );
	parent->setChild( name, plug );
	return plug.get();
}

Gaffer::Plug *loadOutput( const boost::property_tree::ptree &output, Plug *parent )
{
	const string name = output.get<string>( "<xmlattr>.name" );

	TagPlug::Tags tags;
	for( const auto &tag : output.get_child( "tags" ) )
	{
		tags.insert( tag.second.get<string>( "<xmlattr>.value" ) );
	}

	if( tags.find( "color" ) != tags.end() )
	{
		return loadTypedOutput<Color3fPlug>( name, parent );
	}
	else if( tags.find( "float" ) != tags.end() )
	{
		return loadTypedOutput<FloatPlug>( name, parent );
	}
	else if( tags.find( "vector" ) != tags.end() )
	{
		return loadTypedOutput<V3fPlug>( name, parent );
	}
	else if( tags.size() )
	{
		return loadTagParameter( tags, name, parent );
	}
	else
	{
		msg(
			IECore::Msg::Warning, "RenderManShader::loadShader",
			boost::format( "Output \"%s\" has unsupported tags" ) % name
		);
		return nullptr;
	}
}

void loadOutputs( const boost::property_tree::ptree &tree, Plug *parent )
{
	// Load all the parameters

	std::unordered_set<const Plug *> validPlugs;
	for( const auto &child : tree )
	{
		if( child.first == "output" )
		{
			if( const Plug *p = loadOutput( child.second, parent ) )
			{
				validPlugs.insert( p );
			}
		}
	}

	// Remove any old plugs which it turned out we didn't need.

	for( int i = parent->children().size() - 1; i >= 0; --i )
	{
		Plug *child = parent->getChild<Plug>( i );
		if( validPlugs.find( child ) == validPlugs.end() )
		{
			parent->removeChild( child );
		}
	}
}

} // namespace

void RenderManShader::loadShader( const std::string &shaderName, bool keepExistingValues )
{
	const char *pluginPath = getenv( "RMAN_RIXPLUGINPATH" );
	SearchPath searchPath( pluginPath ? pluginPath : "" );

	boost::filesystem::path argsFilename = searchPath.find( "Args/" + shaderName + ".args" );
	if( argsFilename.empty() )
	{
		throw IECore::Exception(
			boost::str(
				boost::format( "Unable to find shader \"%s\" on RMAN_RIXPLUGINPATH" ) % shaderName
			)
		);
	}

	std::ifstream argsStream( argsFilename.string() );

	boost::property_tree::ptree tree;
	boost::property_tree::read_xml( argsStream, tree );

	namePlug()->source<StringPlug>()->setValue( shaderName );

	string shaderType = tree.get<string>( "args.shaderType.tag.<xmlattr>.value" );

	typePlug()->source<StringPlug>()->setValue( "renderman:" + shaderType );

	Plug *parametersPlug = this->parametersPlug()->source<Plug>();
	if( !keepExistingValues )
	{
		parametersPlug->clearChildren();
	}

	loadParameters( tree.get_child( "args" ), parametersPlug );

	if( !keepExistingValues )
	{
		outPlug()->clearChildren();
	}

	loadOutputs( tree.get_child( "args" ), outPlug() );

}
