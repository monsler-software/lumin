//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Corona game engine.
// For overview and more information on licensing please refer to README.md
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////

#include "Renderer/Rtt_BgfxCommandBuffer.h"

#include "Renderer/Rtt_BgfxFrameBufferObject.h"
#include "Renderer/Rtt_BgfxGeometry.h"
#include "Renderer/Rtt_BgfxProgram.h"
#include "Renderer/Rtt_BgfxRenderer.h"
#include "Renderer/Rtt_BgfxTexture.h"
#include "Renderer/Rtt_FormatExtensionList.h"
#include "Renderer/Rtt_FrameBufferObject.h"
#include "Renderer/Rtt_Geometry_Renderer.h"
#include "Renderer/Rtt_Program.h"
#include "Renderer/Rtt_Texture.h"
#include "Renderer/Rtt_Uniform.h"

#include "Core/Rtt_Assert.h"
#include "Core/Rtt_String.h"

// ----------------------------------------------------------------------------

namespace Rtt
{

// ----------------------------------------------------------------------------

size_t
BgfxRendererCapabilities::GetMaxUniformVectorsCount() const
{
	// bgfx does not report a uniform budget; it batches built-ins into its own
	// buffers. The GL floor is the honest conservative answer until content
	// shows a reason to raise it.
	return 128 - kReservedUniformVectors;
}

size_t
BgfxRendererCapabilities::GetMaxVertexTextureUnits() const
{
	const bgfx::Caps* caps = bgfx::getCaps();
	return caps ? caps->limits.maxTextureSamplers : 0;
}

size_t
BgfxRendererCapabilities::GetMaxTextureSize() const
{
	const bgfx::Caps* caps = bgfx::getCaps();
	return caps ? caps->limits.maxTextureSize : 1024;
}

const char *
BgfxRendererCapabilities::GetString( const char *key ) const
{
	if ( Rtt_StringCompare( key, kRenderer ) == 0
	  || Rtt_StringCompare( key, kVersion ) == 0 )
	{
		return bgfx::getRendererName( bgfx::getRendererType() );
	}

	// bgfx deliberately hides the vendor and the extension list behind its own
	// capability bits, so there is nothing truthful to return for those.
	return "";
}

bool
BgfxRendererCapabilities::GetSupportsHighPrecisionFragmentShaders() const
{
	// Every backend bgfx supports provides 32-bit floats in fragment shaders.
	return true;
}

// ----------------------------------------------------------------------------

static U64
BlendParam( BlendMode::Param param )
{
	switch ( param )
	{
		case BlendMode::kZero:					return BGFX_STATE_BLEND_ZERO;
		case BlendMode::kOne:					return BGFX_STATE_BLEND_ONE;
		case BlendMode::kSrcColor:				return BGFX_STATE_BLEND_SRC_COLOR;
		case BlendMode::kOneMinusSrcColor:		return BGFX_STATE_BLEND_INV_SRC_COLOR;
		case BlendMode::kDstColor:				return BGFX_STATE_BLEND_DST_COLOR;
		case BlendMode::kOneMinusDstColor:		return BGFX_STATE_BLEND_INV_DST_COLOR;
		case BlendMode::kSrcAlpha:				return BGFX_STATE_BLEND_SRC_ALPHA;
		case BlendMode::kOneMinusSrcAlpha:		return BGFX_STATE_BLEND_INV_SRC_ALPHA;
		case BlendMode::kDstAlpha:				return BGFX_STATE_BLEND_DST_ALPHA;
		case BlendMode::kOneMinusDstAlpha:		return BGFX_STATE_BLEND_INV_DST_ALPHA;
		case BlendMode::kSrcAlphaSaturate:		return BGFX_STATE_BLEND_SRC_ALPHA_SAT;

		default:
			Rtt_ASSERT_NOT_REACHED();
			return BGFX_STATE_BLEND_ONE;
	}
}

static U64
PrimitiveState( Geometry::PrimitiveType type )
{
	switch ( type )
	{
		case Geometry::kTriangleStrip:		return BGFX_STATE_PT_TRISTRIP;
		case Geometry::kLines:				return BGFX_STATE_PT_LINES;
		case Geometry::kLineLoop:			return BGFX_STATE_PT_LINESTRIP;

		// Triangles are bgfx's default and have no bit of their own.
		case Geometry::kTriangles:
		case Geometry::kIndexedTriangles:
			return 0;

		case Geometry::kTriangleFan:
			// bgfx dropped triangle fans, which no modern API has. SubmitDraw
			// turns them into an indexed triangle list, so by the time the
			// state is assembled they are ordinary triangles.
			return 0;

		default:
			Rtt_ASSERT_NOT_REACHED();
			return 0;
	}
}

// ----------------------------------------------------------------------------

BgfxCommandBuffer::BgfxCommandBuffer( Rtt_Allocator* allocator, BgfxRenderer& renderer )
:	CommandBuffer( allocator ),
	fRenderer( renderer ),
	fState( renderer.GetDrawState() )
{
	InitializeCachedParams();
}

BgfxCommandBuffer::~BgfxCommandBuffer()
{
}

void
BgfxCommandBuffer::Initialize()
{
	InitializeFBO();
	InitializeCachedParams();

	RendererCapabilities::Set( &fCapabilities );
}

void
BgfxCommandBuffer::Denitialize()
{
	if ( &RendererCapabilities::GetCurrent() == &fCapabilities )
	{
		RendererCapabilities::Set( NULL );
	}
}

void
BgfxCommandBuffer::ClearUserUniforms()
{
	// User uniforms live in the program's own uniform handles, which go away
	// with the program.
}

bool
BgfxCommandBuffer::HasFramebufferBlit( bool * canScale ) const
{
	const bgfx::Caps* caps = bgfx::getCaps();
	const bool supported = caps && 0 != ( caps->supported & BGFX_CAPS_TEXTURE_BLIT );

	if ( canScale )
	{
		// bgfx::blit copies texels; it does not filter or rescale.
		*canScale = false;
	}

	return supported;
}

void
BgfxCommandBuffer::GetVertexAttributes( VertexAttributeSupport & support ) const
{
	const bgfx::Caps* caps = bgfx::getCaps();

	support.maxCount = bgfx::Attrib::Count;
	support.hasInstancing = caps && 0 != ( caps->supported & BGFX_CAPS_INSTANCING );
	support.hasDivisors = false; // bgfx exposes instancing through instance buffers, not divisors
	support.hasPerInstance = support.hasInstancing;
	support.suffix = "";
}

// Builds a resource's bgfx side if Corona's create queue has not reached it
// yet. Unlike the GL and Vulkan backends, this one issues its work while the
// frame is being recorded rather than replaying it a frame later, so a
// resource can be used before that queue is flushed -- most visibly by
// display.save and display.captureScreen, which render and read back within
// the frame that created their framebuffer.
//
// The Create methods are written to return early when the resource already
// exists, so Corona's own pass over the queue finds nothing left to do.
void
BgfxCommandBuffer::EnsureCreated( CPUResource* resource )
{
	if ( !resource )
	{
		return;
	}

	GPUResource* gpuResource = resource->GetGPUResource();

	if ( gpuResource )
	{
		gpuResource->Create( resource );
	}
}

void
BgfxCommandBuffer::BindFrameBufferObject( FrameBufferObject* fbo, bool asDrawBuffer )
{
	if ( !fbo )
	{
		fState.fCurrentView = 0; // back to the window
		return;
	}

	BgfxFrameBufferObject* resource = static_cast< BgfxFrameBufferObject* >( fbo->GetGPUResource() );

	if ( !resource )
	{
		return;
	}

	// Corona builds GPU resources in a queue it flushes once a frame, and this
	// bind happens while the frame is still being recorded -- which for a
	// capture is the same frame that then renders into it. So the framebuffer
	// and the texture behind it are built here if they do not exist yet;
	// Corona's own pass over the queue then finds them already done.
	EnsureCreated( fbo->GetTexture() );

	if ( !bgfx::isValid( resource->GetFrameBuffer() ) )
	{
		resource->Create( fbo );
	}

	if ( !bgfx::isValid( resource->GetFrameBuffer() ) )
	{
		return;
	}

	fState.fCurrentView = fRenderer.AcquireViewId();

	resource->SetViewId( fState.fCurrentView );

	bgfx::setViewFrameBuffer( fState.fCurrentView, resource->GetFrameBuffer() );
}

// Copies a region of what has been rendered into a texture, which is how
// Corona's capture groups sample the frame so far. This stays on the GPU: no
// readback is involved, so bgfx::blit covers it exactly.
void
BgfxCommandBuffer::CaptureRect( FrameBufferObject* fbo, Texture& texture, const Rect& rect, const Rect& rawRect )
{
	if ( !fbo || !fbo->GetTexture() )
	{
		// The source would be the window's backbuffer, which bgfx does not
		// expose as a texture.
		Rtt_TRACE( ( "WARNING: the bgfx backend can only capture from a framebuffer, not from the window\n" ) );
		return;
	}

	BgfxTexture* source = static_cast< BgfxTexture* >( fbo->GetTexture()->GetGPUResource() );
	BgfxTexture* destination = static_cast< BgfxTexture* >( texture.GetGPUResource() );

	if ( !source || !destination || !bgfx::isValid( source->GetTexture() ) )
	{
		return;
	}

	// Corona created the destination as an ordinary texture; bgfx wants to have
	// been told it could be blitted to.
	destination->AddFlags( BGFX_TEXTURE_BLIT_DST );

	if ( !bgfx::isValid( destination->GetTexture() ) )
	{
		return;
	}

	const S32 width = rect.xMax - rect.xMin;
	const S32 height = rect.yMax - rect.yMin;

	if ( width <= 0 || height <= 0 )
	{
		return;
	}

	// Where inside the destination the region lands, for a capture whose
	// requested rect ran off the edge of what was rendered.
	U16 destinationX = 0;
	U16 destinationY = 0;

	if ( rawRect.xMin < rect.xMin )
	{
		destinationX = U16( rect.xMin - rawRect.xMin );
	}

	if ( rawRect.yMax > rect.yMax )
	{
		destinationY = U16( rawRect.yMax - rect.yMax );
	}

	bgfx::blit(
		  fState.fCurrentView
		, destination->GetTexture()
		, destinationX
		, destinationY
		, source->GetTexture()
		, U16( rect.xMin )
		, U16( rect.yMin )
		, U16( width )
		, U16( height )
		);
}

void
BgfxCommandBuffer::BindGeometry( Geometry* geometry )
{
	EnsureCreated( geometry );

	fState.fGeometry = geometry ? static_cast< BgfxGeometry* >( geometry->GetGPUResource() ) : NULL;
	fState.fGeometrySource = geometry;
}

void
BgfxCommandBuffer::BindTexture( Texture* texture, U32 unit )
{
	if ( unit >= Texture::kNumUnits )
	{
		return;
	}

	// Not applied here, for the reason given at BindUniform: bgfx drops the
	// binding at every submit(), and Corona rebinds a texture only when the
	// fill changes, so a draw that reuses the previous fill would sample an
	// unbound sampler. SubmitDraw re-applies whatever is remembered here.
	fState.fBoundTextures[unit] = texture;
}

// Re-sends every texture Corona has bound so far. The GPU resource is looked
// up now rather than at bind time because it may not have existed yet then.
void
BgfxCommandBuffer::ApplyTextures()
{
	for ( U32 unit = 0; unit < Texture::kNumUnits; ++unit )
	{
		Texture* texture = fState.fBoundTextures[unit];

		if ( !texture )
		{
			continue;
		}

		EnsureCreated( texture );

		BgfxTexture* resource = static_cast< BgfxTexture* >( texture->GetGPUResource() );

		if ( !resource || !bgfx::isValid( resource->GetTexture() ) )
		{
			continue;
		}

		// The sampler uniform each unit corresponds to is declared by the shell
		// (u_FillSampler0 and friends); the renderer caches those handles since
		// they are shared by every program.
		bgfx::UniformHandle sampler = fRenderer.GetSamplerUniform( unit );

		if ( bgfx::isValid( sampler ) )
		{
			bgfx::setTexture( U8( unit ), sampler, resource->GetTexture(), BgfxTexture::SamplerFlags( *texture ) );
		}
	}
}

void
BgfxCommandBuffer::BindUniform( Uniform* uniform, U32 unit )
{
	if ( unit < Uniform::kNumBuiltInVariables )
	{
		// Not applied here: bgfx drops uniform state at every submit(), and
		// Corona binds a uniform only on the frame its value changes, so
		// applying it now would leave later draws with nothing.
		fState.fBoundUniforms[unit] = uniform;
	}
}

void
BgfxCommandBuffer::BindProgram( Program* program, Program::Version version )
{
	EnsureCreated( program );

	fState.fProgram = program ? static_cast< BgfxProgram* >( program->GetGPUResource() ) : NULL;
	fState.fProgramVersion = version;
}

void
BgfxCommandBuffer::BindInstancing( U32 count, Geometry::Vertex* instanceData )
{
	// bgfx wants instance data in a buffer of its own, which can only be
	// allocated during the frame that draws from it, so this is remembered and
	// filled in at the draw; see SetInstanceData.
	fState.fInstanceCount = count;
	fState.fInstanceData = instanceData;
}

void
BgfxCommandBuffer::BindVertexFormat( FormatExtensionList* extensionList, U16 fullCount, U16 vertexSize, U32 offset )
{
	// Only the instance layout is kept, and it is copied rather than pointed
	// at: Corona builds this list on the stack for the call (see
	// FormatExtensionList::ReconcileFormats) and it is gone by the time the
	// draw happens.
	//
	// Per-vertex extension attributes would additionally mean a second vertex
	// stream with a layout of its own, which this backend does not build yet;
	// BgfxProgram logs that where the shader that wanted them is compiled,
	// rather than once per draw from here.
	BgfxDrawState::InstanceLayout& layout = fState.fInstanceLayout;

	memset( &layout, 0, sizeof( layout ) );

	if ( !extensionList )
	{
		return;
	}

	layout.fInstancedByID = extensionList->IsInstancedByID();
	layout.fStride = BgfxProgram::GetInstanceStride( extensionList );

	for ( U32 i = 0; i < extensionList->GetGroupCount(); ++i )
	{
		const FormatExtensionList::Group& group = extensionList->GetGroups()[i];

		if ( !group.IsInstanceRate() )
		{
			continue;
		}

		if ( group.IsWindowed() )
		{
			// A windowed group hands each instance an overlapping run of the
			// same array, which has no counterpart in a per-instance block.
			Rtt_TRACE( ( "WARNING: the bgfx backend does not support windowed instance attributes\n" ) );
			continue;
		}

		if ( layout.fGroupCount >= BgfxDrawState::InstanceLayout::kMaxGroups )
		{
			Rtt_TRACE( ( "WARNING: more instanced attribute groups than the bgfx backend carries\n" ) );
			break;
		}

		BgfxDrawState::InstanceLayout::Group& copy = layout.fGroups[layout.fGroupCount++];

		copy.fOffset = BgfxProgram::GetInstanceGroupOffset( extensionList, i );
		copy.fSize = group.size;
	}
}

bool
BgfxCommandBuffer::SetInstanceData()
{
	const bgfx::Caps* caps = bgfx::getCaps();

	if ( !caps || 0 == ( caps->supported & BGFX_CAPS_INSTANCING ) )
	{
		Rtt_TRACE( ( "WARNING: this renderer does not support instancing; dropping a draw call\n" ) );
		return false;
	}

	const BgfxDrawState::InstanceLayout& layout = fState.fInstanceLayout;
	const U32 stride = layout.fStride;

	if ( 0 == stride )
	{
		// No vertex format was bound, so there is nothing to say about how an
		// instance is laid out.
		return false;
	}

	if ( fState.fInstanceCount > bgfx::getAvailInstanceDataBuffer( fState.fInstanceCount, U16( stride ) ) )
	{
		Rtt_TRACE( ( "WARNING: out of instance data buffer space; dropping a draw call\n" ) );
		return false;
	}

	bgfx::InstanceDataBuffer buffer;
	bgfx::allocInstanceDataBuffer( &buffer, fState.fInstanceCount, U16( stride ) );

	memset( buffer.data, 0, size_t( fState.fInstanceCount ) * stride );

	// Corona keeps per-instance data a group at a time -- every instance's
	// values for the first group, then every instance's values for the second
	// -- while bgfx wants one contiguous block per instance, so this
	// interleaves them.
	//
	// Geometry that lives on the GPU keeps each group in an array of its own on
	// the geometry itself; the rest arrives through BindInstancing as one run
	// with the groups end to end.
	const U8* source = reinterpret_cast< const U8* >( fState.fInstanceData );

	Geometry::ExtensionBlock* block = fState.fGeometrySource ? fState.fGeometrySource->GetExtensionBlock() : NULL;
	Array< U8 >** storedData = ( block && block->fInstanceData ) ? block->fInstanceData : NULL;

	for ( U32 i = 0; i < layout.fGroupCount; ++i )
	{
		const BgfxDrawState::InstanceLayout::Group& group = layout.fGroups[i];

		if ( 0 == group.fSize )
		{
			continue;
		}

		const U8* groupSource = source;
		U32 available = fState.fInstanceCount;

		if ( storedData && storedData[i] )
		{
			const Array< U8 >& stored = *storedData[i];

			groupSource = stored.ReadAccess();
			available = U32( stored.Length() ) / group.fSize;
		}

		if ( groupSource )
		{
			for ( U32 instance = 0; instance < fState.fInstanceCount && instance < available; ++instance )
			{
				memcpy( buffer.data + instance * stride + group.fOffset, groupSource + instance * group.fSize, group.fSize );
			}
		}

		if ( !storedData )
		{
			source += size_t( fState.fInstanceCount ) * group.fSize;
		}
	}

	if ( layout.fInstancedByID )
	{
		// Nothing to send but the index itself, which is what stands in for
		// gl_InstanceID; see the note in BgfxProgram.
		for ( U32 instance = 0; instance < fState.fInstanceCount; ++instance )
		{
			*reinterpret_cast< float* >( buffer.data + instance * stride ) = float( instance );
		}
	}

	bgfx::setInstanceDataBuffer( &buffer );

	return true;
}

void
BgfxCommandBuffer::SetBlendEnabled( bool enabled )
{
	fState.fBlendEnabled = enabled;
}

void
BgfxCommandBuffer::SetBlendFunction( const BlendMode& mode )
{
	fState.fBlendState = BGFX_STATE_BLEND_FUNC_SEPARATE(
		  BlendParam( mode.fSrcColor )
		, BlendParam( mode.fDstColor )
		, BlendParam( mode.fSrcAlpha )
		, BlendParam( mode.fDstAlpha )
		);
}

void
BgfxCommandBuffer::SetBlendEquation( RenderTypes::BlendEquation equation )
{
	U64 equationState = BGFX_STATE_BLEND_EQUATION_ADD;

	switch ( equation )
	{
		case RenderTypes::kSubtractEquation:
			equationState = BGFX_STATE_BLEND_EQUATION_SUB;
			break;

		case RenderTypes::kReverseSubtractEquation:
			equationState = BGFX_STATE_BLEND_EQUATION_REVSUB;
			break;

		default:
			break;
	}

	fState.fBlendState = ( fState.fBlendState & ~BGFX_STATE_BLEND_EQUATION_MASK )
		| BGFX_STATE_BLEND_EQUATION_SEPARATE( equationState, equationState );
}

void
BgfxCommandBuffer::SetViewport( int x, int y, int width, int height )
{
	bgfx::setViewRect( fState.fCurrentView, U16( x ), U16( y ), U16( width ), U16( height ) );
}

void
BgfxCommandBuffer::SetScissorEnabled( bool enabled )
{
	fState.fScissorEnabled = enabled;

	if ( !enabled )
	{
		bgfx::setViewScissor( fState.fCurrentView ); // no arguments clears it
	}
}

void
BgfxCommandBuffer::SetScissorRegion( int x, int y, int width, int height )
{
	if ( fState.fScissorEnabled )
	{
		bgfx::setViewScissor( fState.fCurrentView, U16( x ), U16( y ), U16( width ), U16( height ) );
	}
}

void
BgfxCommandBuffer::SetMultisampleEnabled( bool enabled )
{
	// Multisampling is a property of the bgfx reset flags, chosen when the
	// window is created, not a per-draw state.
}

void
BgfxCommandBuffer::ClearDepth( Real depth )
{
	bgfx::setViewClear( fState.fCurrentView, BGFX_CLEAR_DEPTH, 0x000000ff, float( depth ), 0 );
}

void
BgfxCommandBuffer::ClearStencil( U32 stencil )
{
	bgfx::setViewClear( fState.fCurrentView, BGFX_CLEAR_STENCIL, 0x000000ff, 1.0f, U8( stencil ) );
}

void
BgfxCommandBuffer::Clear( Real r, Real g, Real b, Real a )
{
	const U32 rgba = ( U32( r * 255.0f ) << 24 )
		| ( U32( g * 255.0f ) << 16 )
		| ( U32( b * 255.0f ) << 8 )
		| U32( a * 255.0f );

	bgfx::setViewClear( fState.fCurrentView, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, rgba, 1.0f, 0 );

	// A view is only cleared when something is submitted to it, and a frame
	// that clears without drawing is legitimate.
	bgfx::touch( fState.fCurrentView );
}

void
BgfxCommandBuffer::ApplyState()
{
	U64 state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A;

	if ( fState.fBlendEnabled )
	{
		state |= fState.fBlendState;
	}

	bgfx::setState( state );
}

// bgfx has no triangle fan primitive, so a fan is drawn as an indexed triangle
// list: every triangle shares vertex 0 and takes the next adjacent pair. The
// indices are transient, since they depend only on the vertex count and are
// cheaper to regenerate than to cache.
//
// Indices are relative to the first vertex bound by setVertexBuffer, so this
// counts from zero regardless of where the fan sits in the vertex buffer.
bool
BgfxCommandBuffer::SetFanIndices( U32 vertexCount )
{
	if ( vertexCount < 3 )
	{
		return false; // not enough vertices for a single triangle
	}

	// Transient index buffers are 16-bit, which no fan Corona tesselates comes
	// close to exhausting.
	if ( vertexCount > 0xFFFF )
	{
		Rtt_TRACE( ( "WARNING: triangle fan of %u vertices exceeds the 16-bit index range; dropping a draw call\n", vertexCount ) );
		return false;
	}

	const U32 indexCount = ( vertexCount - 2 ) * 3;

	if ( bgfx::getAvailTransientIndexBuffer( indexCount ) < indexCount )
	{
		Rtt_TRACE( ( "WARNING: out of transient index buffer space; dropping a draw call\n" ) );
		return false;
	}

	bgfx::TransientIndexBuffer indexBuffer;
	bgfx::allocTransientIndexBuffer( &indexBuffer, indexCount );

	U16* dst = reinterpret_cast< U16* >( indexBuffer.data );

	for ( U32 i = 1; i + 1 < vertexCount; ++i )
	{
		*dst++ = 0;
		*dst++ = U16( i );
		*dst++ = U16( i + 1 );
	}

	bgfx::setIndexBuffer( &indexBuffer );

	return true;
}

void
BgfxCommandBuffer::SubmitDraw( U32 offset, U32 count, Geometry::PrimitiveType type, bool indexed )
{
	if ( !fState.fProgram || !fState.fGeometry )
	{
		return;
	}

	const bool isFan = ( Geometry::kTriangleFan == type );

	bgfx::ProgramHandle program = fState.fProgram->GetProgram( fState.fProgramVersion );

	if ( !bgfx::isValid( program ) )
	{
		return;
	}

	if ( fState.fGeometry->StoredOnGPU() )
	{
		bgfx::setVertexBuffer( 0, fState.fGeometry->GetVertexBuffer(), offset, count );

		if ( isFan )
		{
			if ( !SetFanIndices( count ) )
			{
				return;
			}
		}
		else if ( indexed )
		{
			bgfx::setIndexBuffer( fState.fGeometry->GetIndexBuffer(), offset, count );
		}
	}
	else
	{
		// Most of what Corona draws lives in its GeometryPool and is rewritten
		// every frame. bgfx calls that transient: memory it owns, valid for
		// this frame only, which is why the upload happens here rather than in
		// BgfxGeometry::Create.
		if ( !fState.fGeometrySource )
		{
			return;
		}

		const Geometry::Vertex* vertexData = fState.fGeometrySource->GetVertexData();

		if ( !vertexData || 0 == count )
		{
			return;
		}

		const bgfx::VertexLayout& layout = BgfxGeometry::VertexLayout();

		// A frame has a fixed transient budget; past it, bgfx would hand back
		// a short buffer and the draw would read past the end.
		if ( bgfx::getAvailTransientVertexBuffer( count, layout ) < count )
		{
			Rtt_TRACE( ( "WARNING: out of transient vertex buffer space; dropping a draw call\n" ) );
			return;
		}

		bgfx::TransientVertexBuffer vertexBuffer;
		bgfx::allocTransientVertexBuffer( &vertexBuffer, count, layout );

		memcpy( vertexBuffer.data, vertexData + offset, count * sizeof( Geometry::Vertex ) );


		bgfx::setVertexBuffer( 0, &vertexBuffer );

		if ( isFan )
		{
			if ( !SetFanIndices( count ) )
			{
				return;
			}
		}
		else if ( indexed )
		{
			const Geometry::Index* indexData = fState.fGeometrySource->GetIndexData();

			if ( !indexData )
			{
				return;
			}

			if ( bgfx::getAvailTransientIndexBuffer( count ) < count )
			{
				Rtt_TRACE( ( "WARNING: out of transient index buffer space; dropping a draw call\n" ) );
				return;
			}

			bgfx::TransientIndexBuffer indexBuffer;
			bgfx::allocTransientIndexBuffer( &indexBuffer, count );

			memcpy( indexBuffer.data, indexData + offset, count * sizeof( Geometry::Index ) );

			bgfx::setIndexBuffer( &indexBuffer );
		}
	}

	U64 state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | PrimitiveState( type );

	if ( fState.fBlendEnabled )
	{
		state |= fState.fBlendState;
	}

	if ( fState.fInstanceCount > 0 )
	{
		const bool bound = SetInstanceData();

		// One draw's worth: Corona binds instancing again for the next one.
		fState.fInstanceCount = 0;
		fState.fInstanceData = NULL;

		if ( !bound )
		{
			return;
		}
	}

	// Every value Corona has bound so far, re-sent for this draw.
	for ( U32 i = 0; i < Uniform::kNumBuiltInVariables; ++i )
	{
		Uniform* uniform = fState.fBoundUniforms[i];

		if ( !uniform )
		{
			continue;
		}

		bgfx::UniformHandle handle = fRenderer.GetBuiltInUniform( i );

		if ( bgfx::isValid( handle ) )
		{
			bgfx::setUniform(
				  handle
				, uniform->GetData()
				, BgfxRenderer::GetBuiltInUniformElementCount( i, uniform->GetSizeInBytes() )
				);
		}
	}

	ApplyTextures();

	bgfx::setState( state );
	bgfx::submit( fState.fCurrentView, program );
}

void
BgfxCommandBuffer::Draw( U32 offset, U32 count, Geometry::PrimitiveType type )
{
	SubmitDraw( offset, count, type, false );
}

void
BgfxCommandBuffer::DrawIndexed( U32 offset, U32 count, Geometry::PrimitiveType type )
{
	SubmitDraw( offset, count, type, true );
}

S32
BgfxCommandBuffer::GetCachedParam( CommandBuffer::QueryableParams param )
{
	S32 result = -1;

	if ( param < kNumQueryableParams )
	{
		result = fCachedQuery[param];
	}

	return result;
}

void
BgfxCommandBuffer::AddCommand( const CoronaCommand * command )
{
	// Custom commands from native plugins issue raw API calls, which have no
	// meaning against bgfx.
	Rtt_ASSERT_NOT_IMPLEMENTED();
}

void
BgfxCommandBuffer::IssueCommand( U16 id, const void * data, U32 size )
{
	Rtt_ASSERT_NOT_IMPLEMENTED();
}

const unsigned char *
BgfxCommandBuffer::GetBaseAddress() const
{
	// There is no recorded command stream to point into; see the note in the
	// header on why this backend does not buffer.
	return NULL;
}

bool
BgfxCommandBuffer::WriteNamedUniform( const char * uniformName, const void * data, unsigned int size )
{
	bgfx::UniformHandle handle = fRenderer.GetNamedUniform( uniformName, size );

	if ( !bgfx::isValid( handle ) )
	{
		return false;
	}

	bgfx::setUniform( handle, data, U16( size / ( 4 * sizeof( float ) ) ) );

	return true;
}

Real
BgfxCommandBuffer::Execute( bool measureGPU )
{
	// bgfx has been receiving the work as it was issued; this ends the frame
	// and hands it to bgfx's render thread.
	bgfx::frame();


	fRenderer.ResetViewIds();

	// Any view a render target held is stale once the numbering restarts, so
	// the next frame starts aimed at the window again.
	fState.fCurrentView = 0;

	if ( measureGPU )
	{
		const bgfx::Stats* stats = bgfx::getStats();

		if ( stats && stats->gpuTimerFreq > 0 )
		{
			return Real( double( stats->gpuTimeEnd - stats->gpuTimeBegin ) * 1000.0 / double( stats->gpuTimerFreq ) );
		}
	}

	return Real( 0 );
}

void
BgfxCommandBuffer::InitializeFBO()
{
}

void
BgfxCommandBuffer::InitializeCachedParams()
{
	for ( int i = 0; i < kNumQueryableParams; ++i )
	{
		fCachedQuery[i] = -1;
	}
}

void
BgfxCommandBuffer::CacheQueryParam( CommandBuffer::QueryableParams param )
{
	const bgfx::Caps* caps = bgfx::getCaps();

	if ( CommandBuffer::kMaxTextureSize == param && caps )
	{
		fCachedQuery[param] = S32( caps->limits.maxTextureSize );
	}
}

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------
