//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Corona game engine.
// For overview and more information on licensing please refer to README.md 
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////

#include "Core/Rtt_Build.h"

#include "Display/Rtt_TesselatorRoundedRect.h"

#include "Display/Rtt_TesselatorLine.h"
#include "Rtt_Matrix.h"
#include "Rtt_Transform.h"

// ----------------------------------------------------------------------------

namespace Rtt
{

// ----------------------------------------------------------------------------

TesselatorRoundedRect::TesselatorRoundedRect( Real w, Real h, Real radius )
:	TesselatorRoundedRect( w, h, radius, radius, radius, radius )
{
}

TesselatorRoundedRect::TesselatorRoundedRect( Real w, Real h, Real topLeftRadius, Real topRightRadius, Real bottomRightRadius, Real bottomLeftRadius )
:	Super( w, h ),
	fRadius( topLeftRadius )
{
	fCornerRadii[kTopLeftCorner] = topLeftRadius;
	fCornerRadii[kTopRightCorner] = topRightRadius;
	fCornerRadii[kBottomRightCorner] = bottomRightRadius;
	fCornerRadii[kBottomLeftCorner] = bottomLeftRadius;
}

Geometry::PrimitiveType
TesselatorRoundedRect::GetFillPrimitive() const
{
	return Geometry::kTriangles;
}

static int
RoundedRectSegmentsForRadius( Real radius )
{
	if ( Rtt_RealIsZero( radius ) )
	{
		return 0;
	}

	int result = Rtt_RealToInt( radius );
	if ( radius > 7 )
	{
		result >>= 1;
	}
	if ( result < 1 ) { result = 1; }
	if ( result > 32 ) { result = 32; }
	return result;
}

static void
AppendCorner( ArrayVertex2& vertices, Real centerX, Real centerY, Real radius, Real startAngle, Real endAngle )
{
	if ( Rtt_RealIsZero( radius ) )
	{
		Vertex2 corner = { centerX, centerY };
		vertices.Append( corner );
		return;
	}

	const int segmentCount = RoundedRectSegmentsForRadius( radius );
	for ( int i = 0; i <= segmentCount; i++ )
	{
		Real t = Rtt_RealDivNonZeroAB( Rtt_IntToReal( i ), Rtt_IntToReal( segmentCount ) );
		Real angle = startAngle + Rtt_RealMul( endAngle - startAngle, t );
		Vertex2 point =
		{
			centerX + Rtt_RealMul( radius, Rtt_RealCos( angle ) ),
			centerY + Rtt_RealMul( radius, Rtt_RealSin( angle ) )
		};
		vertices.Append( point );
	}
}

void
TesselatorRoundedRect::AppendRoundedRect( ArrayVertex2& vertices, Real halfW, Real halfH, bool closeLoop ) const
{
	Real maxRadius = Min( halfW, halfH );
	Real topLeftRadius = Clamp( fCornerRadii[kTopLeftCorner], 0.f, maxRadius );
	Real topRightRadius = Clamp( fCornerRadii[kTopRightCorner], 0.f, maxRadius );
	Real bottomRightRadius = Clamp( fCornerRadii[kBottomRightCorner], 0.f, maxRadius );
	Real bottomLeftRadius = Clamp( fCornerRadii[kBottomLeftCorner], 0.f, maxRadius );

	const Real kPi = Rtt_FloatToReal( M_PI );
	const Real kHalfPi = Rtt_RealDiv2( kPi );
	AppendCorner( vertices, halfW - topRightRadius, -halfH + topRightRadius, topRightRadius, -kHalfPi, Rtt_REAL_0 );
	AppendCorner( vertices, halfW - bottomRightRadius, halfH - bottomRightRadius, bottomRightRadius, Rtt_REAL_0, kHalfPi );
	AppendCorner( vertices, -halfW + bottomLeftRadius, halfH - bottomLeftRadius, bottomLeftRadius, kHalfPi, kPi );
	AppendCorner( vertices, -halfW + topLeftRadius, -halfH + topLeftRadius, topLeftRadius, kPi, kPi + kHalfPi );

	if ( closeLoop && vertices.Length() > 0 )
	{
		Vertex2 first = vertices[0];
		vertices.Append( first );
	}
}

void
TesselatorRoundedRect::GenerateFill( ArrayVertex2& vertices )
{
	Rtt_ASSERT( vertices.Length() == 0 );

	ArrayVertex2 perimeter( vertices.Allocator() );
	AppendRoundedRect( perimeter, fHalfW, fHalfH, false );

	Vertex2 center = { Rtt_REAL_0, Rtt_REAL_0 };
	for ( int i = 0, iMax = perimeter.Length(); i < iMax; i++ )
	{
		vertices.Append( center );
		vertices.Append( perimeter[i] );
		vertices.Append( perimeter[(i + 1) % iMax] );
	}
}

void
TesselatorRoundedRect::GenerateFillTexture( ArrayVertex2& texCoords, const Transform& t )
{
	ArrayVertex2& vertices = texCoords;

	Rtt_ASSERT( vertices.Length() == 0 );

	GenerateFill( vertices );
	Normalize( vertices );
	
	Real w = Rtt_RealMul2( fHalfW );
	Real h = Rtt_RealMul2( fHalfH );
	Real invW = Rtt_RealDivNonZeroAB( Rtt_REAL_1, w );
	Real invH = Rtt_RealDivNonZeroAB( Rtt_REAL_1, h );

	if ( t.IsIdentity() )
	{
		for ( int i = 0, iMax = vertices.Length(); i < iMax; i++ )
		{
			Vertex2& v = vertices[i];
			v.x = Rtt_RealMul( v.x + fHalfW, invW );
			v.y = Rtt_RealMul( v.y + fHalfH, invH );
		}
	}
	else
	{
		Matrix m;
		m.Scale( invW * t.GetSx(), invH * t.GetSy() );
		m.Rotate( - t.GetRotation() );
		m.Translate( t.GetX() + Rtt_REAL_HALF, t.GetY() + Rtt_REAL_HALF );
		m.Apply( vertices.WriteAccess(), vertices.Length() );
	}
}

void
TesselatorRoundedRect::GenerateStroke( ArrayVertex2& vertices )
{
	ArrayVertex2 perimeter( vertices.Allocator() );
	AppendRoundedRect( perimeter, fHalfW, fHalfH, false );

	TesselatorLine tesselator( perimeter, TesselatorLine::kLoopMode );
	tesselator.SetInnerWidth( fInnerWidth );
	tesselator.SetOuterWidth( fOuterWidth );
	tesselator.GenerateStroke( vertices );
}

void
TesselatorRoundedRect::GetSelfBounds( Rect& rect )
{
	rect.Initialize( fHalfW, fHalfH );
}

U32
TesselatorRoundedRect::FillVertexCount() const
{
	return PerimeterVertexCount() * 3U;
}

U32
TesselatorRoundedRect::PerimeterVertexCount() const
{
	U32 result = 0U;
	Real maxRadius = Min( fHalfW, fHalfH );
	for ( int i = 0; i < kNumCorners; i++ )
	{
		Real radius = Clamp( fCornerRadii[i], 0.f, maxRadius );
		result += Rtt_RealIsZero( radius ) ? 1U : (U32)RoundedRectSegmentsForRadius( radius ) + 1U;
	}
	return result;
}

U32
TesselatorRoundedRect::StrokeVertexCount() const
{
	return TesselatorLine::VertexCountFromPoints( PerimeterVertexCount(), true );
}

void
TesselatorRoundedRect::SetRadius( Real newValue )
{
	fRadius = newValue;
	for ( int i = 0; i < kNumCorners; i++ )
	{
		fCornerRadii[i] = newValue;
	}
}

void
TesselatorRoundedRect::SetCornerRadius( Corner corner, Real newValue )
{
	fCornerRadii[corner] = newValue;
	fRadius = fCornerRadii[kTopLeftCorner];
}

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------

