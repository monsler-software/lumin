//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Corona game engine.
// For overview and more information on licensing please refer to README.md
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////

#ifndef _Rtt_LuaLibRender_H__
#define _Rtt_LuaLibRender_H__

// ----------------------------------------------------------------------------

struct lua_State;

namespace Rtt
{

class Camera3D;
class Display;
class Material3D;
class ModelPart3D;
class ShaderEffect3D;

// ----------------------------------------------------------------------------

// The `render` module: 3D geometry, cameras, lights and materials.
class LuaLibRender3D
{
	public:
		typedef LuaLibRender3D Self;

	public:
		static void Initialize( lua_State *L, Display& display );

	public:
		// Cameras and materials are plain userdata rather than display objects
		// -- neither is ever drawn or placed in a group -- so they need their
		// own conversions, which the object vtables also use.
		//
		// Both return NULL for nil or for a value of the wrong type, and raise
		// no error: passing nil is a meaningful way to clear a material, and
		// the callers all have somewhere sensible to go when there is nothing.
		static Camera3D* ToCamera( lua_State *L, int index );
		static Material3D* ToMaterial( lua_State *L, int index );

		// The handle model:getMesh() returns: one part of one model instance.
		// Also not a display object -- a part is not independently placed, hidden
		// with `isVisible`, or removable, so the only thing it would inherit from
		// the display list is a set of properties that do not apply to it.
		static ModelPart3D* ToModelPart( lua_State *L, int index );

		// The handle render.newShaderEffect() returns.
		static ShaderEffect3D* ToShaderEffect( lua_State *L, int index );

		// Pushes a new Lua handle for an existing object, retaining it.
		static void PushCamera( lua_State *L, Camera3D* camera );
		static void PushMaterial( lua_State *L, Material3D* material );
		static void PushModelPart( lua_State *L, ModelPart3D* part );
		static void PushShaderEffect( lua_State *L, ShaderEffect3D* effect );
};

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------

#endif // _Rtt_LuaLibRender_H__
