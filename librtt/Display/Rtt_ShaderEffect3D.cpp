//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Corona game engine.
// For overview and more information on licensing please refer to README.md
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////

#include "Core/Rtt_Build.h"

#include "Display/Rtt_ShaderEffect3D.h"

#include <cstring>

// ----------------------------------------------------------------------------

namespace Rtt
{

// ----------------------------------------------------------------------------

ShaderEffect3D::ShaderEffect3D()
:	fCullMode( kCullBack ),
	fIsTranslucent( false ),
	fWritesDepth( true ),
	fGpuId( kInvalidGpuId ),
	fRefCount( 1 )
{
}

ShaderEffect3D::~ShaderEffect3D()
{
}

int
ShaderEffect3D::FindUniform( const char* name ) const
{
	if ( name == NULL )
	{
		return -1;
	}

	for ( size_t i = 0, iMax = fUniforms.size(); i < iMax; ++i )
	{
		if ( fUniforms[i].fName == name )
		{
			return (int) i;
		}
	}

	return -1;
}

bool
ShaderEffect3D::SetUniform( const char* name, const float* value )
{
	if ( name == NULL || *name == '\0' || value == NULL )
	{
		return false;
	}

	const int existing = FindUniform( name );

	if ( existing >= 0 )
	{
		memcpy( fUniforms[existing].fValue, value, 4 * sizeof( float ) );

		return true;
	}

	// A new name after the program has been built would need a uniform handle
	// the compiled shader has no declaration for, so the set is fixed once the
	// effect has been drawn. Callers declare everything up front, which
	// render.newShaderEffect's options table encourages anyway.
	if ( fGpuId != kInvalidGpuId )
	{
		return false;
	}

	if ( fUniforms.size() >= (size_t) kMaxUniforms )
	{
		return false;
	}

	Uniform uniform;

	uniform.fName = name;
	memcpy( uniform.fValue, value, 4 * sizeof( float ) );

	fUniforms.push_back( uniform );

	return true;
}

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------
