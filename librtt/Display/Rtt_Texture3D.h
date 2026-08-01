//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Corona game engine.
// For overview and more information on licensing please refer to README.md
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////

#ifndef _Rtt_Texture3D_H__
#define _Rtt_Texture3D_H__

#include "Core/Rtt_Types.h"

#include <vector>

// ----------------------------------------------------------------------------

namespace Rtt
{

// ----------------------------------------------------------------------------

// An image a 3D material samples, decoded to RGBA8 and waiting to be uploaded.
//
// Separate from Corona's Texture and TextureFactory, which are built around a
// file path resolved through the platform and a paint that owns the result. A
// material's maps mostly do not come from files at all: a .glb keeps its images
// inside its own binary chunk, so there is no path to resolve and no bitmap the
// factory could be asked for. What is shared instead is the arrangement Mesh3D
// already uses -- CPU-side data plus an id the renderer stamps in -- so the 3D
// pipeline owns every GPU resource of its own and Display code sees no bgfx.
//
// Reference counted, and shared: a glTF file whose ten materials name one image
// gets one Texture3D, uploaded once.
class Texture3D
{
	public:
		typedef Texture3D Self;

	public:
		Texture3D();
		~Texture3D();

		void Retain() { ++fRefCount; }
		void Release() { if ( --fRefCount == 0 ) { delete this; } }

	public:
		// Both decode PNG, JPEG, TGA, BMP, PSD and GIF, and return NULL if the
		// data is not an image in any of them -- having logged why, since the only
		// useful response to a corrupt texture is to report it and draw untextured.
		//
		// The result has one reference, which the caller owns.
		static Texture3D* NewFromFile( const char* path );

		// For an image embedded in a model file, which is where a .glb keeps all
		// of them. `name` is used only in log messages.
		static Texture3D* NewFromMemory( const U8* bytes, U32 length, const char* name );

	public:
		U32 GetWidth() const { return fWidth; }
		U32 GetHeight() const { return fHeight; }

		// Tightly packed RGBA8, four bytes per pixel, top row first. Empty once
		// the renderer has taken it; see DiscardPixels.
		const std::vector< U8 >& GetPixels() const { return fPixels; }

		// Whether the values are gamma-encoded, as colour images are and as data
		// images -- a metallic-roughness or normal map -- are not. The renderer
		// turns this into an sRGB texture format so that the hardware linearises
		// on read, which is both free and correct; doing it in the shader would
		// mean a pow() per texture per pixel.
		bool IsColorData() const { return fIsColorData; }
		void SetColorData( bool value ) { fIsColorData = value; }

		// Frees the decoded pixels, for the renderer to call once it has copied
		// them to the GPU. Textures are the largest thing a model brings in -- a
		// single 2048-square map is 16MB decoded -- and after the upload nothing
		// reads them again.
		void DiscardPixels();

		// The handle the renderer stashed for this image's GPU texture, or
		// kInvalidGpuId if it has none yet. Opaque here, as Mesh3D's is.
		enum { kInvalidGpuId = ~0u };

		U32 GetGpuId() const { return fGpuId; }
		void SetGpuId( U32 id ) { fGpuId = id; }

	private:
		std::vector< U8 > fPixels;
		U32 fWidth;
		U32 fHeight;
		bool fIsColorData;
		U32 fGpuId;
		int fRefCount;
};

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------

#endif // _Rtt_Texture3D_H__
