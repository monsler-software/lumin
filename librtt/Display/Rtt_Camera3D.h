//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Corona game engine.
// For overview and more information on licensing please refer to README.md
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////

#ifndef _Rtt_Camera3D_H__
#define _Rtt_Camera3D_H__

#include "Core/Rtt_Types.h"

// ----------------------------------------------------------------------------

namespace Rtt
{

class DisplayObject;

// ----------------------------------------------------------------------------

// The eye the 3D pipeline renders from.
//
// Not a DisplayObject: a camera is never drawn, never hit-tested and never
// belongs to a group, so inheriting the display list's machinery would only
// mean explaining, at every one of those points, why it does not apply. It is
// reference counted for the same reason a mesh is -- setActiveCamera keeps one
// alive past the Lua value that made it.
class Camera3D
{
	public:
		typedef Camera3D Self;

	public:
		Camera3D();
		~Camera3D();

		void Retain() { ++fRefCount; }
		void Release() { if ( --fRefCount == 0 ) { delete this; } }

	public:
		void SetPosition( float x, float y, float z );
		void GetPosition( float& x, float& y, float& z ) const;

		// Aims the camera at a fixed point in world space, clearing any target
		// object -- the two ways of aiming are exclusive, and the last one set
		// is the one that holds.
		void LookAt( float x, float y, float z );

		// Where the camera is aimed, resolving a target object's current position
		// if one is set -- so this is the point the view matrix will actually look
		// at, not merely the last one passed to LookAt.
		void GetLookAt( float& x, float& y, float& z ) const;

		// Aims the camera at an object and keeps it aimed as the object moves.
		// Weak: the camera does not keep the object alive, and stops following
		// once the object is gone, since a camera that pinned its subject would
		// leak the whole scene it was watching.
		void SetTarget( DisplayObject* object );
		DisplayObject* GetTarget() const { return fTarget; }
		void ClearTarget() { fTarget = NULL; }

		float GetFieldOfView() const { return fFieldOfView; }
		void SetFieldOfView( float degrees ) { fFieldOfView = degrees; }

		float GetNear() const { return fNear; }
		void SetNear( float value ) { fNear = value; }

		float GetFar() const { return fFar; }
		void SetFar( float value ) { fFar = value; }

		// Zero means "follow the surface", which is what nearly every project
		// wants and what a camera starts out doing; a non-zero value pins the
		// ratio, for content that must letterbox rather than reframe.
		float GetAspectRatio() const { return fAspectRatio; }
		void SetAspectRatio( float value ) { fAspectRatio = value; }

	public:
		// Fills out[16] with the column-major view matrix, resolving a target
		// object's current position first if one is set.
		void GetViewMatrix( float* out ) const;

		// Fills out[16] with the projection matrix for a surface of the given
		// size in pixels, which is only consulted when the aspect ratio is
		// being taken from the surface.
		//
		// homogeneousDepth says whether clip space runs -1..1 rather than 0..1.
		// That is a property of the graphics API bgfx picked at run time, not
		// of the camera, so the renderer passes down what it read from the caps
		// rather than this guessing.
		void GetProjectionMatrix( float* out, U32 surfaceWidth, U32 surfaceHeight, bool homogeneousDepth ) const;

	public:
		// The world-space ray through a point on the screen, for picking.
		//
		// ndcX and ndcY are normalised device coordinates: -1 at the left and
		// bottom of the viewport, +1 at the right and top, 0 at the centre. The
		// caller converts from content coordinates, since only it knows the
		// content size and that Corona's y grows downwards.
		//
		// aspect is width over height, used only when the camera is taking its
		// aspect ratio from the surface.
		//
		// Built from the camera basis and the field of view rather than by
		// inverting the view-projection matrix. The inverse would have to agree
		// with the backend about whether clip space runs 0..1 or -1..1, which is
		// a property of the graphics API and not something a picking call should
		// have to be told.
		//
		// direction comes back normalised.
		void GetRay( float ndcX, float ndcY, float aspect, float* origin, float* direction ) const;

	private:
		float fX, fY, fZ;
		float fLookAtX, fLookAtY, fLookAtZ;
		float fUpX, fUpY, fUpZ;
		float fFieldOfView;
		float fNear;
		float fFar;
		float fAspectRatio;
		DisplayObject* fTarget;
		int fRefCount;
};

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------

#endif // _Rtt_Camera3D_H__
