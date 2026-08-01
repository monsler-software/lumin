//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Corona game engine.
// For overview and more information on licensing please refer to README.md
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////

#include "Core/Rtt_Build.h"

#include "Display/Rtt_Material3D.h"

#include "Display/Rtt_Texture3D.h"

// ----------------------------------------------------------------------------

namespace Rtt
{

// ----------------------------------------------------------------------------

static float
Clamp01( float value )
{
	if ( value < 0.0f )
	{
		return 0.0f;
	}

	if ( value > 1.0f )
	{
		return 1.0f;
	}

	return value;
}

// A perfect mirror is not a useful default and is hard to look at, so an
// unconfigured material is a plain white dielectric of middling roughness --
// what a matte plastic object looks like.
Material3D::Material3D()
:	fRoughness( 0.5f ),
	fMetallic( 0.0f ),
	fAlbedoMap( NULL ),
	fMetallicRoughnessMap( NULL ),
	fEmissiveMap( NULL ),
	fIsDoubleSided( true ),
	fIsTranslucent( false ),
	fRefCount( 1 )
{
	fAlbedo[0] = 1.0f;
	fAlbedo[1] = 1.0f;
	fAlbedo[2] = 1.0f;
	fAlbedo[3] = 1.0f;

	fEmissive[0] = 0.0f;
	fEmissive[1] = 0.0f;
	fEmissive[2] = 0.0f;
}

Material3D::~Material3D()
{
	if ( fAlbedoMap != NULL ) { fAlbedoMap->Release(); }
	if ( fMetallicRoughnessMap != NULL ) { fMetallicRoughnessMap->Release(); }
	if ( fEmissiveMap != NULL ) { fEmissiveMap->Release(); }
}

// Retain before release throughout, so that setting the map a material already
// has cannot drop the last reference in between and free it.
static void
AssignTexture( Texture3D*& slot, Texture3D* texture, bool isColorData )
{
	if ( texture != NULL )
	{
		texture->Retain();

		// The slot decides how the values are read, not the file: the same PNG is
		// colour as a base map and data as a roughness map.
		texture->SetColorData( isColorData );
	}

	if ( slot != NULL )
	{
		slot->Release();
	}

	slot = texture;
}

void
Material3D::SetAlbedoMap( Texture3D* texture )
{
	AssignTexture( fAlbedoMap, texture, true );
}

void
Material3D::SetMetallicRoughnessMap( Texture3D* texture )
{
	AssignTexture( fMetallicRoughnessMap, texture, false );
}

void
Material3D::SetEmissiveMap( Texture3D* texture )
{
	AssignTexture( fEmissiveMap, texture, true );
}

void
Material3D::SetAlbedo( float r, float g, float b, float a )
{
	fAlbedo[0] = r;
	fAlbedo[1] = g;
	fAlbedo[2] = b;
	fAlbedo[3] = a;
}

void
Material3D::GetAlbedo( float& r, float& g, float& b, float& a ) const
{
	r = fAlbedo[0];
	g = fAlbedo[1];
	b = fAlbedo[2];
	a = fAlbedo[3];
}

void
Material3D::SetRoughness( float value )
{
	// The floor is not zero: the GGX distribution divides by roughness to the
	// fourth, so an exactly smooth surface produces an infinite highlight on
	// whichever pixel happens to hit it.
	fRoughness = Clamp01( value );

	if ( fRoughness < 0.03f )
	{
		fRoughness = 0.03f;
	}
}

void
Material3D::SetMetallic( float value )
{
	fMetallic = Clamp01( value );
}

void
Material3D::SetEmissive( float r, float g, float b )
{
	fEmissive[0] = r;
	fEmissive[1] = g;
	fEmissive[2] = b;
}

void
Material3D::GetEmissive( float& r, float& g, float& b ) const
{
	r = fEmissive[0];
	g = fEmissive[1];
	b = fEmissive[2];
}

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------
