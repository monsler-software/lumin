//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Corona game engine.
// For overview and more information on licensing please refer to README.md
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////

#ifndef _Rtt_BgfxRenderer_H__
#define _Rtt_BgfxRenderer_H__

#include "Renderer/Rtt_BgfxDrawState.h"
#include "Renderer/Rtt_Renderer.h"
#include "Renderer/Rtt_BgfxSurfaceParams.h"
#include "Renderer/Rtt_Texture.h"
#include "Renderer/Rtt_Uniform.h"

#include <bgfx/bgfx.h>

// ----------------------------------------------------------------------------

struct Rtt_Allocator;

namespace Rtt
{

class CPUResource;
class GPUResource;

// ----------------------------------------------------------------------------

// ----------------------------------------------------------------------------

class BgfxRenderer : public Renderer
{
	public:
		typedef Renderer Super;
		typedef BgfxRenderer Self;

	public:
		BgfxRenderer( Rtt_Allocator* allocator, const BgfxSurfaceParams& params );
		virtual ~BgfxRenderer();

		virtual void Initialize();

		virtual void SetSurfaceSize( U32 width, U32 height );

		virtual void CaptureFrameBuffer( RenderingStream & stream, BufferBitmap & bitmap, S32 x_in_pixels, S32 y_in_pixels, S32 w_in_pixels, S32 h_in_pixels );

		// Uniform handles are per-name and shared across programs in bgfx, so
		// they belong to the renderer rather than to any one program.
		bgfx::UniformHandle GetSamplerUniform( U32 unit );
		bgfx::UniformHandle GetBuiltInUniform( U32 index );

		// How many elements bgfx expects for a built-in uniform. A Mat4 counts
		// as one element even though it spans four vec4s, and passing four is
		// rejected as a truncated update.
		static U16 GetBuiltInUniformElementCount( U32 index, U32 sizeInBytes );
		bgfx::UniformHandle GetNamedUniform( const char* name, U32 sizeInBytes );

		// The binding state both command buffers submit through; see the note
		// on BgfxDrawState for why it is shared rather than per buffer.
		BgfxDrawState& GetDrawState() { return fDrawState; }

		// Render-to-texture takes a view id per target, handed out for the
		// frame and reclaimed when it ends.
		bgfx::ViewId AcquireViewId();
		void ResetViewIds();

		// The window's height in pixels, which the window's view rect is
		// measured against.
		U32 GetSurfaceHeight() const { return fParams.fHeight; }

	protected:
		virtual GPUResource* Create( const CPUResource* resource );

	private:
		void CreateUniforms();
		void DestroyUniforms();

		// The staging texture a readback goes through, since bgfx can only read
		// a texture that was created for it. Grown to fit and kept between
		// captures, which are rare but usually repeated (a capture per frame in
		// a recorder, say).
		bool EnsureReadBackTexture( U16 width, U16 height, bgfx::TextureFormat::Enum format );
		void DestroyReadBackTexture();

		BgfxSurfaceParams fParams;
		BgfxDrawState fDrawState;
		bool fInitialized;

		bgfx::ViewId fNextViewId;

		bgfx::TextureHandle fReadBackTexture;
		U16 fReadBackWidth;
		U16 fReadBackHeight;
		bgfx::TextureFormat::Enum fReadBackFormat;
		U8* fReadBackBuffer;
		U32 fReadBackBufferSize;

		bgfx::UniformHandle fSamplerUniforms[Texture::kNumUnits];
		bgfx::UniformHandle fBuiltInUniforms[Uniform::kNumBuiltInVariables];
};

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------

#endif // _Rtt_BgfxRenderer_H__
