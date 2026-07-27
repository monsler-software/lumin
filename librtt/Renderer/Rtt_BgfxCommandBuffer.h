//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Corona game engine.
// For overview and more information on licensing please refer to README.md
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////

#ifndef _Rtt_BgfxCommandBuffer_H__
#define _Rtt_BgfxCommandBuffer_H__

#include "Renderer/Rtt_BgfxDrawState.h"
#include "Renderer/Rtt_CommandBuffer.h"
#include "Renderer/Rtt_RendererCapabilities.h"
#include "Renderer/Rtt_Texture.h"
#include "Renderer/Rtt_Uniform.h"

#include <bgfx/bgfx.h>

// ----------------------------------------------------------------------------

namespace Rtt
{

class BgfxGeometry;
class Geometry;
class FormatExtensionList;
class BgfxProgram;
class BgfxRenderer;

// ----------------------------------------------------------------------------

// Capabilities answered from bgfx::getCaps(), so the limits reported to Lua
// follow the backend bgfx selected at run time.
class BgfxRendererCapabilities : public RendererCapabilities
{
	public:
		virtual size_t GetMaxUniformVectorsCount() const;
		virtual size_t GetMaxVertexTextureUnits() const;
		virtual size_t GetMaxTextureSize() const;
		virtual const char *GetString( const char *key ) const;
		virtual bool GetSupportsHighPrecisionFragmentShaders() const;
};

// ----------------------------------------------------------------------------

// Note on how this differs from the GL and Vulkan command buffers.
//
// Those record Corona's state changes into a byte buffer during frame N and
// replay them into API calls during frame N + 1, which is how Corona gets
// multithreaded rendering out of a single-threaded API. bgfx already works
// that way: its own calls only queue work, which bgfx::frame() then hands to
// its render thread. Recording into a second buffer first would buy nothing
// and cost a frame of latency, so this translates Corona's calls into bgfx
// calls as they arrive, and Execute() only closes the frame.
//
// The consequence is that the inherited fBuffer/fOffset machinery goes unused
// here, and that these calls have to happen on the thread that owns bgfx's API
// side -- which is where Corona already makes them.
class BgfxCommandBuffer : public CommandBuffer
{
	public:
		typedef CommandBuffer Super;
		typedef BgfxCommandBuffer Self;

	public:
		BgfxCommandBuffer( Rtt_Allocator* allocator, BgfxRenderer& renderer );
		virtual ~BgfxCommandBuffer();

		virtual void Initialize();
		virtual void Denitialize();
		virtual void ClearUserUniforms();

		virtual bool HasFramebufferBlit( bool * canScale ) const;
		virtual void GetVertexAttributes( VertexAttributeSupport & support ) const;

		virtual void BindFrameBufferObject( FrameBufferObject* fbo, bool asDrawBuffer );
		virtual void CaptureRect( FrameBufferObject* fbo, Texture& texture, const Rect& rect, const Rect& rawRect );
		virtual void BindGeometry( Geometry* geometry );
		virtual void BindTexture( Texture* texture, U32 unit );
		virtual void BindUniform( Uniform* uniform, U32 unit );
		virtual void BindProgram( Program* program, Program::Version version );
		virtual void BindInstancing( U32 count, Geometry::Vertex* instanceData );
		virtual void BindVertexFormat( FormatExtensionList* extensionList, U16 fullCount, U16 vertexSize, U32 offset );
		virtual void SetBlendEnabled( bool enabled );
		virtual void SetBlendFunction( const BlendMode& mode );
		virtual void SetBlendEquation( RenderTypes::BlendEquation equation );
		virtual void SetViewport( int x, int y, int width, int height );
		virtual void SetScissorEnabled( bool enabled );
		virtual void SetScissorRegion( int x, int y, int width, int height );
		virtual void SetMultisampleEnabled( bool enabled );
		virtual void ClearDepth( Real depth );
		virtual void ClearStencil( U32 stencil );
		virtual void Clear( Real r, Real g, Real b, Real a );
		virtual void Draw( U32 offset, U32 count, Geometry::PrimitiveType type );
		virtual void DrawIndexed( U32 offset, U32 count, Geometry::PrimitiveType type );
		virtual S32 GetCachedParam( CommandBuffer::QueryableParams param );

		virtual void AddCommand( const CoronaCommand * command );
		virtual void IssueCommand( U16 id, const void * data, U32 size );

		virtual const unsigned char * GetBaseAddress() const;

		virtual bool WriteNamedUniform( const char * uniformName, const void * data, unsigned int size );

		virtual Real Execute( bool measureGPU );

	private:
		virtual void InitializeFBO();
		virtual void InitializeCachedParams();
		virtual void CacheQueryParam( CommandBuffer::QueryableParams param );

		// Everything a draw call needs, accumulated by the Bind*/Set* calls and
		// consumed by Draw. bgfx has no persistent binding state: each submit()
		// carries its own, and anything not set is dropped.
		void ApplyState();
		void ApplyTextures();
		void SubmitDraw( U32 offset, U32 count, Geometry::PrimitiveType type, bool indexed );

		// Re-applies the viewport and scissor to the current view, which is what
		// makes them survive a render target switch; see BgfxDrawState.
		void ApplyViewRects();
		U16 ViewRectTop( int y, int height ) const;

		// Whether what is being drawn now has to have its clip-space Y flipped:
		// true when rendering into a texture on a backend whose framebuffers
		// start at the top left, which is every one of them but OpenGL. See
		// where the view-projection matrix is applied.
		bool FlipsOffscreenY() const;

		// Fills a transient index buffer that draws a fan of vertexCount
		// vertices as triangles, and binds it. False if the fan cannot be
		// drawn, in which case nothing was bound.
		bool SetFanIndices( U32 vertexCount );

		// Fills and binds bgfx's instance data buffer from whatever Corona
		// bound through BindInstancing and BindVertexFormat. False if the draw
		// cannot be instanced, in which case nothing was bound.
		bool SetInstanceData();

		// Builds a GPU resource now if Corona's create queue has not reached it
		// yet; see the definition for why this backend needs that.
		static void EnsureCreated( CPUResource* resource );

		BgfxRenderer& fRenderer;

		// Shared with the other command buffer, since Corona alternates between
		// the two while the state deciding what to re-bind is common to both.
		// See the note on BgfxDrawState.
		BgfxDrawState& fState;

		S32 fCachedQuery[kNumQueryableParams];

		BgfxRendererCapabilities fCapabilities;
};

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------

#endif // _Rtt_BgfxCommandBuffer_H__
