//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Corona game engine.
// For overview and more information on licensing please refer to README.md
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////

#ifndef _Rtt_BgfxDrawState_H__
#define _Rtt_BgfxDrawState_H__

#include "Renderer/Rtt_Geometry_Renderer.h"
#include "Renderer/Rtt_Program.h"
#include "Renderer/Rtt_Texture.h"
#include "Renderer/Rtt_Uniform.h"

#include <bgfx/bgfx.h>

// ----------------------------------------------------------------------------

namespace Rtt
{

class BgfxGeometry;
class BgfxProgram;
class FormatExtensionList;

// ----------------------------------------------------------------------------

// What a bgfx submit() has to be told, accumulated by Corona's Bind*/Set*
// calls and consumed by the next draw. bgfx keeps no binding state of its own:
// every submit() carries its own, and whatever is not set is dropped, so this
// remembers the last of everything and re-sends it per draw.
//
// It belongs to the renderer rather than to a command buffer because Corona
// has two command buffers and alternates between them every frame, while the
// state that decides what to re-bind -- Renderer::fPrevious and the uniform
// timestamps -- is shared by both. A value that changes on one frame is only
// handed to the buffer recording that frame, so per-buffer copies would leave
// the other one a frame behind: with the view-projection matrix, that showed
// up as the image flipping upside down on alternate frames.
struct BgfxDrawState
{
	BgfxDrawState();

	void Reset();

	// View 0 is the window; render-to-texture takes the ids above it.
	bgfx::ViewId fCurrentView;

	// Corona treats the viewport and the scissor as context state, set once and
	// left alone; in bgfx they belong to a view, and a view is switched every
	// time a render target is bound. So they are remembered here and re-applied
	// on every switch -- Corona restores them *before* it binds the previous
	// framebuffer back (see Shader::Draw), which without this would leave them
	// on the view being left behind and the one arrived at with none at all.
	S32 fViewport[4];
	S32 fScissor[4];

	BgfxGeometry* fGeometry;

	// Geometry Corona refills every frame owns no bgfx buffers, so the vertex
	// data itself is needed at draw time; see BgfxCommandBuffer::SubmitDraw.
	Geometry* fGeometrySource;

	BgfxProgram* fProgram;
	Program::Version fProgramVersion;

	Uniform* fBoundUniforms[Uniform::kNumBuiltInVariables];
	Texture* fBoundTextures[Texture::kNumUnits];

	// How one instance's data is laid out in the buffer handed to bgfx.
	//
	// This is a copy rather than a pointer to the format list Corona supplies:
	// that list is a temporary built on the stack for the call (see
	// FormatExtensionList::ReconcileFormats), and the GL backend, which copies
	// it into its command stream, is what makes that safe there.
	struct InstanceLayout
	{
		enum { kMaxGroups = 8 };

		U32 fGroupCount;

		struct Group
		{
			U32 fOffset; // into an instance's block
			U32 fSize;   // bytes one instance takes from this group

			// A group's values are shared by `fDivisor` consecutive instances,
			// so instance i reads value i / fDivisor.
			U32 fDivisor;

			// A "windowed" group gives each instance a sliding run of fCount
			// values from one array rather than a block of its own: instance i
			// sees values i, i + 1, ... i + fCount - 1, each fAttributeSize
			// bytes. The values overlap, so they are expanded per instance when
			// the buffer is filled.
			bool fWindowed;
			U32 fCount;
			U32 fAttributeSize;

			// Where this group's values start in the run Corona hands over
			// through BindInstancing, which packs one group after another with
			// each padded up to a whole Geometry::Vertex.
			U32 fSourceOffset;

			// How many values the source holds, so a short array cannot be read
			// past the end.
			U32 fSourceValueCount;
		}
		fGroups[kMaxGroups];
	};

	InstanceLayout fInstanceLayout;

	// The vertex stream a draw reads from, which is Geometry::Vertex on its own
	// unless the effect declared per-vertex extension attributes: those are
	// interleaved after each vertex (see Renderer::CopyExtendedVertexData), so
	// the stride grows and the layout gains their attributes.
	//
	// Built by BindVertexFormat, which is also where Corona says how far into
	// the geometry pool the format it just described begins -- a draw's own
	// offset counts from there.
	bgfx::VertexLayout fVertexLayout;
	U32 fVertexStride;
	U32 fVertexBaseOffset; // in whole Geometry::Vertex units

	// Instancing, gathered before a draw and consumed by it. The data itself is
	// Corona's, and lives until the draw is submitted.
	const Geometry::Vertex* fInstanceData;
	U32 fInstanceCount;

	U64 fBlendState;
	bool fBlendEnabled;
	bool fScissorEnabled;
	bool fMultisampleEnabled;

	// A bgfx view is cleared by one setViewClear, so the depth and stencil
	// clears Corona issues just before a colour clear (see Renderer::Clear) are
	// gathered here and applied together. Anything still pending when the frame
	// ends belongs to a clear that never named a colour, and is flushed then.
	U16 fPendingClearFlags;
	float fPendingClearDepth;
	U8 fPendingClearStencil;

	// Depth and stencil test state, as bgfx state bits. Corona's own drawing
	// never turns these on -- it has no depth to speak of -- but a framebuffer
	// can be asked for a depth or stencil attachment, and a native plugin can
	// drive them through a custom command.
	U64 fDepthState;
	U32 fStencilFront;
	U32 fStencilBack;
};

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------

#endif // _Rtt_BgfxDrawState_H__
