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

class FormatExtensionList;

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

		// The same, plus whatever per-vertex extension attributes an effect
		// declared. Corona interleaves those after each vertex rather than
		// giving them a stream of their own (see Renderer::CopyExtendedVertexData
		// and Geometry::ExtensionBlock::UpdateData), so they become attributes
		// of this layout at a larger stride.
		//
		// `list` is the reconciled list Corona hands to BindVertexFormat, whose
		// attributes are in the order the effect declared them: that is what
		// decides the slot each one lands on, and BgfxProgram names them the
		// same way. `vertexSize` is the full stride, extensions included.
		static void BuildVertexLayout( bgfx::VertexLayout& layout, const FormatExtensionList* list, U32 vertexSize );

		// How many per-vertex extension attributes the slots left over by the
		// stock vertex and by bgfx's instance data can carry.
		enum { kMaxVertexExtensionAttributes = 5 };

		virtual void Create( CPUResource* resource );
		virtual void Update( CPUResource* resource );
		virtual void Destroy();

		// Builds the buffers for geometry Corona keeps on the GPU, at the layout
		// the draw about to happen needs. Geometry carrying per-vertex extension
		// data cannot be built by Create(), which runs before any effect has
		// said what that data looks like; this is called from the draw instead,
		// and rebuilds if the layout has changed since.
		void EnsureBuffers( Geometry* geometry, const bgfx::VertexLayout& layout );

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

		// What the buffers were built for: bgfx fixes a buffer's layout and size
		// when it is created, so a change in either means a new buffer.
		U32 fLayoutHash;
		U32 fVertexCapacity;
		U32 fIndexCapacity;
};

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------

#endif // _Rtt_BgfxGeometry_H__
