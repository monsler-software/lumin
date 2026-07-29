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

#include "Display/Rtt_ObjectHandle.h"

#include "Corona/CoronaGraphics.h"

#include "Core/Rtt_Assert.h"
#include "Core/Rtt_Time.h"
#include "Core/Rtt_Math.h"
#include "Core/Rtt_String.h"

#include <cstdio>
#include <cstdlib>
#include <string.h>

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

	// One cap covers both rates here, so it stays the generous one bgfx allows;
	// the tighter per-rate limits -- five per-vertex slots, five instance data
	// vectors -- are reported where a shader that overruns them is built.
	support.maxCount = bgfx::Attrib::Count;
	support.hasInstancing = caps && 0 != ( caps->supported & BGFX_CAPS_INSTANCING );

	// bgfx has no divisor of its own -- instance data is one block per instance
	// -- but the effect of one is the same as repeating a value, which
	// SetInstanceData does when it fills the buffer.
	support.hasDivisors = support.hasInstancing;
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

		ApplyViewRects();

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

	// View state outlives the frame that set it, and the ids are handed out
	// again from the start every frame, so this id may still be carrying the
	// clear some other target asked for last frame. Left alone, that clear
	// would wipe what earlier views already drew into this framebuffer -- the
	// scene rendered before an effect's first offscreen pass, say. Corona
	// issues its own Clear where it wants one.
	bgfx::setViewClear( fState.fCurrentView, BGFX_CLEAR_NONE );

	// A view carries its own rects, and this one has just been handed out, so
	// whatever Corona set before the switch has to be put onto it.
	ApplyViewRects();
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

bool
BgfxCommandBuffer::FlipsOffscreenY() const
{
	if ( 0 == fState.fCurrentView )
	{
		return false; // the window, which bgfx presents the right way up itself
	}

	const bgfx::Caps* caps = bgfx::getCaps();

	return caps && !caps->originBottomLeft;
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
	// Everything here is copied rather than pointed at: Corona builds this list
	// on the stack for the call (see FormatExtensionList::ReconcileFormats) and
	// it is gone by the time the draw happens.

	// Where in the geometry pool the format just described begins. A draw's own
	// offset counts from there, and Corona rebases it to zero whenever it
	// reconciles formats mid-pool.
	fState.fVertexBaseOffset = offset;
	fState.fVertexStride = vertexSize > 0 ? vertexSize : sizeof( Geometry::Vertex );

	BgfxGeometry::BuildVertexLayout( fState.fVertexLayout, extensionList, fState.fVertexStride );

	BgfxDrawState::InstanceLayout& layout = fState.fInstanceLayout;

	memset( &layout, 0, sizeof( layout ) );

	if ( !extensionList )
	{
		return;
	}

	for ( U32 i = 0; i < extensionList->GetGroupCount(); ++i )
	{
		const FormatExtensionList::Group& group = extensionList->GetGroups()[i];

		if ( !group.IsInstanceRate() )
		{
			continue;
		}

		if ( layout.fGroupCount >= BgfxDrawState::InstanceLayout::kMaxGroups )
		{
			Rtt_TRACE( ( "WARNING: more instanced attribute groups than the bgfx backend carries\n" ) );
			break;
		}

		BgfxDrawState::InstanceLayout::Group& copy = layout.fGroups[layout.fGroupCount++];

		copy.fOffset = BgfxProgram::GetInstanceGroupOffset( extensionList, i );
		copy.fDivisor = group.divisor;
		copy.fWindowed = group.IsWindowed();
		copy.fCount = group.count;

		// The group's attributes all share one type when it is windowed -- each
		// instance sees the same value under `count` names, one step apart.
		const FormatExtensionList::Attribute* first = extensionList->GetAttributes() + i;

		for ( U32 j = 0; j < extensionList->GetAttributeCount(); ++j )
		{
			if ( extensionList->FindGroup( j ) == i )
			{
				first = extensionList->GetAttributes() + j;
				break;
			}
		}

		copy.fAttributeSize = first->GetSize();
		copy.fSize = copy.fWindowed ? copy.fAttributeSize * copy.fCount : group.size;
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

	// The stride and the instance-index question come from the program: only
	// the effect's own extension list knows how wide an instance is and whether
	// it asked for the index, and the reconciled list a draw carries describes
	// the geometry's attributes instead.
	const U32 stride = fState.fProgram ? fState.fProgram->GetInstanceStride() : 0;
	const bool instancedByID = fState.fProgram && fState.fProgram->IsInstancedByID();

	if ( 0 == stride )
	{
		// The effect this draw uses is not instanced, so there is nothing to say
		// about how an instance is laid out.
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
	// with the groups end to end, each padded up to a whole Geometry::Vertex
	// (see Renderer::InsertInstancing).
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

		// A group's values are shared by `divisor` consecutive instances, so
		// there are fewer of them than there are instances.
		const U32 divisor = group.fDivisor > 0 ? group.fDivisor : 1;
		const U32 valueCount = ( fState.fInstanceCount + divisor - 1 ) / divisor;

		// One step through the source array. For a windowed group that is a
		// single attribute, since consecutive instances see runs that overlap
		// all but one value; otherwise it is the whole per-instance block.
		const U32 sourceStride = group.fWindowed ? group.fAttributeSize : group.fSize;

		// How much of the array the group occupies, which is where the next one
		// starts.
		const U32 sourceValues = group.fWindowed
			? valueCount + group.fCount - 1
			: valueCount;

		const U8* groupSource = source;
		U32 available = sourceValues;

		if ( storedData && storedData[i] )
		{
			const Array< U8 >& stored = *storedData[i];

			groupSource = stored.ReadAccess();
			available = U32( stored.Length() ) / ( sourceStride > 0 ? sourceStride : 1 );
		}

		if ( groupSource )
		{
			for ( U32 instance = 0; instance < fState.fInstanceCount; ++instance )
			{
				const U32 value = instance / divisor;

				// A windowed group copies the whole run at once, since the run
				// is contiguous in the source; anything else is one block.
				U32 bytes = group.fSize;

				if ( value + bytes / ( sourceStride > 0 ? sourceStride : 1 ) > available )
				{
					// A source array shorter than the instance count asks for.
					const U32 remaining = ( value < available ) ? ( available - value ) * sourceStride : 0;

					bytes = remaining < bytes ? remaining : bytes;
				}

				if ( 0 == bytes )
				{
					break;
				}

				memcpy(
					  buffer.data + instance * stride + group.fOffset
					, groupSource + size_t( value ) * sourceStride
					, bytes
					);
			}
		}

		if ( !storedData )
		{
			// Corona pads each group up to a whole vertex before the next one.
			const U32 dataSize = sourceValues * sourceStride;

			source += size_t( Geometry::Vertex::SizeInVertices( dataSize ) ) * sizeof( Geometry::Vertex );
		}
	}

	if ( instancedByID )
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

// Where a rect Corona measured from the bottom of the target sits in bgfx's
// own coordinates, which run from the top.
//
// Only the window needs the conversion. What is rendered into a texture is
// deliberately drawn upside down there (see FlipsOffscreenY), so a rect
// measured from the bottom of the image already counts from the top of the
// target it is stored in.
U16
BgfxCommandBuffer::ViewRectTop( int y, int height ) const
{
	if ( 0 != fState.fCurrentView )
	{
		return U16( y );
	}

	// The window's surface, which is what the swapchain covers -- larger than
	// the area Corona draws into when the simulator puts a menu bar above it.
	// Measuring from the bottom is what leaves that bar its room.
	const int top = int( fRenderer.GetSurfaceHeight() ) - ( y + height );

	return U16( top > 0 ? top : 0 );
}

// Puts the viewport and scissor Corona last asked for onto whatever view is
// current now. See the note on BgfxDrawState::fViewport.
void
BgfxCommandBuffer::ApplyViewRects()
{
	if ( fState.fViewport[2] > 0 && fState.fViewport[3] > 0 )
	{
		bgfx::setViewRect(
			  fState.fCurrentView
			, U16( fState.fViewport[0] )
			, ViewRectTop( fState.fViewport[1], fState.fViewport[3] )
			, U16( fState.fViewport[2] )
			, U16( fState.fViewport[3] )
			);
	}

	if ( fState.fScissorEnabled && fState.fScissor[2] > 0 && fState.fScissor[3] > 0 )
	{
		// Corona measures a scissor rect the way OpenGL does and the way its own
		// projection produces: inside the viewport, from its bottom left. (See
		// Renderer::SetScissor, which puts the rect through the view-projection
		// matrix and then through ClipToWindow, whose result is scaled by the
		// viewport's width and height.) bgfx counts from the top left of the
		// whole target, so the rect is turned over inside the viewport and then
		// offset to wherever that viewport sits.
		//
		// This is one calculation for both kinds of target. What is rendered
		// into a texture is deliberately drawn upside down (see FlipsOffscreenY),
		// so a row measured from the bottom of the image is at the same distance
		// from the top of the target it is stored in -- the same turn, and
		// ViewRectTop is what differs.
		const int viewportHeight = fState.fViewport[3];
		const int insideViewport = viewportHeight - ( fState.fScissor[1] + fState.fScissor[3] );

		const int left = fState.fViewport[0] + fState.fScissor[0];
		const int top = int( ViewRectTop( fState.fViewport[1], viewportHeight ) ) + insideViewport;

		bgfx::setViewScissor(
			  fState.fCurrentView
			, U16( left > 0 ? left : 0 )
			, U16( top > 0 ? top : 0 )
			, U16( fState.fScissor[2] )
			, U16( fState.fScissor[3] )
			);
	}
	else
	{
		bgfx::setViewScissor( fState.fCurrentView ); // no arguments clears it
	}
}

void
BgfxCommandBuffer::SetViewport( int x, int y, int width, int height )
{
	fState.fViewport[0] = x;
	fState.fViewport[1] = y;
	fState.fViewport[2] = width;
	fState.fViewport[3] = height;

	ApplyViewRects();
}

void
BgfxCommandBuffer::SetScissorEnabled( bool enabled )
{
	fState.fScissorEnabled = enabled;

	ApplyViewRects();
}

void
BgfxCommandBuffer::SetScissorRegion( int x, int y, int width, int height )
{
	fState.fScissor[0] = x;
	fState.fScissor[1] = y;
	fState.fScissor[2] = width;
	fState.fScissor[3] = height;

	ApplyViewRects();
}

void
BgfxCommandBuffer::SetMultisampleEnabled( bool enabled )
{
	// Two halves to this in bgfx: the backbuffer has to be built with samples,
	// which is a reset flag the renderer owns, and each draw has to say it wants
	// them, which is a state bit assembled with the rest.
	fRenderer.SetMultisampleEnabled( enabled );

	fState.fMultisampleEnabled = enabled;
}

// Corona issues a depth and a stencil clear just before the colour clear they
// belong with (see Renderer::Clear), while a bgfx view takes one clear for all
// three. So these are remembered and handed over together; anything still
// pending when the frame ends belongs to a clear that never named a colour.
void
BgfxCommandBuffer::ClearDepth( Real depth )
{
	fState.fPendingClearFlags |= BGFX_CLEAR_DEPTH;
	fState.fPendingClearDepth = float( depth );
}

void
BgfxCommandBuffer::ClearStencil( U32 stencil )
{
	fState.fPendingClearFlags |= BGFX_CLEAR_STENCIL;
	fState.fPendingClearStencil = U8( stencil );
}

void
BgfxCommandBuffer::Clear( Real r, Real g, Real b, Real a )
{
	const U32 rgba = ( U32( r * 255.0f ) << 24 )
		| ( U32( g * 255.0f ) << 16 )
		| ( U32( b * 255.0f ) << 8 )
		| U32( a * 255.0f );

	fState.fPendingClearFlags |= BGFX_CLEAR_COLOR;

	bgfx::setViewClear(
		  fState.fCurrentView
		, fState.fPendingClearFlags
		, rgba
		, fState.fPendingClearDepth
		, fState.fPendingClearStencil
		);

	fState.fPendingClearFlags = 0;
	fState.fPendingClearDepth = 1.0f;
	fState.fPendingClearStencil = 0;

	// A view is only cleared when something is submitted to it, and a frame
	// that clears without drawing is legitimate.
	bgfx::touch( fState.fCurrentView );
}

void
BgfxCommandBuffer::FlushClear()
{
	if ( 0 == fState.fPendingClearFlags )
	{
		return;
	}

	// Depth or stencil asked for without a colour clear to carry them, which is
	// what display.setDefault( "enableDepthInScene" ) alone produces.
	bgfx::setViewClear(
		  fState.fCurrentView
		, fState.fPendingClearFlags
		, 0
		, fState.fPendingClearDepth
		, fState.fPendingClearStencil
		);

	fState.fPendingClearFlags = 0;
	fState.fPendingClearDepth = 1.0f;
	fState.fPendingClearStencil = 0;

	bgfx::touch( fState.fCurrentView );
}

// Everything a submit() is told about how to blend, test and write. bgfx keeps
// none of this between submits, so it is assembled again for each one.
U64
BgfxCommandBuffer::DrawState( Geometry::PrimitiveType type ) const
{
	U64 state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | PrimitiveState( type ) | fState.fDepthState;

	if ( fState.fBlendEnabled )
	{
		state |= fState.fBlendState;
	}

	if ( fState.fMultisampleEnabled )
	{
		state |= BGFX_STATE_MSAA;

		// Lines get bgfx's own antialiasing, which is what GL_LINE_SMOOTH gave
		// the backend this replaces.
		if ( Geometry::kLines == type || Geometry::kLineLoop == type )
		{
			state |= BGFX_STATE_LINEAA;
		}
	}

	return state;
}

void
BgfxCommandBuffer::ApplyState()
{
	bgfx::setState( DrawState( Geometry::kTriangles ) );
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

	// Nothing has reconciled a vertex format yet, which is the case until the
	// first draw of the first frame. bgfx layouts cannot be built before it is
	// initialized, so this cannot be done when the state is constructed.
	if ( 0 == fState.fVertexLayout.getStride() )
	{
		fState.fVertexLayout = BgfxGeometry::VertexLayout();
		fState.fVertexStride = sizeof( Geometry::Vertex );
	}

	bgfx::ProgramHandle program = fState.fProgram->GetProgram( fState.fProgramVersion );

	if ( !bgfx::isValid( program ) )
	{
		return;
	}

	// Geometry that carries per-vertex extension data has no buffers until the
	// format the effect wants is known, which is now; see BgfxGeometry.
	if ( fState.fGeometrySource && fState.fGeometrySource->GetStoredOnGPU() )
	{
		fState.fGeometry->EnsureBuffers( fState.fGeometrySource, fState.fVertexLayout );
	}

	if ( fState.fGeometry->StoredOnGPU() )
	{
		// An indexed draw's offset and count address the index buffer (see
		// Renderer::CheckAndInsertDrawCommand), so the vertex side takes the
		// whole buffer and the indices pick out of it.
		if ( indexed && !isFan )
		{
			bgfx::setVertexBuffer( 0, fState.fGeometry->GetVertexBuffer() );
		}
		else
		{
			bgfx::setVertexBuffer( 0, fState.fGeometry->GetVertexBuffer(), offset, count );
		}

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

		// Whatever format was last reconciled, which is the stock vertex unless
		// the effect declared per-vertex extension attributes: those are
		// interleaved after each vertex, so the stride is larger and a draw's
		// offset counts in those larger vertices from where the format began.
		const bgfx::VertexLayout& layout = fState.fVertexLayout;
		const U32 stride = layout.getStride();

		// A frame has a fixed transient budget; past it, bgfx would hand back
		// a short buffer and the draw would read past the end.
		if ( bgfx::getAvailTransientVertexBuffer( count, layout ) < count )
		{
			Rtt_TRACE( ( "WARNING: out of transient vertex buffer space; dropping a draw call\n" ) );
			return;
		}

		bgfx::TransientVertexBuffer vertexBuffer;
		bgfx::allocTransientVertexBuffer( &vertexBuffer, count, layout );

		const U8* source = reinterpret_cast< const U8* >( vertexData + fState.fVertexBaseOffset )
			+ size_t( offset ) * stride;

		memcpy( vertexBuffer.data, source, size_t( count ) * stride );

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

	const U64 state = DrawState( type );

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

		if ( !bgfx::isValid( handle ) )
		{
			continue;
		}

		// What Corona hands over is built for OpenGL, whose framebuffers start
		// at the bottom left. Everywhere else a render target starts at the top
		// left, so the image lands in the texture upside down -- visible the
		// moment that texture is sampled again, which is what a snapshot, a
		// canvas or display.captureScreen does. Flipping clip-space Y for
		// offscreen views puts the rows back in the order the rest of Corona
		// (sampling and readback alike) expects. The window itself is left
		// alone: bgfx already presents view 0 the right way up.
		if ( Uniform::kViewProjectionMatrix == i && FlipsOffscreenY() )
		{
			float matrix[16];

			memcpy( matrix, uniform->GetData(), sizeof( matrix ) );

			// Column-major, so the row that produces clip-space Y is every
			// second element.
			for ( U32 element = 1; element < 16; element += 4 )
			{
				matrix[element] = -matrix[element];
			}

			bgfx::setUniform( handle, matrix, 1 );

			continue;
		}

		bgfx::setUniform(
			  handle
			, uniform->GetData()
			, BgfxRenderer::GetBuiltInUniformElementCount( i, uniform->GetSizeInBytes() )
			);
	}

	ApplyTextures();

	bgfx::setState( state );

	if ( BGFX_STENCIL_NONE != fState.fStencilFront || BGFX_STENCIL_NONE != fState.fStencilBack )
	{
		bgfx::setStencil( fState.fStencilFront, fState.fStencilBack );
	}

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
	// Kept on the renderer rather than here: Corona registers a command with
	// whichever buffer is recording that frame and then alternates between the
	// two, so a list of our own would be missing half of them.
	fRenderer.GetCustomCommands().Append( command );
}

// A command a native plugin issued through CoronaRendererIssueCommand.
//
// The GL backend writes the payload into its command stream and runs the
// reader a frame later, when that stream is replayed. This backend has no such
// stream -- it hands work to bgfx as it arrives -- so the reader runs here,
// which puts it at the same point in the draw order either way. The payload
// still goes through the working buffer, because that is what a command's
// writer and reader are given and what CoronaCommandBufferGetBaseAddress
// reports offsets against.
void
BgfxCommandBuffer::IssueCommand( U16 id, const void * data, U32 size )
{
	OBJECT_HANDLE_SCOPE();

	LightPtrArray< const CoronaCommand >& commands = fRenderer.GetCustomCommands();

	if ( id >= commands.Length() )
	{
		Rtt_TRACE( ( "WARNING: a native plugin issued command %u, which was never registered\n", U32( id ) ) );
		return;
	}

	U8 * buffer = Reserve( size );

	OBJECT_HANDLE_STORE( CommandBuffer, commandBuffer, this );

	commands[id]->writer( commandBuffer, buffer, data, size );

	commands[id]->reader( commandBuffer, buffer, size );
}

U8 *
BgfxCommandBuffer::Reserve( U32 size )
{
	const U32 bytesNeeded = fBytesUsed + size;

	if ( bytesNeeded > fBytesAllocated )
	{
		const U32 doubleSize = fBytesUsed ? 2 * fBytesUsed : 4;
		const U32 newSize = Max( bytesNeeded, doubleSize );

		U8* newBuffer = new U8[newSize];

		memcpy( newBuffer, fBuffer, fBytesUsed );
		delete [] fBuffer;

		fBuffer = newBuffer;
		fBytesAllocated = newSize;
	}

	U8 * buffer = fBuffer + fBytesUsed;

	fBytesUsed += size;

	return buffer;
}

const unsigned char *
BgfxCommandBuffer::GetBaseAddress() const
{
	// Not a recorded command stream -- see the note in the header -- but the
	// working memory custom commands write their payloads into, which is what
	// this is for.
	return fBuffer;
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
	// A depth or stencil clear that never found a colour clear to travel with.
	FlushClear();

	// Whatever the host draws over the finished content -- the simulator's
	// menu bar. Last, so it is over everything, and before the frame ends, so
	// it travels with the content it belongs over rather than a frame behind.
	fRenderer.InvokeOverlay();

	// bgfx has been receiving the work as it was issued; this ends the frame
	// and hands it to bgfx's render thread.
	bgfx::frame();

	// Custom commands have run, so their payloads can be overwritten. The
	// allocation itself is kept: the next frame's commands are the same size as
	// this one's, near enough.
	fBytesUsed = 0;


	fRenderer.ResetViewIds();

	// Any view a render target held is stale once the numbering restarts, so
	// the next frame starts aimed at the window again.
	fState.fCurrentView = 0;

	// Nothing a display object owns is remembered past the frame that bound it:
	// the object may be gone by the next one, and every draw re-sends what is
	// remembered. See BgfxDrawState::ReleaseObjectBindings.
	fState.ReleaseObjectBindings();

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
