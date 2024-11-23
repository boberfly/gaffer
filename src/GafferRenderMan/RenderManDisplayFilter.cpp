//////////////////////////////////////////////////////////////////////////
//
//  Copyright (c) 2024, Alex Fuller. All rights reserved.
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

#include "GafferRenderMan/RenderManDisplayFilter.h"

#include "GafferScene/Shader.h"
#include "GafferScene/ShaderPlug.h"

#include "Gaffer/StringPlug.h"

#include "IECoreScene/ShaderNetwork.h"
#include "IECoreScene/ShaderNetworkAlgo.h"

using namespace IECore;
using namespace IECoreScene;
using namespace Gaffer;
using namespace GafferScene;
using namespace GafferRenderMan;

namespace
{

const InternedString g_filterParameterName( "filter" );
const InternedString g_firstFilterParameterName( "filter[0]" );
const InternedString g_secondFilterParameterName( "filter[1]" );
const InternedString g_displayFilterAttributeName( "ri:displayfilter" );
const InternedString g_displayFilterOptionName( "option:ri:displayfilter" );
const InternedString g_displayFilterCombinerName( "__displayFilterCombiner" );

} // namespace

GAFFER_NODE_DEFINE_TYPE( RenderManDisplayFilter );

size_t RenderManDisplayFilter::g_firstPlugIndex = 0;

RenderManDisplayFilter::RenderManDisplayFilter( const std::string &name )
	:	GlobalsProcessor( name )
{
	storeIndexOfNextChild( g_firstPlugIndex );
	addChild( new ShaderPlug( "displayfilter" ) );
	addChild( new IntPlug( "mode", Plug::In, (int)Mode::Replace, (int)Mode::Replace, (int)Mode::InsertLast ) );
}

RenderManDisplayFilter::~RenderManDisplayFilter()
{
}

GafferScene::ShaderPlug *RenderManDisplayFilter::displayFilterPlug()
{
	return getChild<ShaderPlug>( g_firstPlugIndex );
}

const GafferScene::ShaderPlug *RenderManDisplayFilter::displayFilterPlug() const
{
	return getChild<ShaderPlug>( g_firstPlugIndex );
}

Gaffer::IntPlug *RenderManDisplayFilter::modePlug()
{
	return getChild<IntPlug>( g_firstPlugIndex + 1 );
}

const Gaffer::IntPlug *RenderManDisplayFilter::modePlug() const
{
	return getChild<IntPlug>( g_firstPlugIndex + 1 );
}

bool RenderManDisplayFilter::acceptsInput( const Gaffer::Plug *plug, const Gaffer::Plug *inputPlug ) const
{
	if( !GlobalsProcessor::acceptsInput( plug, inputPlug ) )
	{
		return false;
	}

	if( plug != displayFilterPlug() )
	{
		return true;
	}

	if( !inputPlug )
	{
		return true;
	}

	const Plug *sourcePlug = inputPlug->source();
	auto *sourceShader = runTimeCast<const GafferScene::Shader>( sourcePlug->node() );
	if( !sourceShader )
	{
		return true;
	}

	const Plug *sourceShaderOutPlug = sourceShader->outPlug();
	if( !sourceShaderOutPlug )
	{
		return true;
	}

	if( sourcePlug != sourceShaderOutPlug && !sourceShaderOutPlug->isAncestorOf( sourcePlug ) )
	{
		return true;
	}

	return sourceShader->typePlug()->getValue() == "ri:displayfilter";
}

void RenderManDisplayFilter::affects( const Plug *input, AffectedPlugsContainer &outputs ) const
{
	GlobalsProcessor::affects( input, outputs );

	if( input == displayFilterPlug() || input == modePlug() )
	{
		outputs.push_back( outPlug()->globalsPlug() );
	}
}

void RenderManDisplayFilter::hashProcessedGlobals( const Gaffer::Context *context, IECore::MurmurHash &h ) const
{
	h.append( displayFilterPlug()->attributesHash() );
	modePlug()->hash( h );
}

IECore::ConstCompoundObjectPtr RenderManDisplayFilter::computeProcessedGlobals( const Gaffer::Context *context, IECore::ConstCompoundObjectPtr inputGlobals ) const
{
	ConstCompoundObjectPtr attributes = displayFilterPlug()->attributes();
	if( attributes->members().empty() )
	{
		return inputGlobals;
	}

	const IECoreScene::ShaderNetwork *displayFilter = attributes->member<IECoreScene::ShaderNetwork>( g_displayFilterAttributeName );
	if( !displayFilter )
	{
		throw IECore::Exception( "DisplayFilter not found" );
	}

	CompoundObjectPtr result = new CompoundObject;
	// Since we're not going to modify any existing members (only add new ones),
	// and our result becomes const on returning it, we can directly reference
	// the input members in our result without copying. Be careful not to modify
	// them though!
	result->members() = inputGlobals->members();

	const Mode mode = (Mode)modePlug()->getValue();
	if( mode == Mode::InsertFirst || mode == Mode::InsertLast )
	{
		const ShaderNetwork *inputDisplayFilter = inputGlobals->member<ShaderNetwork>( g_displayFilterOptionName );
		if( !inputDisplayFilter || !inputDisplayFilter->size() )
		{
			result->members()[g_displayFilterOptionName] = const_cast<ShaderNetwork *>( displayFilter );
		}
		else
		{
			ShaderNetworkPtr mergedDisplayFilter = inputDisplayFilter->copy();
			ShaderNetwork::Parameter insertedOut = ShaderNetworkAlgo::addShaders( mergedDisplayFilter.get(), displayFilter );

			const IECoreScene::Shader *outputShader = inputDisplayFilter->outputShader();
			if( outputShader->getName() != "PxrDisplayFilterCombiner" )
			{
				// Create new combiner shader and make it the output
				IECoreScene::ShaderPtr combineShader = new IECoreScene::Shader( "PxrDisplayFilterCombiner", "ri:displayfilter" );
				combineShader->parameters()[g_firstFilterParameterName] = new StringData( "filter" );
				combineShader->parameters()[g_secondFilterParameterName] = new StringData( "filter" );
				IECore::InternedString combineHandle = mergedDisplayFilter->addShader( g_displayFilterCombinerName, std::move( combineShader ) );
				mergedDisplayFilter->setOutput( { combineHandle, "out" } );

				// There is only two filters, so set the connection order based on the order mode
				mergedDisplayFilter->addConnection( {
					inputDisplayFilter->getOutput(),
					{ combineHandle, mode == Mode::InsertLast ? g_firstFilterParameterName : g_secondFilterParameterName }
				} );
				mergedDisplayFilter->addConnection( {
					insertedOut,
					{ combineHandle, mode == Mode::InsertLast ? g_secondFilterParameterName : g_firstFilterParameterName }
				} );

				result->members()[g_displayFilterOptionName] = mergedDisplayFilter;
			}
			else
			{
				// Add new filter parameter to copied combine shader and replace the old shader with it
				const size_t arraySize = outputShader->parameters().size();
				IECoreScene::ShaderPtr combineShader = outputShader->copy();
				IECore::InternedString lastFilterParameterName = g_filterParameterName.string() + "[" + std::to_string( arraySize ) + "]";
				combineShader->parameters()[lastFilterParameterName] = new StringData( "filter" );
				mergedDisplayFilter->removeShader( mergedDisplayFilter->getOutput().shader );
				IECore::InternedString combineHandle = mergedDisplayFilter->addShader( g_displayFilterCombinerName, std::move( combineShader ) );
				mergedDisplayFilter->setOutput( { combineHandle, "out" } );

				if( mode == Mode::InsertLast )
				{
					// Add the existing connections back
					for( const ShaderNetwork::Connection &c : inputDisplayFilter->inputConnections( inputDisplayFilter->getOutput().shader ) )
					{
						mergedDisplayFilter->addConnection( {
							c.source,
							{ combineHandle, c.destination.name }
						} );
					}
					// Add the new filter to the last parameter
					mergedDisplayFilter->addConnection( {
						insertedOut,
						{ combineHandle, lastFilterParameterName }
					} );

					result->members()[g_displayFilterOptionName] = mergedDisplayFilter;
				}
				else
				{
					assert( mode == Mode::InsertFirst );
					// Add the new filter to the first parameter
					mergedDisplayFilter->addConnection( {
						insertedOut,
						{ combineHandle, g_firstFilterParameterName }
					} );
					// Add the existing connections back, appending the parameter name and preserve the existing ordering
					for( const ShaderNetwork::Connection &c : inputDisplayFilter->inputConnections( inputDisplayFilter->getOutput().shader ) )
					{
						// TODO: Is the iterator ordered? Do we need to go to this measure to ensure the right number?
						std::string destinationName = c.destination.name;
						std::size_t firstBracket = destinationName.find( "[" );
						assert( firstBracket != std::string::npos );
						std::size_t secondBracket = destinationName.find( "]" );
						assert( secondBracket != std::string::npos );
						std::string numberStr = destinationName.substr( firstBracket + 1, secondBracket - 1 );
						try
						{
							int idx = std::stoi( numberStr ) + 1;
							mergedDisplayFilter->addConnection( {
								c.source,
								{ combineHandle, g_filterParameterName.string() + "[" + std::to_string( idx ) + "]" }
							} );
						}
						catch( const std::exception &e )
						{
							IECore::msg(
								IECore::Msg::Error,
								"RenderManDisplayFilter::computeProcessedGlobals()",
								e.what()
							);
						}
					}
					result->members()[g_displayFilterOptionName] = mergedDisplayFilter;
				}
			}
		}
	}
	else
	{
		assert( mode == Mode::Replace );
		result->members()[g_displayFilterOptionName] = const_cast<ShaderNetwork *>( displayFilter );
	}

	return result;
}
