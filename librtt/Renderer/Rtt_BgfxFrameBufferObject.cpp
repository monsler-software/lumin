//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Corona game engine.
// For overview and more information on licensing please refer to README.md
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////

#include "Renderer/Rtt_BgfxFrameBufferObject.h"

#include "Renderer/Rtt_BgfxTexture.h"
#include "Renderer/Rtt_FrameBufferObject.h"
#include "Renderer/Rtt_Texture.h"

#include "Core/Rtt_Assert.h"

// ----------------------------------------------------------------------------

namespace Rtt
{

// ----------------------------------------------------------------------------

BgfxFrameBufferObject::BgfxFrameBufferObject()
:	fFrameBuffer( BGFX_INVALID_HANDLE ),
	fViewId( 0 )
{
}

void
BgfxFrameBufferObject::Create( CPUResource* resource )
{
	Rtt_ASSERT( CPUResource::kFrameBufferObject == resource->GetType() );
	FrameBufferObject* fbo = static_cast< FrameBufferObject* >( resource );

	// See the note in BgfxTexture::Create: this may already have been built on
	// demand before Corona's create queue got to it.
	if ( bgfx::isValid( fFrameBuffer ) )
	{
		return;
	}

	Texture* texture = fbo->GetTexture();

	if ( !texture )
	{
		return;
	}

	// The texture Corona wants rendered into already exists as a GPU resource;
	// wrapping it here is what makes the result readable as a texture
	// afterwards, which is the whole point of a Corona snapshot or canvas.
	BgfxTexture* bgfxTexture = static_cast< BgfxTexture* >( texture->GetGPUResource() );

	if ( !bgfxTexture )
	{
		return;
	}

	// bgfx only accepts an attachment that was created as a render target, and
	// Corona created this one as an ordinary texture.
	bgfxTexture->AddFlags( BGFX_TEXTURE_RT );

	if ( !bgfx::isValid( bgfxTexture->GetTexture() ) )
	{
		return;
	}

	// false: the texture outlives this framebuffer and is owned by Corona.
	bgfx::TextureHandle attachment = bgfxTexture->GetTexture();

	fFrameBuffer = bgfx::createFrameBuffer( 1, &attachment, false );

	// Depth and stencil attachments are not wired up yet. Corona asks for them
	// through FrameBufferObject::ExtraOptions for object masking and for the
	// depth/stencil work behind display.setDrawMode; until that is handled,
	// those paths render without them rather than silently misbehaving.
	if ( fbo->GetDepthBits() > 0 || fbo->GetStencilBits() > 0 )
	{
		Rtt_TRACE( ( "WARNING: bgfx backend ignores the depth/stencil attachment requested by this framebuffer\n" ) );
	}
}

void
BgfxFrameBufferObject::Update( CPUResource* resource )
{
	// The attachment is the texture, and the texture updates itself. A resized
	// texture is a new framebuffer, though.
	Destroy();
	Create( resource );
}

void
BgfxFrameBufferObject::Destroy()
{
	if ( bgfx::isValid( fFrameBuffer ) )
	{
		bgfx::destroy( fFrameBuffer );
		fFrameBuffer = BGFX_INVALID_HANDLE;
	}
}

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------
