//////////////////////////////////////////////////////////////////////////
//
//  Copyright (c) 2018, John Haddon. All rights reserved.
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

#include "GeometryAlgo.h"

#include "ParamListAlgo.h"

#include "IECoreScene/CurvesPrimitive.h"
#include "IECoreScene/MeshPrimitive.h"
#include "IECoreScene/PointsPrimitive.h"
#include "IECoreScene/SpherePrimitive.h"

#include "IECore/DataAlgo.h"
#include "IECore/MessageHandler.h"

#include "RixPredefinedStrings.hpp"

#include "fmt/format.h"

#include <unordered_map>

using namespace std;
using namespace IECore;
using namespace IECoreScene;

//////////////////////////////////////////////////////////////////////////
// Internal utilities
//////////////////////////////////////////////////////////////////////////

namespace std
{

/// \todo Move to IECore/TypeIds.h
template<>
struct hash<IECore::TypeId>
{
	size_t operator()( IECore::TypeId typeId ) const
	{
		return hash<size_t>()( typeId );
	}
};

} // namespace std

namespace
{

using namespace IECoreRenderMan;

struct Converters
{

	GeometryAlgo::Converter converter;
	GeometryAlgo::MotionConverter motionConverter;

};

typedef std::unordered_map<IECore::TypeId, Converters> Registry;

Registry &registry()
{
	static Registry r;
	return r;
}

RtDetailType detail( IECoreScene::PrimitiveVariable::Interpolation interpolation )
{
	switch( interpolation )
	{
		case PrimitiveVariable::Invalid :
			throw IECore::Exception( "No detail equivalent to PrimitiveVariable::Invalid" );
		case PrimitiveVariable::Constant :
			return RtDetailType::k_constant;
		case PrimitiveVariable::Uniform :
			return RtDetailType::k_uniform;
		case PrimitiveVariable::Vertex :
			return RtDetailType::k_vertex;
		case PrimitiveVariable::Varying :
			return RtDetailType::k_varying;
		case PrimitiveVariable::FaceVarying :
			return RtDetailType::k_facevarying;
		default :
			throw IECore::Exception( "Unknown PrimtiveVariable Interpolation" );
	}
}

RtDataType dataType( IECore::GeometricData::Interpretation interpretation )
{
	switch( interpretation )
	{
		case GeometricData::Vector :
			return RtDataType::k_vector;
		case GeometricData::Normal :
			return RtDataType::k_normal;
		default :
			return RtDataType::k_point;
	}
}

struct PrimitiveVariableConverter
{

	// Simple data

	void operator()( const BoolData *data, RtUString name, const PrimitiveVariable &primitiveVariable, RtPrimVarList &primVarList, unsigned sampleIndex=0 ) const
	{
		const int b = data->readable();
		primVarList.SetIntegerDetail( name, &b, detail( primitiveVariable.interpolation ), sampleIndex );
	}

	void operator()( const IntData *data, RtUString name, const PrimitiveVariable &primitiveVariable, RtPrimVarList &primVarList, unsigned sampleIndex=0 ) const
	{
		primVarList.SetIntegerDetail( name, &data->readable(), detail( primitiveVariable.interpolation ), sampleIndex );
	}

	void operator()( const FloatData *data, RtUString name, const PrimitiveVariable &primitiveVariable, RtPrimVarList &primVarList, unsigned sampleIndex=0 ) const
	{
		primVarList.SetFloatDetail( name, &data->readable(), detail( primitiveVariable.interpolation ), sampleIndex );
	}

	void operator()( const StringData *data, RtUString name, const PrimitiveVariable &primitiveVariable, RtPrimVarList &primVarList, unsigned sampleIndex=0 ) const
	{
		RtUString s( data->readable().c_str() );
		primVarList.SetStringDetail( name, &s, detail( primitiveVariable.interpolation ), sampleIndex );
	}

	void operator()( const Color3fData *data, RtUString name, const PrimitiveVariable &primitiveVariable, RtPrimVarList &primVarList, unsigned sampleIndex=0 ) const
	{
		primVarList.SetColorDetail( name, reinterpret_cast<const RtColorRGB *>( data->readable().getValue() ), detail( primitiveVariable.interpolation ), sampleIndex );
	}

	void operator()( const V3fData *data, RtUString name, const PrimitiveVariable &primitiveVariable, RtPrimVarList &primVarList, unsigned sampleIndex=0 ) const
	{
		primVarList.SetParam(
			{
				name,
				dataType( data->getInterpretation() ),
				detail( primitiveVariable.interpolation ),
				/* length = */ 1,
				/* array = */ false,
				/* motion = */ sampleIndex > 0,
				/* deduplicated = */ false
			},
			data->readable().getValue(),
			sampleIndex
		);
	}

	// Vector data

	void operator()( const IntVectorData *data, RtUString name, const PrimitiveVariable &primitiveVariable, RtPrimVarList &primVarList, unsigned sampleIndex=0 ) const
	{
		emit(
			data,
			{
				name,
				RtDataType::k_integer,
				detail( primitiveVariable.interpolation ),
				/* length = */ 1,
				/* array = */ false,
				/* motion = */ sampleIndex > 0,
				/* deduplicated = */ false
			},
			primitiveVariable,
			primVarList,
			sampleIndex
		);
	}

	void operator()( const FloatVectorData *data, RtUString name, const PrimitiveVariable &primitiveVariable, RtPrimVarList &primVarList, unsigned sampleIndex=0 ) const
	{
		emit(
			data,
			{
				name,
				RtDataType::k_float,
				detail( primitiveVariable.interpolation ),
				/* length = */ 1,
				/* array = */ false,
				/* motion = */ sampleIndex > 0,
				/* deduplicated = */ false
			},
			primitiveVariable,
			primVarList,
			sampleIndex
		);
	}

	void operator()( const V2fVectorData *data, RtUString name, const PrimitiveVariable &primitiveVariable, RtPrimVarList &primVarList, unsigned sampleIndex=0 ) const
	{
		emit(
			data,
			{
				name,
				RtDataType::k_float,
				detail( primitiveVariable.interpolation ),
				/* length = */ 2,
				/* array = */ true,
				/* motion = */ sampleIndex > 0,
				/* deduplicated = */ false
			},
			primitiveVariable,
			primVarList,
			sampleIndex
		);
	}

	void operator()( const V3fVectorData *data, RtUString name, const PrimitiveVariable &primitiveVariable, RtPrimVarList &primVarList, unsigned sampleIndex=0 ) const
	{
		emit(
			data,
			{
				name,
				dataType( data->getInterpretation() ),
				detail( primitiveVariable.interpolation ),
				/* length = */ 1,
				/* array = */ false,
				/* motion = */ sampleIndex > 0,
				/* deduplicated = */ false
			},
			primitiveVariable,
			primVarList,
			sampleIndex
		);
	}

	void operator()( const Color3fVectorData *data, RtUString name, const PrimitiveVariable &primitiveVariable, RtPrimVarList &primVarList, unsigned sampleIndex=0 ) const
	{
		emit(
			data,
			{
				name,
				RtDataType::k_color,
				detail( primitiveVariable.interpolation ),
				/* length = */ 1,
				/* array = */ false,
				/* motion = */ sampleIndex > 0,
				/* deduplicated = */ false
			},
			primitiveVariable,
			primVarList,
			sampleIndex
		);
	}

	void operator()( const Data *data, RtUString name, const PrimitiveVariable &primitiveVariable, RtPrimVarList &primVarList, unsigned sampleIndex=0 ) const
	{
		IECore::msg(
			IECore::Msg::Warning,
			"IECoreRenderMan",
			fmt::format( "Unsupported primitive variable of type \"{}\"", data->typeName() )
		);
	}

	private :

		template<typename T>
		void emit( const T *data, const RtPrimVarList::ParamInfo &paramInfo, const PrimitiveVariable &primitiveVariable, RtPrimVarList &primVarList, unsigned sampleIndex=0 ) const
		{
			if( primitiveVariable.indices )
			{
				typedef RtPrimVarList::Buffer<typename T::ValueType::value_type> Buffer;
				Buffer buffer( primVarList, paramInfo, sampleIndex );
				buffer.Bind();

				const vector<int> &indices = primitiveVariable.indices->readable();
				const typename T::ValueType &values = data->readable();
				for( int i = 0, e = indices.size(); i < e; ++i )
				{
					buffer[i] = values[indices[i]];
				}

				buffer.Unbind();
			}
			else
			{
				primVarList.SetParam(
					paramInfo,
					data->readable().data(),
					sampleIndex
				);
			}
		}

};

} // namespace

//////////////////////////////////////////////////////////////////////////
// Implementation of public API
//////////////////////////////////////////////////////////////////////////

RtUString IECoreRenderMan::GeometryAlgo::convert( const IECore::Object *object, RtPrimVarList &primVars )
{
	Registry &r = registry();
	auto it = r.find( object->typeId() );
	if( it == r.end() )
	{
		return RtUString();
	}
	return it->second.converter( object, primVars );
}

RtUString IECoreRenderMan::GeometryAlgo::convert( const std::vector<const IECore::Object *> &samples, const std::vector<float> &sampleTimes, RtPrimVarList &primVars )
{
	Registry &r = registry();
	auto it = r.find( samples.front()->typeId() );
	if( it == r.end() )
	{
		return RtUString();
	}
	if( it->second.motionConverter )
	{
		return it->second.motionConverter( samples, sampleTimes, primVars );
	}
	else
	{
		return it->second.converter( samples.front(), primVars );
	}
}

void IECoreRenderMan::GeometryAlgo::registerConverter( IECore::TypeId fromType, Converter converter, MotionConverter motionConverter )
{
	registry()[fromType] = { converter, motionConverter };
}

void IECoreRenderMan::GeometryAlgo::convertPrimitiveVariables( const IECoreScene::Primitive *primitive, RtPrimVarList &primVarList )
{
	for( const auto &[name, primitiveVariable] : primitive->variables )
	{
		const RtUString convertedName( name == "uv" ? "st" : name.c_str() );
		dispatch( primitiveVariable.data.get(), PrimitiveVariableConverter(), convertedName, primitiveVariable, primVarList );
	}
}

void IECoreRenderMan::GeometryAlgo::convertPrimitiveVariables( const std::vector<const IECoreScene::Primitive *> &samples, const std::vector<float> &sampleTimes, RtPrimVarList &primVarList )
{
	bool haveSetTimes = false;
	for( const auto &[name, primitiveVariable] : samples[0]->variables )
	{
		bool animated = false;
		for( size_t i = 1; i < samples.size(); ++i )
		{
			auto it = samples[i]->variables.find( name );
			if( it == samples[i]->variables.end() )
			{
				animated = false;
				break;
			}
			else if( it->second != primitiveVariable )
			{
				animated = true;
			}
		}

		const RtUString convertedName( name == "uv" ? "st" : name.c_str() );
		if( animated )
		{
			if( !haveSetTimes )
			{
				primVarList.SetTimes( sampleTimes.size(), sampleTimes.data() );
				haveSetTimes = true;
			}

			for( size_t i = 0; i < samples.size(); ++i )
			{
				auto it = samples[i]->variables.find( name );
				assert( it != samples[i]->variables.end() );
				dispatch( it->second.data.get(), PrimitiveVariableConverter(), convertedName, primitiveVariable, primVarList, i );
			}
		}
		else
		{
			dispatch( primitiveVariable.data.get(), PrimitiveVariableConverter(), convertedName, primitiveVariable, primVarList );
		}
	}
}

//////////////////////////////////////////////////////////////////////////
// Spheres
//////////////////////////////////////////////////////////////////////////

namespace
{

RtUString convertStaticSphere( const IECoreScene::SpherePrimitive *sphere, RtPrimVarList &primVars )
{
	primVars.SetDetail(
		sphere->variableSize( PrimitiveVariable::Uniform ),
		sphere->variableSize( PrimitiveVariable::Vertex ),
		sphere->variableSize( PrimitiveVariable::Varying ),
		sphere->variableSize( PrimitiveVariable::FaceVarying )
	);

	GeometryAlgo::convertPrimitiveVariables( sphere, primVars );

	const float radius = sphere->radius();
	const float zMin = sphere->zMin();
	const float zMax = sphere->zMax();
	const float thetaMax = sphere->thetaMax();

	primVars.SetFloatDetail( Rix::k_Ri_radius, &radius, RtDetailType::k_constant );
	primVars.SetFloatDetail( Rix::k_Ri_zmin, &zMin, RtDetailType::k_constant );
	primVars.SetFloatDetail( Rix::k_Ri_zmax, &zMax, RtDetailType::k_constant );
	primVars.SetFloatDetail( Rix::k_Ri_thetamax, &thetaMax, RtDetailType::k_constant );

	return Rix::k_Ri_Sphere;
}

GeometryAlgo::ConverterDescription<SpherePrimitive> g_sphereConverterDescription( convertStaticSphere );

} // namespace

//////////////////////////////////////////////////////////////////////////
// Meshes
//////////////////////////////////////////////////////////////////////////

namespace
{

int interpolateBoundary( const IECoreScene::MeshPrimitive *mesh )
{
	const InternedString s = mesh->getInterpolateBoundary();
	if( s == IECoreScene::MeshPrimitive::interpolateBoundaryNone )
	{
		return 0;
	}
	else if( s == IECoreScene::MeshPrimitive::interpolateBoundaryEdgeAndCorner )
	{
		return 1;
	}
	else if( s == IECoreScene::MeshPrimitive::interpolateBoundaryEdgeOnly )
	{
		return 2;
	}
	else
	{
		msg( Msg::Error, "GeometryAlgo", fmt::format( "Unknown boundary interpolation \"{}\"", s.string() ) );
		return 0;
	}
}

int faceVaryingInterpolateBoundary( const IECoreScene::MeshPrimitive *mesh )
{
	const InternedString s = mesh->getFaceVaryingLinearInterpolation();
	if( s == IECoreScene::MeshPrimitive::faceVaryingLinearInterpolationNone )
	{
		return 2;
	}
	else if(
		s == IECoreScene::MeshPrimitive::faceVaryingLinearInterpolationCornersOnly ||
		s == IECoreScene::MeshPrimitive::faceVaryingLinearInterpolationCornersPlus1 ||
		s == IECoreScene::MeshPrimitive::faceVaryingLinearInterpolationCornersPlus2
	)
	{
		return 1;
	}
	else if( s == IECoreScene::MeshPrimitive::faceVaryingLinearInterpolationBoundaries )
	{
		return 3;
	}
	else if( s == IECoreScene::MeshPrimitive::faceVaryingLinearInterpolationAll )
	{
		return 0;
	}
	else
	{
		msg( Msg::Error, "GeometryAlgo", fmt::format( "Unknown facevarying linear interpolation \"{}\"", s.string() ) );
		return 0;
	}
}

int smoothTriangles( const IECoreScene::MeshPrimitive *mesh )
{
	const InternedString s = mesh->getTriangleSubdivisionRule();
	if( s == IECoreScene::MeshPrimitive::triangleSubdivisionRuleCatmullClark )
	{
		return 0;
	}
	else if( s == IECoreScene::MeshPrimitive::triangleSubdivisionRuleSmooth )
	{
		return 2;
	}
	else
	{
		msg( Msg::Error, "GeometryAlgo", fmt::format( "Unknown triangle subdivision rule \"{}\"", s.string() ) );
		return 0;
	}
}

RtUString convertMeshTopology( const IECoreScene::MeshPrimitive *mesh, RtPrimVarList &primVars )
{
	primVars.SetDetail(
		mesh->variableSize( PrimitiveVariable::Uniform ),
		mesh->variableSize( PrimitiveVariable::Vertex ),
		mesh->variableSize( PrimitiveVariable::Varying ),
		mesh->variableSize( PrimitiveVariable::FaceVarying )
	);

	primVars.SetIntegerDetail( Rix::k_Ri_nvertices, mesh->verticesPerFace()->readable().data(), RtDetailType::k_uniform );
	primVars.SetIntegerDetail( Rix::k_Ri_vertices, mesh->vertexIds()->readable().data(), RtDetailType::k_facevarying );

	RtUString geometryType = Rix::k_Ri_PolygonMesh;
	if( mesh->interpolation() != MeshPrimitive::interpolationLinear.string() )
	{
		geometryType = Rix::k_Ri_SubdivisionMesh;
		if( mesh->interpolation() == MeshPrimitive::interpolationCatmullClark.string() )
		{
			primVars.SetString( Rix::k_Ri_scheme, Rix::k_catmullclark );
		}
		else if( mesh->interpolation() == MeshPrimitive::interpolationLoop.string() )
		{
			primVars.SetString( Rix::k_Ri_scheme, Rix::k_loop );
		}
		else
		{
			msg( Msg::Error, "GeometryAlgo", fmt::format( "Unknown mesh interpolation \"{}\"", mesh->interpolation() ) );
			primVars.SetString( Rix::k_Ri_scheme, Rix::k_catmullclark );
		}

		vector<RtUString> tagNames;
		vector<RtInt> tagArgCounts;
		vector<RtInt> tagIntArgs;
		vector<RtFloat> tagFloatArgs;
		vector<RtToken> tagStringArgs;

		// Creases

		for( int creaseLength : mesh->creaseLengths()->readable() )
		{
			tagNames.push_back( Rix::k_crease );
			tagArgCounts.push_back( creaseLength ); // integer argument count
			tagArgCounts.push_back( 1 ); // float argument count
			tagArgCounts.push_back( 0 ); // string argument count
		}

		tagIntArgs = mesh->creaseIds()->readable();
		tagFloatArgs = mesh->creaseSharpnesses()->readable();

		// Corners

		if( mesh->cornerIds()->readable().size() )
		{
			tagNames.push_back( Rix::k_corner );
			tagArgCounts.push_back( mesh->cornerIds()->readable().size() ); // integer argument count
			tagArgCounts.push_back( mesh->cornerIds()->readable().size() ); // float argument count
			tagArgCounts.push_back( 0 ); // string argument count
			tagIntArgs.insert( tagIntArgs.end(), mesh->cornerIds()->readable().begin(), mesh->cornerIds()->readable().end() );
			tagFloatArgs.insert( tagFloatArgs.end(), mesh->cornerSharpnesses()->readable().begin(), mesh->cornerSharpnesses()->readable().end() );
		}

		// Interpolation rules

		tagNames.push_back( Rix::k_interpolateboundary );
		tagArgCounts.insert( tagArgCounts.end(), { 1, 0, 0 } );
		tagIntArgs.push_back( interpolateBoundary( mesh ) );

		tagNames.push_back( Rix::k_facevaryinginterpolateboundary );
		tagArgCounts.insert( tagArgCounts.end(), { 1, 0, 0 } );
		tagIntArgs.push_back( faceVaryingInterpolateBoundary( mesh ) );

		tagNames.push_back( Rix::k_smoothtriangles );
		tagArgCounts.insert( tagArgCounts.end(), { 1, 0, 0 } );
		tagIntArgs.push_back( smoothTriangles( mesh ) );

		// Pseudo-primvars to hold the tags

		primVars.SetStringArray( Rix::k_Ri_subdivtags, tagNames.data(), tagNames.size() );
		primVars.SetIntegerArray( Rix::k_Ri_subdivtagnargs, tagArgCounts.data(), tagArgCounts.size() );
		primVars.SetFloatArray( Rix::k_Ri_subdivtagfloatargs, tagFloatArgs.data(), tagFloatArgs.size() );
		primVars.SetIntegerArray( Rix::k_Ri_subdivtagintargs, tagIntArgs.data(), tagIntArgs.size() );
	}

	return geometryType;
}

RtUString convertStaticMesh( const IECoreScene::MeshPrimitive *mesh, RtPrimVarList &primVars )
{
	const RtUString result = convertMeshTopology( mesh, primVars );
	GeometryAlgo::convertPrimitiveVariables( mesh, primVars );
	return result;
}

RtUString convertAnimatedMesh( const std::vector<const IECoreScene::MeshPrimitive *> &samples, const std::vector<float> &sampleTimes, RtPrimVarList &primVars )
{
	const RtUString result = convertMeshTopology( samples[0], primVars );
	GeometryAlgo::convertPrimitiveVariables( reinterpret_cast<const std::vector<const IECoreScene::Primitive *> &>( samples ), sampleTimes, primVars );
	return result;
}

GeometryAlgo::ConverterDescription<MeshPrimitive> g_meshConverterDescription( convertStaticMesh, convertAnimatedMesh );

} // namespace

//////////////////////////////////////////////////////////////////////////
// Points
//////////////////////////////////////////////////////////////////////////

namespace
{

RtUString convertStaticPoints( const IECoreScene::PointsPrimitive *points, RtPrimVarList &primVars )
{
	primVars.SetDetail(
		points->variableSize( PrimitiveVariable::Uniform ),
		points->variableSize( PrimitiveVariable::Vertex ),
		points->variableSize( PrimitiveVariable::Varying ),
		points->variableSize( PrimitiveVariable::FaceVarying )
	);

	GeometryAlgo::convertPrimitiveVariables( points, primVars );

	return Rix::k_Ri_Points;
}

GeometryAlgo::ConverterDescription<PointsPrimitive> g_pointsConverterDescription( convertStaticPoints );

} // namespace

//////////////////////////////////////////////////////////////////////////
// Curves
//////////////////////////////////////////////////////////////////////////

namespace
{

RtUString convertStaticCurves( const IECoreScene::CurvesPrimitive *curves, RtPrimVarList &primVars )
{
	primVars.SetDetail(
		curves->variableSize( PrimitiveVariable::Uniform ),
		curves->variableSize( PrimitiveVariable::Vertex ),
		curves->variableSize( PrimitiveVariable::Varying ),
		curves->variableSize( PrimitiveVariable::FaceVarying )
	);

	GeometryAlgo::convertPrimitiveVariables( curves, primVars );

	if( curves->basis().standardBasis() == StandardCubicBasis::Unknown )
	{
		IECore::msg( IECore::Msg::Warning, "IECoreRenderMan", "Unsupported CubicBasis" );
		primVars.SetString( Rix::k_Ri_type, Rix::k_linear );
	}
	else if( curves->basis().standardBasis() == StandardCubicBasis::Linear )
	{
		primVars.SetString( Rix::k_Ri_type, Rix::k_linear );
	}
	else
	{
		primVars.SetString( Rix::k_Ri_type, Rix::k_cubic );
		switch( curves->basis().standardBasis() )
		{
			case StandardCubicBasis::Bezier :
				primVars.SetString( Rix::k_Ri_Basis, Rix::k_bezier );
				break;
			case StandardCubicBasis::BSpline :
				primVars.SetString( Rix::k_Ri_Basis, Rix::k_bspline );
				break;
			case StandardCubicBasis::CatmullRom :
				primVars.SetString( Rix::k_Ri_Basis, Rix::k_catmullrom );
				break;
			default :
				// Should have dealt with Unknown and Linear above
				assert( false );
		}
	}

	primVars.SetString( Rix::k_Ri_wrap, curves->periodic() ? Rix::k_periodic : Rix::k_nonperiodic );
	primVars.SetIntegerDetail( Rix::k_Ri_nvertices, curves->verticesPerCurve()->readable().data(), RtDetailType::k_uniform );

	return Rix::k_Ri_Curves;
}

GeometryAlgo::ConverterDescription<CurvesPrimitive> g_curvesConverterDescription( convertStaticCurves );

} // namespace
