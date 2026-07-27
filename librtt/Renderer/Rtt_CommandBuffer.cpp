//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Corona game engine.
// For overview and more information on licensing please refer to README.md 
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////

#include "Renderer/Rtt_CommandBuffer.h"
#include "Renderer/Rtt_RendererCapabilities.h"
#include "Display/Rtt_ShaderResource.h"

#include "Core/Rtt_Allocator.h"
#include "Core/Rtt_String.h"
#include <stddef.h>

#include "../Core/Rtt_Math.h"

// ----------------------------------------------------------------------------

namespace Rtt
{

// ----------------------------------------------------------------------------

const char RendererCapabilities::kVendor[] = "vendor";
const char RendererCapabilities::kRenderer[] = "renderer";
const char RendererCapabilities::kVersion[] = "version";
const char RendererCapabilities::kShaderVersion[] = "shaderVersion";
const char RendererCapabilities::kExtensions[] = "extensions";

const char *
RendererCapabilities::KeyFromGlString( const char *s )
{
	if ( NULL == s )
	{
		return NULL;
	}
	else if ( Rtt_StringCompare( s, "GL_VENDOR" ) == 0 )
	{
		return kVendor;
	}
	else if ( Rtt_StringCompare( s, "GL_RENDERER" ) == 0 )
	{
		return kRenderer;
	}
	else if ( Rtt_StringCompare( s, "GL_VERSION" ) == 0 )
	{
		return kVersion;
	}
	else if ( Rtt_StringCompare( s, "GL_SHADING_LANGUAGE_VERSION" ) == 0 )
	{
		return kShaderVersion;
	}
	else if ( Rtt_StringCompare( s, "GL_EXTENSIONS" ) == 0 )
	{
		return kExtensions;
	}

	return NULL;
}

RendererCapabilities::~RendererCapabilities()
{
}

// Used until a backend installs its own. The limits are the minimums an ES2
// class device is required to provide, so they are safe to plan around even
// when nobody has answered yet.
class FallbackRendererCapabilities : public RendererCapabilities
{
	public:
		virtual size_t GetMaxUniformVectorsCount() const { return 128 - kReservedUniformVectors; }
		virtual size_t GetMaxVertexTextureUnits() const { return 0; }
		virtual size_t GetMaxTextureSize() const { return 1024; }
		virtual const char *GetString( const char *key ) const { return ""; }
		virtual bool GetSupportsHighPrecisionFragmentShaders() const { return false; }
};

static const FallbackRendererCapabilities kFallbackCapabilities;

const RendererCapabilities *RendererCapabilities::sCurrent = NULL;

const RendererCapabilities&
RendererCapabilities::GetCurrent()
{
	return sCurrent ? *sCurrent : kFallbackCapabilities;
}

void
RendererCapabilities::Set( const RendererCapabilities *capabilities )
{
	sCurrent = capabilities;
}

// ----------------------------------------------------------------------------

size_t
CommandBuffer::GetMaxUniformVectorsCount()
{
	return RendererCapabilities::GetCurrent().GetMaxUniformVectorsCount();
}

size_t
CommandBuffer::GetMaxVertexTextureUnits()
{
	return RendererCapabilities::GetCurrent().GetMaxVertexTextureUnits();
}

size_t
CommandBuffer::GetMaxTextureSize()
{
	return RendererCapabilities::GetCurrent().GetMaxTextureSize();
}

const char *
CommandBuffer::GetRendererString( const char *key )
{
	const char *result = RendererCapabilities::GetCurrent().GetString( key );
	return result ? result : "";
}

const char *
CommandBuffer::GetGlString( const char *s )
{
	const char *key = RendererCapabilities::KeyFromGlString( s );
	return key ? GetRendererString( key ) : "";
}

bool
CommandBuffer::GetGpuSupportsHighPrecisionFragmentShaders()
{
	return RendererCapabilities::GetCurrent().GetSupportsHighPrecisionFragmentShaders();
}

// ----------------------------------------------------------------------------

CommandBuffer::CommandBuffer( Rtt_Allocator* allocator )
:	fAllocator( allocator ),
	fBuffer( NULL ), 
	fOffset( NULL ), 
	fNumCommands( 0 ), 
	fBytesAllocated( 0 ), 
	fBytesUsed( 0 ),
	fDefaultTransformedTime( -1.f ),
	fTimeTransform( NULL )
{

}

CommandBuffer::~CommandBuffer()
{
    if (fBuffer != NULL)
    {
        delete [] fBuffer;
    }

//	Rtt_DELETE( fDefaultTimeTransform );
}

void
CommandBuffer::ReadBytes( void * value, size_t size )
{
	Rtt_ASSERT( fOffset < fBuffer + fBytesAllocated );
	memcpy( value, fOffset, size );
	fOffset += size;
}

void
CommandBuffer::WriteBytes( const void * value, size_t size )
{
	U32 bytesNeeded = fBytesUsed + size;
	if( bytesNeeded > fBytesAllocated )
	{
		U32 doubleSize = fBytesUsed ? 2 * fBytesUsed : 4;
		U32 newSize = Max( bytesNeeded, doubleSize );
		U8* newBuffer = new U8[newSize];

		memcpy( newBuffer, fBuffer, fBytesUsed );
		delete [] fBuffer;

		fBuffer = newBuffer;
		fBytesAllocated = newSize;
	}

	memcpy( fBuffer + fBytesUsed, value, size );
	fBytesUsed += size;
}
 
void
CommandBuffer::PrepareTimeTransforms( float rawTime, const TimeTransform* transform )
{
	fTimeTransform = NULL;

	if ( transform->func )
	{
		fDefaultTransformedTime = transform->Apply( rawTime );
	}
	else
	{
		fDefaultTransformedTime = rawTime;
	}
}

void
CommandBuffer::AcquireTimeTransform( ShaderResource* resource )
{
	fTimeTransform = resource->GetTimeTransform();
}

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------
