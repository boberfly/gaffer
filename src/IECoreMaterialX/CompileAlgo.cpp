//////////////////////////////////////////////////////////////////////////
//
//  Copyright (c) 2016, Image Engine Design Inc. All rights reserved.
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

#include "IECoreMaterialX/CompileAlgo.h"
#include "IECoreMaterialX/Private/CapturingErrorHandler.h"

#include "IECore/Exception.h"
#include "IECore/SearchPath.h"
#include "IECore/StringAlgo.h"

#include "OSL/oslcomp.h"

#include "boost/algorithm/string/replace.hpp"
#include "boost/filesystem.hpp"

using namespace std;
using namespace IECore;
using namespace OSL;
using namespace IECoreMaterialX;

namespace
{

class ScopedDirectory : boost::noncopyable
{

	public :

		ScopedDirectory( const std::filesystem::path &p )
			:	m_path( p )
		{
			std::filesystem::create_directories( m_path );
		}

		~ScopedDirectory()
		{
			std::filesystem::remove_all( m_path );
		}

	private :

		std::filesystem::path m_path;

};

} // namespace

std::filesystem::path CompileAlgo::compile( const std::string &shaderName, const std::string &shaderSource )
{

	// We need to ensure the existence of a unique .oso file
	// containing the compiled code. We allow the user to specify
	// the base location for such files using the IECORE_MATERIALX_CODE_DIRECTORY
	// environment variable, but we must also assume that multiple processes
	// on multiple machines will be concurrently trying to ensure the
	// same .oso files exist (think renderfarm). We achieve this as follows :
	//
	// - Use a hash of the code itself to generate the final .oso filename.
	//   This does nothing to resolve concurrent accesses, but ensures that
	//   different code goes in different files.
	// - If the required file does not exist yet, first generate it in a temporary
	//   location unique to this process.
	// - Finally, move the file into place using an atomic `rename()`.

	// Start by generating our final desired filename.

	std::filesystem::path directory = std::filesystem::temp_directory_path() / "iecoreMaterialXOSLCode";
	if( const char *cd = getenv( "IECORE_MATERIALX_CODE_DIRECTORY" ) )
	{
		directory = cd;
	}

	const std::filesystem::path osoFileName = directory / ( shaderName + ".oso" );

	// If that exists, then someone else has done our work already.

	if( std::filesystem::exists( osoFileName ) )
	{
		return osoFileName.generic_string();
	}

	// Make a temporary directory we can do our compilation in. The
	// ScopedDirectory class will remove it for us automatically on
	// destruction, so we don't need to worry about exception handling.

	const std::filesystem::path tempDirectory = directory / boost::filesystem::unique_path().string();
	ScopedDirectory scopedTempDirectory( tempDirectory );

	// Write the source code out.

	const std::string tempOSLFileName = ( tempDirectory / ( shaderName + ".osl" ) ).generic_string();
	std::ofstream f( tempOSLFileName.c_str() );
	if( !f.good() )
	{
		throw IECore::IOException( "Unable to open file \"" + tempOSLFileName + "\"" );
	}
	f << shaderSource;
	if( !f.good() )
	{
		throw IECore::IOException( "Failed to write to \"" + tempOSLFileName + "\"" );
	}
	f.close();

	// Compile.

	IECoreMaterialX::Private::CapturingErrorHandler errorHandler;
	OSLCompiler compiler( &errorHandler );

	vector<string> options;
	if( const char *includePaths = getenv( "OSL_SHADER_PATHS" ) )
	{
		SearchPath searchPaths( includePaths );
		for( const auto &p : searchPaths.paths )
		{
			options.push_back( string( "-I" ) + p.generic_string() );
		}
	}

	const std::string tempOSOFileName = ( tempDirectory / ( shaderName + ".oso" ) ).generic_string();
	options.push_back( "-o" );
	options.push_back( tempOSOFileName );

	if( !compiler.compile( tempOSLFileName, options ) )
	{
		if( errorHandler.errors().size() )
		{
			string error = errorHandler.errors();
			boost::replace_all( error, tempOSLFileName, "code" );
			throw IECore::Exception( error );
		}
		else
		{
			throw IECore::Exception( "Unknown compilation error" );
		}
	}

	if( !std::filesystem::file_size( tempOSLFileName ) )
	{
		// Belt and braces. `compiler.compile()` should be reporting all errors,
		// but on rare occasions we have still seen empty `.oso` files being
		// produced. Detect this and warn so we can get to the bottom of it.
		throw IECore::Exception( "Empty file after compilation : \"" + tempOSLFileName + "\"" );
	}

	// Move temp file where we really want it, and clean up.

	std::filesystem::rename( tempOSOFileName, osoFileName );

	if( !std::filesystem::file_size( osoFileName ) )
	{
		// Belt and braces. `rename()` should be reporting all errors,
		// but on rare occasions we have still seen empty `.oso` files being
		// produced. Detect this and warn so we can get to the bottom of it.
		throw IECore::Exception( "Empty file after rename : \"" + osoFileName.generic_string() + "\"" );
	}

	return osoFileName;
}
