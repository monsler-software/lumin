//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Corona game engine.
// For overview and more information on licensing please refer to README.md 
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////

#include "Core/Rtt_Build.h"

#include "Display/Rtt_ShapeAdapterRoundedRect.h"

#include "Core/Rtt_StringHash.h"
#include "Display/Rtt_ShapeObject.h"
#include "Display/Rtt_ShapePath.h"
#include "Display/Rtt_TesselatorRoundedRect.h"
#include "Rtt_LuaContext.h"

// ----------------------------------------------------------------------------

namespace Rtt
{

// ----------------------------------------------------------------------------

const ShapeAdapterRoundedRect&
ShapeAdapterRoundedRect::Constant()
{
	static const ShapeAdapterRoundedRect sAdapter;
	return sAdapter;
}

// ----------------------------------------------------------------------------

ShapeAdapterRoundedRect::ShapeAdapterRoundedRect()
:	Super( kRoundedRectType )
{
}

static void
InvalidateRoundedRectPath( ShapePath *path )
{
	path->Invalidate(
		ClosedPath::kFillSource | ClosedPath::kFillSourceTexture |
		ClosedPath::kStrokeSource | ClosedPath::kStrokeSourceTexture );
	path->GetObserver()->Invalidate( DisplayObject::kGeometryFlag | DisplayObject::kStageBoundsFlag );
}

StringHash *
ShapeAdapterRoundedRect::GetHash( lua_State *L ) const
{
	static const char *keys[] = 
	{
		"width",				// 0
		"height",				// 1
		"radius",				// 2
		"radiusTopLeft",		// 3
		"radiusTopRight",		// 4
		"radiusBottomRight",	// 5
		"radiusBottomLeft",		// 6
	};
	static StringHash sHash( *LuaContext::GetAllocator( L ), keys, sizeof( keys ) / sizeof( const char * ), 7, 0, 1, __FILE__, __LINE__ );
	return &sHash;
}

int
ShapeAdapterRoundedRect::ValueForKey(
	const LuaUserdataProxy& sender,
	lua_State *L,
	const char *key ) const
{
	int result = 0;

	Rtt_ASSERT( key ); // Caller should check at the top-most level

	const ShapePath *path = (const ShapePath *)sender.GetUserdata();
	if ( ! path ) { return result; }

	const TesselatorRoundedRect *tesselator =
		static_cast< const TesselatorRoundedRect * >( path->GetTesselator() );
	if ( ! tesselator ) { return result; }

	result = 1; // Assume 1 Lua value will be pushed on the stack

	int index = GetHash( L )->Lookup( key );
	switch ( index )
	{
		case 0:
			lua_pushnumber( L, tesselator->GetWidth() );
			break;
		case 1:
			lua_pushnumber( L, tesselator->GetHeight() );
			break;
		case 2:
			lua_pushnumber( L, tesselator->GetRadius() );
			break;
		case 3:
			lua_pushnumber( L, tesselator->GetCornerRadius( TesselatorRoundedRect::kTopLeftCorner ) );
			break;
		case 4:
			lua_pushnumber( L, tesselator->GetCornerRadius( TesselatorRoundedRect::kTopRightCorner ) );
			break;
		case 5:
			lua_pushnumber( L, tesselator->GetCornerRadius( TesselatorRoundedRect::kBottomRightCorner ) );
			break;
		case 6:
			lua_pushnumber( L, tesselator->GetCornerRadius( TesselatorRoundedRect::kBottomLeftCorner ) );
			break;
		default:
			result = Super::ValueForKey( sender, L, key );
			break;
	}

	return result;
}

bool
ShapeAdapterRoundedRect::SetValueForKey(
	LuaUserdataProxy& sender,
	lua_State *L,
	const char *key,
	int valueIndex ) const
{
	bool result = false;

	Rtt_ASSERT( key ); // Caller should check at the top-most level

	ShapePath *path = (ShapePath *)sender.GetUserdata();
	if ( ! path ) { return result; }

	TesselatorRoundedRect *tesselator =
		static_cast< TesselatorRoundedRect * >( path->GetTesselator() );
	if ( ! tesselator ) { return result; }

	result = true; // Assume value will be set

	int index = GetHash( L )->Lookup( key );
	switch ( index )
	{
		case 0:
			{
				Real newValue = luaL_toreal( L, valueIndex );
				tesselator->SetWidth( newValue );
				InvalidateRoundedRectPath( path );
			}
			break;
		case 1:
			{
				Real newValue = luaL_toreal( L, valueIndex );
				tesselator->SetHeight( newValue );
				InvalidateRoundedRectPath( path );
			}
			break;
		case 2:
			{
				Real maxRadius = Min( tesselator->GetWidth(), tesselator->GetHeight() );
				maxRadius = Rtt_RealDiv2( maxRadius );

				Real radius = luaL_toreal( L, valueIndex );
				radius = Clamp( radius, 0.f, maxRadius );

				tesselator->SetRadius( radius );
				InvalidateRoundedRectPath( path );
			}
			break;
		case 3:
		case 4:
		case 5:
		case 6:
			{
				Real maxRadius = Min( tesselator->GetWidth(), tesselator->GetHeight() );
				maxRadius = Rtt_RealDiv2( maxRadius );

				Real radius = luaL_toreal( L, valueIndex );
				radius = Clamp( radius, 0.f, maxRadius );

				TesselatorRoundedRect::Corner corner = (TesselatorRoundedRect::Corner)( index - 3 );
				tesselator->SetCornerRadius( corner, radius );
				InvalidateRoundedRectPath( path );
			}
			break;
		default:
			result = Super::SetValueForKey( sender, L, key, valueIndex );
			break;
	}

	return result;
}

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------

