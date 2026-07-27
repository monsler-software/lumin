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

		// Filter and wrap are part of the sampler state a draw call carries in
		// bgfx, not part of the texture as they are in GL. The command buffer
		// asks for these when it binds, which is also why SetFilter/SetWrap on
		// a Corona texture need no work here.
		static U32 SamplerFlags( const Texture& texture );

	private:
		bgfx::TextureHandle fTexture;
		U16 fWidth;
		U16 fHeight;
		bgfx::TextureFormat::Enum fFormat;
};

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------

#endif // _Rtt_BgfxTexture_H__
