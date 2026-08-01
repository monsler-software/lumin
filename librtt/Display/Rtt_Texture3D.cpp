//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Corona game engine.
// For overview and more information on licensing please refer to README.md
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////

#include "Core/Rtt_Build.h"

#include "Display/Rtt_Texture3D.h"

// stb_image, vendored with bimg, which is where the include path comes from.
//
// bimg has its own decoder built on the same header, but reaching it means
// linking bimg_decode, and that target pulls in dav1d and libavif to decode AV1
// stills -- a large build-time cost for formats no model file uses. The header is
// the part that decodes PNG and JPEG, so it is instantiated here instead.
//
// Safe to define the implementation in this translation unit precisely because
// bimg_decode is not linked: it holds the only other copy.
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_NO_HDR
#define STBI_NO_PIC
#define STBI_NO_PNM
#define STBI_NO_LINEAR
#include "stb/stb_image.h"

#include <cstdio>

// ----------------------------------------------------------------------------

namespace Rtt
{

// ----------------------------------------------------------------------------

Texture3D::Texture3D()
:	fWidth( 0 ),
	fHeight( 0 ),
	fIsColorData( true ),
	fGpuId( kInvalidGpuId ),
	fRefCount( 1 )
{
}

Texture3D::~Texture3D()
{
}

void
Texture3D::DiscardPixels()
{
	// swap with an empty vector rather than clear(): clear() keeps the capacity,
	// which for a texture is the whole of what was to be freed.
	std::vector< U8 >().swap( fPixels );
}

// ----------------------------------------------------------------------------

Texture3D*
Texture3D::NewFromMemory( const U8* bytes, U32 length, const char* name )
{
	if ( bytes == NULL || length == 0 )
	{
		return NULL;
	}

	int width = 0;
	int height = 0;
	int channels = 0;

	// Four channels always, whatever the file has: the shader samples rgba, and
	// letting stb expand a greyscale or opaque image here means the pipeline has
	// one format to upload rather than four.
	stbi_uc* pixels = stbi_load_from_memory( bytes, (int) length, &width, &height, &channels, 4 );

	if ( pixels == NULL )
	{
		Rtt_LogException( "ERROR: render: the texture '%s' could not be decoded (%s)\n",
			name != NULL ? name : "<embedded>",
			stbi_failure_reason() != NULL ? stbi_failure_reason() : "unrecognised format" );

		return NULL;
	}

	if ( width <= 0 || height <= 0 )
	{
		stbi_image_free( pixels );

		return NULL;
	}

	Texture3D* texture = new Texture3D;

	texture->fWidth = (U32) width;
	texture->fHeight = (U32) height;

	const size_t size = (size_t) width * (size_t) height * 4;

	texture->fPixels.resize( size );
	memcpy( &texture->fPixels[0], pixels, size );

	stbi_image_free( pixels );

	return texture;
}

Texture3D*
Texture3D::NewFromFile( const char* path )
{
	if ( path == NULL || *path == '\0' )
	{
		return NULL;
	}

	// Read and then decode from memory, rather than through stbi_load: STBI_NO_STDIO
	// is set above, because the embedded case needs the memory entry point anyway
	// and having one decode path means one place for the failure to be reported.
	FILE* file = fopen( path, "rb" );

	if ( file == NULL )
	{
		Rtt_LogException( "ERROR: render: the texture '%s' could not be opened\n", path );

		return NULL;
	}

	fseek( file, 0, SEEK_END );
	const long size = ftell( file );
	fseek( file, 0, SEEK_SET );

	if ( size <= 0 )
	{
		fclose( file );

		Rtt_LogException( "ERROR: render: the texture '%s' is empty\n", path );

		return NULL;
	}

	std::vector< U8 > bytes( (size_t) size );

	const size_t read = fread( &bytes[0], 1, (size_t) size, file );

	fclose( file );

	if ( read == 0 )
	{
		return NULL;
	}

	return NewFromMemory( &bytes[0], (U32) read, path );
}

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------
