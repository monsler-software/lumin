//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Corona game engine.
// For overview and more information on licensing please refer to README.md
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////

#include "Renderer/Rtt_BgfxRenderer.h"

#include "Renderer/Rtt_BgfxCommandBuffer.h"
#include "Renderer/Rtt_BgfxFrameBufferObject.h"
#include "Renderer/Rtt_BgfxGeometry.h"
#include "Renderer/Rtt_BgfxProgram.h"
#include "Renderer/Rtt_BgfxTexture.h"
#include "Renderer/Rtt_CPUResource.h"
#include "Renderer/Rtt_FrameBufferObject.h"

#include "Display/Rtt_BufferBitmap.h"

#include "Core/Rtt_Assert.h"
#include "Core/Rtt_String.h"

#include <bx/thread.h>

#include <map>
#include <string>

// ----------------------------------------------------------------------------

namespace Rtt
{

// ----------------------------------------------------------------------------

// The uniform names the shell declares. Order matches Uniform::Name, since the
// renderer indexes into this with the same value Corona binds by.
static const char* kBuiltInUniformNames[Uniform::kNumBuiltInVariables] =
{
	"u_ViewProjectionMatrix",
	"u_MaskMatrix0",
	"u_MaskMatrix1",
	"u_MaskMatrix2",
	"u_TotalTime",
	"u_DeltaTime",
	"u_TexelSize",
	"u_ContentScale",
	"u_ContentSize",
	"u_UserData0",
	"u_UserData1",
	"u_UserData2",
	"u_UserData3",
};

static bgfx::UniformType::Enum
BuiltInUniformType( U32 index )
{
	switch ( index )
	{
		case Uniform::kViewProjectionMatrix:
			return bgfx::UniformType::Mat4;

		// Corona stores these as 3x3 affine transforms, and that is what the
		// shell declares; passing them as Mat4 would read past the end of the
		// uniform's 9 floats.
		case Uniform::kMaskMatrix0:
		case Uniform::kMaskMatrix1:
		case Uniform::kMaskMatrix2:
			return bgfx::UniformType::Mat3;

		default:
			// bgfx has no scalar uniform type: the single-float built-ins
			// travel in the x component of a vec4.
			return bgfx::UniformType::Vec4;
	}
}

// ----------------------------------------------------------------------------

BgfxRenderer::BgfxRenderer( Rtt_Allocator* allocator, const BgfxSurfaceParams& params )
:	Super( allocator ),
	fParams( params ),
	fCustomCommands( allocator ),
	fInitialized( false ),
	fResetFlags( BGFX_RESET_VSYNC ),
	fMultisampleEnabled( false ),
	fNextViewId( 1 ), // view 0 is the window
	fViewIdsExhausted( false ),
	fOverlayProc( NULL ),
	fOverlayReleaseProc( NULL ),
	fOverlayUserdata( NULL ),
	fReadBackTexture( BGFX_INVALID_HANDLE ),
	fReadBackWidth( 0 ),
	fReadBackHeight( 0 ),
	fReadBackFormat( bgfx::TextureFormat::RGBA8 ),
	fReadBackBuffer( NULL ),
	fReadBackBufferSize( 0 ),
	fUniformLimitReported( false )
{
	for ( U32 i = 0; i < Texture::kNumUnits; ++i )
	{
		fSamplerUniforms[i] = BGFX_INVALID_HANDLE;
	}

	for ( U32 i = 0; i < Uniform::kNumBuiltInVariables; ++i )
	{
		fBuiltInUniforms[i] = BGFX_INVALID_HANDLE;
	}

	fFrontCommandBuffer = Rtt_NEW( allocator, BgfxCommandBuffer( allocator, *this ) );
	fBackCommandBuffer = Rtt_NEW( allocator, BgfxCommandBuffer( allocator, *this ) );
}

BgfxRenderer::~BgfxRenderer()
{
	if ( fInitialized )
	{
		// Order matters, and differs from the other backends. There the
		// graphics context outlives the renderer, so ~Renderer is free to
		// release GPU resources after we return. Here bgfx::shutdown() below
		// destroys the context, and bgfx::destroy() on a destroyed context
		// dereferences null -- so everything holding a bgfx handle has to be
		// released while we are still the most-derived object.
		DestroyGPUResources();

		DestroyUniforms();

		DestroyReadBackTexture();

		if ( NULL != fOverlayReleaseProc )
		{
			fOverlayReleaseProc( fOverlayUserdata );
		}

		bgfx::shutdown();
	}
}

void
BgfxRenderer::Initialize()
{
	if ( fInitialized )
	{
		return;
	}

	bgfx::Init init;

	// Count on bgfx to pick the backend the platform actually supports, rather
	// than deciding here: on Linux that is OpenGL or Vulkan, on Windows D3D,
	// on Apple Metal.
	init.type = bgfx::RendererType::Count;
	init.resolution.width = fParams.fWidth;
	init.resolution.height = fParams.fHeight;
	init.resolution.reset = fResetFlags;

	init.platformData.nwh = fParams.fNativeWindowHandle;
	init.platformData.ndt = fParams.fNativeDisplayType;
	init.platformData.type = fParams.fIsWayland
		? bgfx::NativeWindowHandleType::Wayland
		: bgfx::NativeWindowHandleType::Default;

	if ( !bgfx::init( init ) )
	{
		Rtt_LogException( "ERROR: could not initialize the bgfx renderer\n" );
		return;
	}

	fInitialized = true;

	bgfx::setViewRect( 0, 0, 0, U16( fParams.fWidth ), U16( fParams.fHeight ) );

	CreateUniforms();

	Super::Initialize();
}

void
BgfxRenderer::SetSurfaceSize( U32 width, U32 height )
{
	if ( !fInitialized || 0 == width || 0 == height )
	{
		return;
	}

	if ( width == fParams.fWidth && height == fParams.fHeight )
	{
		return;
	}

	fParams.fWidth = width;
	fParams.fHeight = height;

	// Rebuilds the swapchain. bgfx defers the work to the next frame, so this
	// is safe to call from the event loop.
	bgfx::reset( width, height, fResetFlags );

	// The window's view rect does not follow the reset, and views created for
	// render targets set their own, so only view 0 needs updating here.
	bgfx::setViewRect( 0, 0, 0, U16( width ), U16( height ) );
}

void
BgfxRenderer::SetMultisampleEnabled( bool enabled )
{
	if ( enabled == fMultisampleEnabled )
	{
		return;
	}

	fMultisampleEnabled = enabled;

	// The window's backbuffer only. bgfx can make a render target multisampled
	// too, but such a texture cannot be blitted from or read back, which is what
	// Corona does with every one it creates -- a snapshot samples it, and
	// display.save reads it. The GL backend this replaces draws the same line:
	// glEnable( GL_MULTISAMPLE ) affects whatever the window was created with,
	// and its framebuffer objects have plain texture attachments.
	//
	// Four samples: the most every backend bgfx supports offers without
	// question, and what "antialias = true" is understood to mean elsewhere.
	// Corona has no way to ask for a particular count.
	const U32 flags = enabled
		? ( fResetFlags | BGFX_RESET_MSAA_X4 )
		: ( fResetFlags & ~U32( BGFX_RESET_MSAA_X4 ) );

	if ( flags == fResetFlags )
	{
		return;
	}

	fResetFlags = flags;

	if ( fInitialized )
	{
		bgfx::reset( fParams.fWidth, fParams.fHeight, fResetFlags );

		// A reset drops the window's view rect, which nothing else restores.
		bgfx::setViewRect( 0, 0, 0, U16( fParams.fWidth ), U16( fParams.fHeight ) );
	}
}

void
BgfxRenderer::CreateUniforms()
{
	// The shell declares these samplers; the names have to agree with it.
	static const char* kSamplerNames[Texture::kNumUnits] =
	{
		"u_FillSampler0",
		"u_FillSampler1",
		"u_MaskSampler0",
		"u_MaskSampler1",
		"u_MaskSampler2",
	};

	for ( U32 i = 0; i < Texture::kNumUnits; ++i )
	{
		fSamplerUniforms[i] = bgfx::createUniform( kSamplerNames[i], bgfx::UniformType::Sampler );
	}

	for ( U32 i = 0; i < Uniform::kNumBuiltInVariables; ++i )
	{
		fBuiltInUniforms[i] = bgfx::createUniform( kBuiltInUniformNames[i], BuiltInUniformType( i ) );
	}
}

void
BgfxRenderer::DestroyUniforms()
{
	for ( U32 i = 0; i < Texture::kNumUnits; ++i )
	{
		if ( bgfx::isValid( fSamplerUniforms[i] ) )
		{
			bgfx::destroy( fSamplerUniforms[i] );
			fSamplerUniforms[i] = BGFX_INVALID_HANDLE;
		}
	}

	for ( U32 i = 0; i < Uniform::kNumBuiltInVariables; ++i )
	{
		if ( bgfx::isValid( fBuiltInUniforms[i] ) )
		{
			bgfx::destroy( fBuiltInUniforms[i] );
			fBuiltInUniforms[i] = BGFX_INVALID_HANDLE;
		}
	}

	for ( NamedUniformMap::iterator it = fNamedUniforms.begin(); it != fNamedUniforms.end(); ++it )
	{
		if ( bgfx::isValid( it->second ) )
		{
			bgfx::destroy( it->second );
		}
	}

	fNamedUniforms.clear();

	fUniformLimitReported = false;
}

bgfx::UniformHandle
BgfxRenderer::GetSamplerUniform( U32 unit )
{
	if ( unit >= Texture::kNumUnits )
	{
		return BGFX_INVALID_HANDLE;
	}

	return fSamplerUniforms[unit];
}

bgfx::UniformHandle
BgfxRenderer::GetBuiltInUniform( U32 index )
{
	if ( index >= Uniform::kNumBuiltInVariables )
	{
		return BGFX_INVALID_HANDLE;
	}

	return fBuiltInUniforms[index];
}

U16
BgfxRenderer::GetBuiltInUniformElementCount( U32 index, U32 sizeInBytes )
{
	const bgfx::UniformType::Enum type = BuiltInUniformType( index );

	// A matrix uniform is one element, however many vectors it occupies.
	if ( bgfx::UniformType::Mat4 == type || bgfx::UniformType::Mat3 == type )
	{
		return 1;
	}

	const U32 vectorCount = sizeInBytes / ( 4 * sizeof( float ) );

	return U16( vectorCount > 0 ? vectorCount : 1 );
}

bgfx::UniformHandle
BgfxRenderer::GetNamedUniform( const char* name, U32 sizeInBytes )
{
	if ( !name || '\0' == name[0] )
	{
		return BGFX_INVALID_HANDLE;
	}

	NamedUniformMap::iterator it = fNamedUniforms.find( name );

	if ( it != fNamedUniforms.end() )
	{
		return it->second;
	}

	const U16 vectorCount = U16( sizeInBytes / ( 4 * sizeof( float ) ) );

	bgfx::UniformHandle handle = bgfx::createUniform(
		  name
		, bgfx::UniformType::Vec4
		, vectorCount > 0 ? vectorCount : 1
		);

	// bgfx's uniform pool is fixed at compile time (see BGFX_CONFIG_MAX_UNIFORMS
	// in cmake/bgfx.cmake), and a project with enough distinct kernel variables
	// can still reach the end of it. An invalid handle is not cached: it would
	// turn a temporary exhaustion into a permanent one for that name, and the
	// caller already knows to skip a uniform it cannot get.
	if ( !bgfx::isValid( handle ) )
	{
		if ( !fUniformLimitReported )
		{
			fUniformLimitReported = true;

			Rtt_LogException( "ERROR: this project uses more shader variables than the renderer can hold (%u), so some effects will draw with stale values\n", U32( fNamedUniforms.size() ) );
		}

		return handle;
	}

	fNamedUniforms[name] = handle;

	return handle;
}

bgfx::ViewId
BgfxRenderer::AcquireViewId()
{
	const bgfx::Caps* caps = bgfx::getCaps();
	const U32 limit = caps ? caps->limits.maxViews : 256;

	// Every render target bound in a frame takes an id of its own, and a scene
	// with enough filters and snapshots can ask for more than bgfx has. Reusing
	// the last one draws into the right target with the wrong ordering, which
	// is a good deal better than handing bgfx an id it will reject.
	if ( U32( fNextViewId ) >= limit )
	{
		// Once a frame, not once per target: a frame that overruns the limit
		// overruns it for every target after that one, and a warning per bind
		// would bury the log.
		if ( !fViewIdsExhausted )
		{
			fViewIdsExhausted = true;

			Rtt_LogException( "WARNING: this frame needs more than the %u render passes the renderer allows; some may draw out of order\n", limit );
		}

		return bgfx::ViewId( limit - 1 );
	}

	return fNextViewId++;
}

void
BgfxRenderer::ResetViewIds()
{
	fNextViewId = 1;
	fViewIdsExhausted = false;
}

void
BgfxRenderer::SetOverlay( OverlayProc proc, OverlayReleaseProc releaseProc, void* userdata )
{
	fOverlayProc = proc;
	fOverlayReleaseProc = releaseProc;
	fOverlayUserdata = userdata;
}

void
BgfxRenderer::InvokeOverlay()
{
	if ( !fInitialized || NULL == fOverlayProc )
	{
		return;
	}

	// The window's own size, not the runtime's viewport: the overlay draws
	// over the whole window, including the strip above the content that the
	// menu bar occupies.
	fOverlayProc( fOverlayUserdata, AcquireViewId(), fParams.fWidth, fParams.fHeight );
}

void
BgfxRenderer::RenderOverlayFrame( bool clear )
{
	if ( !fInitialized )
	{
		return;
	}

	if ( clear )
	{
		bgfx::setViewClear( 0, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x000000ff );
		bgfx::setViewRect( 0, 0, 0, U16( fParams.fWidth ), U16( fParams.fHeight ) );

		// A view with nothing submitted to it is skipped, clear and all, so
		// view 0 needs something to make the clear happen.
		bgfx::touch( 0 );
	}

	InvokeOverlay();

	bgfx::frame();

	ResetViewIds();

	if ( clear )
	{
		// Left as the content path expects to find it: the runtime sets its
		// own clear when it draws, but only once it has something to clear
		// for, and a black clear left behind here would fight it.
		bgfx::setViewClear( 0, BGFX_CLEAR_NONE );
	}
}

bool
BgfxRenderer::EnsureReadBackTexture( U16 width, U16 height, bgfx::TextureFormat::Enum format )
{
	if ( bgfx::isValid( fReadBackTexture )
	  && width == fReadBackWidth
	  && height == fReadBackHeight
	  && format == fReadBackFormat )
	{
		return true;
	}

	DestroyReadBackTexture();

	const bgfx::Caps* caps = bgfx::getCaps();

	if ( !caps || 0 == ( caps->supported & BGFX_CAPS_TEXTURE_READ_BACK ) )
	{
		Rtt_LogException( "ERROR: this renderer cannot read pixels back from the GPU, so the capture is empty\n" );
		return false;
	}

	fReadBackTexture = bgfx::createTexture2D(
		  width
		, height
		, false
		, 1
		, format
		, BGFX_TEXTURE_READ_BACK | BGFX_TEXTURE_BLIT_DST
		);

	if ( !bgfx::isValid( fReadBackTexture ) )
	{
		return false;
	}

	bgfx::TextureInfo info;
	bgfx::calcTextureSize( info, width, height, 1, false, false, 1, format );

	fReadBackBuffer = static_cast< U8* >( malloc( info.storageSize ) );
	fReadBackBufferSize = info.storageSize;

	if ( !fReadBackBuffer )
	{
		DestroyReadBackTexture();
		return false;
	}

	fReadBackWidth = width;
	fReadBackHeight = height;
	fReadBackFormat = format;

	return true;
}

void
BgfxRenderer::DestroyReadBackTexture()
{
	if ( bgfx::isValid( fReadBackTexture ) )
	{
		bgfx::destroy( fReadBackTexture );
		fReadBackTexture = BGFX_INVALID_HANDLE;
	}

	free( fReadBackBuffer );

	fReadBackBuffer = NULL;
	fReadBackBufferSize = 0;
	fReadBackWidth = 0;
	fReadBackHeight = 0;
}

// Corona's capture API is synchronous: display.save and display.captureScreen
// hand back a finished image. bgfx readback is not -- it names the frame the
// pixels will be ready in -- so this drives frames until that one arrives. The
// cost is a couple of extra presents at the moment a capture is taken, which
// is what a synchronous readback costs on any modern API.
void
BgfxRenderer::CaptureFrameBuffer( RenderingStream & stream, BufferBitmap & bitmap, S32 x_in_pixels, S32 y_in_pixels, S32 w_in_pixels, S32 h_in_pixels )
{
	if ( w_in_pixels <= 0 || h_in_pixels <= 0 )
	{
		return;
	}

	FrameBufferObject* fbo = GetFrameBufferObject();

	// Corona always renders a capture into a framebuffer of its own before
	// asking for it, so there is nothing sensible to read otherwise: bgfx
	// cannot read the window's backbuffer.
	if ( !fbo || !fbo->GetTexture() )
	{
		Rtt_LogException( "ERROR: the bgfx backend can only capture from a framebuffer, not from the window\n" );
		return;
	}

	BgfxTexture* source = static_cast< BgfxTexture* >( fbo->GetTexture()->GetGPUResource() );


	if ( !source || !bgfx::isValid( source->GetTexture() ) )
	{
		return;
	}

	const U16 width = U16( w_in_pixels );
	const U16 height = U16( h_in_pixels );

	if ( !EnsureReadBackTexture( width, height, source->GetFormat() ) )
	{
		return;
	}

	// The blit is work for the next frame, since the frame that drew the
	// capture has already been submitted by the time this runs.
	const bgfx::ViewId view = AcquireViewId();

	bgfx::blit(
		  view
		, fReadBackTexture
		, 0
		, 0
		, source->GetTexture()
		, U16( x_in_pixels )
		, U16( y_in_pixels )
		, width
		, height
		);

	const U32 frameAvailable = bgfx::readTexture( fReadBackTexture, fReadBackBuffer );

	// bgfx::frame() returns the number of the frame just submitted, so this
	// stops as soon as the one carrying the readback has gone through.
	for ( U32 frame = bgfx::frame(); frame < frameAvailable; frame = bgfx::frame() )
	{
	}

	ResetViewIds();

	void* destination = bitmap.WriteAccess();

	if ( !destination )
	{
		return;
	}

	const U32 bytesPerPixel = U32( PlatformBitmap::BytesPerPixel( bitmap.GetFormat() ) );

	// What came back is the texture's own bytes, and the texture need not be laid
	// out the way the bitmap says it is: a kBGRA render target falls back to
	// RGBA8 where the backend has no BGRA8 (see TextureFormat in
	// Rtt_BgfxTexture.cpp), and then the two disagree on where red and blue sit.
	bool swapRedAndBlue = false;

	if ( !ReadBackMatchesBitmap( bitmap.GetFormat(), bytesPerPixel, swapRedAndBlue ) )
	{
		return;
	}

	// Corona expects what glReadPixels gives it: rows from the bottom up. The
	// texture already holds them that way on every backend, because anything
	// drawn into one has its clip-space Y flipped where the framebuffer origin
	// is not at the bottom left (see BgfxCommandBuffer::FlipsOffscreenY), so
	// the rows only have to be copied across -- row by row, since the readback
	// texture and the bitmap need not be the same width.
	const U32 rowBytes = Min( U32( bitmap.Width() ), U32( fReadBackWidth ) ) * bytesPerPixel;
	const U32 rows = Min( U32( bitmap.Height() ), U32( fReadBackHeight ) );

	U8* dst = static_cast< U8* >( destination );

	for ( U32 row = 0; row < rows; ++row )
	{
		U8* destinationRow = dst + row * U32( bitmap.Width() ) * bytesPerPixel;
		const U8* sourceRow = fReadBackBuffer + row * U32( fReadBackWidth ) * bytesPerPixel;

		if ( swapRedAndBlue )
		{
			for ( U32 offset = 0; offset < rowBytes; offset += 4 )
			{
				destinationRow[offset + 0] = sourceRow[offset + 2];
				destinationRow[offset + 1] = sourceRow[offset + 1];
				destinationRow[offset + 2] = sourceRow[offset + 0];
				destinationRow[offset + 3] = sourceRow[offset + 3];
			}
		}
		else
		{
			memcpy( destinationRow, sourceRow, rowBytes );
		}
	}
}

// Whether the bytes the readback texture holds can be handed to a bitmap of this
// format, and if so whether red and blue have to change places on the way.
bool
BgfxRenderer::ReadBackMatchesBitmap( PlatformBitmap::Format format, U32 bytesPerPixel, bool& swapRedAndBlue ) const
{
	swapRedAndBlue = false;

	if ( U32( BgfxTexture::BytesPerPixel( fReadBackFormat ) ) != bytesPerPixel )
	{
		Rtt_LogException( "ERROR: a capture cannot be read back into a bitmap of a different pixel size\n" );
		return false;
	}

	// Only the two four-channel layouts ever reach a capture; anything else is
	// copied across as it came, which is what the pixel-size check above allows.
	const bool sourceIsBGRA = ( bgfx::TextureFormat::BGRA8 == fReadBackFormat );
	const bool sourceIsRGBA = ( bgfx::TextureFormat::RGBA8 == fReadBackFormat );

	if ( !sourceIsBGRA && !sourceIsRGBA )
	{
		return true;
	}

	// PlatformBitmap names the channels from the most significant byte down, so
	// on a little-endian machine kRGBA is the bytes A,B,G,R -- but the capture
	// path is written around the byte order the renderer produces (see the note
	// in BufferBitmap::UndoPremultipliedAlpha), which is alpha last. So kRGBA
	// here means R,G,B,A in memory and kBGRA means B,G,R,A.
	switch ( format )
	{
		case PlatformBitmap::kRGBA:
		case PlatformBitmap::kABGR:
			swapRedAndBlue = sourceIsBGRA;
			return true;

		case PlatformBitmap::kBGRA:
		case PlatformBitmap::kARGB:
			swapRedAndBlue = sourceIsRGBA;
			return true;

		default:
			return true;
	}
}

GPUResource*
BgfxRenderer::Create( const CPUResource* resource )
{
	switch ( resource->GetType() )
	{
		case CPUResource::kFrameBufferObject:	return new BgfxFrameBufferObject;
		case CPUResource::kGeometry:			return new BgfxGeometry;
		case CPUResource::kProgram:				return new BgfxProgram;
		case CPUResource::kTexture:				return new BgfxTexture;
		case CPUResource::kUniform:				return NULL;

		default:
			Rtt_ASSERT_NOT_REACHED();
			return NULL;
	}
}

Renderer*
BgfxExports::CreateBgfxRenderer( Rtt_Allocator* allocator, const BgfxSurfaceParams& params )
{
	return Rtt_NEW( allocator, BgfxRenderer( allocator, params ) );
}

void
BgfxExports::ClaimStatics()
{
	// Destructors run in reverse order of registration, and a function-local
	// static registers its own the first time it is reached -- so a static
	// created after an atexit() call is destroyed before that handler runs. bx
	// has one of these: the allocator bx::Thread takes its queue from, reached
	// first when bgfx::init() starts the render thread, and used again when
	// bgfx::shutdown() frees that queue. A host that shuts the runtime down from
	// an atexit handler (which is how Lua's os.exit() has to be survived, since
	// it never returns to main) would find it already destroyed, and freeing
	// through a destroyed allocator is a pure virtual call.
	//
	// Creating a thread object here and dropping it is what constructs that
	// allocator early. It never runs anything: bx::Thread only starts a thread
	// when init() is called on it.
	bx::Thread unused;

	(void)unused;
}

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------
