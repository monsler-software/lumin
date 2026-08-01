//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Corona game engine.
// For overview and more information on licensing please refer to README.md
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////

#ifndef _Rtt_Object3D_H__
#define _Rtt_Object3D_H__

#include "Display/Rtt_DisplayObject.h"

#include "Renderer/Rtt_Draw3D.h"

#include "Rtt_LuaProxyVTable.h"

// ----------------------------------------------------------------------------

namespace Rtt
{

class Material3D;
class Mesh3D;
class Object3D;
class Scene3D;
class ShaderEffect3D;

// ----------------------------------------------------------------------------

// Where a ray met a 3D object, in world space.
struct Raycast3DHit
{
	Object3D* fObject;

	float fPoint[3];

	// The surface normal at the hit, pointing out of the front of the triangle
	// that was struck. Not flipped towards the ray: a ray that hits the inside of
	// a shape gets a normal pointing away from it, which is what tells the caller
	// that is what happened.
	float fNormal[3];

	// Along the ray from its origin, in world units, so the nearest of several
	// hits is the smallest.
	float fDistance;

	// Which part of a model was hit, empty for anything that is not a model. The
	// same name model:getMesh() takes.
	const char* fMeshName;
};

// ----------------------------------------------------------------------------

// A 3D object in the 2D display list.
//
// It is a DisplayObject so that everything the display list already does keeps
// working on it unchanged: adding it to a group, `isVisible`, `alpha`,
// `removeSelf`, `display.remove`, and being torn down with the scene when the
// runtime ends. What it overrides is only what 3D means differently -- its
// transform is a 4x4 it maintains itself rather than the 2D affine one, and it
// draws through the 3D path rather than by inserting RenderData.
//
// The 2D transform is left at identity and the inherited x/y are not used for
// placement; `x`, `y` and `z` on the Lua object are the 3D position, and are
// read and written through the 3D fields below. Keeping the two apart avoids a
// half-working state where moving an object in Lua translates it in world space
// but its stage bounds still describe a 2D rectangle somewhere else.
class Object3D : public DisplayObject
{
	public:
		typedef DisplayObject Super;
		typedef Object3D Self;

	public:
		Object3D( Scene3D& scene, Mesh3D* mesh );
		virtual ~Object3D();

	public:
		// MDrawable
		virtual void Prepare( const Display& display );
		virtual void Draw( Renderer& renderer ) const;
		virtual void GetSelfBounds( Rect& rect ) const;

	public:
		virtual bool HitTest( Real contentX, Real contentY );
		virtual bool CanHitTest() const;
		virtual bool CanCull() const;

	public:
		virtual const LuaProxyVTable& ProxyVTable() const;
		virtual Object3D* AsObject3D();

	public:
		// Where the object lands on screen, in content units, according to the
		// camera that is drawing it: its bounding box's eight corners projected
		// and enclosed.
		//
		// GetSelfBounds cannot answer this -- what a 3D object covers depends on
		// the camera and changes whenever it moves, which is why the bounds the
		// display list keeps are empty. Anything that needs a rectangle for a 3D
		// object anyway, and display.capture and display.save both do, asks here
		// and pays for the projection at the moment it asks.
		//
		// False when the object has no geometry, when there is no camera to
		// project with, or when the box crosses the plane behind the camera --
		// which has no finite rectangle -- and the caller then has no rectangle
		// to work with rather than a wrong one.
		bool GetScreenBounds( const Display& display, Rect& bounds ) const;

	public:
		// Intersects a world-space ray with this object's triangles, filling hit
		// with the nearest one and returning true if there was one.
		//
		// direction need not be normalised, but fDistance comes back in units of
		// its length, so a normalised one is what makes the distance a distance.
		//
		// Tests every triangle, after a bounding-sphere rejection. Good enough
		// because a raycast happens when the user taps, not per frame, and an
		// acceleration structure would have to be rebuilt whenever anything moved.
		virtual bool Raycast( const float* origin, const float* direction, Raycast3DHit& hit ) const;

		// The object-space axis-aligned box containing its geometry, before its own
		// transform. False when there is no geometry to bound.
		//
		// Physics uses it to fit a collision shape to an object nobody described:
		// a bounding radius alone cannot tell a plank from a cube.
		virtual bool GetLocalBounds( float* minimum, float* maximum ) const;

	protected:
		// The triangle test itself, shared with ModelObject3D, which runs it once
		// per part with that part's own transform and bone palette.
		//
		// Fills only the geometric fields of hit -- point, normal and distance --
		// leaving the object and mesh name to the caller, which is the one that
		// knows them. Pass NULL for bonePalette to test the mesh as stored.
		static bool RaycastMesh(
			  const Mesh3D& mesh
			, const float* worldMatrix
			, const float* origin
			, const float* direction
			, const float* bonePalette
			, U32 boneCount
			, Raycast3DHit& hit );

		// Inverts a transform of rotation, scale and translation. False if it is
		// singular, which a zero scale makes it.
		static bool InvertAffine( const float* m, float* out );

		// Grows minimum/maximum to contain one mesh, optionally placed by a node
		// transform first. The caller seeds them; ModelObject3D calls this once per
		// part to union the lot.
		static bool GetMeshBounds( const Mesh3D& mesh, const float* nodeMatrix, float* minimum, float* maximum );

	public:
		void GetPosition3D( float& x, float& y, float& z ) const;
		void SetPosition3D( float x, float y, float z );
		void Translate3D( float dx, float dy, float dz );

		// Euler angles in degrees, applied in X then Y then Z order -- the same
		// order and units Corona's 2D `rotation` uses, so that a project moving
		// into 3D does not have to learn radians and quaternions first.
		void GetRotation3D( float& x, float& y, float& z ) const;
		void SetRotation3D( float x, float y, float z );
		void Rotate3D( float dx, float dy, float dz );

		void GetScale3D( float& x, float& y, float& z ) const;
		void SetScale3D( float x, float y, float z );

		// Orientation as a quaternion, which is what a physics solver produces and
		// what cannot be handed back through the Euler angles above without loss:
		// converting to Euler and back is stable almost everywhere and degenerate
		// at the poles, where a tumbling body would jitter or flip.
		//
		// Setting one makes it the object's orientation and the Euler angles stop
		// being consulted; setting any of rotationX/Y/Z afterwards goes back to
		// them. So a body under physics is driven by quaternion, and one animated
		// from Lua by angles, without either having to know about the other.
		void SetOrientation( float x, float y, float z, float w );
		void GetOrientation( float& x, float& y, float& z, float& w ) const;

		bool HasOrientation() const { return fHasOrientation; }
		void ClearOrientation() { fHasOrientation = false; InvalidateDisplay(); }

		Material3D* GetMaterial() const { return fMaterial; }
		void SetMaterial( Material3D* material );

		// The custom shading program this object draws with, or NULL for the
		// standard one. Passing NULL puts it back.
		ShaderEffect3D* GetEffect() const { return fEffect; }
		void SetEffect( ShaderEffect3D* effect );

		Mesh3D* GetMesh() const { return fMesh; }

	protected:
		// Composes the current position, rotation and scale into out[16],
		// column major, including any parent group's 3D contribution.
		void GetWorldMatrix( float* out ) const;

		Scene3D& fScene;
		Mesh3D* fMesh;
		Material3D* fMaterial;
		ShaderEffect3D* fEffect;

		float fPosition[3];
		float fRotation[3];
		float fScale[3];

		// x, y, z, w. Consulted instead of fRotation while fHasOrientation.
		float fOrientation[4];
		bool fHasOrientation;
};

// ----------------------------------------------------------------------------

// A light source placed in the scene.
//
// An Object3D with no mesh rather than a type of its own, because the spec puts
// lights in groups alongside geometry, and everything that implies -- being
// positioned, being hidden with `isVisible`, being removed with `removeSelf`,
// following a parent group -- is behaviour Object3D already has. Draw() is
// where the two part company: a light contributes to other objects' shading
// instead of producing a draw call.
class Light3D : public Object3D
{
	public:
		typedef Object3D Super;
		typedef Light3D Self;

	public:
		Light3D( Scene3D& scene, Draw3DLight::Kind kind );
		virtual ~Light3D();

	public:
		virtual void Draw( Renderer& renderer ) const;
		virtual const LuaProxyVTable& ProxyVTable() const;

	public:
		Draw3DLight::Kind GetKind() const { return fKind; }

		void SetColor( float r, float g, float b );
		void GetColor( float& r, float& g, float& b ) const;

		float GetIntensity() const { return fIntensity; }
		void SetIntensity( float value ) { fIntensity = value; }

		float GetRange() const { return fRange; }
		void SetRange( float value ) { fRange = value; }

		// Degrees, as everything else angular in this API is.
		float GetInnerAngle() const { return fInnerAngle; }
		void SetInnerAngle( float degrees );

		float GetOuterAngle() const { return fOuterAngle; }
		void SetOuterAngle( float degrees );

		// Fills a light's contribution for this frame. Returns false if it
		// should not contribute at all -- a hidden light, which by the same
		// rule as a hidden mesh must leave no trace in the rendered frame.
		bool GetLightData( Draw3DLight& out ) const;

	public:
		// Whether this light casts shadows. Only one light in a scene does at a
		// time -- a shadow map per light means a depth pass per light, and the
		// second one costs as much as the first -- so the scene picks the first
		// that asks; see Scene3D::GetShadowLight.
		bool CastsShadows() const { return fCastsShadows; }
		void SetCastsShadows( bool value ) { fCastsShadows = value; }

		// The side of the square of world the shadow map covers, centred on what
		// the camera is looking at.
		//
		// A shadow map has a fixed resolution, so this trades reach against
		// sharpness: 20 units across a 2048 map is about a centimetre per texel,
		// which is crisp for a scene of a few rooms and useless for a landscape.
		float GetShadowExtent() const { return fShadowExtent; }
		void SetShadowExtent( float value ) { fShadowExtent = ( value > 0.01f ) ? value : 0.01f; }

		// How far along the surface normal a depth comparison is nudged, in world
		// units, to keep a surface from shadowing itself where the map's resolution
		// cannot resolve its own slope.
		float GetShadowBias() const { return fShadowBias; }
		void SetShadowBias( float value ) { fShadowBias = value; }

		// 0 leaves a shadowed surface fully lit and 1 takes the light away
		// entirely; anything between is a partial shadow, which is what content
		// usually wants since ambient still reaches the surface.
		float GetShadowStrength() const { return fShadowStrength; }
		void SetShadowStrength( float value );

		// Fills out[16] with the light's view-projection: an orthographic box
		// aimed down the light's direction and centred on `centre`, which the
		// caller passes as whatever the shadows should be sharp around -- normally
		// what the camera is looking at.
		//
		// Only meaningful for a directional light. A point or spot light needs a
		// perspective projection, and a point light needs six of them.
		void GetShadowMatrix( const float* centre, bool homogeneousDepth, float* out ) const;

	private:
		Draw3DLight::Kind fKind;
		float fColor[3];
		float fIntensity;
		float fRange;
		float fInnerAngle;
		float fOuterAngle;
		bool fCastsShadows;
		float fShadowExtent;
		float fShadowBias;
		float fShadowStrength;
};

// ----------------------------------------------------------------------------

// What `cube.x`, `cube.rotationY` and `cube:setMaterial()` resolve to.
//
// Note that x and y are intercepted rather than inherited: on a 2D object they
// are content-space coordinates in points, and on a 3D object they are two
// thirds of a world-space position. Anything not listed here falls through to
// the display object vtable, which is where isVisible, alpha, removeSelf and
// the rest keep their usual meanings.
class LuaObject3DProxyVTable : public LuaDisplayObjectProxyVTable
{
	public:
		typedef LuaObject3DProxyVTable Self;
		typedef LuaDisplayObjectProxyVTable Super;

	public:
		static const Self& Constant();

	protected:
		LuaObject3DProxyVTable() {}

	public:
		virtual int ValueForKey( lua_State *L, const MLuaProxyable& object, const char key[], bool overrideRestriction = false ) const;
		virtual bool SetValueForKey( lua_State *L, MLuaProxyable& object, const char key[], int valueIndex ) const;
		virtual const LuaProxyVTable& Parent() const;
};

// ----------------------------------------------------------------------------

class LuaLight3DProxyVTable : public LuaObject3DProxyVTable
{
	public:
		typedef LuaLight3DProxyVTable Self;
		typedef LuaObject3DProxyVTable Super;

	public:
		static const Self& Constant();

	protected:
		LuaLight3DProxyVTable() {}

	public:
		virtual int ValueForKey( lua_State *L, const MLuaProxyable& object, const char key[], bool overrideRestriction = false ) const;
		virtual bool SetValueForKey( lua_State *L, MLuaProxyable& object, const char key[], int valueIndex ) const;
		virtual const LuaProxyVTable& Parent() const;
};

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------

#endif // _Rtt_Object3D_H__
