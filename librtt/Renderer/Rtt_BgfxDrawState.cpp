//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Corona game engine.
// For overview and more information on licensing please refer to README.md
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////

#include "Renderer/Rtt_BgfxDrawState.h"

#include <string.h>

// ----------------------------------------------------------------------------

namespace Rtt
{

// ----------------------------------------------------------------------------

BgfxDrawState::BgfxDrawState()
{
	Reset();
}

void
BgfxDrawState::Reset()
{
	fCurrentView = 0;
	fGeometry = NULL;
	fGeometrySource = NULL;
	fProgram = NULL;
	fProgramVersion = Program::kMaskCount0;
	memset( &fInstanceLayout, 0, sizeof( fInstanceLayout ) );

	fInstanceData = NULL;
	fInstanceCount = 0;
	fBlendState = 0;
	fBlendEnabled = false;
	fScissorEnabled = false;
	fMultisampleEnabled = false;

	fVertexStride = sizeof( Geometry::Vertex );
	fVertexBaseOffset = 0;

	fPendingClearFlags = 0;
	fPendingClearDepth = 1.0f;
	fPendingClearStencil = 0;

	fDepthState = 0; // no depth test, no depth write
	fStencilFront = BGFX_STENCIL_NONE;
	fStencilBack = BGFX_STENCIL_NONE;

	for ( U32 i = 0; i < 4; ++i )
	{
		fViewport[i] = 0;
		fScissor[i] = 0;
	}

	for ( U32 i = 0; i < Uniform::kNumBuiltInVariables; ++i )
	{
		fBoundUniforms[i] = NULL;
	}

	for ( U32 i = 0; i < Texture::kNumUnits; ++i )
	{
		fBoundTextures[i] = NULL;
	}
}

// Drops everything remembered that belongs to a display object rather than to
// the renderer, which is everything whose lifetime this state does not control.
//
// Corona binds those only on the frame their value changes and never unbinds
// them, so a slot left alone outlives the object it came from: the mask matrix
// of a masked image, say, is still remembered -- and re-sent by every draw, see
// BgfxCommandBuffer::SubmitDraw -- after the image has been removed and its
// Uniform freed. Left in, that reads freed memory on every frame from then on.
//
// Dropping them per frame is safe because Renderer::BeginFrame clears the
// fPrevious it decides "changed" against, so a frame rebinds whatever its own
// draws need before the first of them. What is left behind is the renderer's
// own long-lived uniforms -- the time, the content scale, the texel size and
// the view-projection matrix -- which no display object owns and which are not
// all rebound every frame.
void
BgfxDrawState::ReleaseObjectBindings()
{
	fBoundUniforms[Uniform::kMaskMatrix0] = NULL;
	fBoundUniforms[Uniform::kMaskMatrix1] = NULL;
	fBoundUniforms[Uniform::kMaskMatrix2] = NULL;
	fBoundUniforms[Uniform::kUserData0] = NULL;
	fBoundUniforms[Uniform::kUserData1] = NULL;
	fBoundUniforms[Uniform::kUserData2] = NULL;
	fBoundUniforms[Uniform::kUserData3] = NULL;

	for ( U32 i = 0; i < Texture::kNumUnits; ++i )
	{
		fBoundTextures[i] = NULL;
	}

	fGeometry = NULL;
	fGeometrySource = NULL;
	fProgram = NULL;

	fInstanceData = NULL;
	fInstanceCount = 0;
}

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------
