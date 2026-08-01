//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Corona game engine.
// For overview and more information on licensing please refer to README.md
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////

#include "Core/Rtt_Build.h"

#include "Display/Rtt_Physics3D.h"

#include "Display/Rtt_Display.h"
#include "Display/Rtt_Mesh3D.h"
#include "Display/Rtt_Object3D.h"

#include "box3d/box3d.h"

#include <cmath>
#include <cstring>

// ----------------------------------------------------------------------------

namespace Rtt
{

// ----------------------------------------------------------------------------

// Box3D's ids are small structs; this module stores them as their bits so that
// Rtt_Physics3D.h can stay free of box3d.h, the same arrangement the renderer
// handles get. The casts are safe because both ids are 64 bits of plain data.
static_assert( sizeof( b3WorldId ) <= sizeof( U64 ), "b3WorldId no longer fits in a U64" );
static_assert( sizeof( b3BodyId ) <= sizeof( U64 ), "b3BodyId no longer fits in a U64" );

static U64
PackWorld( b3WorldId id )
{
	U64 bits = 0;
	memcpy( &bits, &id, sizeof( id ) );

	return bits;
}

static b3WorldId
UnpackWorld( U64 bits )
{
	b3WorldId id;
	memcpy( &id, &bits, sizeof( id ) );

	return id;
}

static U64
PackBody( b3BodyId id )
{
	U64 bits = 0;
	memcpy( &bits, &id, sizeof( id ) );

	return bits;
}

static b3BodyId
UnpackBody( U64 bits )
{
	b3BodyId id;
	memcpy( &id, &bits, sizeof( id ) );

	return id;
}

// ----------------------------------------------------------------------------

Physics3D::BodyDescription::BodyDescription()
:	fType( kDynamic ),
	fShape( kBox ),
	fDensity( 1.0f ),
	fFriction( 0.6f ),
	fRestitution( 0.0f ),
	fRadius( 0.0f ),
	fHalfHeight( 0.0f ),
	fIsBullet( false ),
	fIsSensor( false ),
	fFixedRotation( false )
{
	fHalfExtents[0] = fHalfExtents[1] = fHalfExtents[2] = 0.0f;
}

// ----------------------------------------------------------------------------

Physics3D::Physics3D( Display& display )
:	fDisplay( display ),
	fWorldId( 0 ),
	fTimeStep( 1.0f / 60.0f ),
	fSubStepCount( 4 ),
	fAccumulator( 0.0f ),
	fLastTimeMs( 0 ),
	fHasLastTime( false ),
	fIsStarted( false ),
	fIsRunning( false )
{
	// Down is -Y, matching the camera's +Y up, and 9.8 units a second squared --
	// which reads as metres because the 3D API has no pixel-to-metre scale to
	// bridge. That is the whole reason physics3d needs no setScale: unlike the 2D
	// world, where content is in pixels, a 3D scene is already in whatever unit
	// the content chose, and treating it as metres is the useful default.
	fGravity[0] = 0.0f;
	fGravity[1] = -9.8f;
	fGravity[2] = 0.0f;
}

Physics3D::~Physics3D()
{
	Stop();
}

// ----------------------------------------------------------------------------

void
Physics3D::Start()
{
	if ( !fIsStarted )
	{
		b3WorldDef def = b3DefaultWorldDef();

		def.gravity.x = fGravity[0];
		def.gravity.y = fGravity[1];
		def.gravity.z = fGravity[2];

		fWorldId = PackWorld( b3CreateWorld( &def ) );
		fIsStarted = true;

		// The clock is read afresh, so that a project which starts physics some
		// way into its run does not hand the solver every second since launch.
		fHasLastTime = false;
		fAccumulator = 0.0f;
	}

	fIsRunning = true;
}

void
Physics3D::Pause()
{
	fIsRunning = false;
}

void
Physics3D::Stop()
{
	if ( fIsStarted )
	{
		// Destroying the world takes its bodies with it, so the ids held here have
		// to go at the same time rather than being destroyed individually.
		b3DestroyWorld( UnpackWorld( fWorldId ) );

		fWorldId = 0;
		fIsStarted = false;
	}

	fBodies.clear();
	fIsRunning = false;
}

// ----------------------------------------------------------------------------

void
Physics3D::SetGravity( float x, float y, float z )
{
	fGravity[0] = x;
	fGravity[1] = y;
	fGravity[2] = z;

	if ( fIsStarted )
	{
		b3Vec3 gravity = { x, y, z };

		b3World_SetGravity( UnpackWorld( fWorldId ), gravity );
	}
}

void
Physics3D::GetGravity( float& x, float& y, float& z ) const
{
	x = fGravity[0];
	y = fGravity[1];
	z = fGravity[2];
}

void
Physics3D::SetTimeStep( float seconds )
{
	// A step of zero would never advance and a huge one makes the solver explode;
	// the bounds are what a fixed-step simulation stays stable across.
	if ( seconds < 1.0f / 1000.0f )
	{
		seconds = 1.0f / 1000.0f;
	}
	else if ( seconds > 1.0f / 10.0f )
	{
		seconds = 1.0f / 10.0f;
	}

	fTimeStep = seconds;
}

void
Physics3D::SetSubStepCount( int value )
{
	if ( value < 1 )
	{
		value = 1;
	}
	else if ( value > 16 )
	{
		value = 16;
	}

	fSubStepCount = value;
}

// ----------------------------------------------------------------------------

const Physics3D::Body*
Physics3D::Find( const Object3D& object ) const
{
	for ( size_t i = 0, iMax = fBodies.size(); i < iMax; ++i )
	{
		if ( fBodies[i].fObject == &object )
		{
			return &fBodies[i];
		}
	}

	return NULL;
}

Physics3D::Body*
Physics3D::Find( const Object3D& object )
{
	// The same search, non-const. Written out rather than const_cast'ing the other
	// one, which would be shorter and less obvious.
	for ( size_t i = 0, iMax = fBodies.size(); i < iMax; ++i )
	{
		if ( fBodies[i].fObject == &object )
		{
			return &fBodies[i];
		}
	}

	return NULL;
}

bool
Physics3D::HasBody( const Object3D& object ) const
{
	return Find( object ) != NULL;
}

// ----------------------------------------------------------------------------

bool
Physics3D::AddBody( Object3D& object, const BodyDescription& description )
{
	if ( Find( object ) != NULL )
	{
		return false;
	}

	// Adding a body is what starts the world if nothing else has: a project that
	// calls addBody without start() has said what it wants clearly enough.
	if ( !fIsStarted )
	{
		Start();
	}

	// The object's own extent, which every unspecified dimension is fitted to.
	float minimum[3] = { -0.5f, -0.5f, -0.5f };
	float maximum[3] = { 0.5f, 0.5f, 0.5f };

	const bool hasBounds = object.GetLocalBounds( minimum, maximum );

	if ( !hasBounds && description.fShape == kHull )
	{
		// A hull needs vertices; the others can fall back to a unit box.
		return false;
	}

	float scale[3];
	object.GetScale3D( scale[0], scale[1], scale[2] );

	// The shape is built in world units, so the object's scale is baked into it.
	// Box3D has no per-body scale, and leaving it out would give a model scaled to
	// a tenth of its size a collision shape ten times too large.
	float half[3];
	float centre[3];

	for ( int i = 0; i < 3; ++i )
	{
		half[i] = ( maximum[i] - minimum[i] ) * 0.5f * std::fabs( scale[i] );
		centre[i] = ( maximum[i] + minimum[i] ) * 0.5f * scale[i];

		if ( half[i] < 0.001f )
		{
			// A flat object -- a plane -- still needs a volume the solver can
			// resolve contacts against.
			half[i] = 0.001f;
		}
	}

	float position[3];
	object.GetPosition3D( position[0], position[1], position[2] );

	b3BodyDef bodyDef = b3DefaultBodyDef();

	switch ( description.fType )
	{
		case kStatic:    bodyDef.type = b3_staticBody;    break;
		case kKinematic: bodyDef.type = b3_kinematicBody; break;
		default:         bodyDef.type = b3_dynamicBody;   break;
	}

	bodyDef.position.x = position[0];
	bodyDef.position.y = position[1];
	bodyDef.position.z = position[2];

	// The object's current orientation, so a body added to something already
	// rotated starts rotated rather than snapping upright on the first step.
	if ( object.HasOrientation() )
	{
		float qx, qy, qz, qw;
		object.GetOrientation( qx, qy, qz, qw );

		bodyDef.rotation.v.x = qx;
		bodyDef.rotation.v.y = qy;
		bodyDef.rotation.v.z = qz;
		bodyDef.rotation.s = qw;
	}

	bodyDef.isBullet = description.fIsBullet;

	// Box3D locks each axis separately; "fixed rotation" is all three at once,
	// which is what keeps a crate from tipping or a character from toppling.
	bodyDef.motionLocks.angularX = description.fFixedRotation;
	bodyDef.motionLocks.angularY = description.fFixedRotation;
	bodyDef.motionLocks.angularZ = description.fFixedRotation;

	b3BodyId bodyId = b3CreateBody( UnpackWorld( fWorldId ), &bodyDef );

	b3ShapeDef shapeDef = b3DefaultShapeDef();

	shapeDef.density = description.fDensity;
	shapeDef.baseMaterial.friction = description.fFriction;
	shapeDef.baseMaterial.restitution = description.fRestitution;
	shapeDef.isSensor = description.fIsSensor;

	// Contact events are off by default in Box3D, and a physics API whose
	// collision listener never fired would be a puzzle rather than a feature.
	shapeDef.enableContactEvents = true;
	shapeDef.enableSensorEvents = description.fIsSensor;

	bool built = false;

	switch ( description.fShape )
	{
		case kSphere:
		{
			b3Sphere sphere;

			sphere.center.x = centre[0];
			sphere.center.y = centre[1];
			sphere.center.z = centre[2];

			// The largest half-extent, so the sphere contains the object rather
			// than cutting through it.
			sphere.radius = description.fRadius > 0.0f
				? description.fRadius
				: ( half[0] > half[1] ? ( half[0] > half[2] ? half[0] : half[2] ) : ( half[1] > half[2] ? half[1] : half[2] ) );

			built = b3Shape_IsValid( b3CreateSphereShape( bodyId, &shapeDef, &sphere ) );
			break;
		}

		case kCapsule:
		{
			b3Capsule capsule;

			// Along Y, which is up: a capsule stands, being what a character is
			// nearly always modelled with.
			const float radius = description.fRadius > 0.0f
				? description.fRadius
				: ( half[0] > half[2] ? half[0] : half[2] );

			float halfHeight = description.fHalfHeight > 0.0f ? description.fHalfHeight : half[1] - radius;

			// A capsule shorter than it is round is a sphere; letting the segment
			// go negative would turn it inside out.
			if ( halfHeight < 0.0f )
			{
				halfHeight = 0.0f;
			}

			capsule.center1.x = centre[0];
			capsule.center1.y = centre[1] - halfHeight;
			capsule.center1.z = centre[2];

			capsule.center2.x = centre[0];
			capsule.center2.y = centre[1] + halfHeight;
			capsule.center2.z = centre[2];

			capsule.radius = radius;

			built = b3Shape_IsValid( b3CreateCapsuleShape( bodyId, &shapeDef, &capsule ) );
			break;
		}

		case kHull:
		{
			// The mesh's own vertices, scaled, reduced to their convex hull. Box3D
			// caps a hull's vertex count, and b3CreateHull does the reduction, so
			// a mesh of any size can be handed over.
			std::vector< b3Vec3 > points;

			const Mesh3D* mesh = object.GetMesh();

			if ( mesh != NULL )
			{
				const std::vector< Vertex3D >& vertices = mesh->GetVertices();

				points.reserve( vertices.size() );

				for ( size_t i = 0, iMax = vertices.size(); i < iMax; ++i )
				{
					b3Vec3 point;

					point.x = vertices[i].x * scale[0];
					point.y = vertices[i].y * scale[1];
					point.z = vertices[i].z * scale[2];

					points.push_back( point );
				}
			}

			if ( points.empty() )
			{
				// A model has no mesh of its own, only parts. Its hull would have to
				// union them, which for a posed skinned model is not one convex
				// shape at all -- so the fitted box is used and said so.
				Rtt_LogException( "WARNING: physics3d.addBody(): a hull shape needs a single mesh; this object has none, so a fitted box is used instead\n" );

				b3BoxHull box = b3MakeOffsetBoxHull( half[0], half[1], half[2],
					b3Vec3{ centre[0], centre[1], centre[2] } );

				built = b3Shape_IsValid( b3CreateHullShape( bodyId, &shapeDef, &box.base ) );
				break;
			}

			b3HullData* hull = b3CreateHull( &points[0], (int) points.size(), 0 );

			if ( hull != NULL )
			{
				built = b3Shape_IsValid( b3CreateHullShape( bodyId, &shapeDef, hull ) );

				// Box3D copies the hull into the shape, so the working copy is done
				// with as soon as the shape exists.
				b3DestroyHull( hull );
			}

			break;
		}

		default:
		{
			b3BoxHull box = b3MakeOffsetBoxHull( half[0], half[1], half[2],
				b3Vec3{ centre[0], centre[1], centre[2] } );

			built = b3Shape_IsValid( b3CreateHullShape( bodyId, &shapeDef, &box.base ) );
			break;
		}
	}

	if ( !built )
	{
		// A body with no shape would fall forever without ever colliding, which is
		// worse than not having one.
		b3DestroyBody( bodyId );

		return false;
	}

	Body body;

	body.fObject = &object;
	body.fBodyId = PackBody( bodyId );

	fBodies.push_back( body );

	return true;
}

void
Physics3D::RemoveBody( Object3D& object )
{
	for ( size_t i = 0, iMax = fBodies.size(); i < iMax; ++i )
	{
		if ( fBodies[i].fObject != &object )
		{
			continue;
		}

		if ( fIsStarted )
		{
			b3DestroyBody( UnpackBody( fBodies[i].fBodyId ) );
		}

		fBodies.erase( fBodies.begin() + i );

		// The object goes back to being placed by whatever sets its transform,
		// which for one that had been under physics means its angles again.
		object.ClearOrientation();

		return;
	}
}

// ----------------------------------------------------------------------------

void
Physics3D::SyncToObject( const Body& body )
{
	const b3BodyId bodyId = UnpackBody( body.fBodyId );

	const b3Pos position = b3Body_GetPosition( bodyId );
	const b3Quat rotation = b3Body_GetRotation( bodyId );

	body.fObject->SetPosition3D( (float) position.x, (float) position.y, (float) position.z );

	// Orientation rather than angles: see Object3D::SetOrientation for why a
	// solver's rotation must not be routed through Euler.
	body.fObject->SetOrientation( rotation.v.x, rotation.v.y, rotation.v.z, rotation.s );
}

void
Physics3D::Advance( Display& display )
{
	if ( !fIsRunning || !fIsStarted )
	{
		// The clock keeps being read while paused, so that resuming does not hand
		// the solver the whole pause as one delta.
		fHasLastTime = false;

		return;
	}

	const U64 now = Rtt_AbsoluteToMilliseconds( display.GetElapsedTime() );

	if ( !fHasLastTime )
	{
		fLastTimeMs = now;
		fHasLastTime = true;
	}

	float delta = (float) ( now - fLastTimeMs ) / 1000.0f;

	fLastTimeMs = now;

	// A frame that took a quarter second -- a hitch, a breakpoint, a window drag --
	// is clamped rather than simulated, which is what stops the accumulator from
	// running up a debt it then works off in a burst of steps.
	if ( delta > 0.25f )
	{
		delta = 0.25f;
	}

	fAccumulator += delta;

	int steps = 0;

	while ( fAccumulator >= fTimeStep && steps < 8 )
	{
		b3World_Step( UnpackWorld( fWorldId ), fTimeStep, fSubStepCount );

		fAccumulator -= fTimeStep;
		++steps;
	}

	if ( steps == 0 )
	{
		// Not enough time has passed for a step, so nothing has moved.
		return;
	}

	for ( size_t i = 0, iMax = fBodies.size(); i < iMax; ++i )
	{
		// Static bodies never move, so writing their transform back every frame
		// would only mean invalidating a display object that has not changed.
		if ( b3Body_GetType( UnpackBody( fBodies[i].fBodyId ) ) == b3_staticBody )
		{
			continue;
		}

		SyncToObject( fBodies[i] );
	}
}

// ----------------------------------------------------------------------------

void
Physics3D::ApplyForce( Object3D& object, float x, float y, float z )
{
	Body* body = Find( object );

	if ( body == NULL )
	{
		return;
	}

	b3Vec3 force = { x, y, z };

	b3Body_ApplyForceToCenter( UnpackBody( body->fBodyId ), force, true );
}

void
Physics3D::ApplyImpulse( Object3D& object, float x, float y, float z )
{
	Body* body = Find( object );

	if ( body == NULL )
	{
		return;
	}

	b3Vec3 impulse = { x, y, z };

	b3Body_ApplyLinearImpulseToCenter( UnpackBody( body->fBodyId ), impulse, true );
}

void
Physics3D::ApplyTorque( Object3D& object, float x, float y, float z )
{
	Body* body = Find( object );

	if ( body == NULL )
	{
		return;
	}

	b3Vec3 torque = { x, y, z };

	b3Body_ApplyTorque( UnpackBody( body->fBodyId ), torque, true );
}

void
Physics3D::SetLinearVelocity( Object3D& object, float x, float y, float z )
{
	Body* body = Find( object );

	if ( body == NULL )
	{
		return;
	}

	b3Vec3 velocity = { x, y, z };

	b3Body_SetLinearVelocity( UnpackBody( body->fBodyId ), velocity );
}

bool
Physics3D::GetLinearVelocity( const Object3D& object, float& x, float& y, float& z ) const
{
	const Body* body = Find( object );

	if ( body == NULL )
	{
		return false;
	}

	const b3Vec3 velocity = b3Body_GetLinearVelocity( UnpackBody( body->fBodyId ) );

	x = velocity.x;
	y = velocity.y;
	z = velocity.z;

	return true;
}

void
Physics3D::SetAngularVelocity( Object3D& object, float x, float y, float z )
{
	Body* body = Find( object );

	if ( body == NULL )
	{
		return;
	}

	b3Vec3 velocity = { x, y, z };

	b3Body_SetAngularVelocity( UnpackBody( body->fBodyId ), velocity );
}

bool
Physics3D::GetAngularVelocity( const Object3D& object, float& x, float& y, float& z ) const
{
	const Body* body = Find( object );

	if ( body == NULL )
	{
		return false;
	}

	const b3Vec3 velocity = b3Body_GetAngularVelocity( UnpackBody( body->fBodyId ) );

	x = velocity.x;
	y = velocity.y;
	z = velocity.z;

	return true;
}

void
Physics3D::SetTransform( Object3D& object, float x, float y, float z )
{
	Body* body = Find( object );

	if ( body == NULL )
	{
		// No body: just move the object, which is what setting x/y/z does anyway.
		object.SetPosition3D( x, y, z );

		return;
	}

	const b3BodyId bodyId = UnpackBody( body->fBodyId );

	b3Pos position;

	position.x = x;
	position.y = y;
	position.z = z;

	b3Body_SetTransform( bodyId, position, b3Body_GetRotation( bodyId ) );

	// Woken deliberately: a sleeping body moved to a new place would stay asleep
	// there, ignoring whatever it now overlaps.
	b3Body_SetAwake( bodyId, true );

	object.SetPosition3D( x, y, z );
}

bool
Physics3D::IsAwake( const Object3D& object ) const
{
	const Body* body = Find( object );

	return ( body != NULL ) && b3Body_IsAwake( UnpackBody( body->fBodyId ) );
}

void
Physics3D::Wake( Object3D& object )
{
	Body* body = Find( object );

	if ( body != NULL )
	{
		b3Body_SetAwake( UnpackBody( body->fBodyId ), true );
	}
}

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------
