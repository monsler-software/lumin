//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Corona game engine.
// For overview and more information on licensing please refer to README.md
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////

#include "Core/Rtt_Build.h"

#include "Display/Rtt_Camera3D.h"

#include "Display/Rtt_Object3D.h"

#include <cmath>

// ----------------------------------------------------------------------------

namespace Rtt
{

// ----------------------------------------------------------------------------

Camera3D::Camera3D()
:	fX( 0.0f ),
	fY( 0.0f ),
	fZ( -10.0f ),
	fLookAtX( 0.0f ),
	fLookAtY( 0.0f ),
	fLookAtZ( 0.0f ),
	fUpX( 0.0f ),
	fUpY( 1.0f ),
	fUpZ( 0.0f ),
	fFieldOfView( 60.0f ),
	fNear( 0.1f ),
	fFar( 1000.0f ),
	fAspectRatio( 0.0f ),
	fTarget( NULL ),
	fRefCount( 1 )
{
}

Camera3D::~Camera3D()
{
}

void
Camera3D::SetPosition( float x, float y, float z )
{
	fX = x;
	fY = y;
	fZ = z;
}

void
Camera3D::GetPosition( float& x, float& y, float& z ) const
{
	x = fX;
	y = fY;
	z = fZ;
}

void
Camera3D::LookAt( float x, float y, float z )
{
	fLookAtX = x;
	fLookAtY = y;
	fLookAtZ = z;
	fTarget = NULL;
}

void
Camera3D::SetTarget( DisplayObject* object )
{
	fTarget = object;
}

// ----------------------------------------------------------------------------

static void
Normalize( float& x, float& y, float& z )
{
	const float lengthSquared = x * x + y * y + z * z;

	if ( lengthSquared > 0.0f )
	{
		const float inverse = 1.0f / std::sqrt( lengthSquared );

		x *= inverse;
		y *= inverse;
		z *= inverse;
	}
}

static void
Cross( float ax, float ay, float az, float bx, float by, float bz, float& x, float& y, float& z )
{
	x = ay * bz - az * by;
	y = az * bx - ax * bz;
	z = ax * by - ay * bx;
}

void
Camera3D::GetViewMatrix( float* out ) const
{
	float atX = fLookAtX;
	float atY = fLookAtY;
	float atZ = fLookAtZ;

	// A target that has since been removed leaves fTarget dangling as far as
	// following goes, so the object is asked whether it is still in the scene
	// before its position is trusted.
	if ( fTarget != NULL )
	{
		Object3D* object = fTarget->AsObject3D();

		if ( object != NULL )
		{
			object->GetPosition3D( atX, atY, atZ );
		}
	}

	// Left-handed: +X right, +Y up, +Z away from the viewer. This matches
	// bgfx's own convention, and means a camera at negative Z looking at the
	// origin -- as in the example every project starts from -- sees the front
	// of what is there.
	float fx = atX - fX;
	float fy = atY - fY;
	float fz = atZ - fZ;

	Normalize( fx, fy, fz );

	// Degenerate when the camera sits exactly on what it is looking at. Rather
	// than emit a matrix full of NaNs that quietly blanks the scene, fall back
	// to facing forwards.
	if ( fx == 0.0f && fy == 0.0f && fz == 0.0f )
	{
		fz = 1.0f;
	}

	float rx, ry, rz;
	Cross( fUpX, fUpY, fUpZ, fx, fy, fz, rx, ry, rz );
	Normalize( rx, ry, rz );

	// Also degenerate: looking straight up or down makes the view direction
	// parallel to up, so the right vector vanishes. Any perpendicular will do,
	// and +X is the one that keeps the horizon where it was.
	if ( rx == 0.0f && ry == 0.0f && rz == 0.0f )
	{
		rx = 1.0f;
		ry = 0.0f;
		rz = 0.0f;
	}

	float ux, uy, uz;
	Cross( fx, fy, fz, rx, ry, rz, ux, uy, uz );

	out[0] = rx;  out[1] = ux;  out[2] = fx;  out[3] = 0.0f;
	out[4] = ry;  out[5] = uy;  out[6] = fy;  out[7] = 0.0f;
	out[8] = rz;  out[9] = uz;  out[10] = fz; out[11] = 0.0f;

	out[12] = -( rx * fX + ry * fY + rz * fZ );
	out[13] = -( ux * fX + uy * fY + uz * fZ );
	out[14] = -( fx * fX + fy * fY + fz * fZ );
	out[15] = 1.0f;
}

void
Camera3D::GetProjectionMatrix( float* out, U32 surfaceWidth, U32 surfaceHeight, bool homogeneousDepth ) const
{
	float aspect = fAspectRatio;

	if ( aspect <= 0.0f )
	{
		aspect = ( surfaceHeight > 0 )
			? (float) surfaceWidth / (float) surfaceHeight
			: 1.0f;
	}

	const float radians = fFieldOfView * ( 3.14159265358979323846f / 180.0f );
	const float height = 1.0f / std::tan( radians * 0.5f );
	const float width = height / aspect;

	const float range = fFar - fNear;
	const float depthScale = ( range != 0.0f ) ? fFar / range : 0.0f;

	for ( int i = 0; i < 16; ++i )
	{
		out[i] = 0.0f;
	}

	out[0] = width;
	out[5] = height;
	out[10] = depthScale;
	out[11] = 1.0f;
	out[14] = -fNear * depthScale;

	// OpenGL-style clip space puts the near plane at -1 rather than 0, so the
	// 0..1 mapping above is stretched to cover it.
	if ( homogeneousDepth )
	{
		out[10] = depthScale * 2.0f - 1.0f;
		out[14] = -fNear * depthScale * 2.0f;
	}
}

// ----------------------------------------------------------------------------

void
Camera3D::GetLookAt( float& x, float& y, float& z ) const
{
	x = fLookAtX;
	y = fLookAtY;
	z = fLookAtZ;

	if ( fTarget != NULL )
	{
		Object3D* object = fTarget->AsObject3D();

		if ( object != NULL )
		{
			object->GetPosition3D( x, y, z );
		}
	}
}

void
Camera3D::GetRay( float ndcX, float ndcY, float aspect, float* origin, float* direction ) const
{
	// The eye, which is where every ray through a perspective camera starts.
	origin[0] = fX;
	origin[1] = fY;
	origin[2] = fZ;

	// The same basis GetViewMatrix builds, including the two degenerate cases it
	// guards against and for the same reasons.
	float atX = fLookAtX;
	float atY = fLookAtY;
	float atZ = fLookAtZ;

	if ( fTarget != NULL )
	{
		Object3D* object = fTarget->AsObject3D();

		if ( object != NULL )
		{
			object->GetPosition3D( atX, atY, atZ );
		}
	}

	float fx = atX - fX;
	float fy = atY - fY;
	float fz = atZ - fZ;

	Normalize( fx, fy, fz );

	if ( fx == 0.0f && fy == 0.0f && fz == 0.0f )
	{
		fz = 1.0f;
	}

	float rx, ry, rz;
	Cross( fUpX, fUpY, fUpZ, fx, fy, fz, rx, ry, rz );
	Normalize( rx, ry, rz );

	if ( rx == 0.0f && ry == 0.0f && rz == 0.0f )
	{
		rx = 1.0f;
		ry = 0.0f;
		rz = 0.0f;
	}

	float ux, uy, uz;
	Cross( fx, fy, fz, rx, ry, rz, ux, uy, uz );

	// A pinned aspect ratio wins over the surface's, exactly as in the projection.
	if ( fAspectRatio > 0.0f )
	{
		aspect = fAspectRatio;
	}

	if ( aspect <= 0.0f )
	{
		aspect = 1.0f;
	}

	// The projection scales view-space x by 1/(tan(fov/2) * aspect) and y by
	// 1/tan(fov/2) before dividing by z, so undoing it is a multiply by each --
	// which is all this is. Kept in step with GetProjectionMatrix: the field of
	// view is the vertical one there, and so it is here.
	const float radians = fFieldOfView * ( 3.14159265358979323846f / 180.0f );
	const float tanHalf = std::tan( radians * 0.5f );

	const float sx = ndcX * tanHalf * aspect;
	const float sy = ndcY * tanHalf;

	direction[0] = fx + rx * sx + ux * sy;
	direction[1] = fy + ry * sx + uy * sy;
	direction[2] = fz + rz * sx + uz * sy;

	Normalize( direction[0], direction[1], direction[2] );
}

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------
