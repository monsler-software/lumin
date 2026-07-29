//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Corona game engine.
// For overview and more information on licensing please refer to README.md
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////

#ifndef _Rtt_BgfxUIDraw_H__
#define _Rtt_BgfxUIDraw_H__

#include "Core/Rtt_Types.h"

#include <vector>

#include <bgfx/bgfx.h>

// ----------------------------------------------------------------------------

namespace Rtt
{

// ----------------------------------------------------------------------------

// The 2D drawing the simulator's own chrome is made of: filled rectangles and
// short runs of text, in window pixels, painted over whatever the runtime just
// rendered.
//
// This is deliberately not a general-purpose UI toolkit. It exists because the
// chrome used to be drawn by three different things -- Dear ImGui on Linux,
// MFC on Windows, AppKit on macOS -- and under bgfx none of them can reach the
// window any more: bgfx owns the swapchain, and there is no GL context left for
// an immediate-mode backend to draw through. Everything the chrome needs turns
// out to be a quad, so that is all this offers.
//
// Every quad goes into one batch and one draw call, in the order it was
// submitted, with the glyph atlas bound throughout. Solid fills use a white
// texel baked into the atlas alongside the glyphs, so a rectangle and a letter
// are the same primitive and no state changes between them -- which is what
// makes a menu, its drop-down and the text on both come out in painter's order
// without any sorting or depth testing.
class BgfxUIDraw
{
	public:
		BgfxUIDraw();
		~BgfxUIDraw();

		// ttf is a font file held in memory, which the caller owns and must
		// keep alive only for this call. pixelHeight is the em size the glyphs
		// are baked at; the atlas is fixed at that size, since the chrome has
		// no need to scale text and a baked atlas cannot be resized.
		//
		// Must be called with bgfx already initialized. Returns false if the
		// font could not be baked or the shaders would not compile, in which
		// case the object stays unusable and every draw call is a no-op --
		// chrome that cannot draw is better than a simulator that will not
		// start.
		bool Initialize( const void* ttf, size_t ttfSize, float pixelHeight );
		void Finalize();

		bool IsInitialized() const { return fInitialized; }

		// Opens a batch aimed at `view`, whose rect is the whole window and
		// whose transform is an orthographic projection in pixels with the
		// origin at the top left -- so the coordinates passed to Rect and Text
		// are the window's own, the ones mouse events arrive in.
		void Begin( bgfx::ViewId view, U32 width, U32 height );

		// Submits the batch. Nothing reaches bgfx before this.
		void End();

		// abgr, matching bgfx's own packing: 0xAABBGGRR.
		void Rect( float x, float y, float width, float height, U32 abgr );

		// text is UTF-8, though only the ASCII range is baked; anything else
		// draws nothing. y is the top of the line, not the baseline, so that
		// callers can lay text out against a row's rectangle.
		void Text( float x, float y, const char* text, U32 abgr );

		// What Text would advance by, for laying out columns and measuring how
		// wide a menu has to be.
		float MeasureText( const char* text ) const;

		// The distance between the tops of two consecutive lines, which is what
		// a row's height is built from.
		float GetLineHeight() const { return fLineHeight; }

	private:
		struct Vertex
		{
			float fX;
			float fY;
			float fU;
			float fV;
			U32 fAbgr;
		};

		// One entry per baked glyph, in the atlas and in advance terms. This
		// mirrors stbtt_bakedchar so that the header does not have to drag
		// stb_truetype in behind it.
		struct Glyph
		{
			U16 fX0, fY0, fX1, fY1;
			float fXOffset, fYOffset;
			float fXAdvance;
		};

		bool BakeFont( const void* ttf, size_t ttfSize, float pixelHeight );
		bool CreateProgram();
		void Quad( float x, float y, float width, float height,
			float u0, float v0, float u1, float v1, U32 abgr );

		static const int kFirstChar = 32;
		static const int kCharCount = 96; // through '~'

		bool fInitialized;
		bool fInBatch;

		bgfx::ViewId fView;

		bgfx::VertexLayout fLayout;
		bgfx::ProgramHandle fProgram;
		bgfx::TextureHandle fAtlas;
		bgfx::UniformHandle fSampler;

		std::vector< Vertex > fVertices;
		std::vector< U16 > fIndices;

		Glyph fGlyphs[kCharCount];
		float fAscent;
		float fLineHeight;

		// The centre of the solid white texel the glyphs share the atlas with,
		// which is what a filled rectangle samples.
		float fWhiteU;
		float fWhiteV;
};

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------

#endif // _Rtt_BgfxUIDraw_H__
