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

		U32 fStride;
		U32 fGroupCount;
		bool fInstancedByID;

		struct Group
		{
			U32 fOffset; // into an instance's block
			U32 fSize;
		}
		fGroups[kMaxGroups];
	};

	InstanceLayout fInstanceLayout;

	// Instancing, gathered before a draw and consumed by it. The data itself is
	// Corona's, and lives until the draw is submitted.
	const Geometry::Vertex* fInstanceData;
	U32 fInstanceCount;

	U64 fBlendState;
	bool fBlendEnabled;
	bool fScissorEnabled;
};

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------

#endif // _Rtt_BgfxDrawState_H__
