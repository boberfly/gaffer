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

#include <unordered_map>

using namespace std;
using namespace riley;
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

RixDetailType detail( IECoreScene::PrimitiveVariable::Interpolation interpolation )
{
	switch( interpolation )
	{
		case PrimitiveVariable::Invalid :
			throw IECore::Exception( "No detail equivalent to PrimitiveVariable::Invalid" );
		case PrimitiveVariable::Constant :
			return RixDetailType::k_constant;
		case PrimitiveVariable::Uniform :
			return RixDetailType::k_uniform;
		case PrimitiveVariable::Vertex :
			return RixDetailType::k_vertex;
		case PrimitiveVariable::Varying :
			return RixDetailType::k_varying;
		case PrimitiveVariable::FaceVarying :
			return RixDetailType::k_facevarying;
		default :
			throw IECore::Exception( "Unknown PrimtiveVariable Interpolation" );
	}
}

RixDataType dataType( IECore::GeometricData::Interpretation interpretation )
{
	switch( interpretation )
	{
		case GeometricData::Vector :
			return RixDataType::k_vector;
		case GeometricData::Normal :
			return RixDataType::k_normal;
		default :
			return RixDataType::k_point;
	}
}

struct PrimitiveVariableConverter
{

	// Simple data

	void operator()( const BoolData *data, RtUString name, const PrimitiveVariable &primitiveVariable, RixParamList &paramList ) const
	{
		int b = data->readable();
		paramList.SetIntegerDetail( name, &b, detail( primitiveVariable.interpolation ) );
	}

	void operator()( const IntData *data, RtUString name, const PrimitiveVariable &primitiveVariable, RixParamList &paramList ) const
	{
		paramList.SetIntegerDetail( name, &data->readable(), detail( primitiveVariable.interpolation ) );
	}

	void operator()( const FloatData *data, RtUString name, const PrimitiveVariable &primitiveVariable, RixParamList &paramList ) const
	{
		paramList.SetFloatDetail( name, &data->readable(), detail( primitiveVariable.interpolation ) );
	}

	void operator()( const StringData *data, RtUString name, const PrimitiveVariable &primitiveVariable, RixParamList &paramList ) const
	{
		RtUString s( data->readable().c_str() );
		paramList.SetStringDetail( name, &s, detail( primitiveVariable.interpolation ) );
	}

	void operator()( const Color3fData *data, RtUString name, const PrimitiveVariable &primitiveVariable, RixParamList &paramList ) const
	{
		paramList.SetColorDetail( name, reinterpret_cast<const RtColorRGB *>( data->readable().getValue() ), detail( primitiveVariable.interpolation ) );
	}

	void operator()( const V3fData *data, RtUString name, const PrimitiveVariable &primitiveVariable, RixParamList &paramList ) const
	{
		paramList.SetParam(
			{
				name,
				dataType( data->getInterpretation() ),
				/* length = */ 1,
				detail( primitiveVariable.interpolation ),
				/* array = */ false
			},
			data->readable().getValue(),
			0
		);
	}

	// Vector data

	void operator()( const FloatVectorData *data, RtUString name, const PrimitiveVariable &primitiveVariable, RixParamList &paramList ) const
	{
		emit(
			data,
			{
				name,
				RixDataType::k_float,
				/* length = */ 1,
				detail( primitiveVariable.interpolation ),
				/* array = */ false,
			},
			primitiveVariable,
			paramList
		);
	}

	void operator()( const V2fVectorData *data, RtUString name, const PrimitiveVariable &primitiveVariable, RixParamList &paramList ) const
	{
		emit(
			data,
			{
				name,
				RixDataType::k_float,
				/* length = */ 2,
				detail( primitiveVariable.interpolation ),
				/* array = */ true,
			},
			primitiveVariable,
			paramList
		);
	}

	void operator()( const V3fVectorData *data, RtUString name, const PrimitiveVariable &primitiveVariable, RixParamList &paramList ) const
	{
		emit(
			data,
			{
				name,
				dataType( data->getInterpretation() ),
				/* length = */ 1,
				detail( primitiveVariable.interpolation ),
				/* array = */ false,
			},
			primitiveVariable,
			paramList
		);
	}

	void operator()( const Color3fVectorData *data, RtUString name, const PrimitiveVariable &primitiveVariable, RixParamList &paramList ) const
	{
		emit(
			data,
			{
				name,
				RixDataType::k_color,
				/* length = */ 1,
				detail( primitiveVariable.interpolation ),
				/* array = */ false,
			},
			primitiveVariable,
			paramList
		);
	}

	void operator()( const Data *data, RtUString name, const PrimitiveVariable &primitiveVariable, RixParamList &paramList ) const
	{
		IECore::msg(
			IECore::Msg::Warning,
			"IECoreRenderMan",
			boost::format( "Unsupported primitive variable of type \"%s\"" ) % data->typeName()
		);
	}

	private :

		template<typename T>
		void emit( const T *data, const RixParamList::ParamInfo &paramInfo, const PrimitiveVariable &primitiveVariable, RixParamList &paramList ) const
		{
			if( primitiveVariable.indices )
			{
				typedef RixParamList::Buffer<typename T::ValueType::value_type> Buffer;
				Buffer buffer( paramList, paramInfo, /* time = */ 0 );
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
				paramList.SetParam(
					paramInfo,
					data->readable().data(),
					0
				);
			}
		}

};

} // namespace

//////////////////////////////////////////////////////////////////////////
// Implementation of public API
//////////////////////////////////////////////////////////////////////////

namespace IECoreRenderMan
{

namespace GeometryAlgo
{

riley::GeometryMasterId convert( const IECore::Object *object, riley::Riley *riley )
{
	Registry &r = registry();
	auto it = r.find( object->typeId() );
	if( it == r.end() )
	{
		return GeometryMasterId::k_InvalidId;
	}
	return it->second.converter( object, riley );
}

riley::GeometryMasterId convert( const std::vector<const IECore::Object *> &samples, const std::vector<float> &sampleTimes, riley::Riley *riley )
{
	Registry &r = registry();
	auto it = r.find( samples.front()->typeId() );
	if( it == r.end() )
	{
		return GeometryMasterId::k_InvalidId;
	}
	if( it->second.motionConverter )
	{
		return it->second.motionConverter( samples, sampleTimes, riley );
	}
	else
	{
		return it->second.converter( samples.front(), riley );
	}
}

void registerConverter( IECore::TypeId fromType, Converter converter, MotionConverter motionConverter )
{
	registry()[fromType] = { converter, motionConverter };
}

void convertPrimitiveVariable( RtUString name, const IECoreScene::PrimitiveVariable &primitiveVariable, RixParamList &paramList )
{
	dispatch( primitiveVariable.data.get(), PrimitiveVariableConverter(), name, primitiveVariable, paramList );
}

} // namespace GeometryAlgo

} // namespace IECoreRenderMan

//////////////////////////////////////////////////////////////////////////
// Spheres
//////////////////////////////////////////////////////////////////////////

namespace
{

riley::GeometryMasterId convertStaticSphere( const IECoreScene::SpherePrimitive *sphere, riley::Riley *riley )
{
	auto primVars = ParamListAlgo::makePrimitiveVariableList(
		sphere->variableSize( PrimitiveVariable::Uniform ),
		sphere->variableSize( PrimitiveVariable::Vertex ),
		sphere->variableSize( PrimitiveVariable::Varying ),
		sphere->variableSize( PrimitiveVariable::FaceVarying )
	);

	for( auto &primitiveVariable : sphere->variables )
	{
		GeometryAlgo::convertPrimitiveVariable( RtUString( primitiveVariable.first.c_str() ), primitiveVariable.second, *primVars );
	}

	const float radius = sphere->radius();
	const float zMin = sphere->zMin();
	const float zMax = sphere->zMax();
	const float thetaMax = sphere->thetaMax();

	primVars->SetFloatDetail( Rix::k_Ri_radius, &radius, RixDetailType::k_constant );
	primVars->SetFloatDetail( Rix::k_Ri_zmin, &zMin, RixDetailType::k_constant );
	primVars->SetFloatDetail( Rix::k_Ri_zmax, &zMax, RixDetailType::k_constant );
	primVars->SetFloatDetail( Rix::k_Ri_thetamax, &thetaMax, RixDetailType::k_constant );

	return riley->CreateGeometryMaster(
		Rix::k_Ri_Sphere, DisplacementId::k_InvalidId, *primVars
	);
}

GeometryAlgo::ConverterDescription<SpherePrimitive> g_sphereConverterDescription( convertStaticSphere );

} // namespace

//////////////////////////////////////////////////////////////////////////
// Meshes
//////////////////////////////////////////////////////////////////////////

namespace
{

riley::GeometryMasterId convertStaticMesh( const IECoreScene::MeshPrimitive *mesh, riley::Riley *riley )
{
	auto primVars = ParamListAlgo::makePrimitiveVariableList(
		mesh->variableSize( PrimitiveVariable::Uniform ),
		mesh->variableSize( PrimitiveVariable::Vertex ),
		mesh->variableSize( PrimitiveVariable::Varying ),
		mesh->variableSize( PrimitiveVariable::FaceVarying )
	);

	for( auto &primitiveVariable : mesh->variables )
	{
		const RtUString name( primitiveVariable.first == "uv" ? "st" : primitiveVariable.first.c_str() );
		GeometryAlgo::convertPrimitiveVariable( name, primitiveVariable.second, *primVars );
	}

	primVars->SetIntegerDetail( Rix::k_Ri_nvertices, mesh->verticesPerFace()->readable().data(), RixDetailType::k_uniform );
	primVars->SetIntegerDetail( Rix::k_Ri_vertices, mesh->vertexIds()->readable().data(), RixDetailType::k_facevarying );

	RtUString geometryType = Rix::k_Ri_PolygonMesh;
	if( mesh->interpolation() == "catmullClark" )
	{
		geometryType = Rix::k_Ri_SubdivisionMesh;
		primVars->SetString( Rix::k_Ri_scheme, Rix::k_catmullclark );

		vector<RtUString> tagNames;
		vector<RtInt> tagArgCounts;
		vector<RtInt> tagIntArgs;
		vector<RtFloat> tagFloatArgs;
		vector<RtToken> tagStringArgs;

		for( int creaseLength : mesh->creaseLengths()->readable() )
		{
			tagNames.push_back( Rix::k_crease );
			tagArgCounts.push_back( creaseLength ); // integer argument count
			tagArgCounts.push_back( 1 ); // float argument count
			tagArgCounts.push_back( 0 ); // string argument count
		}

		tagIntArgs = mesh->creaseIds()->readable();
		tagFloatArgs = mesh->creaseSharpnesses()->readable();

		if( mesh->cornerIds()->readable().size() )
		{
			tagNames.push_back( Rix::k_corner );
			tagArgCounts.push_back( mesh->cornerIds()->readable().size() ); // integer argument count
			tagArgCounts.push_back( mesh->cornerIds()->readable().size() ); // float argument count
			tagArgCounts.push_back( 0 ); // string argument count
			tagIntArgs.insert( tagIntArgs.end(), mesh->cornerIds()->readable().begin(), mesh->cornerIds()->readable().end() );
			tagFloatArgs.insert( tagFloatArgs.end(), mesh->cornerSharpnesses()->readable().begin(), mesh->cornerSharpnesses()->readable().end() );
		}

		primVars->SetStringArray( Rix::k_Ri_subdivtags, tagNames.data(), tagNames.size() );
		primVars->SetIntegerArray( Rix::k_Ri_subdivtagnargs, tagArgCounts.data(), tagArgCounts.size() );
		primVars->SetFloatArray( Rix::k_Ri_subdivtagfloatargs, tagFloatArgs.data(), tagFloatArgs.size() );
		primVars->SetIntegerArray( Rix::k_Ri_subdivtagintargs, tagIntArgs.data(), tagIntArgs.size() );
	}

	return riley->CreateGeometryMaster(
		geometryType, DisplacementId::k_InvalidId, *primVars
	);
}

GeometryAlgo::ConverterDescription<MeshPrimitive> g_meshConverterDescription( convertStaticMesh );

} // namespace

//////////////////////////////////////////////////////////////////////////
// Points
//////////////////////////////////////////////////////////////////////////

namespace
{

riley::GeometryMasterId convertStaticPoints( const IECoreScene::PointsPrimitive *points, riley::Riley *riley )
{
	auto primVars = ParamListAlgo::makePrimitiveVariableList(
		points->variableSize( PrimitiveVariable::Uniform ),
		points->variableSize( PrimitiveVariable::Vertex ),
		points->variableSize( PrimitiveVariable::Varying ),
		points->variableSize( PrimitiveVariable::FaceVarying )
	);

	for( auto &primitiveVariable : points->variables )
	{
		GeometryAlgo::convertPrimitiveVariable( RtUString( primitiveVariable.first.c_str() ), primitiveVariable.second, *primVars );
	}

	return riley->CreateGeometryMaster(
		Rix::k_Ri_Points, DisplacementId::k_InvalidId, *primVars
	);
}

GeometryAlgo::ConverterDescription<PointsPrimitive> g_pointsConverterDescription( convertStaticPoints );

} // namespace

//////////////////////////////////////////////////////////////////////////
// Curves
//////////////////////////////////////////////////////////////////////////

namespace
{

riley::GeometryMasterId convertStaticCurves( const IECoreScene::CurvesPrimitive *curves, riley::Riley *riley )
{
	auto primVars = ParamListAlgo::makePrimitiveVariableList(
		curves->variableSize( PrimitiveVariable::Uniform ),
		curves->variableSize( PrimitiveVariable::Vertex ),
		curves->variableSize( PrimitiveVariable::Varying ),
		curves->variableSize( PrimitiveVariable::FaceVarying )
	);

	for( auto &primitiveVariable : curves->variables )
	{
		GeometryAlgo::convertPrimitiveVariable( RtUString( primitiveVariable.first.c_str() ), primitiveVariable.second, *primVars );
	}

	if( curves->basis().standardBasis() == StandardCubicBasis::Unknown )
	{
		IECore::msg( IECore::Msg::Warning, "IECoreRenderMan", "Unsupported CubicBasis" );
		primVars->SetString( Rix::k_Ri_type, Rix::k_linear );
	}
	else if( curves->basis().standardBasis() == StandardCubicBasis::Linear )
	{
		primVars->SetString( Rix::k_Ri_type, Rix::k_linear );
	}
	else
	{
		primVars->SetString( Rix::k_Ri_type, Rix::k_cubic );
		switch( curves->basis().standardBasis() )
		{
			case StandardCubicBasis::Bezier :
				primVars->SetString( Rix::k_Ri_Basis, Rix::k_bezier );
				break;
			case StandardCubicBasis::BSpline :
				primVars->SetString( Rix::k_Ri_Basis, Rix::k_bspline );
				break;
			case StandardCubicBasis::CatmullRom :
				primVars->SetString( Rix::k_Ri_Basis, Rix::k_catmullrom );
				break;
			default :
				// Should have dealt with Unknown and Linear above
				assert( false );
		}
	}

	primVars->SetString( Rix::k_Ri_wrap, curves->periodic() ? Rix::k_periodic : Rix::k_nonperiodic );
	primVars->SetIntegerDetail( Rix::k_Ri_nvertices, curves->verticesPerCurve()->readable().data(), RixDetailType::k_uniform );

	return riley->CreateGeometryMaster(
		Rix::k_Ri_Curves, DisplacementId::k_InvalidId, *primVars
	);
}

GeometryAlgo::ConverterDescription<CurvesPrimitive> g_curvesConverterDescription( convertStaticCurves );

} // namespace
