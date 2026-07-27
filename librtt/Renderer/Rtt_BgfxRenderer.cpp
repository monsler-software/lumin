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

// Uniforms created for names that only exist once a shader has been compiled,
// i.e. whatever a defineEffect kernel declared. Keyed by name because that is
// all bgfx identifies them by.
static std::map< std::string, bgfx::UniformHandle >&
NamedUniforms()
{
	static std::map< std::string, bgfx::UniformHandle > sNamedUniforms;
	return sNamedUniforms;
}

// ----------------------------------------------------------------------------

BgfxRenderer::BgfxRenderer( Rtt_Allocator* allocator, const BgfxSurfaceParams& params )
:	Super( allocator ),
	fParams( params ),
	fInitialized( false ),
	fNextViewId( 1 ), // view 0 is the window
	fReadBackTexture( BGFX_INVALID_HANDLE ),
	fReadBackWidth( 0 ),
	fReadBackHeight( 0 ),
	fReadBackFormat( bgfx::TextureFormat::RGBA8 ),
	fReadBackBuffer( NULL ),
	fReadBackBufferSize( 0 )
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
	init.resolution.reset = BGFX_RESET_VSYNC;

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
	bgfx::reset( width, height, BGFX_RESET_VSYNC );

	// The window's view rect does not follow the reset, and views created for
	// render targets set their own, so only view 0 needs updating here.
	bgfx::setViewRect( 0, 0, 0, U16( width ), U16( height ) );
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

	std::map< std::string, bgfx::UniformHandle >& named = NamedUniforms();

	for ( std::map< std::string, bgfx::UniformHandle >::iterator it = named.begin(); it != named.end(); ++it )
	{
		if ( bgfx::isValid( it->second ) )
		{
			bgfx::destroy( it->second );
		}
	}

	named.clear();
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

	std::map< std::string, bgfx::UniformHandle >& named = NamedUniforms();
	std::map< std::string, bgfx::UniformHandle >::iterator it = named.find( name );

	if ( it != named.end() )
	{
		return it->second;
	}

	const U16 vectorCount = U16( sizeInBytes / ( 4 * sizeof( float ) ) );

	bgfx::UniformHandle handle = bgfx::createUniform(
		  name
		, bgfx::UniformType::Vec4
		, vectorCount > 0 ? vectorCount : 1
		);

	named[name] = handle;

	return handle;
}

bgfx::ViewId
BgfxRenderer::AcquireViewId()
{
	return fNextViewId++;
}

void
BgfxRenderer::ResetViewIds()
{
	fNextViewId = 1;
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
	const U32 bitmapBytes = U32( bitmap.Width() * bitmap.Height() ) * bytesPerPixel;

	const bgfx::Caps* caps = bgfx::getCaps();

	// Corona expects what glReadPixels gives it: rows from the bottom up, since
	// that is the convention its offscreen projection is built for. bgfx hands
	// back the texture in its own memory order, which is bottom-up only on the
	// backends whose textures start at the bottom left -- OpenGL. Everywhere
	// else the rows arrive top-down and have to be turned over, or the capture
	// comes out upside down.
	if ( caps && !caps->originBottomLeft )
	{
		const U32 rowBytes = Min( U32( bitmap.Width() ) * bytesPerPixel, U32( fReadBackWidth ) * bytesPerPixel );
		const U32 rows = Min( U32( bitmap.Height() ), U32( fReadBackHeight ) );

		U8* dst = static_cast< U8* >( destination );

		for ( U32 row = 0; row < rows; ++row )
		{
			memcpy(
				  dst + ( rows - 1 - row ) * U32( bitmap.Width() ) * bytesPerPixel
				, fReadBackBuffer + row * U32( fReadBackWidth ) * bytesPerPixel
				, rowBytes
				);
		}

		return;
	}

	memcpy( destination, fReadBackBuffer, Min( fReadBackBufferSize, bitmapBytes ) );
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

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------
