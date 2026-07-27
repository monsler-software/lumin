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

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------
