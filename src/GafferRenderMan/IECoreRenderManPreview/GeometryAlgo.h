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

#ifndef IECORERENDERMAN_GEOMETRYALGO_H
#define IECORERENDERMAN_GEOMETRYALGO_H

#include "IECoreScene/Primitive.h"

#include "IECore/Object.h"
#include "IECore/VectorTypedData.h"

#include "Riley.h"
#include "RtParamList.h"

#include <vector>

namespace IECoreRenderMan
{

namespace GeometryAlgo
{

/// Geometry conversion
/// ===================

/// Converts the specified IECore::Object into an equivalent
/// geometry master in Riley. Returns k_InvalidId if no converter
/// is available.
riley::GeometryMasterId convert( const IECore::Object *object, riley::Riley *riley );

/// As above, but converting a moving object. If no motion converter
/// is available, the first sample is converted instead.
riley::GeometryMasterId convert( const std::vector<const IECore::Object *> &samples, const std::vector<float> &sampleTimes, riley::Riley *riley );

/// Signature of a function which can convert an IECore::Object
/// into a geometry master.
using Converter = riley::GeometryMasterId (*)( const IECore::Object *, riley::Riley * );
using MotionConverter = riley::GeometryMasterId (*)( const std::vector<const IECore::Object *> &samples, const std::vector<float> &sampleTimes, riley::Riley *riley );

/// Registers a converter for a specific type.
/// Use the ConverterDescription utility class in preference to
/// this, since it provides additional type safety.
void registerConverter( IECore::TypeId fromType, Converter converter, MotionConverter motionConverter = nullptr );

/// Class which registers a converter for type T automatically
/// when instantiated.
template<typename T>
class ConverterDescription
{

	public :

		/// Type-specific conversion functions.
		using Converter = riley::GeometryMasterId (*)( const T *, riley::Riley * );
		using MotionConverter = riley::GeometryMasterId (*)( const std::vector<const T *> &samples, const std::vector<float> &sampleTimes, riley::Riley * );

		ConverterDescription( Converter converter, MotionConverter motionConverter = nullptr )
		{
			registerConverter(
				T::staticTypeId(),
				reinterpret_cast<GeometryAlgo::Converter>( converter ),
				reinterpret_cast<GeometryAlgo::MotionConverter>( motionConverter )
			);
		}

};

/// PrimitiveVariable conversion
/// ============================

void convertPrimitiveVariable( RtUString name, const IECoreScene::PrimitiveVariable &primitiveVariable, RtParamList &paramList );

} // namespace GeometryAlgo

} // namespace IECoreRenderMan

#endif // IECORERENDERMAN_GEOMETRYALGO_H
