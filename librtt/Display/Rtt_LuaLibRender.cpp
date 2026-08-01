//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Corona game engine.
// For overview and more information on licensing please refer to README.md
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////

#include "Core/Rtt_Build.h"

#include "Display/Rtt_LuaLibRender.h"

#include "Display/Rtt_Camera3D.h"
#include "Display/Rtt_Display.h"
#include "Display/Rtt_GroupObject.h"
#include "Display/Rtt_LuaLibDisplay.h"
#include "Display/Rtt_Material3D.h"
#include "Display/Rtt_Mesh3D.h"
#include "Display/Rtt_Model3D.h"
#include "Display/Rtt_ModelObject3D.h"
#include "Display/Rtt_Object3D.h"
#include "Display/Rtt_Scene3D.h"
#include "Display/Rtt_ShaderEffect3D.h"
#include "Display/Rtt_StageObject.h"
#include "Display/Rtt_Texture3D.h"
#include "Rtt_LuaContext.h"
#include "Rtt_LuaLibSystem.h"
#include "Rtt_MPlatform.h"
#include "Rtt_Runtime.h"

#include "CoronaLibrary.h"
#include "CoronaLua.h"

// ----------------------------------------------------------------------------

namespace Rtt
{

// ----------------------------------------------------------------------------

// Metatable names for the two handle types. File- and type-unique, following
// the same convention the display library uses for its own registry keys.
static const char kCameraMetatable[] = "render.camera";
static const char kMaterialMetatable[] = "render.material";
static const char kModelPartMetatable[] = "render.mesh";
static const char kEffectMetatable[] = "render.effect";

// ----------------------------------------------------------------------------

class RenderLibrary
{
	public:
		typedef RenderLibrary Self;

	public:
		static const char kName[];

	public:
		RenderLibrary( Display& display ) : fDisplay( display ) {}

		Display& GetDisplay() { return fDisplay; }

	public:
		static int Open( lua_State *L );

	private:
		static int Finalizer( lua_State *L );
		static Self* ToLibrary( lua_State *L );

	private:
		static int newBox( lua_State *L );
		static int newSphere( lua_State *L );
		static int newPlane( lua_State *L );
		static int newCylinder( lua_State *L );

		static int newCamera( lua_State *L );
		static int setActiveCamera( lua_State *L );

		static int newDirectionalLight( lua_State *L );
		static int newPointLight( lua_State *L );
		static int newSpotLight( lua_State *L );

		static int newMaterial( lua_State *L );

		static int newModel( lua_State *L );
		static int newCustomMesh( lua_State *L );

		static int newShaderEffect( lua_State *L );

		static int raycast( lua_State *L );

		static int setAmbientLight( lua_State *L );
		static int setEnvironmentMap( lua_State *L );

		static int notImplemented( lua_State *L );

	private:
		// Shared tail of the four primitive constructors: takes a mesh, wraps
		// it in an Object3D, puts that in the group the caller named (or the
		// current stage), and leaves it on the stack.
		static int PushObject3D( lua_State *L, Mesh3D* mesh, GroupObject* parent, Display& display );

		static int PushLight( lua_State *L, Draw3DLight::Kind kind );

		Display& fDisplay;
};

const char RenderLibrary::kName[] = "render";

// ----------------------------------------------------------------------------

RenderLibrary*
RenderLibrary::ToLibrary( lua_State *L )
{
	return (Self *)lua_touserdata( L, lua_upvalueindex( 1 ) );
}

int
RenderLibrary::Finalizer( lua_State *L )
{
	Self *library = (Self *)CoronaLuaToUserdata( L, 1 );

	delete library;

	return 0;
}

// ----------------------------------------------------------------------------

// Camera and material handles are userdata holding a pointer, with a __gc that
// releases the reference. The objects themselves are reference counted, so a
// camera stays alive while it is the active one and a material while any object
// is using it, however soon the Lua value is collected.

struct HandleUserdata
{
	void* fObject;
};

static int
CameraFinalizer( lua_State *L )
{
	HandleUserdata* ud = (HandleUserdata *)lua_touserdata( L, 1 );

	if ( ud != NULL && ud->fObject != NULL )
	{
		( (Camera3D *)ud->fObject )->Release();
		ud->fObject = NULL;
	}

	return 0;
}

static int
MaterialFinalizer( lua_State *L )
{
	HandleUserdata* ud = (HandleUserdata *)lua_touserdata( L, 1 );

	if ( ud != NULL && ud->fObject != NULL )
	{
		( (Material3D *)ud->fObject )->Release();
		ud->fObject = NULL;
	}

	return 0;
}

static int
ModelPartFinalizer( lua_State *L )
{
	HandleUserdata* ud = (HandleUserdata *)lua_touserdata( L, 1 );

	if ( ud != NULL && ud->fObject != NULL )
	{
		( (ModelPart3D *)ud->fObject )->Release();
		ud->fObject = NULL;
	}

	return 0;
}

static int
EffectFinalizer( lua_State *L )
{
	HandleUserdata* ud = (HandleUserdata *)lua_touserdata( L, 1 );

	if ( ud != NULL && ud->fObject != NULL )
	{
		( (ShaderEffect3D *)ud->fObject )->Release();
		ud->fObject = NULL;
	}

	return 0;
}

static void*
ToHandle( lua_State *L, int index, const char* metatableName )
{
	if ( !lua_isuserdata( L, index ) )
	{
		return NULL;
	}

	HandleUserdata* ud = (HandleUserdata *)lua_touserdata( L, index );

	if ( ud == NULL || !lua_getmetatable( L, index ) )
	{
		return NULL;
	}

	luaL_getmetatable( L, metatableName );

	const bool matches = lua_rawequal( L, -1, -2 ) != 0;

	lua_pop( L, 2 );

	return matches ? ud->fObject : NULL;
}

Camera3D*
LuaLibRender3D::ToCamera( lua_State *L, int index )
{
	return (Camera3D *)ToHandle( L, index, kCameraMetatable );
}

Material3D*
LuaLibRender3D::ToMaterial( lua_State *L, int index )
{
	return (Material3D *)ToHandle( L, index, kMaterialMetatable );
}

ModelPart3D*
LuaLibRender3D::ToModelPart( lua_State *L, int index )
{
	return (ModelPart3D *)ToHandle( L, index, kModelPartMetatable );
}

ShaderEffect3D*
LuaLibRender3D::ToShaderEffect( lua_State *L, int index )
{
	return (ShaderEffect3D *)ToHandle( L, index, kEffectMetatable );
}

// Camera methods are looked up on the metatable's __index table, which is the
// metatable itself, so this covers both lookups and the userdata's identity.
static int
CameraSetPosition( lua_State *L )
{
	Camera3D* camera = LuaLibRender3D::ToCamera( L, 1 );

	if ( camera != NULL )
	{
		camera->SetPosition(
			(float) luaL_optnumber( L, 2, 0.0 ),
			(float) luaL_optnumber( L, 3, 0.0 ),
			(float) luaL_optnumber( L, 4, 0.0 ) );
	}

	return 0;
}

static int
CameraLookAt( lua_State *L )
{
	Camera3D* camera = LuaLibRender3D::ToCamera( L, 1 );

	if ( camera != NULL )
	{
		camera->LookAt(
			(float) luaL_optnumber( L, 2, 0.0 ),
			(float) luaL_optnumber( L, 3, 0.0 ),
			(float) luaL_optnumber( L, 4, 0.0 ) );
	}

	return 0;
}

static int
CameraSetTarget( lua_State *L )
{
	Camera3D* camera = LuaLibRender3D::ToCamera( L, 1 );

	if ( camera != NULL )
	{
		// Passing nil is how following is turned off, which leaves the camera
		// aimed wherever it was last pointed rather than snapping it somewhere.
		DisplayObject* object = (DisplayObject *)LuaProxy::GetProxyableObject( L, 2 );

		camera->SetTarget( object );
	}

	return 0;
}

static int
CameraIndex( lua_State *L )
{
	Camera3D* camera = LuaLibRender3D::ToCamera( L, 1 );
	const char* key = lua_tostring( L, 2 );

	if ( camera == NULL || key == NULL )
	{
		return 0;
	}

	float x, y, z;
	camera->GetPosition( x, y, z );

	if ( strcmp( key, "x" ) == 0 ) { lua_pushnumber( L, x ); return 1; }
	if ( strcmp( key, "y" ) == 0 ) { lua_pushnumber( L, y ); return 1; }
	if ( strcmp( key, "z" ) == 0 ) { lua_pushnumber( L, z ); return 1; }

	if ( strcmp( key, "fov" ) == 0 ) { lua_pushnumber( L, camera->GetFieldOfView() ); return 1; }
	if ( strcmp( key, "near" ) == 0 ) { lua_pushnumber( L, camera->GetNear() ); return 1; }
	if ( strcmp( key, "far" ) == 0 ) { lua_pushnumber( L, camera->GetFar() ); return 1; }
	if ( strcmp( key, "aspectRatio" ) == 0 ) { lua_pushnumber( L, camera->GetAspectRatio() ); return 1; }

	if ( strcmp( key, "setPosition" ) == 0 ) { lua_pushcfunction( L, CameraSetPosition ); return 1; }
	if ( strcmp( key, "lookAt" ) == 0 ) { lua_pushcfunction( L, CameraLookAt ); return 1; }
	if ( strcmp( key, "setTarget" ) == 0 ) { lua_pushcfunction( L, CameraSetTarget ); return 1; }

	return 0;
}

static int
CameraNewIndex( lua_State *L )
{
	Camera3D* camera = LuaLibRender3D::ToCamera( L, 1 );
	const char* key = lua_tostring( L, 2 );

	if ( camera == NULL || key == NULL )
	{
		return 0;
	}

	const float value = (float) lua_tonumber( L, 3 );

	float x, y, z;
	camera->GetPosition( x, y, z );

	if ( strcmp( key, "x" ) == 0 ) { camera->SetPosition( value, y, z ); return 0; }
	if ( strcmp( key, "y" ) == 0 ) { camera->SetPosition( x, value, z ); return 0; }
	if ( strcmp( key, "z" ) == 0 ) { camera->SetPosition( x, y, value ); return 0; }

	if ( strcmp( key, "fov" ) == 0 ) { camera->SetFieldOfView( value ); return 0; }
	if ( strcmp( key, "near" ) == 0 ) { camera->SetNear( value ); return 0; }
	if ( strcmp( key, "far" ) == 0 ) { camera->SetFar( value ); return 0; }
	if ( strcmp( key, "aspectRatio" ) == 0 ) { camera->SetAspectRatio( value ); return 0; }

	return 0;
}

static int
MaterialIndex( lua_State *L )
{
	Material3D* material = LuaLibRender3D::ToMaterial( L, 1 );
	const char* key = lua_tostring( L, 2 );

	if ( material == NULL || key == NULL )
	{
		return 0;
	}

	if ( strcmp( key, "roughness" ) == 0 ) { lua_pushnumber( L, material->GetRoughness() ); return 1; }
	if ( strcmp( key, "metallic" ) == 0 ) { lua_pushnumber( L, material->GetMetallic() ); return 1; }

	return 0;
}

static int
MaterialNewIndex( lua_State *L )
{
	Material3D* material = LuaLibRender3D::ToMaterial( L, 1 );
	const char* key = lua_tostring( L, 2 );

	if ( material == NULL || key == NULL )
	{
		return 0;
	}

	if ( strcmp( key, "roughness" ) == 0 ) { material->SetRoughness( (float) lua_tonumber( L, 3 ) ); return 0; }
	if ( strcmp( key, "metallic" ) == 0 ) { material->SetMetallic( (float) lua_tonumber( L, 3 ) ); return 0; }

	return 0;
}

// ----------------------------------------------------------------------------

// The mesh handle: one part of one model instance.
//
// Deliberately narrow. A part is not a display object -- it cannot be moved,
// removed or reparented independently of the model it belongs to -- so what it
// offers is only what is genuinely per-part: which material draws it, and
// whether it draws at all.

static int
ModelPartSetMaterial( lua_State *L )
{
	ModelPart3D* part = LuaLibRender3D::ToModelPart( L, 1 );

	if ( part != NULL )
	{
		// Passing nil returns the part to the model's material, or failing that
		// to the one the file described -- not to the default surface, which
		// would make clearing a material look like losing one.
		part->SetOverrideMaterial( LuaLibRender3D::ToMaterial( L, 2 ) );
	}

	return 0;
}

static int
ModelPartIndex( lua_State *L )
{
	ModelPart3D* part = LuaLibRender3D::ToModelPart( L, 1 );
	const char* key = lua_tostring( L, 2 );

	if ( part == NULL || key == NULL )
	{
		return 0;
	}

	if ( strcmp( key, "name" ) == 0 ) { lua_pushstring( L, part->GetName().c_str() ); return 1; }
	if ( strcmp( key, "isVisible" ) == 0 ) { lua_pushboolean( L, part->IsVisible() ? 1 : 0 ); return 1; }

	if ( strcmp( key, "setMaterial" ) == 0 ) { lua_pushcfunction( L, ModelPartSetMaterial ); return 1; }

	return 0;
}

static int
ModelPartNewIndex( lua_State *L )
{
	ModelPart3D* part = LuaLibRender3D::ToModelPart( L, 1 );
	const char* key = lua_tostring( L, 2 );

	if ( part == NULL || key == NULL )
	{
		return 0;
	}

	if ( strcmp( key, "isVisible" ) == 0 )
	{
		part->SetVisible( lua_toboolean( L, 3 ) != 0 );

		return 0;
	}

	return 0;
}

// ----------------------------------------------------------------------------

// The effect handle. Reading and writing a declared uniform by name is the whole
// of it: `effect.outlineWidth = 0.02` sets x, and a table sets all four.
static int
EffectIndex( lua_State *L )
{
	ShaderEffect3D* effect = LuaLibRender3D::ToShaderEffect( L, 1 );
	const char* key = lua_tostring( L, 2 );

	if ( effect == NULL || key == NULL )
	{
		return 0;
	}

	if ( strcmp( key, "name" ) == 0 )
	{
		lua_pushstring( L, effect->GetName().c_str() );

		return 1;
	}

	const int index = effect->FindUniform( key );

	if ( index < 0 )
	{
		return 0;
	}

	const float* value = effect->GetUniforms()[index].fValue;

	// A scalar comes back as a number and a vector as a table. Which one a
	// uniform is was fixed when it was declared, so the caller gets back what
	// they put in rather than having to unwrap a table around every float.
	lua_createtable( L, 4, 0 );

	for ( int i = 0; i < 4; ++i )
	{
		lua_pushnumber( L, value[i] );
		lua_rawseti( L, -2, i + 1 );
	}

	return 1;
}

static int
EffectNewIndex( lua_State *L )
{
	ShaderEffect3D* effect = LuaLibRender3D::ToShaderEffect( L, 1 );
	const char* key = lua_tostring( L, 2 );

	if ( effect == NULL || key == NULL )
	{
		return 0;
	}

	if ( effect->FindUniform( key ) < 0 )
	{
		// Named, because a mistyped uniform is otherwise an effect that ignores
		// half of what it was told and gives no hint why.
		CoronaLuaWarning( L, "render effect '%s' has no uniform named '%s'; declare it in the 'uniforms' table passed to render.newShaderEffect()",
			effect->GetName().c_str(), key );

		return 0;
	}

	float value[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

	if ( lua_istable( L, 3 ) )
	{
		for ( int i = 0; i < 4; ++i )
		{
			lua_rawgeti( L, 3, i + 1 );
			value[i] = (float) luaL_optnumber( L, -1, 0.0 );
			lua_pop( L, 1 );
		}
	}
	else
	{
		value[0] = (float) lua_tonumber( L, 3 );
	}

	effect->SetUniform( key, value );

	return 0;
}

// ----------------------------------------------------------------------------

static void
PushHandle( lua_State *L, void* object, const char* metatableName )
{
	HandleUserdata* ud = (HandleUserdata *)lua_newuserdata( L, sizeof( HandleUserdata ) );

	ud->fObject = object;

	luaL_getmetatable( L, metatableName );
	lua_setmetatable( L, -2 );
}

void
LuaLibRender3D::PushCamera( lua_State *L, Camera3D* camera )
{
	camera->Retain();

	PushHandle( L, camera, kCameraMetatable );
}

void
LuaLibRender3D::PushMaterial( lua_State *L, Material3D* material )
{
	material->Retain();

	PushHandle( L, material, kMaterialMetatable );
}

void
LuaLibRender3D::PushModelPart( lua_State *L, ModelPart3D* part )
{
	part->Retain();

	PushHandle( L, part, kModelPartMetatable );
}

void
LuaLibRender3D::PushShaderEffect( lua_State *L, ShaderEffect3D* effect )
{
	effect->Retain();

	PushHandle( L, effect, kEffectMetatable );
}

// ----------------------------------------------------------------------------

// The optional leading group argument.
//
// LuaLibDisplay::GetParent cannot be used directly here. It assumes any table
// in that position is a display object and raises "Proxy expected, got nil" if
// it is not -- fine for display.newRect( [group,] x, y, w, h ), where the next
// argument is a number, but not for render.newBox( [group,] options ), where a
// plain options table is exactly what turns up when no group was passed.
//
// So the proxy field is checked for first, without the raising lookup, and
// GetParent is only called once the table is known to be a display object.
static GroupObject*
GetOptionalParent( lua_State *L, int& nextArg )
{
	if ( !lua_istable( L, nextArg ) )
	{
		return NULL;
	}

	lua_getfield( L, nextArg, "_proxy" );

	const bool isDisplayObject = lua_isuserdata( L, -1 ) != 0;

	lua_pop( L, 1 );

	if ( !isDisplayObject )
	{
		return NULL;
	}

	return LuaLibDisplay::GetParent( L, nextArg );
}

int
RenderLibrary::PushObject3D( lua_State *L, Mesh3D* mesh, GroupObject* parent, Display& display )
{
	Object3D* object = Rtt_NEW( display.GetAllocator(), Object3D( display.GetScene3D(), mesh ) );

	// The object took its own reference; this one was the constructor's.
	mesh->Release();

	return LuaLibDisplay::AssignParentAndPushResult( L, display, object, parent );
}

int
RenderLibrary::newBox( lua_State *L )
{
	Self *library = ToLibrary( L );
	Display& display = library->GetDisplay();

	int nextArg = 1;
	GroupObject *parent = GetOptionalParent( L, nextArg );

	float width = 1.0f, height = 1.0f, depth = 1.0f;

	if ( lua_istable( L, nextArg ) )
	{
		lua_getfield( L, nextArg, "width" );
		width = (float) luaL_optnumber( L, -1, 1.0 );
		lua_pop( L, 1 );

		lua_getfield( L, nextArg, "height" );
		height = (float) luaL_optnumber( L, -1, 1.0 );
		lua_pop( L, 1 );

		lua_getfield( L, nextArg, "depth" );
		depth = (float) luaL_optnumber( L, -1, 1.0 );
		lua_pop( L, 1 );
	}

	return PushObject3D( L, Mesh3D::NewBox( width, height, depth ), parent, display );
}

int
RenderLibrary::newSphere( lua_State *L )
{
	Self *library = ToLibrary( L );
	Display& display = library->GetDisplay();

	int nextArg = 1;
	GroupObject *parent = GetOptionalParent( L, nextArg );

	float radius = 0.5f;
	int segments = 32;

	if ( lua_istable( L, nextArg ) )
	{
		lua_getfield( L, nextArg, "radius" );
		radius = (float) luaL_optnumber( L, -1, 0.5 );
		lua_pop( L, 1 );

		lua_getfield( L, nextArg, "segments" );
		segments = (int) luaL_optinteger( L, -1, 32 );
		lua_pop( L, 1 );
	}

	return PushObject3D( L, Mesh3D::NewSphere( radius, (U32) segments ), parent, display );
}

int
RenderLibrary::newPlane( lua_State *L )
{
	Self *library = ToLibrary( L );
	Display& display = library->GetDisplay();

	int nextArg = 1;
	GroupObject *parent = GetOptionalParent( L, nextArg );

	float width = 1.0f, depth = 1.0f;

	if ( lua_istable( L, nextArg ) )
	{
		lua_getfield( L, nextArg, "width" );
		width = (float) luaL_optnumber( L, -1, 1.0 );
		lua_pop( L, 1 );

		// Accepts "height" as well as "depth": a plane lying in the ground has
		// no height, but it is the word that comes to hand when the other
		// dimension was called width.
		lua_getfield( L, nextArg, "depth" );

		if ( lua_isnil( L, -1 ) )
		{
			lua_pop( L, 1 );
			lua_getfield( L, nextArg, "height" );
		}

		depth = (float) luaL_optnumber( L, -1, 1.0 );
		lua_pop( L, 1 );
	}

	return PushObject3D( L, Mesh3D::NewPlane( width, depth ), parent, display );
}

int
RenderLibrary::newCylinder( lua_State *L )
{
	Self *library = ToLibrary( L );
	Display& display = library->GetDisplay();

	int nextArg = 1;
	GroupObject *parent = GetOptionalParent( L, nextArg );

	float radius = 0.5f;
	float radiusTop = -1.0f;
	float radiusBottom = -1.0f;
	float height = 1.0f;
	int segments = 32;

	if ( lua_istable( L, nextArg ) )
	{
		lua_getfield( L, nextArg, "radius" );
		radius = (float) luaL_optnumber( L, -1, 0.5 );
		lua_pop( L, 1 );

		lua_getfield( L, nextArg, "radiusTop" );
		radiusTop = (float) luaL_optnumber( L, -1, -1.0 );
		lua_pop( L, 1 );

		lua_getfield( L, nextArg, "radiusBottom" );
		radiusBottom = (float) luaL_optnumber( L, -1, -1.0 );
		lua_pop( L, 1 );

		lua_getfield( L, nextArg, "height" );
		height = (float) luaL_optnumber( L, -1, 1.0 );
		lua_pop( L, 1 );

		lua_getfield( L, nextArg, "segments" );
		segments = (int) luaL_optinteger( L, -1, 32 );
		lua_pop( L, 1 );
	}

	// A cone is a cylinder with one end pinched shut, so rather than a separate
	// constructor the two radii are given separately when they differ and both
	// default to `radius` when they do not.
	if ( radiusTop < 0.0f ) { radiusTop = radius; }
	if ( radiusBottom < 0.0f ) { radiusBottom = radius; }

	return PushObject3D( L, Mesh3D::NewCylinder( radiusTop, radiusBottom, height, (U32) segments ), parent, display );
}

// ----------------------------------------------------------------------------

// Reads a flat array of numbers from the field `name` of the table at `index`.
//
// Returns the count, or -1 if the field is present but not an array of numbers,
// having raised nothing -- the caller reports the problem with the field's name
// in hand.
static int
ReadNumberArray( lua_State *L, int index, const char* name, std::vector< float >& out )
{
	lua_getfield( L, index, name );

	if ( lua_isnil( L, -1 ) )
	{
		lua_pop( L, 1 );

		return 0;
	}

	if ( !lua_istable( L, -1 ) )
	{
		lua_pop( L, 1 );

		return -1;
	}

	const int count = (int) lua_objlen( L, -1 );

	out.reserve( (size_t) count );

	for ( int i = 1; i <= count; ++i )
	{
		lua_rawgeti( L, -1, i );

		if ( !lua_isnumber( L, -1 ) )
		{
			lua_pop( L, 2 );

			return -1;
		}

		out.push_back( (float) lua_tonumber( L, -1 ) );
		lua_pop( L, 1 );
	}

	lua_pop( L, 1 );

	return count;
}

// render.newCustomMesh( [group,] { vertices = {...}, indices = {...},
//                                  normals = {...}, uvs = {...} } )
//
// vertices is x, y, z per vertex; normals the same; uvs is u, v per vertex; and
// indices is three 1-based vertex numbers per triangle. Only vertices is
// required: without indices the vertices are taken as a plain triangle list, and
// without normals they are computed from the faces.
//
// 1-based indices, because every other list a Corona project builds is 1-based
// and a table of 0-based ones beside a table of 1-based ones is a trap.
int
RenderLibrary::newCustomMesh( lua_State *L )
{
	Self *library = ToLibrary( L );
	Display& display = library->GetDisplay();

	int nextArg = 1;
	GroupObject *parent = GetOptionalParent( L, nextArg );

	if ( !lua_istable( L, nextArg ) )
	{
		return luaL_error( L, "render.newCustomMesh() requires a table of mesh data" );
	}

	std::vector< float > positions;
	std::vector< float > normals;
	std::vector< float > uvs;
	std::vector< float > indexNumbers;

	if ( ReadNumberArray( L, nextArg, "vertices", positions ) < 0 )
	{
		return luaL_error( L, "render.newCustomMesh(): 'vertices' must be an array of numbers" );
	}

	if ( ReadNumberArray( L, nextArg, "normals", normals ) < 0 )
	{
		return luaL_error( L, "render.newCustomMesh(): 'normals' must be an array of numbers" );
	}

	if ( ReadNumberArray( L, nextArg, "uvs", uvs ) < 0 )
	{
		return luaL_error( L, "render.newCustomMesh(): 'uvs' must be an array of numbers" );
	}

	if ( ReadNumberArray( L, nextArg, "indices", indexNumbers ) < 0 )
	{
		return luaL_error( L, "render.newCustomMesh(): 'indices' must be an array of numbers" );
	}

	if ( positions.empty() || ( positions.size() % 3 ) != 0 )
	{
		return luaL_error( L, "render.newCustomMesh(): 'vertices' must hold three numbers per vertex, and at least one vertex" );
	}

	const size_t vertexCount = positions.size() / 3;

	// Checked rather than padded: a normals array of the wrong length means the
	// caller built one of the two lists differently than they think, and quietly
	// using part of it would hide that until the shading looked wrong.
	if ( !normals.empty() && normals.size() != vertexCount * 3 )
	{
		return luaL_error( L, "render.newCustomMesh(): 'normals' has %d numbers, but %d vertices need %d",
			(int) normals.size(), (int) vertexCount, (int) ( vertexCount * 3 ) );
	}

	if ( !uvs.empty() && uvs.size() != vertexCount * 2 )
	{
		return luaL_error( L, "render.newCustomMesh(): 'uvs' has %d numbers, but %d vertices need %d",
			(int) uvs.size(), (int) vertexCount, (int) ( vertexCount * 2 ) );
	}

	std::vector< Vertex3D > vertices( vertexCount );

	for ( size_t i = 0; i < vertexCount; ++i )
	{
		Vertex3D& vertex = vertices[i];

		vertex.x = positions[i * 3 + 0];
		vertex.y = positions[i * 3 + 1];
		vertex.z = positions[i * 3 + 2];

		vertex.nx = normals.empty() ? 0.0f : normals[i * 3 + 0];
		vertex.ny = normals.empty() ? 0.0f : normals[i * 3 + 1];
		vertex.nz = normals.empty() ? 0.0f : normals[i * 3 + 2];

		vertex.u = uvs.empty() ? 0.0f : uvs[i * 2 + 0];
		vertex.v = uvs.empty() ? 0.0f : uvs[i * 2 + 1];
	}

	std::vector< U32 > indices;

	if ( indexNumbers.empty() )
	{
		if ( ( vertexCount % 3 ) != 0 )
		{
			return luaL_error( L, "render.newCustomMesh(): with no 'indices' the vertices are taken as triangles, so their count must be a multiple of three, not %d",
				(int) vertexCount );
		}

		indices.resize( vertexCount );

		for ( size_t i = 0; i < vertexCount; ++i )
		{
			indices[i] = (U32) i;
		}
	}
	else
	{
		if ( ( indexNumbers.size() % 3 ) != 0 )
		{
			return luaL_error( L, "render.newCustomMesh(): 'indices' must hold three per triangle, and %d is not a multiple of three",
				(int) indexNumbers.size() );
		}

		indices.resize( indexNumbers.size() );

		for ( size_t i = 0, iMax = indexNumbers.size(); i < iMax; ++i )
		{
			const double value = indexNumbers[i];

			// Named with the offending position and value: an out-of-range index
			// is otherwise a mesh that either does not appear or draws garbage,
			// with nothing to say which of a thousand numbers was wrong.
			if ( value < 1.0 || value > (double) vertexCount )
			{
				return luaL_error( L, "render.newCustomMesh(): indices[%d] is %d, outside the 1 to %d range of the %d vertices given",
					(int) i + 1, (int) value, (int) vertexCount, (int) vertexCount );
			}

			indices[i] = (U32) value - 1;
		}
	}

	if ( normals.empty() )
	{
		Mesh3D::GenerateNormals( vertices, indices );
	}

	Mesh3D* mesh = Mesh3D::NewFromGeometry( vertices, indices, NULL );

	if ( mesh == NULL )
	{
		return luaL_error( L, "render.newCustomMesh(): the geometry given could not be used" );
	}

	return PushObject3D( L, mesh, parent, display );
}

// ----------------------------------------------------------------------------

int
RenderLibrary::newCamera( lua_State *L )
{
	Camera3D* camera = new Camera3D;

	if ( lua_istable( L, 1 ) )
	{
		lua_getfield( L, 1, "fov" );
		if ( !lua_isnil( L, -1 ) ) { camera->SetFieldOfView( (float) lua_tonumber( L, -1 ) ); }
		lua_pop( L, 1 );

		lua_getfield( L, 1, "near" );
		if ( !lua_isnil( L, -1 ) ) { camera->SetNear( (float) lua_tonumber( L, -1 ) ); }
		lua_pop( L, 1 );

		lua_getfield( L, 1, "far" );
		if ( !lua_isnil( L, -1 ) ) { camera->SetFar( (float) lua_tonumber( L, -1 ) ); }
		lua_pop( L, 1 );

		lua_getfield( L, 1, "aspectRatio" );
		if ( !lua_isnil( L, -1 ) ) { camera->SetAspectRatio( (float) lua_tonumber( L, -1 ) ); }
		lua_pop( L, 1 );
	}

	LuaLibRender3D::PushCamera( L, camera );

	// PushCamera took a reference of its own; this one was the constructor's.
	camera->Release();

	return 1;
}

int
RenderLibrary::setActiveCamera( lua_State *L )
{
	Self *library = ToLibrary( L );

	library->GetDisplay().GetScene3D().SetActiveCamera( LuaLibRender3D::ToCamera( L, 1 ) );

	return 0;
}

// ----------------------------------------------------------------------------

int
RenderLibrary::PushLight( lua_State *L, Draw3DLight::Kind kind )
{
	Self *library = ToLibrary( L );
	Display& display = library->GetDisplay();

	int nextArg = 1;
	GroupObject *parent = GetOptionalParent( L, nextArg );

	Light3D* light = Rtt_NEW( display.GetAllocator(), Light3D( display.GetScene3D(), kind ) );

	int result = LuaLibDisplay::AssignParentAndPushResult( L, display, light, parent );

	if ( lua_istable( L, nextArg ) )
	{
		lua_getfield( L, nextArg, "intensity" );
		if ( !lua_isnil( L, -1 ) ) { light->SetIntensity( (float) lua_tonumber( L, -1 ) ); }
		lua_pop( L, 1 );

		lua_getfield( L, nextArg, "range" );
		if ( !lua_isnil( L, -1 ) ) { light->SetRange( (float) lua_tonumber( L, -1 ) ); }
		lua_pop( L, 1 );

		lua_getfield( L, nextArg, "innerAngle" );
		if ( !lua_isnil( L, -1 ) ) { light->SetInnerAngle( (float) lua_tonumber( L, -1 ) ); }
		lua_pop( L, 1 );

		lua_getfield( L, nextArg, "outerAngle" );
		if ( !lua_isnil( L, -1 ) ) { light->SetOuterAngle( (float) lua_tonumber( L, -1 ) ); }
		lua_pop( L, 1 );

		lua_getfield( L, nextArg, "color" );

		if ( lua_istable( L, -1 ) )
		{
			float rgb[3] = { 1.0f, 1.0f, 1.0f };

			for ( int i = 0; i < 3; ++i )
			{
				lua_rawgeti( L, -1, i + 1 );
				rgb[i] = (float) luaL_optnumber( L, -1, 1.0 );
				lua_pop( L, 1 );
			}

			light->SetColor( rgb[0], rgb[1], rgb[2] );
		}

		lua_pop( L, 1 );
	}

	return result;
}

int
RenderLibrary::newDirectionalLight( lua_State *L )
{
	return PushLight( L, Draw3DLight::kDirectional );
}

int
RenderLibrary::newPointLight( lua_State *L )
{
	return PushLight( L, Draw3DLight::kPoint );
}

int
RenderLibrary::newSpotLight( lua_State *L )
{
	return PushLight( L, Draw3DLight::kSpot );
}

// ----------------------------------------------------------------------------

// Loads a texture named by the value on top of the stack, which may be a path or
// a table of { filePath, baseDir }, and hands it to the setter.
//
// The value is left where it was: the caller pops it, as it does for every other
// field it read.
static void
ApplyTextureField(
	  lua_State *L
	, Display& display
	, Material3D& material
	, void (Material3D::*setter)( Texture3D* )
	, const char* fieldName )
{
	const char* filename = NULL;
	MPlatform::Directory baseDir = MPlatform::kResourceDir;

	if ( lua_istable( L, -1 ) )
	{
		lua_getfield( L, -1, "filePath" );
		filename = lua_tostring( L, -1 );

		lua_getfield( L, -2, "baseDir" );

		if ( !lua_isnil( L, -1 ) )
		{
			baseDir = LuaLibSystem::ToDirectory( L, -1 );
		}

		lua_pop( L, 1 );

		// The filePath string stays on the stack while it is used, since
		// lua_tostring hands back a pointer into the Lua value.
	}
	else
	{
		filename = lua_tostring( L, -1 );
	}

	if ( filename == NULL )
	{
		if ( lua_istable( L, -1 ) )
		{
			lua_pop( L, 1 );
		}

		return;
	}

	String path( display.GetAllocator() );

	display.GetRuntime().Platform().PathForFile( filename, baseDir, MPlatform::kTestFileExists, path );

	if ( path.GetString() == NULL )
	{
		CoronaLuaWarning( L, "render.newMaterial: could not find the '%s' texture '%s'", fieldName, filename );
	}
	else
	{
		Texture3D* texture = Texture3D::NewFromFile( path.GetString() );

		if ( texture != NULL )
		{
			( material.*setter )( texture );

			// The setter took its own reference; this one was the loader's.
			texture->Release();
		}
	}

	if ( lua_istable( L, -1 ) )
	{
		lua_pop( L, 1 );
	}
}

int
RenderLibrary::newMaterial( lua_State *L )
{
	Self *library = ToLibrary( L );
	Display& display = library->GetDisplay();

	Material3D* material = new Material3D;

	if ( lua_istable( L, 1 ) )
	{
		lua_getfield( L, 1, "albedo" );

		if ( lua_istable( L, -1 ) )
		{
			float rgba[4] = { 1.0f, 1.0f, 1.0f, 1.0f };

			for ( int i = 0; i < 4; ++i )
			{
				lua_rawgeti( L, -1, i + 1 );
				rgba[i] = (float) luaL_optnumber( L, -1, 1.0 );
				lua_pop( L, 1 );
			}

			material->SetAlbedo( rgba[0], rgba[1], rgba[2], rgba[3] );
		}
		else if ( !lua_isnil( L, -1 ) )
		{
			// A path instead of a colour: the map alone, with the factor left at
			// white so the texture arrives as it was authored.
			ApplyTextureField( L, display, *material, &Material3D::SetAlbedoMap, "albedo" );
		}

		lua_pop( L, 1 );

		lua_getfield( L, 1, "roughness" );
		if ( !lua_isnil( L, -1 ) ) { material->SetRoughness( (float) lua_tonumber( L, -1 ) ); }
		lua_pop( L, 1 );

		lua_getfield( L, 1, "metallic" );
		if ( !lua_isnil( L, -1 ) ) { material->SetMetallic( (float) lua_tonumber( L, -1 ) ); }
		lua_pop( L, 1 );

		lua_getfield( L, 1, "emissive" );

		if ( lua_istable( L, -1 ) )
		{
			float rgb[3] = { 0.0f, 0.0f, 0.0f };

			for ( int i = 0; i < 3; ++i )
			{
				lua_rawgeti( L, -1, i + 1 );
				rgb[i] = (float) luaL_optnumber( L, -1, 0.0 );
				lua_pop( L, 1 );
			}

			material->SetEmissive( rgb[0], rgb[1], rgb[2] );
		}

		lua_pop( L, 1 );

		// The remaining maps. Each takes a path, or a table naming a base
		// directory, on the same terms as 'albedo' above.
		lua_getfield( L, 1, "metallicRoughness" );

		if ( !lua_isnil( L, -1 ) )
		{
			ApplyTextureField( L, display, *material, &Material3D::SetMetallicRoughnessMap, "metallicRoughness" );
		}

		lua_pop( L, 1 );

		lua_getfield( L, 1, "emissiveMap" );

		if ( !lua_isnil( L, -1 ) )
		{
			ApplyTextureField( L, display, *material, &Material3D::SetEmissiveMap, "emissiveMap" );
		}

		lua_pop( L, 1 );

		lua_getfield( L, 1, "normal" );

		if ( !lua_isnil( L, -1 ) )
		{
			// Applying a normal map needs per-vertex tangents, which Vertex3D does
			// not carry yet -- so this is still declined rather than silently
			// ignored.
			CoronaLuaWarning( L, "render.newMaterial: normal maps are not supported yet" );
		}

		lua_pop( L, 1 );
	}

	LuaLibRender3D::PushMaterial( L, material );

	material->Release();

	return 1;
}

// ----------------------------------------------------------------------------

// render.newModel( [group,] filePath [, baseDirectory] )
//
// The path may also be given as a table, { filePath = ..., baseDir = ... },
// which is the form the rest of the render module's constructors take and the one
// that reads better where the base directory is not the default.
//
// Nothing is cached: loading the same file twice parses and uploads it twice. A
// scene built from many copies of one model should load it once and keep the
// object around, which is what a project would do with an image sheet too.
int
RenderLibrary::newModel( lua_State *L )
{
	Self *library = ToLibrary( L );
	Display& display = library->GetDisplay();

	int nextArg = 1;
	GroupObject *parent = GetOptionalParent( L, nextArg );

	const char* filename = NULL;
	MPlatform::Directory baseDir = MPlatform::kResourceDir;

	if ( lua_istable( L, nextArg ) )
	{
		lua_getfield( L, nextArg, "filePath" );

		if ( lua_isnil( L, -1 ) )
		{
			lua_pop( L, 1 );
			lua_getfield( L, nextArg, "filename" );
		}

		filename = lua_tostring( L, -1 );

		// Left on the stack deliberately: lua_tostring hands back a pointer into
		// the Lua value, which a pop would make collectable. It goes when the
		// call returns.
		lua_getfield( L, nextArg, "baseDir" );

		if ( !lua_isnil( L, -1 ) )
		{
			baseDir = LuaLibSystem::ToDirectory( L, -1 );
		}

		lua_pop( L, 1 );
	}
	else
	{
		filename = lua_tostring( L, nextArg );

		if ( !lua_isnoneornil( L, nextArg + 1 ) )
		{
			baseDir = LuaLibSystem::ToDirectory( L, nextArg + 1 );
		}
	}

	if ( filename == NULL )
	{
		// An error rather than a warning and a nil: every later line of a project
		// that got here would be operating on a nil model, and the first of them
		// would report a problem that has nothing to do with the real one.
		return luaL_error( L, "render.newModel() requires a file path" );
	}

	String path( display.GetAllocator() );

	display.GetRuntime().Platform().PathForFile( filename, baseDir, MPlatform::kTestFileExists, path );

	if ( path.GetString() == NULL )
	{
		CoronaLuaWarning( L, "render.newModel() could not find '%s'", filename );

		return 0;
	}

	Model3D* model = Model3D::NewFromFile( path.GetString() );

	if ( model == NULL )
	{
		// NewFromFile has already logged what was wrong with the file, in more
		// detail than could be repeated here.
		return 0;
	}

	ModelObject3D* object = Rtt_NEW( display.GetAllocator(), ModelObject3D( display.GetScene3D(), model ) );

	// The object took its own reference; this one was the loader's.
	model->Release();

	return LuaLibDisplay::AssignParentAndPushResult( L, display, object, parent );
}

// ----------------------------------------------------------------------------

// ----------------------------------------------------------------------------

// render.newShaderEffect( { name = "outline", fragment = "...", uniforms = {...},
//                           cull = "back" | "front" | "none",
//                           blend = false, depthWrite = true } )
//
// The fragment source is GLSL that defines main() and assigns gl_FragColor. It is
// compiled behind declarations for the varyings (v_worldPos, v_normal,
// v_texcoord0), the standard 3D uniforms, the material's samplers, and whatever
// the uniforms table declared -- so a snippet neither has to know nor can get
// wrong how any of those are spelled.
//
// The vertex stage stays the pipeline's, which is what lets an effect be set on a
// skinned model and still be skinned. See ShaderEffect3D.
int
RenderLibrary::newShaderEffect( lua_State *L )
{
	if ( !lua_istable( L, 1 ) )
	{
		return luaL_error( L, "render.newShaderEffect() requires a table of options" );
	}

	ShaderEffect3D* effect = new ShaderEffect3D;

	lua_getfield( L, 1, "name" );
	effect->SetName( lua_isstring( L, -1 ) ? lua_tostring( L, -1 ) : "unnamed" );
	lua_pop( L, 1 );

	lua_getfield( L, 1, "fragment" );

	if ( !lua_isstring( L, -1 ) )
	{
		lua_pop( L, 1 );
		effect->Release();

		return luaL_error( L, "render.newShaderEffect() requires 'fragment' as a string of GLSL defining main()" );
	}

	effect->SetFragmentSource( lua_tostring( L, -1 ) );
	lua_pop( L, 1 );

	lua_getfield( L, 1, "cull" );

	if ( lua_isstring( L, -1 ) )
	{
		const char* cull = lua_tostring( L, -1 );

		if ( strcmp( cull, "back" ) == 0 )
		{
			effect->SetCullMode( ShaderEffect3D::kCullBack );
		}
		else if ( strcmp( cull, "front" ) == 0 )
		{
			effect->SetCullMode( ShaderEffect3D::kCullFront );
		}
		else if ( strcmp( cull, "none" ) == 0 )
		{
			effect->SetCullMode( ShaderEffect3D::kCullNone );
		}
		else
		{
			CoronaLuaWarning( L, "render.newShaderEffect(): 'cull' must be \"back\", \"front\" or \"none\", not \"%s\"; using \"back\"", cull );
		}
	}

	lua_pop( L, 1 );

	lua_getfield( L, 1, "blend" );
	effect->SetTranslucent( lua_toboolean( L, -1 ) != 0 );
	lua_pop( L, 1 );

	lua_getfield( L, 1, "depthWrite" );

	// Absent means on, which is what opaque geometry wants; only an effect that
	// says otherwise gives it up.
	if ( !lua_isnil( L, -1 ) )
	{
		effect->SetWritesDepth( lua_toboolean( L, -1 ) != 0 );
	}

	lua_pop( L, 1 );

	lua_getfield( L, 1, "uniforms" );

	if ( lua_istable( L, -1 ) )
	{
		lua_pushnil( L );

		while ( lua_next( L, -2 ) != 0 )
		{
			const char* name = lua_tostring( L, -2 );

			if ( name != NULL )
			{
				float value[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

				if ( lua_istable( L, -1 ) )
				{
					for ( int i = 0; i < 4; ++i )
					{
						lua_rawgeti( L, -1, i + 1 );
						value[i] = (float) luaL_optnumber( L, -1, 0.0 );
						lua_pop( L, 1 );
					}
				}
				else
				{
					value[0] = (float) lua_tonumber( L, -1 );
				}

				if ( !effect->SetUniform( name, value ) )
				{
					CoronaLuaWarning( L, "render.newShaderEffect(): '%s' has more than %d uniforms; '%s' was not declared",
						effect->GetName().c_str(), (int) ShaderEffect3D::kMaxUniforms, name );
				}
			}

			lua_pop( L, 1 );
		}
	}

	lua_pop( L, 1 );

	LuaLibRender3D::PushShaderEffect( L, effect );

	// PushShaderEffect took a reference of its own; this one was the constructor's.
	effect->Release();

	return 1;
}

// ----------------------------------------------------------------------------

// Collects every 3D object in a group and its subgroups.
//
// The display list is walked rather than a registry being kept, so that what can
// be picked is exactly what is in the scene: an object removed with removeSelf,
// or one never added to the stage, is gone from both at once with nothing to keep
// in step.
static void
GatherObject3D( GroupObject& group, std::vector< Object3D* >& out )
{
	for ( S32 i = 0, iMax = group.NumChildren(); i < iMax; ++i )
	{
		DisplayObject& child = group.ChildAt( i );

		Object3D* object = child.AsObject3D();

		if ( object != NULL )
		{
			out.push_back( object );
		}

		GroupObject* childGroup = child.AsGroupObject();

		if ( childGroup != NULL )
		{
			GatherObject3D( *childGroup, out );
		}
	}
}

// Pushes one hit as { object, x, y, z, normal = { x, y, z }, distance, meshName }.
static void
PushHit( lua_State *L, const Raycast3DHit& hit )
{
	lua_createtable( L, 0, 7 );

	if ( hit.fObject != NULL )
	{
		// The Lua proxy for the display object, so the caller gets back the same
		// value they created rather than a new wrapper around it.
		hit.fObject->GetProxy()->PushTable( L );
		lua_setfield( L, -2, "object" );
	}

	lua_pushnumber( L, hit.fPoint[0] );
	lua_setfield( L, -2, "x" );
	lua_pushnumber( L, hit.fPoint[1] );
	lua_setfield( L, -2, "y" );
	lua_pushnumber( L, hit.fPoint[2] );
	lua_setfield( L, -2, "z" );

	lua_createtable( L, 0, 3 );
	lua_pushnumber( L, hit.fNormal[0] );
	lua_setfield( L, -2, "x" );
	lua_pushnumber( L, hit.fNormal[1] );
	lua_setfield( L, -2, "y" );
	lua_pushnumber( L, hit.fNormal[2] );
	lua_setfield( L, -2, "z" );
	lua_setfield( L, -2, "normal" );

	lua_pushnumber( L, hit.fDistance );
	lua_setfield( L, -2, "distance" );

	if ( hit.fMeshName != NULL && *hit.fMeshName != '\0' )
	{
		lua_pushstring( L, hit.fMeshName );
		lua_setfield( L, -2, "meshName" );
	}
}

// render.raycast( x, y [, options] )
// render.raycast( { from = { x, y, z }, direction = { x, y, z } } [, options] )
// render.raycast( { from = { x, y, z }, to = { x, y, z } } [, options] )
//
// The first form casts through the active camera from a point in content
// coordinates, which is what a tap gives -- so `render.raycast(event.x, event.y)`
// is the whole of picking.
//
// Returns nil when nothing was hit, the nearest hit otherwise, or an array of
// every hit sorted by distance when options.all is true. nil rather than an empty
// table, so that `if render.raycast(x, y) then` reads correctly.
int
RenderLibrary::raycast( lua_State *L )
{
	Self *library = ToLibrary( L );
	Display& display = library->GetDisplay();

	float origin[3] = { 0.0f, 0.0f, 0.0f };
	float direction[3] = { 0.0f, 0.0f, 1.0f };

	int optionsIndex = 3;

	if ( lua_istable( L, 1 ) )
	{
		optionsIndex = 2;

		lua_getfield( L, 1, "from" );

		if ( !lua_istable( L, -1 ) )
		{
			lua_pop( L, 1 );

			return luaL_error( L, "render.raycast(): a table form needs 'from' as { x, y, z }" );
		}

		for ( int i = 0; i < 3; ++i )
		{
			lua_rawgeti( L, -1, i + 1 );
			origin[i] = (float) luaL_optnumber( L, -1, 0.0 );
			lua_pop( L, 1 );
		}

		lua_pop( L, 1 );

		lua_getfield( L, 1, "direction" );

		bool haveDirection = lua_istable( L, -1 ) != 0;

		if ( haveDirection )
		{
			for ( int i = 0; i < 3; ++i )
			{
				lua_rawgeti( L, -1, i + 1 );
				direction[i] = (float) luaL_optnumber( L, -1, 0.0 );
				lua_pop( L, 1 );
			}
		}

		lua_pop( L, 1 );

		if ( !haveDirection )
		{
			// 'to' is the same ray said the other way, and is what a caster
			// aiming at a known point already has.
			lua_getfield( L, 1, "to" );

			if ( !lua_istable( L, -1 ) )
			{
				lua_pop( L, 1 );

				return luaL_error( L, "render.raycast(): a table form needs either 'direction' or 'to'" );
			}

			for ( int i = 0; i < 3; ++i )
			{
				lua_rawgeti( L, -1, i + 1 );
				direction[i] = (float) luaL_optnumber( L, -1, 0.0 ) - origin[i];
				lua_pop( L, 1 );
			}

			lua_pop( L, 1 );
		}

		// Normalised so that the distance a hit reports is in world units, which
		// is the only reading of "distance" that survives the caller scaling the
		// vector they passed.
		const float length = std::sqrt(
			direction[0] * direction[0] + direction[1] * direction[1] + direction[2] * direction[2] );

		if ( length <= 0.0f )
		{
			return luaL_error( L, "render.raycast(): the ray has no direction" );
		}

		direction[0] /= length;
		direction[1] /= length;
		direction[2] /= length;
	}
	else
	{
		Camera3D* camera = display.GetScene3D().GetActiveCamera();

		if ( camera == NULL )
		{
			// Without a camera there is no way to turn a screen point into a ray.
			// A warning and nil rather than an error: a project that taps before
			// its camera exists is mid-setup, not broken.
			CoronaLuaWarning( L, "render.raycast() needs an active camera to cast from a screen point; set one with render.setActiveCamera()" );

			return 0;
		}

		const float x = (float) luaL_checknumber( L, 1 );
		const float y = (float) luaL_checknumber( L, 2 );

		// The content extent actually drawn, which is what the projection's
		// viewport covers. Content and pixels coincide unless the project scales
		// or letterboxes, and where they differ this is the pair that matches
		// what the camera projected into.
		const float width = (float) display.RenderedContentWidth();
		const float height = (float) display.RenderedContentHeight();

		if ( width <= 0.0f || height <= 0.0f )
		{
			return 0;
		}

		// Content coordinates to normalised device ones. The y is inverted
		// because Corona's grows downwards from the top and the projection's
		// grows upwards from the centre.
		const float ndcX = ( x / width ) * 2.0f - 1.0f;
		const float ndcY = 1.0f - ( y / height ) * 2.0f;

		camera->GetRay( ndcX, ndcY, width / height, origin, direction );
	}

	bool wantsAll = false;

	if ( lua_istable( L, optionsIndex ) )
	{
		lua_getfield( L, optionsIndex, "all" );
		wantsAll = lua_toboolean( L, -1 ) != 0;
		lua_pop( L, 1 );
	}

	StageObject* stage = display.GetStage();

	if ( stage == NULL )
	{
		return 0;
	}

	std::vector< Object3D* > objects;
	GatherObject3D( *stage, objects );

	std::vector< Raycast3DHit > hits;

	for ( size_t i = 0, iMax = objects.size(); i < iMax; ++i )
	{
		Raycast3DHit hit;
		memset( &hit, 0, sizeof( hit ) );

		if ( objects[i]->Raycast( origin, direction, hit ) )
		{
			hits.push_back( hit );
		}
	}

	if ( hits.empty() )
	{
		return 0;
	}

	if ( !wantsAll )
	{
		size_t nearest = 0;

		for ( size_t i = 1, iMax = hits.size(); i < iMax; ++i )
		{
			if ( hits[i].fDistance < hits[nearest].fDistance )
			{
				nearest = i;
			}
		}

		PushHit( L, hits[nearest] );

		return 1;
	}

	// Insertion sort: the list is one entry per object the ray met, which is a
	// handful even in a busy scene.
	for ( size_t i = 1, iMax = hits.size(); i < iMax; ++i )
	{
		Raycast3DHit moving = hits[i];
		size_t j = i;

		while ( j > 0 && hits[j - 1].fDistance > moving.fDistance )
		{
			hits[j] = hits[j - 1];
			--j;
		}

		hits[j] = moving;
	}

	lua_createtable( L, (int) hits.size(), 0 );

	for ( size_t i = 0, iMax = hits.size(); i < iMax; ++i )
	{
		PushHit( L, hits[i] );
		lua_rawseti( L, -2, (int) i + 1 );
	}

	return 1;
}

// ----------------------------------------------------------------------------

// render.setEnvironmentMap( path [, baseDirectory] [, options] )
// render.setEnvironmentMap( nil )
//
// The image is equirectangular -- the projection every HDRI and sky photograph
// comes in, twice as wide as it is tall. Setting one lights the scene from it and
// reflects it; passing nil removes it and returns the scene to the flat ambient
// term.
//
// options.intensity scales its contribution, so a map can be dimmed without
// being reprojected.
//
// The environment replaces the ambient term rather than adding to it: both stand
// for light arriving from everywhere that is not one of the lights.
int
RenderLibrary::setEnvironmentMap( lua_State *L )
{
	Self *library = ToLibrary( L );
	Display& display = library->GetDisplay();

	if ( lua_isnoneornil( L, 1 ) )
	{
		display.GetScene3D().SetEnvironmentMap( NULL );

		return 0;
	}

	const char* filename = lua_tostring( L, 1 );

	if ( filename == NULL )
	{
		return luaL_error( L, "render.setEnvironmentMap() takes a file path, or nil to remove the current one" );
	}

	MPlatform::Directory baseDir = MPlatform::kResourceDir;

	int optionsIndex = 2;

	if ( !lua_isnoneornil( L, 2 ) && !lua_istable( L, 2 ) )
	{
		baseDir = LuaLibSystem::ToDirectory( L, 2 );
		optionsIndex = 3;
	}

	String path( display.GetAllocator() );

	display.GetRuntime().Platform().PathForFile( filename, baseDir, MPlatform::kTestFileExists, path );

	if ( path.GetString() == NULL )
	{
		CoronaLuaWarning( L, "render.setEnvironmentMap() could not find '%s'", filename );

		return 0;
	}

	Texture3D* texture = Texture3D::NewFromFile( path.GetString() );

	if ( texture == NULL )
	{
		// NewFromFile has already said what was wrong with the image.
		return 0;
	}

	// An equirectangular map is twice as wide as it is tall. A square or portrait
	// image is almost always a cubemap cross or a mistake, and either way what
	// comes out is not the sky the caller expected -- so it is said once here
	// rather than left to look like a bug in the lighting.
	if ( texture->GetHeight() > 0 && texture->GetWidth() != texture->GetHeight() * 2 )
	{
		CoronaLuaWarning( L, "render.setEnvironmentMap(): '%s' is %dx%d; an equirectangular map should be twice as wide as it is tall, and this one will be stretched",
			filename, (int) texture->GetWidth(), (int) texture->GetHeight() );
	}

	display.GetScene3D().SetEnvironmentMap( texture );

	// The scene took its own reference; this one was the loader's.
	texture->Release();

	if ( lua_istable( L, optionsIndex ) )
	{
		lua_getfield( L, optionsIndex, "intensity" );

		if ( !lua_isnil( L, -1 ) )
		{
			display.GetScene3D().SetEnvironmentIntensity( (float) lua_tonumber( L, -1 ) );
		}

		lua_pop( L, 1 );
	}

	return 0;
}

int
RenderLibrary::setAmbientLight( lua_State *L )
{
	Self *library = ToLibrary( L );

	library->GetDisplay().GetScene3D().SetAmbient(
		(float) luaL_optnumber( L, 1, 0.03 ),
		(float) luaL_optnumber( L, 2, 0.03 ),
		(float) luaL_optnumber( L, 3, 0.03 ) );

	return 0;
}

// Registered for everything in the spec that has no implementation yet, so that
// calling one fails where it was called with the reason, rather than as a nil
// index somewhere further along.
int
RenderLibrary::notImplemented( lua_State *L )
{
	return luaL_error( L, "render.%s is not implemented yet", lua_tostring( L, lua_upvalueindex( 2 ) ) );
}

// ----------------------------------------------------------------------------

int
RenderLibrary::Open( lua_State *L )
{
	Display *display = (Display *)lua_touserdata( L, lua_upvalueindex( 1 ) );
	Rtt_ASSERT( display );

	const char kMetatableName[] = __FILE__;
	CoronaLuaInitializeGCMetatable( L, kMetatableName, Finalizer );

	// The camera and material metatables, created once per Lua state.
	luaL_newmetatable( L, kCameraMetatable );
	lua_pushcfunction( L, CameraIndex );
	lua_setfield( L, -2, "__index" );
	lua_pushcfunction( L, CameraNewIndex );
	lua_setfield( L, -2, "__newindex" );
	lua_pushcfunction( L, CameraFinalizer );
	lua_setfield( L, -2, "__gc" );
	lua_pop( L, 1 );

	luaL_newmetatable( L, kMaterialMetatable );
	lua_pushcfunction( L, MaterialIndex );
	lua_setfield( L, -2, "__index" );
	lua_pushcfunction( L, MaterialNewIndex );
	lua_setfield( L, -2, "__newindex" );
	lua_pushcfunction( L, MaterialFinalizer );
	lua_setfield( L, -2, "__gc" );
	lua_pop( L, 1 );

	luaL_newmetatable( L, kEffectMetatable );
	lua_pushcfunction( L, EffectIndex );
	lua_setfield( L, -2, "__index" );
	lua_pushcfunction( L, EffectNewIndex );
	lua_setfield( L, -2, "__newindex" );
	lua_pushcfunction( L, EffectFinalizer );
	lua_setfield( L, -2, "__gc" );
	lua_pop( L, 1 );

	luaL_newmetatable( L, kModelPartMetatable );
	lua_pushcfunction( L, ModelPartIndex );
	lua_setfield( L, -2, "__index" );
	lua_pushcfunction( L, ModelPartNewIndex );
	lua_setfield( L, -2, "__newindex" );
	lua_pushcfunction( L, ModelPartFinalizer );
	lua_setfield( L, -2, "__gc" );
	lua_pop( L, 1 );

	const luaL_Reg kVTable[] =
	{
		{ "newBox", newBox },
		{ "newSphere", newSphere },
		{ "newPlane", newPlane },
		{ "newCylinder", newCylinder },

		{ "newCamera", newCamera },
		{ "setActiveCamera", setActiveCamera },

		{ "newDirectionalLight", newDirectionalLight },
		{ "newPointLight", newPointLight },
		{ "newSpotLight", newSpotLight },

		{ "newMaterial", newMaterial },
		{ "newModel", newModel },
		{ "newCustomMesh", newCustomMesh },
		{ "newShaderEffect", newShaderEffect },
		{ "raycast", raycast },
		{ "setAmbientLight", setAmbientLight },
		{ "setEnvironmentMap", setEnvironmentMap },

		{ NULL, NULL }
	};

	Self *library = new Self( * display );

	CoronaLuaPushUserdata( L, library, kMetatableName );
	lua_pushstring( L, kMetatableName );
	lua_settable( L, LUA_REGISTRYINDEX );

	int result = CoronaLibraryNew( L, kName, "com.coronalabs", 1, 1, kVTable, library );

	// The rest of the spec, present and failing loudly. Each closure carries the
	// library as its first upvalue -- matching everything else here -- and its
	// own name as the second, so one function can name whichever was called.
	static const char* kNotImplemented[] =
	{
		NULL
	};

	for ( int i = 0; kNotImplemented[i] != NULL; ++i )
	{
		lua_pushlightuserdata( L, library );
		lua_pushstring( L, kNotImplemented[i] );
		lua_pushcclosure( L, notImplemented, 2 );
		lua_setfield( L, -2, kNotImplemented[i] );
	}

	return result;
}

// ----------------------------------------------------------------------------

void
LuaLibRender3D::Initialize( lua_State *L, Display& display )
{
	Rtt_LUA_STACK_GUARD( L );

	lua_pushlightuserdata( L, & display );
	CoronaLuaRegisterModuleLoader( L, RenderLibrary::kName, RenderLibrary::Open, 1 );

	CoronaLuaPushModule( L, RenderLibrary::kName );
	lua_setglobal( L, RenderLibrary::kName ); // render = library
}

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------
