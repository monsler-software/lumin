//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Corona game engine.
// For overview and more information on licensing please refer to README.md
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////

#ifndef _Rtt_BgfxFrameBufferObject_H__
#define _Rtt_BgfxFrameBufferObject_H__

#include "Renderer/Rtt_GPUResource.h"

#include <bgfx/bgfx.h>

// ----------------------------------------------------------------------------

namespace Rtt
{

// ----------------------------------------------------------------------------

class BgfxFrameBufferObject : public GPUResource
{
	public:
		typedef GPUResource Super;
		typedef BgfxFrameBufferObject Self;

	public:
		BgfxFrameBufferObject();

		virtual void Create( CPUResource* resource );
		virtual void Update( CPUResource* resource );
		virtual void Destroy();

		bgfx::FrameBufferHandle GetFrameBuffer() const { return fFrameBuffer; }

		// bgfx addresses render targets by view rather than by binding a
		// framebuffer, so each of these owns a view id for the duration it is
		// bound. Assigned by BgfxCommandBuffer, which owns the view range.
		bgfx::ViewId GetViewId() const { return fViewId; }
		void SetViewId( bgfx::ViewId viewId ) { fViewId = viewId; }

	private:
		bgfx::FrameBufferHandle fFrameBuffer;
		bgfx::ViewId fViewId;

		// Corona can ask a framebuffer for depth and stencil bits (see
		// FrameBufferObject::ExtraOptions). Unlike the colour attachment, which
		// is a texture Corona owns, this one is created and destroyed here.
		bgfx::TextureHandle fDepthStencil;
};

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------

#endif // _Rtt_BgfxFrameBufferObject_H__
