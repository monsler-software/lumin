//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Corona game engine.
// For overview and more information on licensing please refer to README.md
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////

#ifndef _Rtt_BgfxSurfaceParams_H__
#define _Rtt_BgfxSurfaceParams_H__

#include "Core/Rtt_Types.h"

struct Rtt_Allocator;

// ----------------------------------------------------------------------------

namespace Rtt
{

// ----------------------------------------------------------------------------

// What a platform must hand over for bgfx to reach the window.
//
// Corona's existing surfaces create a graphics context themselves and give the
// renderer that; bgfx instead wants the window and makes its own context. The
// platform fills this in and passes it to Runtime::SetBackend.
//
// This lives apart from Rtt_BgfxRenderer.h on purpose: platform code needs the
// struct but not bgfx itself, and bgfx's headers require C++20 while the engine
// builds as C++17. Nothing here may include a bgfx header.
struct BgfxSurfaceParams
{
	BgfxSurfaceParams()
	:	fNativeWindowHandle( NULL ),
		fNativeDisplayType( NULL ),
		fWidth( 0 ),
		fHeight( 0 ),
		fIsWayland( false )
	{
	}

	void* fNativeWindowHandle;  // X11 Window, HWND, NSWindow, ANativeWindow
	void* fNativeDisplayType;   // X11 Display or Wayland display; NULL elsewhere
	U32 fWidth;
	U32 fHeight;

	// Linux has two window systems and the handles above cannot be told apart
	// by inspection, so the platform has to say which it filled in. bgfx
	// otherwise assumes X11 and tries to make an Xlib surface out of a Wayland
	// surface, which does not fail gracefully.
	bool fIsWayland;
};

// Creates the bgfx renderer without dragging bgfx's headers, and their C++20
// requirement, into the caller. Mirrors what VulkanExports does for Vulkan.
class Renderer;

struct BgfxExports
{
	static Renderer* CreateBgfxRenderer( Rtt_Allocator* allocator, const BgfxSurfaceParams& params );
};

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------

#endif // _Rtt_BgfxSurfaceParams_H__
