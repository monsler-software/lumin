//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Corona game engine.
// For overview and more information on licensing please refer to README.md
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////

#ifndef _Rtt_BgfxGeometry_H__
#define _Rtt_BgfxGeometry_H__

#include "Renderer/Rtt_GPUResource.h"
#include "Renderer/Rtt_Geometry_Renderer.h"

#include <bgfx/bgfx.h>

// ----------------------------------------------------------------------------

namespace Rtt
{

// ----------------------------------------------------------------------------

class BgfxGeometry : public GPUResource
{
	public:
		typedef GPUResource Super;
		typedef BgfxGeometry Self;

	public:
		BgfxGeometry();

		// The layout of Geometry::Vertex, which every Corona kernel consumes:
		// position (3 floats), texture coordinate (3 floats), colour scale
		// (4 normalized bytes) and user data (4 floats). Built once, on the
		// first call, because bgfx layouts depend on the renderer being up.
		static const bgfx::VertexLayout& VertexLayout();

		virtual void Create( CPUResource* resource );
		virtual void Update( CPUResource* resource );
		virtual void Destroy();

		// Geometry that Corona keeps on the GPU owns bgfx buffers, which these
		// expose. Geometry that does not is uploaded per frame through bgfx's
		// transient buffers instead, and reports invalid handles here; see
		// BgfxCommandBuffer, which does that upload at draw time.
		bool StoredOnGPU() const { return bgfx::isValid( fVertexBuffer ); }

		bgfx::DynamicVertexBufferHandle GetVertexBuffer() const { return fVertexBuffer; }
		bgfx::DynamicIndexBufferHandle GetIndexBuffer() const { return fIndexBuffer; }

	private:
		bgfx::DynamicVertexBufferHandle fVertexBuffer;
		bgfx::DynamicIndexBufferHandle fIndexBuffer;
};

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------

#endif // _Rtt_BgfxGeometry_H__
