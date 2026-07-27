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
			// bgfx dropped triangle fans, which no modern API has. Corona uses
			// them for filled paths, so those need converting to a strip or an
			// indexed list before this backend can draw them.
			Rtt_TRACE( ( "ERROR: bgfx has no triangle fan primitive\n" ) );
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
	fCurrentView( 0 ),
	fGeometry( NULL ),
	fGeometrySource( NULL ),
	fProgram( NULL ),
	fProgramVersion( Program::kMaskCount0 ),
	fBlendState( 0 ),
	fBlendEnabled( false ),
	fScissorEnabled( false )
{
	for ( U32 i = 0; i < Uniform::kNumBuiltInVariables; ++i )
	{
		fBoundUniforms[i] = NULL;
	}

	for ( U32 i = 0; i < Texture::kNumUnits; ++i )
	{
		fBoundTextures[i] = NULL;
	}

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

void
BgfxCommandBuffer::BindFrameBufferObject( FrameBufferObject* fbo, bool asDrawBuffer )
{
	if ( !fbo )
	{
		fCurrentView = 0; // back to the window
		return;
	}

	BgfxFrameBufferObject* resource = static_cast< BgfxFrameBufferObject* >( fbo->GetGPUResource() );

	if ( !resource || !bgfx::isValid( resource->GetFrameBuffer() ) )
	{
		return;
	}

	fCurrentView = fRenderer.AcquireViewId();

	resource->SetViewId( fCurrentView );

	bgfx::setViewFrameBuffer( fCurrentView, resource->GetFrameBuffer() );
}

void
BgfxCommandBuffer::CaptureRect( FrameBufferObject* fbo, Texture& texture, const Rect& rect, const Rect& rawRect )
{
	// Reading a region back into a texture. bgfx::blit plus bgfx::readTexture
	// covers this, but readTexture only delivers its result two frames later,
	// so it needs a completion path Corona's synchronous API does not have yet.
	Rtt_ASSERT_NOT_IMPLEMENTED();
}

void
BgfxCommandBuffer::BindGeometry( Geometry* geometry )
{
	fGeometry = geometry ? static_cast< BgfxGeometry* >( geometry->GetGPUResource() ) : NULL;
	fGeometrySource = geometry;
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
	fBoundTextures[unit] = texture;
}

// Re-sends every texture Corona has bound so far. The GPU resource is looked
// up now rather than at bind time because it may not have existed yet then.
void
BgfxCommandBuffer::ApplyTextures()
{
	for ( U32 unit = 0; unit < Texture::kNumUnits; ++unit )
	{
		Texture* texture = fBoundTextures[unit];

		if ( !texture )
		{
			continue;
		}

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
		fBoundUniforms[unit] = uniform;
	}
}

void
BgfxCommandBuffer::BindProgram( Program* program, Program::Version version )
{
	fProgram = program ? static_cast< BgfxProgram* >( program->GetGPUResource() ) : NULL;
	fProgramVersion = version;
}

void
BgfxCommandBuffer::BindInstancing( U32 count, Geometry::Vertex* instanceData )
{
	// bgfx wants instance data in a buffer of its own
	// (allocInstanceDataBuffer), not in a divisor-tagged vertex stream.
	Rtt_ASSERT_NOT_IMPLEMENTED();
}

void
BgfxCommandBuffer::BindVertexFormat( FormatExtensionList* extensionList, U16 fullCount, U16 vertexSize, U32 offset )
{
	// Custom vertex formats mean a bgfx VertexLayout built per extension list
	// and cached. Stock geometry uses BgfxGeometry::VertexLayout and needs
	// nothing here.
	if ( extensionList )
	{
		Rtt_ASSERT_NOT_IMPLEMENTED();
	}
}

void
BgfxCommandBuffer::SetBlendEnabled( bool enabled )
{
	fBlendEnabled = enabled;
}

void
BgfxCommandBuffer::SetBlendFunction( const BlendMode& mode )
{
	fBlendState = BGFX_STATE_BLEND_FUNC_SEPARATE(
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

	fBlendState = ( fBlendState & ~BGFX_STATE_BLEND_EQUATION_MASK )
		| BGFX_STATE_BLEND_EQUATION_SEPARATE( equationState, equationState );
}

void
BgfxCommandBuffer::SetViewport( int x, int y, int width, int height )
{
	bgfx::setViewRect( fCurrentView, U16( x ), U16( y ), U16( width ), U16( height ) );
}

void
BgfxCommandBuffer::SetScissorEnabled( bool enabled )
{
	fScissorEnabled = enabled;

	if ( !enabled )
	{
		bgfx::setViewScissor( fCurrentView ); // no arguments clears it
	}
}

void
BgfxCommandBuffer::SetScissorRegion( int x, int y, int width, int height )
{
	if ( fScissorEnabled )
	{
		bgfx::setViewScissor( fCurrentView, U16( x ), U16( y ), U16( width ), U16( height ) );
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
	bgfx::setViewClear( fCurrentView, BGFX_CLEAR_DEPTH, 0x000000ff, float( depth ), 0 );
}

void
BgfxCommandBuffer::ClearStencil( U32 stencil )
{
	bgfx::setViewClear( fCurrentView, BGFX_CLEAR_STENCIL, 0x000000ff, 1.0f, U8( stencil ) );
}

void
BgfxCommandBuffer::Clear( Real r, Real g, Real b, Real a )
{
	const U32 rgba = ( U32( r * 255.0f ) << 24 )
		| ( U32( g * 255.0f ) << 16 )
		| ( U32( b * 255.0f ) << 8 )
		| U32( a * 255.0f );

	bgfx::setViewClear( fCurrentView, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, rgba, 1.0f, 0 );

	// A view is only cleared when something is submitted to it, and a frame
	// that clears without drawing is legitimate.
	bgfx::touch( fCurrentView );
}

void
BgfxCommandBuffer::ApplyState()
{
	U64 state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A;

	if ( fBlendEnabled )
	{
		state |= fBlendState;
	}

	bgfx::setState( state );
}

void
BgfxCommandBuffer::SubmitDraw( U32 offset, U32 count, Geometry::PrimitiveType type, bool indexed )
{
	if ( !fProgram || !fGeometry )
	{
		return;
	}

	bgfx::ProgramHandle program = fProgram->GetProgram( fProgramVersion );

	if ( !bgfx::isValid( program ) )
	{
		return;
	}

	if ( fGeometry->StoredOnGPU() )
	{
		bgfx::setVertexBuffer( 0, fGeometry->GetVertexBuffer(), offset, count );

		if ( indexed )
		{
			bgfx::setIndexBuffer( fGeometry->GetIndexBuffer(), offset, count );
		}
	}
	else
	{
		// Most of what Corona draws lives in its GeometryPool and is rewritten
		// every frame. bgfx calls that transient: memory it owns, valid for
		// this frame only, which is why the upload happens here rather than in
		// BgfxGeometry::Create.
		if ( !fGeometrySource )
		{
			return;
		}

		const Geometry::Vertex* vertexData = fGeometrySource->GetVertexData();

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

		if ( indexed )
		{
			const Geometry::Index* indexData = fGeometrySource->GetIndexData();

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

	if ( fBlendEnabled )
	{
		state |= fBlendState;
	}

	// Every value Corona has bound so far, re-sent for this draw.
	for ( U32 i = 0; i < Uniform::kNumBuiltInVariables; ++i )
	{
		Uniform* uniform = fBoundUniforms[i];

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
	bgfx::submit( fCurrentView, program );
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
