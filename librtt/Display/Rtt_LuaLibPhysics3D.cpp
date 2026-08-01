//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Corona game engine.
// For overview and more information on licensing please refer to README.md
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////

#include "Core/Rtt_Build.h"

#include "Display/Rtt_LuaLibPhysics3D.h"

#include "Display/Rtt_Display.h"
#include "Display/Rtt_Object3D.h"
#include "Display/Rtt_Physics3D.h"
#include "Rtt_LuaContext.h"
#include "Rtt_LuaProxy.h"

#include "CoronaLibrary.h"
#include "CoronaLua.h"

#include <cstring>

// ----------------------------------------------------------------------------

namespace Rtt
{

// ----------------------------------------------------------------------------

class Physics3DLibrary
{
	public:
		typedef Physics3DLibrary Self;

	public:
		static const char kName[];

	public:
		Physics3DLibrary( Display& display ) : fDisplay( display ) {}

		Display& GetDisplay() { return fDisplay; }

	public:
		static int Open( lua_State *L );

	private:
		static int Finalizer( lua_State *L );
		static Self* ToLibrary( lua_State *L );

	private:
		static int start( lua_State *L );
		static int pause( lua_State *L );
		static int stop( lua_State *L );

		static int setGravity( lua_State *L );
		static int getGravity( lua_State *L );

		static int setTimeStep( lua_State *L );

		static int addBody( lua_State *L );
		static int removeBody( lua_State *L );

		static int applyForce( lua_State *L );
		static int applyImpulse( lua_State *L );
		static int applyTorque( lua_State *L );

		static int setLinearVelocity( lua_State *L );
		static int getLinearVelocity( lua_State *L );
		static int setAngularVelocity( lua_State *L );
		static int getAngularVelocity( lua_State *L );

		static int setPosition( lua_State *L );

		static int isAwake( lua_State *L );
		static int wake( lua_State *L );

		Display& fDisplay;
};

const char Physics3DLibrary::kName[] = "physics3d";

// ----------------------------------------------------------------------------

Physics3DLibrary*
Physics3DLibrary::ToLibrary( lua_State *L )
{
	return (Self *)lua_touserdata( L, lua_upvalueindex( 1 ) );
}

int
Physics3DLibrary::Finalizer( lua_State *L )
{
	Self *library = (Self *)CoronaLuaToUserdata( L, 1 );

	delete library;

	return 0;
}

// ----------------------------------------------------------------------------

// The 3D object at the given index, or NULL with a warning naming the function.
//
// Every call here takes one, and a call given a 2D object -- or a group, or a
// nil -- is a mistake worth saying out loud rather than ignoring: the alternative
// is a body that never appears and no clue why.
static Object3D*
ToObject3D( lua_State *L, int index, const char* functionName )
{
	DisplayObject* object = (DisplayObject *)LuaProxy::GetProxyableObject( L, index );

	Object3D* result = ( object != NULL ) ? object->AsObject3D() : NULL;

	if ( result == NULL )
	{
		CoronaLuaWarning( L, "physics3d.%s() expects a 3D object from render.*", functionName );
	}

	return result;
}

// Reads three numbers, either as three arguments or as one table of three, so
// that both physics3d.setGravity(0, -9.8, 0) and setGravity({0, -9.8, 0}) work.
static void
ReadVector( lua_State *L, int index, float* out )
{
	if ( lua_istable( L, index ) )
	{
		for ( int i = 0; i < 3; ++i )
		{
			lua_rawgeti( L, index, i + 1 );
			out[i] = (float) luaL_optnumber( L, -1, 0.0 );
			lua_pop( L, 1 );
		}

		return;
	}

	for ( int i = 0; i < 3; ++i )
	{
		out[i] = (float) luaL_optnumber( L, index + i, 0.0 );
	}
}

// ----------------------------------------------------------------------------

int
Physics3DLibrary::start( lua_State *L )
{
	ToLibrary( L )->GetDisplay().GetPhysics3D().Start();

	return 0;
}

int
Physics3DLibrary::pause( lua_State *L )
{
	Display& display = ToLibrary( L )->GetDisplay();

	// Asked rather than created: pausing physics that was never started should not
	// bring a world into being in order to pause it.
	if ( display.HasPhysics3D() )
	{
		display.GetPhysics3D().Pause();
	}

	return 0;
}

int
Physics3DLibrary::stop( lua_State *L )
{
	Display& display = ToLibrary( L )->GetDisplay();

	if ( display.HasPhysics3D() )
	{
		display.GetPhysics3D().Stop();
	}

	return 0;
}

int
Physics3DLibrary::setGravity( lua_State *L )
{
	float gravity[3] = { 0.0f, -9.8f, 0.0f };

	ReadVector( L, 1, gravity );

	ToLibrary( L )->GetDisplay().GetPhysics3D().SetGravity( gravity[0], gravity[1], gravity[2] );

	return 0;
}

int
Physics3DLibrary::getGravity( lua_State *L )
{
	float x, y, z;

	ToLibrary( L )->GetDisplay().GetPhysics3D().GetGravity( x, y, z );

	lua_pushnumber( L, x );
	lua_pushnumber( L, y );
	lua_pushnumber( L, z );

	return 3;
}

int
Physics3DLibrary::setTimeStep( lua_State *L )
{
	Physics3D& physics = ToLibrary( L )->GetDisplay().GetPhysics3D();

	// Zero means "back to the default", matching what physics.setTimeStep does in
	// 2D, where zero restores frame-rate-driven stepping.
	const float step = (float) luaL_optnumber( L, 1, 0.0 );

	physics.SetTimeStep( step > 0.0f ? step : 1.0f / 60.0f );

	if ( !lua_isnoneornil( L, 2 ) )
	{
		physics.SetSubStepCount( (int) lua_tointeger( L, 2 ) );
	}

	return 0;
}

// ----------------------------------------------------------------------------

// physics3d.addBody( object [, bodyType] [, options] )
//
// bodyType is "dynamic", "static" or "kinematic", defaulting to dynamic, and may
// be left out entirely with the options table taking its place.
//
// options: shape ("box", "sphere", "capsule", "hull"), density, friction,
// bounce, radius, halfHeight, isBullet, isSensor, isFixedRotation.
//
// Every dimension left out is fitted to the object's own mesh, so addBody(cube)
// alone gives a body the size of the cube.
int
Physics3DLibrary::addBody( lua_State *L )
{
	Object3D* object = ToObject3D( L, 1, "addBody" );

	if ( object == NULL )
	{
		lua_pushboolean( L, 0 );

		return 1;
	}

	Physics3D::BodyDescription description;

	int optionsIndex = 2;

	if ( lua_isstring( L, 2 ) )
	{
		const char* type = lua_tostring( L, 2 );

		if ( strcmp( type, "static" ) == 0 )
		{
			description.fType = Physics3D::kStatic;
		}
		else if ( strcmp( type, "kinematic" ) == 0 )
		{
			description.fType = Physics3D::kKinematic;
		}
		else if ( strcmp( type, "dynamic" ) != 0 )
		{
			CoronaLuaWarning( L, "physics3d.addBody(): body type must be \"dynamic\", \"static\" or \"kinematic\", not \"%s\"; using \"dynamic\"", type );
		}

		optionsIndex = 3;
	}

	if ( lua_istable( L, optionsIndex ) )
	{
		lua_getfield( L, optionsIndex, "shape" );

		if ( lua_isstring( L, -1 ) )
		{
			const char* shape = lua_tostring( L, -1 );

			if ( strcmp( shape, "sphere" ) == 0 )
			{
				description.fShape = Physics3D::kSphere;
			}
			else if ( strcmp( shape, "capsule" ) == 0 )
			{
				description.fShape = Physics3D::kCapsule;
			}
			else if ( strcmp( shape, "hull" ) == 0 )
			{
				description.fShape = Physics3D::kHull;
			}
			else if ( strcmp( shape, "box" ) != 0 )
			{
				CoronaLuaWarning( L, "physics3d.addBody(): 'shape' must be \"box\", \"sphere\", \"capsule\" or \"hull\", not \"%s\"; using \"box\"", shape );
			}
		}

		lua_pop( L, 1 );

		lua_getfield( L, optionsIndex, "density" );
		if ( !lua_isnil( L, -1 ) ) { description.fDensity = (float) lua_tonumber( L, -1 ); }
		lua_pop( L, 1 );

		lua_getfield( L, optionsIndex, "friction" );
		if ( !lua_isnil( L, -1 ) ) { description.fFriction = (float) lua_tonumber( L, -1 ); }
		lua_pop( L, 1 );

		// "bounce" rather than "restitution", which is the word the 2D library
		// uses and the one content authors actually say.
		lua_getfield( L, optionsIndex, "bounce" );
		if ( !lua_isnil( L, -1 ) ) { description.fRestitution = (float) lua_tonumber( L, -1 ); }
		lua_pop( L, 1 );

		lua_getfield( L, optionsIndex, "radius" );
		if ( !lua_isnil( L, -1 ) ) { description.fRadius = (float) lua_tonumber( L, -1 ); }
		lua_pop( L, 1 );

		lua_getfield( L, optionsIndex, "halfHeight" );
		if ( !lua_isnil( L, -1 ) ) { description.fHalfHeight = (float) lua_tonumber( L, -1 ); }
		lua_pop( L, 1 );

		lua_getfield( L, optionsIndex, "isBullet" );
		description.fIsBullet = lua_toboolean( L, -1 ) != 0;
		lua_pop( L, 1 );

		lua_getfield( L, optionsIndex, "isSensor" );
		description.fIsSensor = lua_toboolean( L, -1 ) != 0;
		lua_pop( L, 1 );

		lua_getfield( L, optionsIndex, "isFixedRotation" );
		description.fFixedRotation = lua_toboolean( L, -1 ) != 0;
		lua_pop( L, 1 );
	}

	const bool added = ToLibrary( L )->GetDisplay().GetPhysics3D().AddBody( *object, description );

	if ( !added )
	{
		CoronaLuaWarning( L, "physics3d.addBody(): the object could not be given a body; it may already have one, or have no geometry to fit a shape to" );
	}

	lua_pushboolean( L, added ? 1 : 0 );

	return 1;
}

int
Physics3DLibrary::removeBody( lua_State *L )
{
	Object3D* object = ToObject3D( L, 1, "removeBody" );

	Display& display = ToLibrary( L )->GetDisplay();

	if ( object != NULL && display.HasPhysics3D() )
	{
		display.GetPhysics3D().RemoveBody( *object );
	}

	return 0;
}

// ----------------------------------------------------------------------------

int
Physics3DLibrary::applyForce( lua_State *L )
{
	Object3D* object = ToObject3D( L, 1, "applyForce" );

	if ( object == NULL )
	{
		return 0;
	}

	float v[3] = { 0.0f, 0.0f, 0.0f };
	ReadVector( L, 2, v );

	ToLibrary( L )->GetDisplay().GetPhysics3D().ApplyForce( *object, v[0], v[1], v[2] );

	return 0;
}

int
Physics3DLibrary::applyImpulse( lua_State *L )
{
	Object3D* object = ToObject3D( L, 1, "applyImpulse" );

	if ( object == NULL )
	{
		return 0;
	}

	float v[3] = { 0.0f, 0.0f, 0.0f };
	ReadVector( L, 2, v );

	ToLibrary( L )->GetDisplay().GetPhysics3D().ApplyImpulse( *object, v[0], v[1], v[2] );

	return 0;
}

int
Physics3DLibrary::applyTorque( lua_State *L )
{
	Object3D* object = ToObject3D( L, 1, "applyTorque" );

	if ( object == NULL )
	{
		return 0;
	}

	float v[3] = { 0.0f, 0.0f, 0.0f };
	ReadVector( L, 2, v );

	ToLibrary( L )->GetDisplay().GetPhysics3D().ApplyTorque( *object, v[0], v[1], v[2] );

	return 0;
}

int
Physics3DLibrary::setLinearVelocity( lua_State *L )
{
	Object3D* object = ToObject3D( L, 1, "setLinearVelocity" );

	if ( object == NULL )
	{
		return 0;
	}

	float v[3] = { 0.0f, 0.0f, 0.0f };
	ReadVector( L, 2, v );

	ToLibrary( L )->GetDisplay().GetPhysics3D().SetLinearVelocity( *object, v[0], v[1], v[2] );

	return 0;
}

int
Physics3DLibrary::getLinearVelocity( lua_State *L )
{
	Object3D* object = ToObject3D( L, 1, "getLinearVelocity" );

	Display& display = ToLibrary( L )->GetDisplay();

	float x = 0.0f, y = 0.0f, z = 0.0f;

	if ( object == NULL || !display.HasPhysics3D()
		|| !display.GetPhysics3D().GetLinearVelocity( *object, x, y, z ) )
	{
		return 0;
	}

	lua_pushnumber( L, x );
	lua_pushnumber( L, y );
	lua_pushnumber( L, z );

	return 3;
}

int
Physics3DLibrary::setAngularVelocity( lua_State *L )
{
	Object3D* object = ToObject3D( L, 1, "setAngularVelocity" );

	if ( object == NULL )
	{
		return 0;
	}

	float v[3] = { 0.0f, 0.0f, 0.0f };
	ReadVector( L, 2, v );

	ToLibrary( L )->GetDisplay().GetPhysics3D().SetAngularVelocity( *object, v[0], v[1], v[2] );

	return 0;
}

int
Physics3DLibrary::getAngularVelocity( lua_State *L )
{
	Object3D* object = ToObject3D( L, 1, "getAngularVelocity" );

	Display& display = ToLibrary( L )->GetDisplay();

	float x = 0.0f, y = 0.0f, z = 0.0f;

	if ( object == NULL || !display.HasPhysics3D()
		|| !display.GetPhysics3D().GetAngularVelocity( *object, x, y, z ) )
	{
		return 0;
	}

	lua_pushnumber( L, x );
	lua_pushnumber( L, y );
	lua_pushnumber( L, z );

	return 3;
}

// Moving a body by assigning to object.x would be undone by the next step, since
// the solver owns the transform. This is how a project teleports one.
int
Physics3DLibrary::setPosition( lua_State *L )
{
	Object3D* object = ToObject3D( L, 1, "setPosition" );

	if ( object == NULL )
	{
		return 0;
	}

	float v[3] = { 0.0f, 0.0f, 0.0f };
	ReadVector( L, 2, v );

	ToLibrary( L )->GetDisplay().GetPhysics3D().SetTransform( *object, v[0], v[1], v[2] );

	return 0;
}

int
Physics3DLibrary::isAwake( lua_State *L )
{
	Object3D* object = ToObject3D( L, 1, "isAwake" );

	Display& display = ToLibrary( L )->GetDisplay();

	lua_pushboolean( L,
		( object != NULL && display.HasPhysics3D() && display.GetPhysics3D().IsAwake( *object ) ) ? 1 : 0 );

	return 1;
}

int
Physics3DLibrary::wake( lua_State *L )
{
	Object3D* object = ToObject3D( L, 1, "wake" );

	if ( object != NULL )
	{
		ToLibrary( L )->GetDisplay().GetPhysics3D().Wake( *object );
	}

	return 0;
}

// ----------------------------------------------------------------------------

int
Physics3DLibrary::Open( lua_State *L )
{
	Display *display = (Display *)lua_touserdata( L, lua_upvalueindex( 1 ) );
	Rtt_ASSERT( display );

	const char kMetatableName[] = __FILE__;
	CoronaLuaInitializeGCMetatable( L, kMetatableName, Finalizer );

	const luaL_Reg kVTable[] =
	{
		{ "start", start },
		{ "pause", pause },
		{ "stop", stop },

		{ "setGravity", setGravity },
		{ "getGravity", getGravity },
		{ "setTimeStep", setTimeStep },

		{ "addBody", addBody },
		{ "removeBody", removeBody },

		{ "applyForce", applyForce },
		{ "applyImpulse", applyImpulse },
		{ "applyTorque", applyTorque },

		{ "setLinearVelocity", setLinearVelocity },
		{ "getLinearVelocity", getLinearVelocity },
		{ "setAngularVelocity", setAngularVelocity },
		{ "getAngularVelocity", getAngularVelocity },

		{ "setPosition", setPosition },

		{ "isAwake", isAwake },
		{ "wake", wake },

		{ NULL, NULL }
	};

	Self *library = new Self( * display );

	CoronaLuaPushUserdata( L, library, kMetatableName );
	lua_pushstring( L, kMetatableName );
	lua_settable( L, LUA_REGISTRYINDEX );

	return CoronaLibraryNew( L, kName, "com.coronalabs", 1, 1, kVTable, library );
}

// ----------------------------------------------------------------------------

void
LuaLibPhysics3D::Initialize( lua_State *L, Display& display )
{
	Rtt_LUA_STACK_GUARD( L );

	lua_pushlightuserdata( L, & display );
	CoronaLuaRegisterModuleLoader( L, Physics3DLibrary::kName, Physics3DLibrary::Open, 1 );
}

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------
