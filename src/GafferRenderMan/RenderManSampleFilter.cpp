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

#include "GafferRenderMan/RenderManSampleFilter.h"

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
const InternedString g_sampleFilterAttributeName( "ri:samplefilter" );
const InternedString g_sampleFilterOptionName( "option:ri:samplefilter" );
const InternedString g_sampleFilterCombinerName( "__sampleFilterCombiner" );

} // namespace

GAFFER_NODE_DEFINE_TYPE( RenderManSampleFilter );

size_t RenderManSampleFilter::g_firstPlugIndex = 0;

RenderManSampleFilter::RenderManSampleFilter( const std::string &name )
	:	GlobalsProcessor( name )
{
	storeIndexOfNextChild( g_firstPlugIndex );
	addChild( new ShaderPlug( "samplefilter" ) );
	addChild( new IntPlug( "mode", Plug::In, (int)Mode::Replace, (int)Mode::Replace, (int)Mode::InsertLast ) );
}

RenderManSampleFilter::~RenderManSampleFilter()
{
}

GafferScene::ShaderPlug *RenderManSampleFilter::sampleFilterPlug()
{
	return getChild<ShaderPlug>( g_firstPlugIndex );
}

const GafferScene::ShaderPlug *RenderManSampleFilter::sampleFilterPlug() const
{
	return getChild<ShaderPlug>( g_firstPlugIndex );
}

Gaffer::IntPlug *RenderManSampleFilter::modePlug()
{
	return getChild<IntPlug>( g_firstPlugIndex + 1 );
}

const Gaffer::IntPlug *RenderManSampleFilter::modePlug() const
{
	return getChild<IntPlug>( g_firstPlugIndex + 1 );
}

bool RenderManSampleFilter::acceptsInput( const Gaffer::Plug *plug, const Gaffer::Plug *inputPlug ) const
{
	if( !GlobalsProcessor::acceptsInput( plug, inputPlug ) )
	{
		return false;
	}

	if( plug != sampleFilterPlug() )
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

	return sourceShader->typePlug()->getValue() == "ri:samplefilter";
}

void RenderManSampleFilter::affects( const Plug *input, AffectedPlugsContainer &outputs ) const
{
	GlobalsProcessor::affects( input, outputs );

	if( input == sampleFilterPlug() || input == modePlug() )
	{
		outputs.push_back( outPlug()->globalsPlug() );
	}
}

void RenderManSampleFilter::hashProcessedGlobals( const Gaffer::Context *context, IECore::MurmurHash &h ) const
{
	h.append( sampleFilterPlug()->attributesHash() );
	modePlug()->hash( h );
}

IECore::ConstCompoundObjectPtr RenderManSampleFilter::computeProcessedGlobals( const Gaffer::Context *context, IECore::ConstCompoundObjectPtr inputGlobals ) const
{
	ConstCompoundObjectPtr attributes = sampleFilterPlug()->attributes();
	if( attributes->members().empty() )
	{
		return inputGlobals;
	}

	const IECoreScene::ShaderNetwork *sampleFilter = attributes->member<IECoreScene::ShaderNetwork>( g_sampleFilterAttributeName );
	if( !sampleFilter )
	{
		throw IECore::Exception( "SampleFilter not found" );
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
		const ShaderNetwork *inputSampleFilter = inputGlobals->member<ShaderNetwork>( g_sampleFilterOptionName );
		if( !inputSampleFilter || !inputSampleFilter->size() )
		{
			result->members()[g_sampleFilterOptionName] = const_cast<ShaderNetwork *>( sampleFilter );
		}
		else
		{
			ShaderNetworkPtr mergedSampleFilter = inputSampleFilter->copy();
			ShaderNetwork::Parameter insertedOut = ShaderNetworkAlgo::addShaders( mergedSampleFilter.get(), sampleFilter );

			const IECoreScene::Shader *outputShader = inputSampleFilter->outputShader();
			if( outputShader->getName() != "PxrSampleFilterCombiner" )
			{
				// Create new combiner shader and make it the output
				IECoreScene::ShaderPtr combineShader = new IECoreScene::Shader( "PxrSampleFilterCombiner", "ri:samplefilter" );
				combineShader->parameters()[g_firstFilterParameterName] = new StringData( "filter" );
				combineShader->parameters()[g_secondFilterParameterName] = new StringData( "filter" );
				IECore::InternedString combineHandle = mergedSampleFilter->addShader( g_sampleFilterCombinerName, std::move( combineShader ) );
				mergedSampleFilter->setOutput( { combineHandle, "out" } );

				// There is only two filters, so set the connection order based on the order mode
				mergedSampleFilter->addConnection( {
					inputSampleFilter->getOutput(),
					{ combineHandle, mode == Mode::InsertLast ? g_firstFilterParameterName : g_secondFilterParameterName }
				} );
				mergedSampleFilter->addConnection( {
					insertedOut,
					{ combineHandle, mode == Mode::InsertLast ? g_secondFilterParameterName : g_firstFilterParameterName }
				} );

				result->members()[g_sampleFilterOptionName] = mergedSampleFilter;
			}
			else
			{
				// Add new filter parameter to copied combine shader and replace the old shader with it
				const size_t arraySize = outputShader->parameters().size();
				IECoreScene::ShaderPtr combineShader = outputShader->copy();
				IECore::InternedString lastFilterParameterName = g_filterParameterName.string() + "[" + std::to_string( arraySize ) + "]";
				combineShader->parameters()[lastFilterParameterName] = new StringData( "filter" );
				mergedSampleFilter->removeShader( mergedSampleFilter->getOutput().shader );
				IECore::InternedString combineHandle = mergedSampleFilter->addShader( g_sampleFilterCombinerName, std::move( combineShader ) );
				mergedSampleFilter->setOutput( { combineHandle, "out" } );

				if( mode == Mode::InsertLast )
				{
					// Add the existing connections back
					for( const ShaderNetwork::Connection &c : inputSampleFilter->inputConnections( inputSampleFilter->getOutput().shader ) )
					{
						mergedSampleFilter->addConnection( {
							c.source,
							{ combineHandle, c.destination.name }
						} );
					}
					// Add the new filter to the last parameter
					mergedSampleFilter->addConnection( {
						insertedOut,
						{ combineHandle, lastFilterParameterName }
					} );

					result->members()[g_sampleFilterOptionName] = mergedSampleFilter;
				}
				else
				{
					assert( mode == Mode::InsertFirst );
					// Add the new filter to the first parameter
					mergedSampleFilter->addConnection( {
						insertedOut,
						{ combineHandle, g_firstFilterParameterName }
					} );
					// Add the existing connections back, appending the number on the parameter and preserve the existing ordering
					for( const ShaderNetwork::Connection &c : inputSampleFilter->inputConnections( inputSampleFilter->getOutput().shader ) )
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
							mergedSampleFilter->addConnection( {
								c.source,
								{ combineHandle, g_filterParameterName.string() + "[" + std::to_string( idx ) + "]" }
							} );
						}
						catch( const std::exception &e )
						{
							IECore::msg(
								IECore::Msg::Error,
								"RenderManSampleFilter::computeProcessedGlobals()",
								e.what()
							);
						}
					}
					result->members()[g_sampleFilterOptionName] = mergedSampleFilter;
				}
			}
		}
	}
	else
	{
		assert( mode == Mode::Replace );
		result->members()[g_sampleFilterOptionName] = const_cast<ShaderNetwork *>( sampleFilter );
	}

	return result;
}
