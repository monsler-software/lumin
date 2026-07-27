//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Corona game engine.
// For overview and more information on licensing please refer to README.md
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////

#include "Renderer/Rtt_BgfxTexture.h"

#include "Core/Rtt_Assert.h"

// ----------------------------------------------------------------------------

namespace Rtt
{

// ----------------------------------------------------------------------------

static bgfx::TextureFormat::Enum
TextureFormat( Texture::Format format )
{
	switch ( format )
	{
		case Texture::kAlpha:			return bgfx::TextureFormat::A8;
		case Texture::kLuminance:		return bgfx::TextureFormat::R8;
		case Texture::kLuminanceAlpha:	return bgfx::TextureFormat::RG8;
		case Texture::kRGB:				return bgfx::TextureFormat::RGB8;
		case Texture::kRGBA:			return bgfx::TextureFormat::RGBA8;
		case Texture::kBGRA:			return bgfx::TextureFormat::BGRA8;

		// bgfx has no ABGR8 or ARGB8. Corona hands these to GL as a swizzled
		// read of a 32-bit word, which has no bgfx equivalent; a swizzle in the
		// shell would be the way to support them if content turns out to need
		// it. Report them rather than silently sampling garbage.
		case Texture::kABGR:
		case Texture::kARGB:
			Rtt_TRACE( ( "ERROR: bgfx backend has no format for Texture::Format %d\n", format ) );
			return bgfx::TextureFormat::RGBA8;

		default:
			Rtt_ASSERT_NOT_REACHED();
			return bgfx::TextureFormat::RGBA8;
	}
}

U32
BgfxTexture::SamplerFlags( const Texture& texture )
{
	U32 flags = 0;

	// bgfx defaults to bilinear and repeat, so only the other cases set bits.
	if ( Texture::kNearest == texture.GetFilter() )
	{
		flags |= BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT | BGFX_SAMPLER_MIP_POINT;
	}

	switch ( texture.GetWrapX() )
	{
		case Texture::kClampToEdge:		flags |= BGFX_SAMPLER_U_CLAMP; break;
		case Texture::kMirroredRepeat:	flags |= BGFX_SAMPLER_U_MIRROR; break;
		default: break; // kRepeat
	}

	switch ( texture.GetWrapY() )
	{
		case Texture::kClampToEdge:		flags |= BGFX_SAMPLER_V_CLAMP; break;
		case Texture::kMirroredRepeat:	flags |= BGFX_SAMPLER_V_MIRROR; break;
		default: break; // kRepeat
	}

	return flags;
}

BgfxTexture::BgfxTexture()
:	fTexture( BGFX_INVALID_HANDLE ),
	fWidth( 0 ),
	fHeight( 0 ),
	fFormat( bgfx::TextureFormat::RGBA8 )
{
}

void
BgfxTexture::Create( CPUResource* resource )
{
	Rtt_ASSERT( CPUResource::kTexture == resource->GetType() );
	Texture* texture = static_cast< Texture* >( resource );

	fWidth = U16( texture->GetWidth() );
	fHeight = U16( texture->GetHeight() );
	fFormat = TextureFormat( texture->GetFormat() );

	if ( 0 == fWidth || 0 == fHeight )
	{
		return;
	}

	const U8* data = texture->GetData();

	// Sampler state is supplied per draw call, so none of it is baked in here.
	fTexture = bgfx::createTexture2D(
		  fWidth
		, fHeight
		, false // no mips: Corona's texture resources do not carry a mip chain
		, 1
		, fFormat
		, BGFX_TEXTURE_NONE
		, data ? bgfx::copy( data, texture->GetSizeInBytes() ) : NULL
		);

	// Corona frees the CPU-side copy once the GPU has it, exactly as the GL
	// backend does.
	texture->ReleaseData();
}

void
BgfxTexture::Update( CPUResource* resource )
{
	Rtt_ASSERT( CPUResource::kTexture == resource->GetType() );
	Texture* texture = static_cast< Texture* >( resource );

	if ( !bgfx::isValid( fTexture ) )
	{
		Create( resource );
		return;
	}

	// A resize is a different texture as far as bgfx is concerned.
	if ( U16( texture->GetWidth() ) != fWidth
	  || U16( texture->GetHeight() ) != fHeight
	  || TextureFormat( texture->GetFormat() ) != fFormat )
	{
		Destroy();
		Create( resource );
		return;
	}

	const U8* data = texture->GetData();

	if ( data )
	{
		bgfx::updateTexture2D(
			  fTexture
			, 0
			, 0
			, 0
			, 0
			, fWidth
			, fHeight
			, bgfx::copy( data, texture->GetSizeInBytes() )
			);

		texture->ReleaseData();
	}
}

void
BgfxTexture::Destroy()
{
	if ( bgfx::isValid( fTexture ) )
	{
		bgfx::destroy( fTexture );
		fTexture = BGFX_INVALID_HANDLE;
	}
}

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------
