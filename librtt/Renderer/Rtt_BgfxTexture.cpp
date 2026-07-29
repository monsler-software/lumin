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

#include <string.h>

// ----------------------------------------------------------------------------

namespace Rtt
{

// ----------------------------------------------------------------------------

// Corona names its four-channel formats after the 32-bit word a little-endian
// machine reads out of them, not after the order the bytes sit in -- so kARGB
// is the word 0xAARRGGBB, which is B, G, R, A in memory, and kABGR is
// 0xAABBGGRR, which is R, G, B, A. (See the channel indices in
// PlatformBitmap::GetColorByteIndexes, and what the platforms that produce them
// actually hand over: Windows GDI gives kARGB for its BGRA DIBs, iOS gives
// kABGR for CoreGraphics' RGBA bitmaps.)
//
// Read that way, every Corona format has a bgfx counterpart, which is what this
// returns. `swapRedAndBlue` is set when the counterpart is only reachable by
// exchanging those two channels, which happens on a renderer with no BGRA8.
static bgfx::TextureFormat::Enum
TextureFormat( Texture::Format format, bool& swapRedAndBlue )
{
	swapRedAndBlue = false;

	switch ( format )
	{
		case Texture::kAlpha:			return bgfx::TextureFormat::A8;
		case Texture::kLuminance:		return bgfx::TextureFormat::R8;
		case Texture::kLuminanceAlpha:	return bgfx::TextureFormat::RG8;
		case Texture::kRGB:				return bgfx::TextureFormat::RGB8;

		// Bytes in memory: R, G, B, A.
		case Texture::kRGBA:
		case Texture::kABGR:
			return bgfx::TextureFormat::RGBA8;

		// Bytes in memory: B, G, R, A.
		case Texture::kBGRA:
		case Texture::kARGB:
		{
			const bgfx::Caps* caps = bgfx::getCaps();

			if ( caps && 0 != ( caps->formats[bgfx::TextureFormat::BGRA8] & BGFX_CAPS_FORMAT_TEXTURE_2D ) )
			{
				return bgfx::TextureFormat::BGRA8;
			}

			// No BGRA8 here, so the channels are exchanged on the way in.
			swapRedAndBlue = true;

			return bgfx::TextureFormat::RGBA8;
		}

		default:
			Rtt_ASSERT_NOT_REACHED();
			return bgfx::TextureFormat::RGBA8;
	}
}

static U32
BytesPerPixel( bgfx::TextureFormat::Enum format )
{
	switch ( format )
	{
		case bgfx::TextureFormat::A8:
		case bgfx::TextureFormat::R8:		return 1;
		case bgfx::TextureFormat::RG8:		return 2;
		case bgfx::TextureFormat::RGB8:		return 3;
		default:							return 4;
	}
}

// How many levels a full chain down to 1x1 has.
static U8
MipCount( U16 width, U16 height )
{
	U8 count = 1;

	while ( width > 1 || height > 1 )
	{
		width = width > 1 ? width / 2 : 1;
		height = height > 1 ? height / 2 : 1;

		++count;
	}

	return count;
}

// One mip level from the one above it: a box filter, which is what every
// runtime mip generator uses and what glGenerateMipmap does. Odd dimensions
// round down, so the pair of source texels a destination texel averages is
// clamped to what exists.
static void
Downsample( const U8* source, U16 sourceWidth, U16 sourceHeight, U8* destination, U16 width, U16 height, U32 bytesPerPixel )
{
	for ( U16 y = 0; y < height; ++y )
	{
		const U16 y0 = U16( y * 2 );
		const U16 y1 = U16( y0 + 1 < sourceHeight ? y0 + 1 : y0 );

		for ( U16 x = 0; x < width; ++x )
		{
			const U16 x0 = U16( x * 2 );
			const U16 x1 = U16( x0 + 1 < sourceWidth ? x0 + 1 : x0 );

			const U8* texels[4] =
			{
				  source + ( size_t( y0 ) * sourceWidth + x0 ) * bytesPerPixel
				, source + ( size_t( y0 ) * sourceWidth + x1 ) * bytesPerPixel
				, source + ( size_t( y1 ) * sourceWidth + x0 ) * bytesPerPixel
				, source + ( size_t( y1 ) * sourceWidth + x1 ) * bytesPerPixel
			};

			U8* out = destination + ( size_t( y ) * width + x ) * bytesPerPixel;

			for ( U32 channel = 0; channel < bytesPerPixel; ++channel )
			{
				const U32 sum = U32( texels[0][channel] ) + texels[1][channel]
					+ texels[2][channel] + texels[3][channel];

				out[channel] = U8( ( sum + 2 ) / 4 );
			}
		}
	}
}

// Exchanges the first and third bytes of every pixel, which is how a BGRA
// bitmap is uploaded to a renderer that only has RGBA8.
static void
SwapRedAndBlue( U8* pixels, size_t count )
{
	for ( size_t i = 0; i < count; ++i )
	{
		U8* pixel = pixels + i * 4;
		const U8 red = pixel[0];

		pixel[0] = pixel[2];
		pixel[2] = red;
	}
}

// The whole mip chain laid out end to end, which is the order bgfx wants for a
// texture created with mips: level 0 as Corona supplied it, then each level
// averaged from the one before.
void
BgfxTexture::BuildLevels( const U8* data, U16 width, U16 height, U8 levels, std::vector< U8 >& out ) const
{
	const U32 bytesPerPixel = BytesPerPixel( fFormat );

	bgfx::TextureInfo info;
	bgfx::calcTextureSize( info, width, height, 1, false, levels > 1, 1, fFormat );

	out.resize( info.storageSize );

	U8* destination = &out[0];

	memcpy( destination, data, size_t( width ) * height * bytesPerPixel );

	if ( fSwapRedAndBlue )
	{
		SwapRedAndBlue( destination, size_t( width ) * height );
	}

	U16 sourceWidth = width;
	U16 sourceHeight = height;
	U8* source = destination;

	for ( U8 level = 1; level < levels; ++level )
	{
		destination = source + size_t( sourceWidth ) * sourceHeight * bytesPerPixel;

		const U16 levelWidth = U16( sourceWidth > 1 ? sourceWidth / 2 : 1 );
		const U16 levelHeight = U16( sourceHeight > 1 ? sourceHeight / 2 : 1 );

		Downsample( source, sourceWidth, sourceHeight, destination, levelWidth, levelHeight, bytesPerPixel );

		source = destination;
		sourceWidth = levelWidth;
		sourceHeight = levelHeight;
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
	fFormat( bgfx::TextureFormat::RGBA8 ),
	fFlags( BGFX_TEXTURE_NONE ),
	fLevels( 1 ),
	fSwapRedAndBlue( false )
{
}

// How many mip levels this texture should carry.
//
// bgfx has no glGenerateMipmap: a chain is built when the texture is uploaded
// or not at all, which is why this is decided here rather than left to the
// renderer. A render target has no CPU-side pixels to average, and a texture
// small enough already is its own smallest level.
U8
BgfxTexture::LevelsFor( const Texture& texture ) const
{
	if ( 0 != ( fFlags & ( BGFX_TEXTURE_RT | BGFX_TEXTURE_RT_WRITE_ONLY ) ) )
	{
		return 1;
	}

	if ( fWidth < 2 && fHeight < 2 )
	{
		return 1;
	}

	return MipCount( fWidth, fHeight );
}

void
BgfxTexture::AddFlags( U64 flags )
{
	if ( flags == ( fFlags & flags ) )
	{
		return; // already created with everything asked for
	}

	fFlags |= flags;

	if ( 0 == fWidth || 0 == fHeight )
	{
		return; // Create has not run yet; it will pick the flags up
	}

	// The pixels are not preserved, which is what makes this cheap enough to
	// do lazily: a texture that acquires these flags is a render target or a
	// capture destination, and its contents come from the GPU either way.
	if ( bgfx::isValid( fTexture ) )
	{
		bgfx::destroy( fTexture );
	}

	// A render target has nothing to build a mip chain from, so acquiring these
	// flags drops one it may have had.
	fLevels = 1;

	fTexture = bgfx::createTexture2D( fWidth, fHeight, false, 1, fFormat, fFlags );
}

void
BgfxTexture::Create( CPUResource* resource )
{
	Rtt_ASSERT( CPUResource::kTexture == resource->GetType() );
	Texture* texture = static_cast< Texture* >( resource );

	// Corona creates GPU resources in a queue it flushes once a frame, but this
	// backend issues its work as it is recorded, so anything used before that
	// flush is created on the spot -- and then asked for again when the queue
	// does come round. Nothing to do the second time.
	if ( bgfx::isValid( fTexture ) )
	{
		return;
	}

	fWidth = U16( texture->GetWidth() );
	fHeight = U16( texture->GetHeight() );
	fFormat = TextureFormat( texture->GetFormat(), fSwapRedAndBlue );

	if ( 0 == fWidth || 0 == fHeight )
	{
		return;
	}

	// bgfx rejects initial contents for a render target, which the GPU fills
	// anyway.
	const U8* data = ( fFlags & BGFX_TEXTURE_RT ) ? NULL : texture->GetData();

	fLevels = data ? LevelsFor( *texture ) : 1;

	std::vector< U8 > levels;

	if ( data )
	{
		BuildLevels( data, fWidth, fHeight, fLevels, levels );
	}

	// Sampler state is supplied per draw call, so none of it is baked in here.
	fTexture = bgfx::createTexture2D(
		  fWidth
		, fHeight
		, fLevels > 1
		, 1
		, fFormat
		, fFlags
		, levels.empty() ? NULL : bgfx::copy( &levels[0], U32( levels.size() ) )
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

	bool swapRedAndBlue = false;

	// A resize is a different texture as far as bgfx is concerned.
	if ( U16( texture->GetWidth() ) != fWidth
	  || U16( texture->GetHeight() ) != fHeight
	  || TextureFormat( texture->GetFormat(), swapRedAndBlue ) != fFormat )
	{
		Destroy();
		Create( resource );
		return;
	}

	const U8* data = texture->GetData();

	if ( data )
	{
		// The mip chain is rebuilt from the new contents: bgfx has no call that
		// regenerates one, and levels left behind would be the old image seen
		// wherever the texture is minified.
		std::vector< U8 > levels;

		BuildLevels( data, fWidth, fHeight, fLevels, levels );

		U16 levelWidth = fWidth;
		U16 levelHeight = fHeight;
		U32 offset = 0;

		const U32 bytesPerPixel = BytesPerPixel( fFormat );

		for ( U8 level = 0; level < fLevels; ++level )
		{
			const U32 size = levelWidth * levelHeight * bytesPerPixel;

			bgfx::updateTexture2D(
				  fTexture
				, 0
				, level
				, 0
				, 0
				, levelWidth
				, levelHeight
				, bgfx::copy( &levels[offset], size )
				);

			offset += size;

			levelWidth = U16( levelWidth > 1 ? levelWidth / 2 : 1 );
			levelHeight = U16( levelHeight > 1 ? levelHeight / 2 : 1 );
		}

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
