//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Corona game engine.
// For overview and more information on licensing please refer to README.md
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////

#include "Core/Rtt_Build.h"

#include "Display/Rtt_Scene3D.h"

#include "Display/Rtt_Camera3D.h"
#include "Display/Rtt_Object3D.h"
#include "Display/Rtt_Texture3D.h"

#include <algorithm>
#include <cmath>

// ----------------------------------------------------------------------------

namespace Rtt
{

// ----------------------------------------------------------------------------

Scene3D::Scene3D()
:	fActiveCamera( NULL ),
	fEnvironmentMap( NULL ),
	fEnvironmentIntensity( 1.0f )
{
	memset( fIrradiance, 0, sizeof( fIrradiance ) );

	// A dim ambient rather than none, so that a scene with a single light is
	// legible from the first frame instead of showing solid black backs that
	// read as broken rendering.
	fAmbient[0] = 0.03f;
	fAmbient[1] = 0.03f;
	fAmbient[2] = 0.03f;
}

Scene3D::~Scene3D()
{
	if ( fActiveCamera != NULL )
	{
		fActiveCamera->Release();
		fActiveCamera = NULL;
	}

	if ( fEnvironmentMap != NULL )
	{
		fEnvironmentMap->Release();
		fEnvironmentMap = NULL;
	}
}

// ----------------------------------------------------------------------------

void
Scene3D::SetEnvironmentIntensity( float value )
{
	fEnvironmentIntensity = ( value < 0.0f ) ? 0.0f : value;
}

void
Scene3D::SetEnvironmentMap( Texture3D* texture )
{
	// Retained before the old one is released, as everything else shared here is.
	if ( texture != NULL )
	{
		texture->Retain();
	}

	if ( fEnvironmentMap != NULL )
	{
		fEnvironmentMap->Release();
	}

	fEnvironmentMap = texture;

	memset( fIrradiance, 0, sizeof( fIrradiance ) );

	if ( fEnvironmentMap != NULL )
	{
		// The environment is colour data, so the renderer must linearise it on
		// read; the projection below does its own linearisation of the same texels.
		fEnvironmentMap->SetColorData( true );

		ProjectIrradiance();
	}
}

// Projects the environment onto second-order spherical harmonics.
//
// Every texel is one sample of incoming radiance from the direction its
// equirectangular position names, weighted by the solid angle it covers -- which
// shrinks towards the poles as sin(theta), and is why the weight is not uniform.
//
// The nine basis functions and the convolution constants are the standard ones
// from Ramamoorthi and Hanrahan: projecting radiance and then multiplying by the
// per-band factors below turns the projection into irradiance, so the shader can
// evaluate it directly instead of convolving with a cosine lobe itself.
void
Scene3D::ProjectIrradiance()
{
	const std::vector< U8 >& pixels = fEnvironmentMap->GetPixels();

	const U32 width = fEnvironmentMap->GetWidth();
	const U32 height = fEnvironmentMap->GetHeight();

	if ( pixels.empty() || width == 0 || height == 0 )
	{
		return;
	}

	// A large map is sampled on a grid rather than in full: 128 by 64 is far more
	// samples than nine coefficients need, and it keeps setEnvironmentMap from
	// stalling on a 4K HDRI.
	const U32 stepX = ( width > 128 ) ? width / 128 : 1;
	const U32 stepY = ( height > 64 ) ? height / 64 : 1;

	double coefficients[kIrradianceCoefficients][3];
	memset( coefficients, 0, sizeof( coefficients ) );

	double totalWeight = 0.0;

	const double kPi = 3.14159265358979323846;

	for ( U32 y = 0; y < height; y += stepY )
	{
		// Theta from the top of the image down, which is how equirectangular maps
		// are laid out.
		const double theta = ( ( y + 0.5 ) / (double) height ) * kPi;
		const double sinTheta = std::sin( theta );
		const double cosTheta = std::cos( theta );

		for ( U32 x = 0; x < width; x += stepX )
		{
			const double phi = ( ( x + 0.5 ) / (double) width ) * 2.0 * kPi;

			// The same mapping the shader inverts, so a direction sampled there
			// lands on the texel projected here.
			const double dx = sinTheta * std::sin( phi );
			const double dy = cosTheta;
			const double dz = -sinTheta * std::cos( phi );

			const size_t offset = ( (size_t) y * width + x ) * 4;

			double rgb[3];

			for ( int c = 0; c < 3; ++c )
			{
				const double v = pixels[offset + c] / 255.0;

				// sRGB to linear, matching what the sampler will do to these same
				// texels: projecting gamma-encoded values would bias the result
				// bright, most visibly in the dark half of a sky.
				rgb[c] = ( v <= 0.04045 ) ? ( v / 12.92 ) : std::pow( ( v + 0.055 ) / 1.055, 2.4 );
			}

			// Solid angle of this texel. Constant factors cancel in the
			// normalisation below, so only the sin(theta) shape matters.
			const double weight = sinTheta;

			const double basis[kIrradianceCoefficients] =
			{
				0.282095,                                   // Y00
				0.488603 * dy,                              // Y1-1
				0.488603 * dz,                              // Y10
				0.488603 * dx,                              // Y11
				1.092548 * dx * dy,                         // Y2-2
				1.092548 * dy * dz,                         // Y2-1
				0.315392 * ( 3.0 * dz * dz - 1.0 ),         // Y20
				1.092548 * dx * dz,                         // Y21
				0.546274 * ( dx * dx - dy * dy )            // Y22
			};

			for ( int i = 0; i < kIrradianceCoefficients; ++i )
			{
				for ( int c = 0; c < 3; ++c )
				{
					coefficients[i][c] += rgb[c] * basis[i] * weight;
				}
			}

			totalWeight += weight;
		}
	}

	if ( totalWeight <= 0.0 )
	{
		return;
	}

	// The samples covered the whole sphere, so their weights should sum to 4*pi.
	const double normalise = 4.0 * kPi / totalWeight;

	// Per-band cosine convolution: turns the radiance projection into irradiance.
	static const double kBandFactor[kIrradianceCoefficients] =
	{
		3.141593,
		2.094395, 2.094395, 2.094395,
		0.785398, 0.785398, 0.785398, 0.785398, 0.785398
	};

	for ( int i = 0; i < kIrradianceCoefficients; ++i )
	{
		for ( int c = 0; c < 3; ++c )
		{
			// Divided by pi so that what the shader gets back, multiplied by
			// albedo, is the diffuse term directly.
			fIrradiance[i * 4 + c] = (float) ( coefficients[i][c] * normalise * kBandFactor[i] / kPi );
		}

		fIrradiance[i * 4 + 3] = 0.0f;
	}
}

void
Scene3D::SetActiveCamera( Camera3D* camera )
{
	// Retained before the old one is released, since setting the active camera
	// to the camera that is already active would otherwise drop the last
	// reference and free it.
	if ( camera != NULL )
	{
		camera->Retain();
	}

	if ( fActiveCamera != NULL )
	{
		fActiveCamera->Release();
	}

	fActiveCamera = camera;
}

void
Scene3D::AddLight( Light3D* light )
{
	fLights.push_back( light );
}

void
Scene3D::RemoveLight( Light3D* light )
{
	std::vector< Light3D* >::iterator i = std::find( fLights.begin(), fLights.end(), light );

	if ( i != fLights.end() )
	{
		fLights.erase( i );
	}
}

Light3D*
Scene3D::GetShadowLight() const
{
	for ( size_t i = 0, iMax = fLights.size(); i < iMax; ++i )
	{
		// Hidden lights light nothing, so they cast nothing either.
		if ( fLights[i]->CastsShadows()
			&& fLights[i]->IsVisible()
			&& fLights[i]->GetKind() == Draw3DLight::kDirectional )
		{
			return fLights[i];
		}
	}

	return NULL;
}

U32
Scene3D::GatherLights( Draw3DLight* out, const Light3D* target, U32* targetIndex ) const
{
	U32 count = 0;

	if ( targetIndex != NULL )
	{
		*targetIndex = kMaxDraw3DLights;
	}

	for ( size_t i = 0, iMax = fLights.size(); i < iMax && count < kMaxDraw3DLights; ++i )
	{
		if ( fLights[i]->GetLightData( out[count] ) )
		{
			if ( targetIndex != NULL && fLights[i] == target )
			{
				*targetIndex = count;
			}

			++count;
		}
	}

	return count;
}

void
Scene3D::SetAmbient( float r, float g, float b )
{
	fAmbient[0] = r;
	fAmbient[1] = g;
	fAmbient[2] = b;
}

void
Scene3D::GetAmbient( float& r, float& g, float& b ) const
{
	r = fAmbient[0];
	g = fAmbient[1];
	b = fAmbient[2];
}

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------
