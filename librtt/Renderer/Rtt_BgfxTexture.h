//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Corona game engine.
// For overview and more information on licensing please refer to README.md
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////

#ifndef _Rtt_BgfxTexture_H__
#define _Rtt_BgfxTexture_H__

#include "Renderer/Rtt_GPUResource.h"
#include "Renderer/Rtt_Texture.h"

#include <bgfx/bgfx.h>

#include <vector>

// ----------------------------------------------------------------------------

namespace Rtt
{

// ----------------------------------------------------------------------------

class BgfxTexture : public GPUResource
{
	public:
		typedef GPUResource Super;
		typedef BgfxTexture Self;

	public:
		BgfxTexture();

		virtual void Create( CPUResource* resource );
		virtual void Update( CPUResource* resource );
		virtual void Destroy();

		bgfx::TextureHandle GetTexture() const { return fTexture; }

		U16 GetWidth() const { return fWidth; }
		U16 GetHeight() const { return fHeight; }
		bgfx::TextureFormat::Enum GetFormat() const { return fFormat; }

		// bgfx has to be told at creation time whether a texture can be rendered
		// into or blitted to, and Corona has no such distinction: the texture
		// behind a snapshot or a capture is created like any other and only
		// later handed to a framebuffer. This recreates it with the flags it
		// turns out to need, dropping its contents in the process -- which is
		// what makes it cheap, since such a texture is filled by the GPU.
		void AddFlags( U64 flags );

		// Filter and wrap are part of the sampler state a draw call carries in
		// bgfx, not part of the texture as they are in GL. The command buffer
		// asks for these when it binds, which is also why SetFilter/SetWrap on
		// a Corona texture need no work here.
		static U32 SamplerFlags( const Texture& texture );

	private:
		// How many mip levels this texture carries, given its size and what it
		// is for.
		U8 LevelsFor( const Texture& texture ) const;

		// Builds level 0 plus the whole chain below it into one block, applying
		// the channel swap if this texture needs one.
		void BuildLevels( const U8* data, U16 width, U16 height, U8 levels, std::vector< U8 >& out ) const;

		bgfx::TextureHandle fTexture;
		U16 fWidth;
		U16 fHeight;
		bgfx::TextureFormat::Enum fFormat;

		// Creation flags, which accumulate as the texture turns out to be used
		// as a render target or as the destination of a capture.
		U64 fFlags;

		// bgfx builds a mip chain when the texture is uploaded or never: it has
		// no glGenerateMipmap. So the chain is generated on the CPU, and this is
		// how many levels the texture was created with -- an update has to
		// refill every one of them.
		U8 fLevels;

		// Set for a Corona format whose byte order this renderer has no texture
		// format for, which BuildLevels corrects on the way in.
		bool fSwapRedAndBlue;
};

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------

#endif // _Rtt_BgfxTexture_H__
