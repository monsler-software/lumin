//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Corona game engine.
// For overview and more information on licensing please refer to README.md
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////

#ifndef _Rtt_LuaLibPhysics3D_H__
#define _Rtt_LuaLibPhysics3D_H__

// ----------------------------------------------------------------------------

struct lua_State;

namespace Rtt
{

class Display;

// ----------------------------------------------------------------------------

// The `physics3d` module: rigid bodies for render.* objects, on Box3D.
//
// Named and shaped after Corona's 2D `physics` library rather than inventing a
// vocabulary -- start, pause, setGravity, addBody, removeBody -- so that what a
// project already knows about the 2D one carries over. What it does not have is
// setScale: a 2D world has to bridge pixels and metres, and a 3D scene is already
// in whatever unit its content chose.
class LuaLibPhysics3D
{
	public:
		typedef LuaLibPhysics3D Self;

	public:
		static void Initialize( lua_State *L, Display& display );
};

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------

#endif // _Rtt_LuaLibPhysics3D_H__
