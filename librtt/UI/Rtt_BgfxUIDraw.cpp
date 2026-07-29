//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Corona game engine.
// For overview and more information on licensing please refer to README.md
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////

#include "Core/Rtt_Build.h"

#include "UI/Rtt_BgfxUIDraw.h"

#include "Renderer/Rtt_BgfxShaderCompiler.h"

#include "Core/Rtt_Math.h"

#include <bx/math.h>

#include <string.h>

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include <stb_truetype.h>

// ----------------------------------------------------------------------------

namespace Rtt
{

// ----------------------------------------------------------------------------

// The atlas the glyphs are baked into. Large enough that the whole printable
// ASCII range fits at any size the chrome would use, and small enough to be
// beneath notice as a texture.
static const int kAtlasWidth = 512;
static const int kAtlasHeight = 256;

// The rows appended below the baked glyphs and filled solid, which is what a
// filled rectangle samples. Two rather than one so that the texel sampled is
// surrounded by its own colour: a linear filter reaching half a texel past it
// still finds white, rather than bleeding in whatever the bake left behind.
static const int kWhiteRows = 2;

// ----------------------------------------------------------------------------

// What travels between the stages, in the form the runtime shader compiler
// expects -- bgfx has no varying keyword, so the link between the stages is
// this table rather than matching declarations.
static const char* kVaryingDef =
	"vec2 v_texcoord0 : TEXCOORD0;\n"
	"vec4 v_color0    : COLOR0;\n"
	"\n"
	"vec2 a_position  : POSITION;\n"
	"vec2 a_texcoord0 : TEXCOORD0;\n"
	"vec4 a_color0    : COLOR0;\n";

static const char* kVertexShader =
	"$input a_position, a_texcoord0, a_color0\n"
	"$output v_texcoord0, v_color0\n"
	"#include <bgfx_shader.sh>\n"
	"void main()\n"
	"{\n"
	"	gl_Position = mul( u_modelViewProj, vec4( a_position, 0.0, 1.0 ) );\n"
	"	v_texcoord0 = a_texcoord0;\n"
	"	v_color0 = a_color0;\n"
	"}\n";

// The atlas holds coverage, not colour: a glyph's texel says how much of it the
// pixel is covered by, and the solid rows say "all of it". So the sample only
// ever scales alpha, and one shader draws both text and fills.
static const char* kFragmentShader =
	"$input v_texcoord0, v_color0\n"
	"#include <bgfx_shader.sh>\n"
	"SAMPLER2D( s_uiTexture, 0 );\n"
	"void main()\n"
	"{\n"
	"	float coverage = texture2D( s_uiTexture, v_texcoord0 ).x;\n"
	"	gl_FragColor = vec4( v_color0.xyz, v_color0.w * coverage );\n"
	"}\n";

// ----------------------------------------------------------------------------

// Which shaderc profile the backend bgfx picked at run time needs. The same
// choice Rtt_BgfxProgram makes for content shaders; kept separate rather than
// shared because that one is bound up with the effect shell's own conventions.
static const char*
ProfileForRenderer( char shaderType )
{
	switch ( bgfx::getRendererType() )
	{
		case bgfx::RendererType::OpenGL:
			return "120";

		case bgfx::RendererType::OpenGLES:
			return "300_es";

		case bgfx::RendererType::Vulkan:
			return "spirv";

		case bgfx::RendererType::Metal:
			return "metal";

		case bgfx::RendererType::Direct3D11:
		case bgfx::RendererType::Direct3D12:
			return 'v' == shaderType ? "s_5_0" : "p_5_0";

		default:
			return "120";
	}
}

static bgfx::ShaderHandle
CompileStage( const char* source, BgfxShaderCompiler::Stage stage, const char* debugName )
{
	std::vector< U8 > blob;
	std::string messages;

	const bool compiled = BgfxShaderCompiler::Compile(
		  source
		, kVaryingDef
		, stage
		, ProfileForRenderer( BgfxShaderCompiler::kVertex == stage ? 'v' : 'f' )
		, LUMIN_BGFX_SHADER_INCLUDE_DIR
		, debugName
		, blob
		, messages
		);

	if ( !compiled )
	{
		Rtt_LogException( "ERROR: could not compile the simulator's %s shader: %s\n",
			debugName, messages.c_str() );

		return BGFX_INVALID_HANDLE;
	}

	return bgfx::createShader( bgfx::copy( &blob[0], U32( blob.size() ) ) );
}

// ----------------------------------------------------------------------------

BgfxUIDraw::BgfxUIDraw()
:	fInitialized( false ),
	fInBatch( false ),
	fView( 0 ),
	fProgram( BGFX_INVALID_HANDLE ),
	fAtlas( BGFX_INVALID_HANDLE ),
	fSampler( BGFX_INVALID_HANDLE ),
	fAscent( 0.f ),
	fLineHeight( 0.f ),
	fWhiteU( 0.f ),
	fWhiteV( 0.f )
{
	memset( fGlyphs, 0, sizeof( fGlyphs ) );
}

BgfxUIDraw::~BgfxUIDraw()
{
	Finalize();
}

bool
BgfxUIDraw::Initialize( const void* ttf, size_t ttfSize, float pixelHeight )
{
	if ( fInitialized )
	{
		return true;
	}

	if ( !BakeFont( ttf, ttfSize, pixelHeight ) )
	{
		return false;
	}

	if ( !CreateProgram() )
	{
		bgfx::destroy( fAtlas );
		fAtlas = BGFX_INVALID_HANDLE;

		return false;
	}

	fLayout.begin()
		// Two components, matching Vertex: a layout wider than the struct
		// leaves bgfx striding past every vertex after the first.
		.add( bgfx::Attrib::Position, 2, bgfx::AttribType::Float )
		.add( bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float )
		.add( bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true )
		.end();

	fSampler = bgfx::createUniform( "s_uiTexture", bgfx::UniformType::Sampler );

	fInitialized = true;

	return true;
}

void
BgfxUIDraw::Finalize()
{
	if ( bgfx::isValid( fProgram ) )
	{
		bgfx::destroy( fProgram );
		fProgram = BGFX_INVALID_HANDLE;
	}

	if ( bgfx::isValid( fAtlas ) )
	{
		bgfx::destroy( fAtlas );
		fAtlas = BGFX_INVALID_HANDLE;
	}

	if ( bgfx::isValid( fSampler ) )
	{
		bgfx::destroy( fSampler );
		fSampler = BGFX_INVALID_HANDLE;
	}

	fVertices.clear();
	fIndices.clear();

	fInitialized = false;
	fInBatch = false;
}

bool
BgfxUIDraw::BakeFont( const void* ttf, size_t ttfSize, float pixelHeight )
{
	if ( NULL == ttf || 0 == ttfSize || pixelHeight <= 0.f )
	{
		return false;
	}

	const int bakedHeight = kAtlasHeight - kWhiteRows;

	U8* bitmap = new U8[kAtlasWidth * kAtlasHeight];

	memset( bitmap, 0, kAtlasWidth * kAtlasHeight );

	stbtt_bakedchar baked[kCharCount];

	// A negative result means the atlas ran out of room partway through. The
	// rows it did fill are still usable, so this is not fatal: the glyphs that
	// landed draw, and the ones that did not come out blank. At the sizes the
	// chrome uses it does not happen at all.
	const int result = stbtt_BakeFontBitmap(
		static_cast< const unsigned char* >( ttf ), 0, pixelHeight,
		bitmap, kAtlasWidth, bakedHeight, kFirstChar, kCharCount, baked );

	if ( 0 == result )
	{
		delete [] bitmap;

		return false;
	}

	for ( int i = 0; i < kCharCount; ++i )
	{
		fGlyphs[i].fX0 = baked[i].x0;
		fGlyphs[i].fY0 = baked[i].y0;
		fGlyphs[i].fX1 = baked[i].x1;
		fGlyphs[i].fY1 = baked[i].y1;
		fGlyphs[i].fXOffset = baked[i].xoff;
		fGlyphs[i].fYOffset = baked[i].yoff;
		fGlyphs[i].fXAdvance = baked[i].xadvance;
	}

	// The solid rows, and the texel a fill samples: the centre of the second
	// one in, so that neither edge of the atlas nor the baked rows above are
	// within a filter's reach.
	memset( bitmap + kAtlasWidth * bakedHeight, 0xff, kAtlasWidth * kWhiteRows );

	fWhiteU = 1.5f / float( kAtlasWidth );
	fWhiteV = ( float( bakedHeight ) + 1.f ) / float( kAtlasHeight );

	stbtt_fontinfo info;

	if ( stbtt_InitFont( &info, static_cast< const unsigned char* >( ttf ), 0 ) )
	{
		int ascent = 0;
		int descent = 0;
		int lineGap = 0;

		stbtt_GetFontVMetrics( &info, &ascent, &descent, &lineGap );

		const float scale = stbtt_ScaleForPixelHeight( &info, pixelHeight );

		fAscent = ascent * scale;
		fLineHeight = ( ascent - descent + lineGap ) * scale;
	}
	else
	{
		fAscent = pixelHeight * 0.8f;
		fLineHeight = pixelHeight * 1.2f;
	}

	// R8 rather than A8: what a sample's .x holds is the same either way on the
	// backends that support both, but A8 is not universally available and R8 is.
	fAtlas = bgfx::createTexture2D(
		kAtlasWidth, kAtlasHeight, false, 1, bgfx::TextureFormat::R8,
		BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP,
		bgfx::copy( bitmap, kAtlasWidth * kAtlasHeight ) );

	delete [] bitmap;

	return bgfx::isValid( fAtlas );
}

bool
BgfxUIDraw::CreateProgram()
{
	bgfx::ShaderHandle vertex = CompileStage( kVertexShader, BgfxShaderCompiler::kVertex, "simulator chrome vertex" );

	if ( !bgfx::isValid( vertex ) )
	{
		return false;
	}

	bgfx::ShaderHandle fragment = CompileStage( kFragmentShader, BgfxShaderCompiler::kFragment, "simulator chrome fragment" );

	if ( !bgfx::isValid( fragment ) )
	{
		bgfx::destroy( vertex );

		return false;
	}

	// Handing the shaders over: the program owns them from here.
	fProgram = bgfx::createProgram( vertex, fragment, true );

	return bgfx::isValid( fProgram );
}

void
BgfxUIDraw::Begin( bgfx::ViewId view, U32 width, U32 height )
{
	if ( !fInitialized || 0 == width || 0 == height )
	{
		return;
	}

	fView = view;
	fInBatch = true;

	fVertices.clear();
	fIndices.clear();

	// Pixels, with y growing downwards from the top of the window, which is
	// how a menu bar is naturally described and how mouse events arrive. That
	// is bx::mtxOrtho's `top` and `bottom` swapped.
	//
	// The view rect is the whole window. Narrowing it to just the strip the
	// bar occupies was tried, to shrink the overlay's pass over the
	// backbuffer, and it made the bar vanish: a sub-rect viewport does not
	// compose with how the backends disagree about which way NDC y runs.
	// There is no measured cost to justify going back to it -- see the note in
	// Rtt_MenuBar's Render.
	float projection[16];

	bx::mtxOrtho( projection, 0.f, float( width ), float( height ), 0.f, 0.f, 100.f, 0.f,
		bgfx::getCaps()->homogeneousDepth );

	// View state outlives the frame that set it, and the renderer hands the
	// ids out again from the start every frame, so the id this batch was just
	// given may still be pointed at whatever render target held it last frame
	// -- a snapshot's, or a capture's. Left alone, the chrome is drawn into
	// that texture instead of the window, which is how a second menu bar ends
	// up somewhere in the middle of the content, and why the real one at the
	// top comes and goes with whatever the scene happens to render offscreen.
	bgfx::setViewFrameBuffer( fView, BGFX_INVALID_HANDLE );

	// Likewise a scissor rect the target set: it would clip the chrome to
	// whatever region that pass cared about.
	bgfx::setViewScissor( fView ); // no arguments clears it

	bgfx::setViewRect( fView, 0, 0, U16( width ), U16( height ) );
	bgfx::setViewTransform( fView, NULL, projection );

	// Whatever the runtime drew is underneath, so the chrome must not clear it.
	bgfx::setViewClear( fView, BGFX_CLEAR_NONE );

	// Views bgfx has nothing to submit to are skipped, and a view that is
	// skipped never applies its rect -- which matters because these ids are
	// recycled every frame between render targets and this.
	bgfx::setViewMode( fView, bgfx::ViewMode::Sequential );
}

void
BgfxUIDraw::End()
{
	if ( !fInBatch )
	{
		return;
	}

	fInBatch = false;

	if ( fVertices.empty() )
	{
		return;
	}

	const U32 vertexCount = U32( fVertices.size() );
	const U32 indexCount = U32( fIndices.size() );

	// Transient buffers are the right shape for this: the geometry is rebuilt
	// from scratch every frame and is never referenced again once the frame is
	// submitted. If bgfx cannot fit the batch this frame, the chrome is skipped
	// for that frame rather than drawn in pieces.
	if ( bgfx::getAvailTransientVertexBuffer( vertexCount, fLayout ) < vertexCount
		|| bgfx::getAvailTransientIndexBuffer( indexCount ) < indexCount )
	{
		return;
	}

	bgfx::TransientVertexBuffer vertexBuffer;
	bgfx::TransientIndexBuffer indexBuffer;

	bgfx::allocTransientVertexBuffer( &vertexBuffer, vertexCount, fLayout );
	bgfx::allocTransientIndexBuffer( &indexBuffer, indexCount );

	memcpy( vertexBuffer.data, &fVertices[0], vertexCount * sizeof( Vertex ) );
	memcpy( indexBuffer.data, &fIndices[0], indexCount * sizeof( U16 ) );

	bgfx::setVertexBuffer( 0, &vertexBuffer );
	bgfx::setIndexBuffer( &indexBuffer );
	bgfx::setTexture( 0, fSampler, fAtlas );

	// No depth test and no writes to it: the batch is in painter's order
	// already, and a drop-down that overlaps the content below it must blend
	// with it rather than be rejected by whatever the runtime left in the
	// depth buffer.
	bgfx::setState( BGFX_STATE_WRITE_RGB
		| BGFX_STATE_WRITE_A
		| BGFX_STATE_BLEND_FUNC( BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_INV_SRC_ALPHA ) );

	bgfx::submit( fView, fProgram );
}

void
BgfxUIDraw::Quad( float x, float y, float width, float height,
	float u0, float v0, float u1, float v1, U32 abgr )
{
	if ( !fInBatch || width <= 0.f || height <= 0.f )
	{
		return;
	}

	// Indices are 16-bit, so a batch tops out at 65536 vertices. The chrome
	// never approaches that -- a full menu is a few hundred quads -- but a
	// wrapped index would corrupt the whole batch rather than lose the excess,
	// so the excess is dropped instead.
	if ( fVertices.size() + 4 > 65536 )
	{
		return;
	}

	const U16 base = U16( fVertices.size() );

	Vertex vertex;

	vertex.fAbgr = abgr;

	vertex.fX = x;         vertex.fY = y;          vertex.fU = u0; vertex.fV = v0; fVertices.push_back( vertex );
	vertex.fX = x + width; vertex.fY = y;          vertex.fU = u1; vertex.fV = v0; fVertices.push_back( vertex );
	vertex.fX = x + width; vertex.fY = y + height; vertex.fU = u1; vertex.fV = v1; fVertices.push_back( vertex );
	vertex.fX = x;         vertex.fY = y + height; vertex.fU = u0; vertex.fV = v1; fVertices.push_back( vertex );

	fIndices.push_back( base + 0 );
	fIndices.push_back( base + 1 );
	fIndices.push_back( base + 2 );
	fIndices.push_back( base + 0 );
	fIndices.push_back( base + 2 );
	fIndices.push_back( base + 3 );
}

void
BgfxUIDraw::Rect( float x, float y, float width, float height, U32 abgr )
{
	Quad( x, y, width, height, fWhiteU, fWhiteV, fWhiteU, fWhiteV, abgr );
}

void
BgfxUIDraw::Text( float x, float y, const char* text, U32 abgr )
{
	if ( !fInBatch || NULL == text )
	{
		return;
	}

	// y is given as the top of the line, which is where a row's rectangle
	// starts; the glyph offsets are relative to the baseline.
	const float baseline = y + fAscent;

	float pen = x;

	for ( const char* p = text; *p != '\0'; ++p )
	{
		const int c = U8( *p );

		if ( c < kFirstChar || c >= kFirstChar + kCharCount )
		{
			continue;
		}

		const Glyph& glyph = fGlyphs[c - kFirstChar];

		const float width = float( glyph.fX1 - glyph.fX0 );
		const float height = float( glyph.fY1 - glyph.fY0 );

		// Snapped to whole pixels: the atlas is baked at one size and sampled
		// at it, so a glyph that lands on a half-pixel is blurred for nothing.
		const float left = bx::round( pen + glyph.fXOffset );
		const float top = bx::round( baseline + glyph.fYOffset );

		Quad( left, top, width, height,
			float( glyph.fX0 ) / float( kAtlasWidth ),
			float( glyph.fY0 ) / float( kAtlasHeight ),
			float( glyph.fX1 ) / float( kAtlasWidth ),
			float( glyph.fY1 ) / float( kAtlasHeight ),
			abgr );

		pen += glyph.fXAdvance;
	}
}

float
BgfxUIDraw::MeasureText( const char* text ) const
{
	if ( !fInitialized || NULL == text )
	{
		return 0.f;
	}

	float width = 0.f;

	for ( const char* p = text; *p != '\0'; ++p )
	{
		const int c = U8( *p );

		if ( c < kFirstChar || c >= kFirstChar + kCharCount )
		{
			continue;
		}

		width += fGlyphs[c - kFirstChar].fXAdvance;
	}

	return width;
}

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------
