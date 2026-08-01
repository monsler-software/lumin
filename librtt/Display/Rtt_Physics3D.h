//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Corona game engine.
// For overview and more information on licensing please refer to README.md
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////

#ifndef _Rtt_Physics3D_H__
#define _Rtt_Physics3D_H__

#include "Core/Rtt_Types.h"

#include <vector>

// ----------------------------------------------------------------------------

namespace Rtt
{

class Display;
class Object3D;

// ----------------------------------------------------------------------------

// The 3D physics world, and the bodies standing for 3D display objects in it.
//
// Backed by Box3D, whose C API is kept entirely inside the implementation: the
// ids it deals in are stored here as opaque integers so that no Display header
// has to include box3d.h, exactly as the bgfx handles are kept out of Mesh3D.
//
// One per Display and created on first use, as Scene3D is: a project with no
// physics in it should not pay for a world, and the simulator opens a runtime per
// project so a process-wide world would leak one project's bodies into the next.
class Physics3D
{
	public:
		typedef Physics3D Self;

		enum BodyType
		{
			kStatic,
			kKinematic,
			kDynamic
		};

		// How a body's collision volume is derived. Every one but kHull is an
		// approximation fitted to the object's mesh bounds, which is what makes
		// physics3d.addBody(object) work with no further description.
		enum ShapeKind
		{
			kBox,
			kSphere,
			kCapsule,

			// The convex hull of the mesh's own vertices: accurate for a convex
			// shape, and the convex approximation of a concave one. Concave
			// collision needs a decomposition Box3D does not do for us.
			kHull
		};

		struct BodyDescription
		{
			BodyType fType;
			ShapeKind fShape;

			float fDensity;
			float fFriction;
			float fRestitution;

			// Half-extents for a box, radius for a sphere or capsule, and the
			// capsule's half-height. Any left at zero is fitted to the mesh.
			float fHalfExtents[3];
			float fRadius;
			float fHalfHeight;

			bool fIsBullet;
			bool fIsSensor;
			bool fFixedRotation;

			BodyDescription();
		};

	public:
		Physics3D( Display& display );
		~Physics3D();

	public:
		// Creating the world is deferred until this is called, which is what
		// physics3d.start() does. Calling it again while running is harmless.
		void Start();

		// Leaves the world and its bodies intact, so starting again resumes rather
		// than restarting -- the same distinction physics.pause() draws in 2D.
		void Pause();
		void Stop();

		bool IsRunning() const { return fIsRunning; }
		bool IsStarted() const { return fIsStarted; }

	public:
		void SetGravity( float x, float y, float z );
		void GetGravity( float& x, float& y, float& z ) const;

		// Fixed timestep, in seconds. A solver fed the frame's real delta behaves
		// differently on a fast machine than a slow one; a fixed step with an
		// accumulator behaves the same on both.
		float GetTimeStep() const { return fTimeStep; }
		void SetTimeStep( float seconds );

		int GetSubStepCount() const { return fSubStepCount; }
		void SetSubStepCount( int value );

	public:
		// Gives an object a body. Returns false if it already has one, or if the
		// shape could not be built -- an object with no mesh, or a mesh with no
		// vertices to hull.
		bool AddBody( Object3D& object, const BodyDescription& description );

		// Silently does nothing for an object that has no body, so that removing
		// twice, or removing from an object that never had one, is not an error.
		void RemoveBody( Object3D& object );

		bool HasBody( const Object3D& object ) const;

	public:
		// Advances the world by however much time has passed and writes every
		// dynamic body's transform onto its object. Called once per frame by the
		// Display.
		void Advance( Display& display );

	public:
		// Applied at the body's centre of mass. Ignored for an object with no
		// body, which is the same rule the setters below follow.
		void ApplyForce( Object3D& object, float x, float y, float z );
		void ApplyImpulse( Object3D& object, float x, float y, float z );
		void ApplyTorque( Object3D& object, float x, float y, float z );

		void SetLinearVelocity( Object3D& object, float x, float y, float z );
		bool GetLinearVelocity( const Object3D& object, float& x, float& y, float& z ) const;

		void SetAngularVelocity( Object3D& object, float x, float y, float z );
		bool GetAngularVelocity( const Object3D& object, float& x, float& y, float& z ) const;

		// Moves a body, which a solver otherwise owns. Needed for teleporting and
		// for placing a kinematic body, and wakes the body so it is not left
		// asleep at its old place.
		void SetTransform( Object3D& object, float x, float y, float z );

		bool IsAwake( const Object3D& object ) const;
		void Wake( Object3D& object );

	private:
		// One object's body. Kept in a flat vector and searched linearly: a scene
		// has tens of physical objects, and every operation here already costs
		// more than the search.
		struct Body
		{
			Object3D* fObject;

			// b3BodyId, stored as its bits so this header stays free of box3d.
			U64 fBodyId;
		};

		const Body* Find( const Object3D& object ) const;
		Body* Find( const Object3D& object );

		// Copies a body's solved transform onto its object.
		void SyncToObject( const Body& body );

		Display& fDisplay;

		// b3WorldId as bits, zero before Start.
		U64 fWorldId;

		std::vector< Body > fBodies;

		float fGravity[3];
		float fTimeStep;
		int fSubStepCount;

		// Time owed to the solver: the frame's delta accumulates here and whole
		// steps are taken out of it, so the simulation advances at a fixed rate
		// whatever the frame rate is.
		float fAccumulator;

		// The runtime clock at the last Advance, in milliseconds.
		U64 fLastTimeMs;
		bool fHasLastTime;

		bool fIsStarted;
		bool fIsRunning;
};

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------

#endif // _Rtt_Physics3D_H__
