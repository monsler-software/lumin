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
	fViewId( 0 ),
	fDepthStencil( BGFX_INVALID_HANDLE )
{
}

// The depth format to attach, given what Corona asked for. bgfx does not let a
// format be assembled from bit counts, so each request is answered with the
// smallest format that covers it and the renderer is asked whether it exists.
static bgfx::TextureFormat::Enum
DepthStencilFormat( U8 depthBits, U8 stencilBits )
{
	bgfx::TextureFormat::Enum candidates[4];
	U32 count = 0;

	if ( stencilBits > 0 )
	{
		// Nothing packs stencil on its own, so a stencil request brings depth
		// with it whether or not depth was asked for.
		candidates[count++] = bgfx::TextureFormat::D24S8;
		candidates[count++] = bgfx::TextureFormat::D32F;
	}
	else
	{
		if ( depthBits > 24 )
		{
			candidates[count++] = bgfx::TextureFormat::D32F;
		}

		if ( depthBits > 16 )
		{
			candidates[count++] = bgfx::TextureFormat::D24;
		}

		candidates[count++] = bgfx::TextureFormat::D16;
		candidates[count++] = bgfx::TextureFormat::D24S8;
	}

	for ( U32 i = 0; i < count; ++i )
	{
		if ( 0 != ( bgfx::getCaps()->formats[candidates[i]] & BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER ) )
		{
			return candidates[i];
		}
	}

	return bgfx::TextureFormat::Count;
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

	// A previous attempt that got as far as the depth attachment and then failed
	// to build the framebuffer itself, which is what running out of framebuffer
	// handles looks like. Corona retries every time the target is bound, so
	// without this each retry would strand another depth texture -- and once
	// enough of those pile up, texture handles run out as well.
	if ( bgfx::isValid( fDepthStencil ) )
	{
		bgfx::destroy( fDepthStencil );
		fDepthStencil = BGFX_INVALID_HANDLE;
	}

	// bgfx only accepts an attachment that was created as a render target, and
	// Corona created this one as an ordinary texture.
	bgfxTexture->AddFlags( BGFX_TEXTURE_RT );

	if ( !bgfx::isValid( bgfxTexture->GetTexture() ) )
	{
		return;
	}

	bgfx::TextureHandle attachments[2];
	U8 count = 0;

	attachments[count++] = bgfxTexture->GetTexture();

	if ( fbo->GetDepthBits() > 0 || fbo->GetStencilBits() > 0 )
	{
		const bgfx::TextureFormat::Enum format = DepthStencilFormat( fbo->GetDepthBits(), fbo->GetStencilBits() );

		if ( bgfx::TextureFormat::Count == format )
		{
			Rtt_TRACE( ( "WARNING: this renderer has no depth format for the %u depth / %u stencil bits this framebuffer asked for\n",
				U32( fbo->GetDepthBits() ), U32( fbo->GetStencilBits() ) ) );
		}
		else
		{
			// Nothing samples this: it exists for the duration of the passes
			// that render into the framebuffer, which is what
			// BGFX_TEXTURE_RT_WRITE_ONLY says and what lets a backend keep it in
			// whatever form is cheapest.
			fDepthStencil = bgfx::createTexture2D(
				  U16( texture->GetWidth() )
				, U16( texture->GetHeight() )
				, false
				, 1
				, format
				, BGFX_TEXTURE_RT_WRITE_ONLY
				);

			if ( bgfx::isValid( fDepthStencil ) )
			{
				attachments[count++] = fDepthStencil;
			}
		}
	}

	// false: the attachments outlive this framebuffer. The colour one is
	// Corona's texture, and the depth one is destroyed by Destroy() below --
	// letting bgfx own it would double-free when a resized framebuffer is
	// rebuilt.
	fFrameBuffer = bgfx::createFrameBuffer( count, attachments, false );
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

	if ( bgfx::isValid( fDepthStencil ) )
	{
		bgfx::destroy( fDepthStencil );
		fDepthStencil = BGFX_INVALID_HANDLE;
	}
}

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------
