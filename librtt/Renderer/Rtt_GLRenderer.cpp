//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Corona game engine.
// For overview and more information on licensing please refer to README.md 
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////

#include "Renderer/Rtt_GLRenderer.h"

#include "Renderer/Rtt_GLCommandBuffer.h"
#include "Renderer/Rtt_GLFrameBufferObject.h"
#include "Renderer/Rtt_GLGeometry.h"
#include "Renderer/Rtt_GLProgram.h"
#include "Renderer/Rtt_GLTexture.h"
#include "Renderer/Rtt_CPUResource.h"
#include "Renderer/Rtt_GL.h"
#include "Display/Rtt_BufferBitmap.h"
#include "Core/Rtt_Assert.h"

// TODO: Temporary hack
#ifdef Rtt_IPHONE_ENV
#include "../platform/iphone/Rtt_IPhoneGLVideoTexture.h"
#endif

// ----------------------------------------------------------------------------

namespace Rtt
{

// ----------------------------------------------------------------------------

GLRenderer::GLRenderer( Rtt_Allocator* allocator )
:   Super( allocator )
{
	fFrontCommandBuffer = Rtt_NEW( allocator, GLCommandBuffer( allocator ) );
	fBackCommandBuffer = Rtt_NEW( allocator, GLCommandBuffer( allocator ) );
}

// Moved here verbatim from Rtt_GPUStream.cpp, whose only remaining callers were
// the capture below.
static GLenum
GL_GetPixelFormat( PlatformBitmap::Format format )
{
#	ifdef Rtt_OPENGLES

		switch ( format )
		{
			case PlatformBitmap::kRGBA:
				return GL_RGBA;
			case PlatformBitmap::kMask:
				return GL_ALPHA;
			case PlatformBitmap::kRGB:
			case PlatformBitmap::kBGRA:
			case PlatformBitmap::kARGB:
			default:
				Rtt_ASSERT_NOT_IMPLEMENTED();
				return GL_ALPHA;
		}

#	else // Not Rtt_OPENGLES.

		switch ( format )
		{
			case PlatformBitmap::kBGRA:
			case PlatformBitmap::kARGB:
				return GL_BGRA;
			case PlatformBitmap::kRGBA:
				return GL_RGBA;
			case PlatformBitmap::kRGB:
				return GL_BGR;
			case PlatformBitmap::kMask:
				return GL_ALPHA;
			default:
				Rtt_ASSERT_NOT_IMPLEMENTED();
				return GL_ALPHA;
		}

#	endif // Rtt_OPENGLES
}

static GLenum
GL_GetPixelType( PlatformBitmap::Format format )
{
#	ifdef Rtt_OPENGLES

		switch( format )
		{
			case PlatformBitmap::kMask:
				return GL_UNSIGNED_BYTE;
			case PlatformBitmap::kBGRA:
			case PlatformBitmap::kARGB:
			case PlatformBitmap::kRGBA:
			default:
				Rtt_ASSERT_NOT_IMPLEMENTED();
				return GL_UNSIGNED_BYTE;
		}

#	else // Not Rtt_OPENGLES.

		switch( format )
		{
			case PlatformBitmap::kBGRA:
				#ifdef Rtt_BIG_ENDIAN
					return GL_UNSIGNED_INT_8_8_8_8_REV;
				#else
					return GL_UNSIGNED_INT_8_8_8_8;
				#endif
			case PlatformBitmap::kARGB:
				#ifdef Rtt_BIG_ENDIAN
					return GL_UNSIGNED_INT_8_8_8_8;
				#else
					return GL_UNSIGNED_INT_8_8_8_8_REV;
				#endif
			case PlatformBitmap::kRGBA:
				#ifdef Rtt_BIG_ENDIAN
					return GL_UNSIGNED_INT_8_8_8_8_REV;
				#else
					return GL_UNSIGNED_INT_8_8_8_8;
				#endif
			case PlatformBitmap::kMask:
				return GL_UNSIGNED_BYTE;
			default:
				Rtt_ASSERT_NOT_IMPLEMENTED();
				return GL_UNSIGNED_BYTE;
		}

#	endif // Rtt_OPENGLES
}

void
GLRenderer::CaptureFrameBuffer( RenderingStream & stream, BufferBitmap & bitmap, S32 x_in_pixels, S32 y_in_pixels, S32 w_in_pixels, S32 h_in_pixels )
{
#ifdef Rtt_OPENGLES
	const GLenum kFormat = GL_RGBA;
	const GLenum kType = GL_UNSIGNED_BYTE;
#else
	PlatformBitmap::Format format = bitmap.GetFormat();
	const GLenum kFormat = GL_GetPixelFormat( format );
	const GLenum kType = GL_GetPixelType( format );
#endif

	glReadPixels( x_in_pixels,
					y_in_pixels,
					w_in_pixels,
					h_in_pixels,
					kFormat,
					kType,
					bitmap.WriteAccess() );
}

GPUResource*
GLRenderer::Create( const CPUResource* resource )
{
	switch( resource->GetType() )
	{
		case CPUResource::kFrameBufferObject: return new GLFrameBufferObject;
		case CPUResource::kGeometry: return new GLGeometry;
		case CPUResource::kProgram: return new GLProgram;
		case CPUResource::kTexture: return new GLTexture;
		case CPUResource::kUniform: return NULL;
#ifdef Rtt_IPHONE_ENV
		case CPUResource::kVideoTexture: return new IPhoneGLVideoTexture;
#endif
		default: Rtt_ASSERT_NOT_REACHED(); return NULL;
	}
}

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------
