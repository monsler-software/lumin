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
		case Uniform::kMaskMatrix0:
		case Uniform::kMaskMatrix1:
		case Uniform::kMaskMatrix2:
			return bgfx::UniformType::Mat4;

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
	fNextViewId( 1 ) // view 0 is the window
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
		DestroyUniforms();

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
	if ( bgfx::UniformType::Mat4 == BuiltInUniformType( index ) )
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

void
BgfxRenderer::CaptureFrameBuffer( RenderingStream & stream, BufferBitmap & bitmap, S32 x_in_pixels, S32 y_in_pixels, S32 w_in_pixels, S32 h_in_pixels )
{
	// bgfx::readTexture reports back two frames later, which display.save and
	// display.captureScreen expect to have finished on return. Wiring that up
	// means giving those a completion path they do not have today.
	Rtt_ASSERT_NOT_IMPLEMENTED();
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
