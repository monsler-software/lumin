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

#include "Renderer/Rtt_Bgfx3DPipeline.h"
#include "Renderer/Rtt_BgfxDrawState.h"
#include "Renderer/Rtt_Renderer.h"
#include "Renderer/Rtt_BgfxSurfaceParams.h"
#include "Renderer/Rtt_Texture.h"
#include "Renderer/Rtt_Uniform.h"

#include "Display/Rtt_PlatformBitmap.h"

#include "Core/Rtt_Array.h"

#include <bgfx/bgfx.h>

#include <map>
#include <string>

// ----------------------------------------------------------------------------

struct Rtt_Allocator;
struct CoronaCommand;

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

		// The 3D pipeline, shared between the two command buffers for the same
		// reason the draw state is.
		Bgfx3DPipeline& Get3DPipeline() { return f3DPipeline; }

		// Commands a native plugin registered through
		// CoronaRendererRegisterCommand. Shared for the same reason the draw
		// state is: Corona registers a command with whichever buffer is
		// recording that frame, then alternates, so a per-buffer list would
		// leave the other one unable to run half the commands issued to it.
		LightPtrArray< const CoronaCommand >& GetCustomCommands() { return fCustomCommands; }

		// Render-to-texture takes a view id per target, handed out for the
		// frame and reclaimed when it ends.
		bgfx::ViewId AcquireViewId();

		// The view the 3D shadow pass renders into, reserved rather than acquired.
		//
		// bgfx draws views in ascending id order and the window is view 0, so a
		// shadow pass given an ordinary id would run after the frame that needs to
		// read it. The reserved id is hoisted in front of everything by
		// SetupShadowViewOrder, which leaves the relative order of every other view
		// exactly as it was.
		bgfx::ViewId GetShadowViewId() const;

		// Puts a view into submission order, which is what Corona's back-to-front
		// drawing needs; see the call in Initialize().
		static void SetSequentialViewMode( bgfx::ViewId view );

		// Puts the shadow view first. Cheap enough to call whenever the shadow pass
		// is used, and idempotent.
		void SetupShadowViewOrder();

		// SetupShadowViewOrder, at most once a frame. bgfx keeps a view order until
		// something replaces it, but Corona hands the ids out again from the start
		// every frame, so the order is reasserted rather than assumed.
		void EnsureShadowViewOrder();
		void ResetViewIds();

		// Drawing the host puts over the finished frame -- the simulator's
		// menu bar, and nothing else so far.
		//
		// It has to happen here rather than in the host's own loop because
		// bgfx::frame() is what ends a frame, and the command buffer is what
		// calls it: anything the host submitted afterwards would land in the
		// next frame instead, a frame behind the content it belongs over. The
		// view id passed in is freshly acquired, so it is above every view the
		// content used and is drawn after all of them.
		typedef void (*OverlayProc)( void* userdata, U16 view, U32 width, U32 height );

		// Called before bgfx::shutdown(), so the overlay can let go of its
		// textures and shaders while there is still a bgfx to give them back
		// to. This is not only an end-of-run concern: opening a project tears
		// the whole runtime down and builds another, so bgfx is shut down and
		// reinitialized, and an overlay that kept its handles across that
		// would be drawing with numbers that now mean something else.
		typedef void (*OverlayReleaseProc)( void* userdata );

		void SetOverlay( OverlayProc proc, OverlayReleaseProc releaseProc, void* userdata );

		// Called by the command buffer, immediately before it ends the frame.
		void InvokeOverlay();

		// Submits a frame that is nothing but the overlay.
		//
		// A suspended runtime advances nothing and so ends no frames, which
		// under bgfx means the window stops being presented at all -- and the
		// menu bar has to keep working while suspended, since Resume is on it.
		// The host calls this instead for as long as that lasts. `clear` paints
		// the window black first, which is what suspending has always looked
		// like; without it the swapchain shows whatever it happens to hold.
		void RenderOverlayFrame( bool clear );

		// The window's height in pixels, which the window's view rect is
		// measured against.
		U32 GetSurfaceHeight() const { return fParams.fHeight; }
		U32 GetSurfaceWidth() const { return fParams.fWidth; }

		// Multisampling is a property of the swapchain in bgfx, not a state a
		// draw call can turn on, so asking for it rebuilds the backbuffer. The
		// scene asks for this once a frame with the value it read from
		// config.lua, so only the first one does any work.
		void SetMultisampleEnabled( bool enabled );
		bool GetMultisampleEnabled() const { return fMultisampleEnabled; }

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

		// Whether what the readback texture holds can be copied into a bitmap of
		// this format, and if so whether red and blue have to be exchanged: a
		// kBGRA render target is an RGBA8 texture where the backend has no BGRA8.
		bool ReadBackMatchesBitmap( PlatformBitmap::Format format, U32 bytesPerPixel, bool& swapRedAndBlue ) const;

		BgfxSurfaceParams fParams;
		BgfxDrawState fDrawState;
		Bgfx3DPipeline f3DPipeline;
		LightPtrArray< const CoronaCommand > fCustomCommands;
		bool fInitialized;

		// What bgfx::reset was last given. Multisampling lives in here rather
		// than in any per-draw state.
		U32 fResetFlags;
		bool fMultisampleEnabled;

		bgfx::ViewId fNextViewId;

		// Whether this frame has already said it ran out of them; cleared when
		// the numbering restarts.
		bool fViewIdsExhausted;

		// Whether the shadow pass has been put at the head of the view order this
		// frame. Cleared with the view ids.
		bool fShadowOrderSet;

		OverlayProc fOverlayProc;
		OverlayReleaseProc fOverlayReleaseProc;
		void* fOverlayUserdata;

		bgfx::TextureHandle fReadBackTexture;
		U16 fReadBackWidth;
		U16 fReadBackHeight;
		bgfx::TextureFormat::Enum fReadBackFormat;
		U8* fReadBackBuffer;
		U32 fReadBackBufferSize;

		bgfx::UniformHandle fSamplerUniforms[Texture::kNumUnits];
		bgfx::UniformHandle fBuiltInUniforms[Uniform::kNumBuiltInVariables];

		// Uniforms created for names that only exist once a shader has been
		// compiled, i.e. whatever a defineEffect kernel declared. Keyed by name
		// because that is all bgfx identifies them by.
		//
		// A member rather than a file-scope map: these hold bgfx handles, and
		// bgfx is shut down and reinitialized every time a project is opened, so
		// a cache that outlived the renderer would hand out handles from a
		// previous context. It also has to be gone before bgfx::shutdown(),
		// which a static would not manage.
		typedef std::map< std::string, bgfx::UniformHandle > NamedUniformMap;

		NamedUniformMap fNamedUniforms;

		// Whether the uniform pool running dry has already been reported, so it
		// is said once rather than per effect that fails to bind.
		bool fUniformLimitReported;
};

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------

#endif // _Rtt_BgfxRenderer_H__
