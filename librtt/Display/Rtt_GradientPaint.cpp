//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Corona game engine.
// For overview and more information on licensing please refer to README.md 
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////

#include "Core/Rtt_Build.h"

#include "Display/Rtt_GradientPaint.h"

#include "Core/Rtt_String.h"
#include "Display/Rtt_BufferBitmap.h"
#include "Display/Rtt_Display.h"
#include "Display/Rtt_DisplayObject.h"
#include "Display/Rtt_GradientPaintAdapter.h"
#include "Display/Rtt_TextureFactory.h"
#include "Display/Rtt_TextureResource.h"

// ----------------------------------------------------------------------------

namespace Rtt
{

// ----------------------------------------------------------------------------

GradientPaint::Direction
GradientPaint::StringToDirection( const char *str )
{
	Direction result = kDefaultDirection;

	if ( str )
	{
		if ( 0 == Rtt_StringCompareNoCase( str, "up" ) )
		{
			result = kUpDirection;
		}
		else if ( 0 == Rtt_StringCompareNoCase( str, "left" ) )
		{
			result = kLeftDirection;
		}
		else if ( 0 == Rtt_StringCompareNoCase( str, "right" ) )
		{
			result = kRightDirection;
		}
		else if ( 0 == Rtt_StringCompareNoCase( str, "down" ) )
		{
			result = kDownDirection;
		}
	}

	return result;
}

// ----------------------------------------------------------------------------

namespace { // anonymous
	enum {
		kBufferWidth = 1,
		kBufferHeight = 256
	};
}

static void
PremultiplyColor( Color color, float& r, float& g, float& b, float& a )
{
	ColorUnion c;
	c.pixel = color;

	a = c.rgba.a;
	const float alpha = a / 255.f;
	r = c.rgba.r * alpha;
	g = c.rgba.g * alpha;
	b = c.rgba.b * alpha;
}

static void
FillGradientPixels( BufferBitmap *bitmap, Color start, Color end )
{
	Color *pixels = (Color *)bitmap->WriteAccess();

	float startR, startG, startB, startA;
	float endR, endG, endB, endA;
	PremultiplyColor( start, startR, startG, startB, startA );
	PremultiplyColor( end, endR, endG, endB, endA );

	for ( int i = 0, iMax = (int)kBufferHeight - 1; i <= iMax; i++ )
	{
		const float endWeight = ((float)i) / iMax;
		const float startWeight = 1.f - endWeight;

		ColorUnion c;
		c.rgba.r = (U8)( startR * startWeight + endR * endWeight );
		c.rgba.g = (U8)( startG * startWeight + endG * endWeight );
		c.rgba.b = (U8)( startB * startWeight + endB * endWeight );
		c.rgba.a = (U8)( startA * startWeight + endA * endWeight );
		pixels[i] = c.pixel;
	}
}

// Create a 1x256 bitmap used as the gradient ramp.
static BufferBitmap *
NewBufferBitmap(
	Rtt_Allocator *pAllocator,
	Color start,
	Color end,
	GradientPaint::Direction direction )
{
	const PlatformBitmap::Format kFormat = PlatformBitmap::kRGBA;
	PlatformBitmap::Orientation orientation = PlatformBitmap::kDown;

	switch ( direction )
	{
		case GradientPaint::kUpDirection:
			orientation = PlatformBitmap::kUp;
			break;
		case GradientPaint::kRightDirection:
			orientation = PlatformBitmap::kRight;
			break;
		case GradientPaint::kLeftDirection:
			orientation = PlatformBitmap::kLeft;
			break;
		default:
			break;
	}

	BufferBitmap *result =
		Rtt_NEW( pAllocator, BufferBitmap( pAllocator, kBufferWidth, kBufferHeight, kFormat, orientation ) );
	result->SetProperty( PlatformBitmap::kIsPremultiplied, true );

	FillGradientPixels( result, start, end );

	const Real kScale = ((float)(kBufferHeight - 1)) / kBufferHeight; // 0.5;
	result->SetNormalizationScaleY( kScale );

	return result;
}

// ----------------------------------------------------------------------------

GradientPaint *
GradientPaint::New( TextureFactory& factory, Color start, Color end, Direction direction, Rtt_Real angle )
{
	Rtt_Allocator *allocator = factory.GetDisplay().GetAllocator();
	BufferBitmap *bitmap = NewBufferBitmap( allocator, start, end, direction );

	SharedPtr< TextureResource > resource = factory.FindOrCreate( bitmap, true );
	Rtt_ASSERT( resource.NotNull() );

	GradientPaint *result = Rtt_NEW( allocator, GradientPaint( resource, start, end, angle ) );

	return result;
}

// ----------------------------------------------------------------------------

GradientPaint::GradientPaint( const SharedPtr< TextureResource >& resource, Color start, Color end, Rtt_Real angle )
:	Super( resource ),
	fStart( start ),
	fEnd( end )
{
	if(!Rtt_RealIsZero(angle))
	{
		Transform & t = GetTransform();
		t.SetProperty(kRotation, angle);
	}
	Initialize( kGradient );
	
	Invalidate( Paint::kTextureTransformFlag );
}

const Paint*
GradientPaint::AsPaint( Super::Type type ) const
{
	return ( kGradient == type || kBitmap == type ? this : NULL );
}

const MLuaUserdataAdapter&
GradientPaint::GetAdapter() const
{
	return GradientPaintAdapter::Constant();
}

Color
GradientPaint::GetStart() const
{
	return fStart;
}

Color
GradientPaint::GetEnd() const
{
	return fEnd;
}

void
GradientPaint::SetStart( Color color )
{
	fStart = color;

	BufferBitmap *bufferBitmap = static_cast< BufferBitmap * >( GetBitmap() );
	FillGradientPixels( bufferBitmap, fStart, fEnd );

	GetTexture()->Invalidate(); // Force Renderer to update GPU texture

	GetObserver()->InvalidateDisplay(); // Force reblit
}

void
GradientPaint::SetEnd( Color color )
{
	fEnd = color;

	BufferBitmap *bufferBitmap = static_cast< BufferBitmap * >( GetBitmap() );
	FillGradientPixels( bufferBitmap, fStart, fEnd );

	GetTexture()->Invalidate(); // Force Renderer to update GPU texture

	GetObserver()->InvalidateDisplay(); // Force reblit
}

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------

