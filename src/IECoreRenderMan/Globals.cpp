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

#include "Globals.h"
#include "ParamListAlgo.h"
#include "Transform.h"

#include "IECoreScene/ShaderNetwork.h"

#include "IECore/SimpleTypedData.h"
#include "IECore/StringAlgo.h"

#include "RixPredefinedStrings.hpp"

#include "boost/algorithm/string.hpp"
#include "boost/algorithm/string/predicate.hpp"

#include "fmt/format.h"

using namespace std;
using namespace IECore;
using namespace IECoreScene;
using namespace IECoreRenderMan;

namespace
{

const string g_renderManPrefix( "ri:" );
const IECore::InternedString g_cameraOption( "camera" );
const IECore::InternedString g_layerName( "layerName" );
const IECore::InternedString g_sampleMotionOption( "sampleMotion" );
const IECore::InternedString g_frameOption( "frame" );
const IECore::InternedString g_integratorOption( "ri:integrator" );

const vector<InternedString> g_rejectedOutputFilterParameters = {
	"filter",
	"filterwidth"
};

template<typename T>
T *optionCast( const IECore::RunTimeTyped *v, const IECore::InternedString &name )
{
	if( !v )
	{
		return nullptr;
	}

	T *t = IECore::runTimeCast<T>( v );
	if( t )
	{
		return t;
	}

	IECore::msg( IECore::Msg::Warning, "IECoreRenderMan::Renderer", fmt::format( "Expected {} but got {} for option \"{}\".", T::staticTypeName(), v->typeName(), name.c_str() ) );
	return nullptr;
}

template<typename T>
T parameter( const CompoundDataMap &parameters, const IECore::InternedString &name, const T &defaultValue )
{
	auto it = parameters.find( name );
	if( it == parameters.end() )
	{
		return defaultValue;
	}

	using DataType = IECore::TypedData<T>;
	if( auto data = runTimeCast<DataType>( it->second.get() ) )
	{
		return data->readable();
	}

	IECore::msg( IECore::Msg::Warning, "IECoreRenderMan::Renderer", fmt::format( "Expected {} but got {} for parameter \"{}\".", DataType::staticTypeName(), it->second->typeName(), name.c_str() ) );
	return defaultValue;
}

} // namespace

Globals::Globals( IECoreScenePreview::Renderer::RenderType renderType, const IECore::MessageHandlerPtr &messageHandler )
	:	m_renderType( renderType ), m_messageHandler( messageHandler ),
		m_renderTargetExtent()
{
	// Initialise `m_integratorToConvert`.
	option( g_integratorOption, nullptr );

	if( char *p = getenv( "RMAN_DISPLAYS_PATH" ) )
	{
		string searchPath = string( p ) + ":@";
		m_options.SetString( Rix::k_searchpath_display, RtUString( searchPath.c_str() ) );
	}

	if( char *p = getenv( "OSL_SHADER_PATHS" ) )
	{
		string searchPath = string( p ) + ":@";
		m_options.SetString( Rix::k_searchpath_shader, RtUString( searchPath.c_str() ) );
	}

	if( renderType == IECoreScenePreview::Renderer::Interactive )
	{
		m_options.SetInteger( Rix::k_hider_incremental, 1 );
		m_options.SetString( Rix::k_bucket_order, RtUString( "circle" ) );
	}

	// The RenderMan documentation tells you to use "CU6L" as the normal AOV
	// for the denoiser, but that doesn't actually work unless the right user
	// lobe has been defined. So we define it here.
	/// \todo What if people want to define the lobes differently themselves?
	/// Should we be hardcoding this at all? Should we use `normal Nn` for the
	/// denoiser instead?
	m_options.SetString(
		RtUString( "lpe:user6" ),
		RtUString( "Normal,DiffuseNormal,HairTangent,SubsurfaceNormal,SpecularNormal,RoughSpecularNormal,SingleScatterNormal,FuzzNormal,IridescenceNormal,GlassNormal" )
	);
}

Globals::~Globals()
{
	pause();
}

void Globals::option( const IECore::InternedString &name, const IECore::Object *value )
{
	if( name == g_integratorOption )
	{
		if( auto *network = optionCast<const ShaderNetwork>( value, name ) )
		{
			m_integratorToConvert = network->outputShader();
		}
		else
		{
			m_integratorToConvert = new Shader( "PxrPathTracer", "ri:integrator" );
		}
	}
	else if( name == g_cameraOption )
	{
		if( auto *d = optionCast<const StringData>( value, name ) )
		{
			m_cameraOption = d->readable();
		}
	}
	else if( name == g_frameOption )
	{
		if( auto *d = optionCast<const IntData>( value, name ) )
		{
			m_options.SetInteger( RtUString( "Ri:Frame" ), d->readable() );
		}
		else
		{
			m_options.Remove( RtUString( "Ri:Frame" ) );
		}
	}
	else if( name == g_sampleMotionOption )
	{
		if( auto *d = optionCast<const BoolData>( value, name ) )
		{
			m_options.SetInteger( RtUString( "hider:samplemotion" ), d->readable() );
		}
		else
		{
			m_options.Remove( RtUString( "hider:samplemotion" ) );
		}
	}
	else if( boost::starts_with( name.c_str(), g_renderManPrefix.c_str() ) )
	{
		const RtUString renderManName( name.c_str() + g_renderManPrefix.size() );
		if( auto data = optionCast<const Data>( value, name ) )
		{
			ParamListAlgo::convertParameter( renderManName, data, m_options );
		}
		else
		{
			m_options.Remove( renderManName );
		}
	}
	else if( boost::starts_with( name.c_str(), "user:" ) )
	{
		const RtUString renderManName( name.c_str() );
		if( auto data = optionCast<const Data>( value, name ) )
		{
			ParamListAlgo::convertParameter( renderManName, data, m_options );
		}
		else
		{
			m_options.Remove( renderManName );
		}
	}
}

void Globals::output( const IECore::InternedString &name, const Output *output )
{
	if( output )
	{
		OutputPtr copy = output->copy();
		for( const auto &n : g_rejectedOutputFilterParameters )
		{
			if( copy->parameters().erase( n ) )
			{
				IECore::msg( IECore::Msg::Warning, "RenderManRenderer", fmt::format( "Ignoring unsupported parameter \"{}\" on output \"{}\". Filters must be specified via global options.", n.string(), name.string() ) );
			}
		}
		m_outputs[name] = copy;
	}
	else
	{
		m_outputs.erase( name );
	}

	deleteRenderView();
}

Session *Globals::acquireSession()
{
	if( !m_session )
	{
		m_session = std::make_unique<Session>( m_renderType, m_options, m_messageHandler );
	}

	return m_session.get();
}

void Globals::updateIntegrator()
{
	if( !m_integratorToConvert )
	{
		return;
	}

	if( m_integratorId != riley::IntegratorId::InvalidId() )
	{
		// Note : we update the render view to use the new integrator in
		// `updateRenderView()`, called immediately after `updateIntegrator()`.
		// So far it seems to be OK that the render view has a dangling
		// integrator in the meantime.
		m_session->riley->DeleteIntegrator( m_integratorId );
	}

	RtParamList integratorParamList;
	ParamListAlgo::convertParameters( m_integratorToConvert->parameters(), integratorParamList );

	riley::ShadingNode integratorNode = {
		riley::ShadingNode::Type::k_Integrator,
		RtUString( m_integratorToConvert->getName().c_str() ),
		RtUString( "integrator" ),
		integratorParamList
	};

	m_integratorId = m_session->riley->CreateIntegrator( riley::UserId(), integratorNode );
	m_integratorToConvert = nullptr;
}

void Globals::render()
{
	acquireSession();
	updateIntegrator();
	updateRenderView();
	m_session->updatePortals();

	/// \todo Is it worth avoiding this work when nothing has changed?
	Session::CameraInfo camera = m_session->getCamera( m_cameraOption );
	m_options.Update( camera.options );
	m_session->riley->SetOptions( m_options );

	switch( m_session->renderType )
	{
		case IECoreScenePreview::Renderer::Batch : {
			RtParamList renderOptions;
			renderOptions.SetString( RtUString( "renderMode" ), RtUString( "batch" ) );
			m_session->riley->Render( { 1, &m_renderView }, renderOptions );
			break;
		}
		case IECoreScenePreview::Renderer::Interactive :
			/// \todo Would it reduce latency if we reused the same thread?
			m_interactiveRenderThread = std::thread(
				[this] {
					RtParamList renderOptions;
					renderOptions.SetString( RtUString( "renderMode" ), RtUString( "interactive" ) );
					m_session->riley->Render( { 1, &m_renderView }, renderOptions );
				}
			);
			break;
		case IECoreScenePreview::Renderer::SceneDescription :
			// Protected against in RenderManRenderer constructor
			assert( 0 );
	}
}

void Globals::pause()
{
	if( m_interactiveRenderThread.joinable() )
	{
		m_session->riley->Stop();
		m_interactiveRenderThread.join();
	}
}

void Globals::updateRenderView()
{
	// Find camera.

	Session::CameraInfo camera = m_session->getCamera( m_cameraOption );
	if( camera.id == riley::CameraId::InvalidId() )
	{
		/// \todo Should the Camera and/or Session class be responsible for
		/// providing a default camera?
		if( m_defaultCamera == riley::CameraId::InvalidId() )
		{
			const auto matrix = Imath::M44f().scale( Imath::V3f( 1, 1, -1 ) );
			m_defaultCamera = m_session->riley->CreateCamera(
				riley::UserId(),
				RtUString( "ieCoreRenderMan:defaultCamera" ),
				/// \todo Projection?
				{
					riley::ShadingNode::Type::k_Projection, RtUString( "PxrCamera" ),
					RtUString( "projection" ), RtParamList()
				},
				StaticTransform( matrix ),
				RtParamList()
			);
		}
		camera.id = m_defaultCamera;
	}

	riley::Extent extent = { 640, 480, 0 };
	if( auto *resolution = camera.options.GetIntegerArray( Rix::k_Ri_FormatResolution, 2 ) )
	{
		extent.x = resolution[0];
		extent.y = resolution[1];
	}

	// If we still have a render view, then it is valid for
	// `m_outputs`, and all we need to do is update the camera and
	// resolution.

	if( m_renderView != riley::RenderViewId::InvalidId() )
	{
		if( extent.x != m_renderTargetExtent.x || extent.y != m_renderTargetExtent.y )
		{
			// Must only modify this if it has actually changed, because it causes
			// Riley to close and reopen all the display drivers.
			m_session->riley->ModifyRenderTarget(
				m_renderTarget, nullptr, &extent, nullptr, nullptr, nullptr
			);
			m_renderTargetExtent = extent;
		}
		m_session->riley->ModifyRenderView(
			m_renderView, nullptr, &camera.id, &m_integratorId, nullptr, nullptr, nullptr
		);
		return;
	}

	// Otherwise we need to build the render view from our list of outputs.

	struct DisplayDefinition
	{
		RtUString driver;
		vector<riley::RenderOutputId> outputs;
		RtParamList driverParamList;
	};

	std::unordered_map<std::string, DisplayDefinition> displayDefinitions;
	vector<riley::RenderOutputId> renderTargetOutputs;

	for( const auto &[name, output] : m_outputs )
	{
		// Render outputs.

		const vector<riley::RenderOutputId> &renderOutputs = acquireRenderOutputs( output.get());
		if( renderOutputs.empty() )
		{
			IECore::msg( IECore::Msg::Warning, "RenderManRenderer", fmt::format( "Ignoring unsupported output {}", name.c_str() ) );
			continue;
		}

		// Display driver. We allow multiple outputs to write to the same driver
		// if their output (file)name matches.

		DisplayDefinition &display = displayDefinitions[output->getName()];

		string driver = output->getType();
		if( driver == "exr" )
		{
			driver = "openexr";

			int asRGBA = 0;
			display.driverParamList.GetInteger( RtUString( "asrgba" ), asRGBA );
			const string layerName = parameter<string>( output->parameters(), g_layerName, "" );
			if( layerName.empty() || output->getData() == "rgb" || output->getData() == "rgba" )
			{
				asRGBA = 1;
			}
			display.driverParamList.SetInteger( RtUString( "asrgba" ), asRGBA );

			for( const auto &[parameterName, parameterValue] : output->parameters() )
			{
				if( boost::starts_with( parameterName.c_str(), "header:" ) )
				{
					const string exrName = "exrheader_" + parameterName.string().substr( 7 );
					ParamListAlgo::convertParameter( RtUString( exrName.c_str() ), parameterValue.get(), display.driverParamList );
				}
			}
		}

		display.driver = RtUString( driver.c_str() ); /// TODO CHECK FOR CONFLICTS, IN DRIVER PARAMS TOO.

		/// TODO : USE A PREFIX TO IDENTIFY DRIVER PARAMETERS.
		ParamListAlgo::convertParameters( output->parameters(), display.driverParamList );

		// For the most part it doesn't seem to matter what order we put the outputs
		// in. But the `quicklyNoiseless` driver assumes that the first 4 channels are
		// the ones to be passed through before denoising happens. So make sure we insert
		// the beauty first - it is the only one to have two render outputs (the second
		// one being for alpha).

		const bool beauty = renderOutputs.size() == 2;

		display.outputs.insert(
			beauty ? display.outputs.begin() : display.outputs.end(),
			renderOutputs.begin(), renderOutputs.end()
		);

		renderTargetOutputs.insert(
			beauty ? renderTargetOutputs.begin() : renderTargetOutputs.end(),
			renderOutputs.begin(), renderOutputs.end()
		);
	}

	m_renderTarget = m_session->riley->CreateRenderTarget(
		riley::UserId(),
		{ (uint32_t)renderTargetOutputs.size(), renderTargetOutputs.data() },
		// Why must the resolution be specified both here _and_ via the
		// `k_Ri_FormatResolution` option? Riley only knows.
		extent,
		RtUString( "importance" ),
		0.015f, // TODO : GET FROM OPTIONS
		RtParamList()
	);
	m_renderTargetExtent = extent;

	for( const auto &[name, definition] : displayDefinitions )
	{
		m_displays.push_back(
			m_session->riley->CreateDisplay(
				riley::UserId(),
				m_renderTarget,
				RtUString( name.c_str() ),
				definition.driver,
				{ (uint32_t)definition.outputs.size(), definition.outputs.data() },
				definition.driverParamList
			)
		);
	}

	m_renderView = m_session->riley->CreateRenderView(
		riley::UserId(),
		m_renderTarget,
		camera.id,
		m_integratorId,
		{ 0, nullptr },
		{ 0, nullptr },
		RtParamList()
	);

}

void Globals::deleteRenderView()
{
	if( m_renderView == riley::RenderViewId::InvalidId() )
	{
		return;
	}

	m_session->riley->DeleteRenderView( m_renderView );
	m_renderView = riley::RenderViewId::InvalidId();

	for( const auto &display : m_displays )
	{
		m_session->riley->DeleteDisplay( display );
	}
	m_displays.clear();

	m_session->riley->DeleteRenderTarget( m_renderTarget );
	m_renderTarget = riley::RenderTargetId::InvalidId();
}

const std::vector<riley::RenderOutputId> &Globals::acquireRenderOutputs( const IECoreScene::Output *output )
{
	// Identify type and source.

	const string layerName = parameter<string>( output->parameters(), g_layerName, "" );

	std::optional<riley::RenderOutputType> type;
	RtUString source;
	if( output->getData() == "rgb" || output->getData() == "rgba" )
	{
		type = riley::RenderOutputType::k_Color;
		source = RtUString( "Ci" );
	}
	else
	{
		vector<string> tokens;
		StringAlgo::tokenize( output->getData(), ' ', tokens );
		if( tokens.size() == 2 )
		{
			source = RtUString( tokens[1].c_str() );
			if( tokens[0] == "color" )
			{
				type = riley::RenderOutputType::k_Color;
			}
			else if( tokens[0] == "float" )
			{
				type = riley::RenderOutputType::k_Float;
			}
			else if( tokens[0] == "int" )
			{
				type = riley::RenderOutputType::k_Integer;
			}
			else if( tokens[0] == "vector" )
			{
				type = riley::RenderOutputType::k_Vector;
			}
			else if( tokens[0] == "lpe" )
			{
				if( layerName == "normal" )
				{
					type = riley::RenderOutputType::k_Vector;
				}
				else
				{
					type = riley::RenderOutputType::k_Color;
				}
				source = RtUString( ( "lpe:" + tokens[1] ).c_str() );
			}
		}
	}

	if( !type )
	{
		static const vector<riley::RenderOutputId> g_emptyOutputs;
		return g_emptyOutputs;
	}

	// The name that will be passed to the display driver. Note that this
	// doesn't need to be unique among all render outputs.

	RtUString renderOutputName = source;
	if( !layerName.empty() )
	{
		renderOutputName = RtUString( layerName.c_str() );
	}

	// Additional parameters.

	const RtUString accumulationRule( parameter( output->parameters(), "ri:accumulationRule", string( "filter" ) ).c_str() );
	const float relativePixelVariance = parameter( output->parameters(), "ri:relativePixelVariance", 0.0f );

	const RtUString filter = Rix::k_gaussian; // TODO : GET FROM OPTIONS
	const riley::FilterSize filterSize = { 2.0, 2.0 }; // TODO : GET FROM OPTIONS

	// Hash.

	MurmurHash hash;
	hash.append( renderOutputName.CStr() );
	hash.append( *type );
	hash.append( source.CStr() );
	hash.append( accumulationRule.CStr() );
	hash.append( filter.CStr() );
	hash.append( filterSize.width );
	hash.append( filterSize.height );
	hash.append( output->getData() == "rgba" );

	// Return previously created result if we have one.

	vector<riley::RenderOutputId> &result = m_renderOutputs[hash];
	if( result.size() )
	{
		return result;
	}

	// Otherwise create and return.

	result.push_back(
		m_session->riley->CreateRenderOutput(
			riley::UserId(),
			renderOutputName, *type, source,
			accumulationRule, filter, filterSize, relativePixelVariance,
			RtParamList()
		)
	);

	if( output->getData() == "rgba" )
	{
		result.push_back(
			m_session->riley->CreateRenderOutput(
				riley::UserId(),
				RtUString( "a" ), riley::RenderOutputType::k_Float, Rix::k_a,
				accumulationRule, filter, filterSize, relativePixelVariance,
				RtParamList()
			)
		);
	}

	return result;
}
