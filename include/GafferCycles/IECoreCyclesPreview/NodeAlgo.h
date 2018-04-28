//////////////////////////////////////////////////////////////////////////
//
//  Copyright (c) 2018, Alex Fuller. All rights reserved.
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

#ifndef IECORECYCLES_NODEALGO_H
#define IECORECYCLES_NODEALGO_H

#include <vector>

#include "cycles/render/camera.h"
#include "cycles/render/mesh.h"
#include "cycles/render/object.h"
#include "cycles/render/light.h"

#include "IECoreScene/Camera.h"

#include "IECore/Object.h"

// Change this to "IECoreCycles/Export.h" and remove the define when it goes into Cortex.
#include "GafferCycles/Export.h"
#define IECORECYCLES_API GAFFERCYCLES_API

namespace IECoreCycles
{

namespace NodeAlgo
{

IECORECYCLES_API ccl::Camera *convert( const IECoreScene::Camera *camera, const std::string &nodeName );

IECORECYCLES_API ccl::Light *convert( const IECoreScene::Light *light, const std::string &nodeName );

/// Converts the specified IECore::Object into an equivalent
/// Cycles object, returning nullptr if no conversion is
/// available.
IECORECYCLES_API ccl::Object *convert( const IECore::Object *object, const std::string &nodeName );
/// As above, but converting a moving object. If no motion converter
/// is available, the first sample is converted instead.
IECORECYCLES_API ccl::Object *convert( const std::vector<const IECore::Object *> &samples, const std::string &nodeName );

/// Signature of a function which can convert an IECore::Object
/// into a Cycles Object.
typedef ccl::Object * (*Converter)( const IECore::Object *, const std::string &nodeName );
typedef ccl::Object * (*MotionConverter)( const std::vector<const IECore::Object *> &samples, const std::string &nodeName );

/// Registers a converter for a specific type.
/// Use the ConverterDescription utility class in preference to
/// this, since it provides additional type safety.
IECORECYCLES_API void registerConverter( IECore::TypeId fromType, Converter converter, MotionConverter motionConverter = nullptr );

/// Class which registers a converter for type T automatically
/// when instantiated.
template<typename T>
class ConverterDescription
{

	public :

		/// Type-specific conversion functions.
		typedef ccl::Object * (*Converter)( const T *, const std::string& );
		typedef ccl::Object * (*MotionConverter)( const std::vector<const T *> &, const std::string& );

		ConverterDescription( Converter converter, MotionConverter motionConverter = nullptr )
		{
			registerConverter(
				T::staticTypeId(),
				reinterpret_cast<ObjectAlgo::Converter>( converter ),
				reinterpret_cast<ObjectAlgo::MotionConverter>( motionConverter )
			);
		}

};

} // namespace NodeAlgo

} // namespace IECoreCycles

#endif // IECORECYCLES_NODEALGO_H
