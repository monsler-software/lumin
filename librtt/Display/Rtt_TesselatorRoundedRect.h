//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Corona game engine.
// For overview and more information on licensing please refer to README.md 
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////

#ifndef _Rtt_TesselatorRoundedRect_H__
#define _Rtt_TesselatorRoundedRect_H__

#include "Display/Rtt_TesselatorRect.h"

// ----------------------------------------------------------------------------

namespace Rtt
{

// ----------------------------------------------------------------------------

class TesselatorRoundedRect : public TesselatorRectBase
{
	public:
		typedef TesselatorRectBase Super;

	public:
		TesselatorRoundedRect( Real w, Real h, Real radius );
		TesselatorRoundedRect( Real w, Real h, Real topLeftRadius, Real topRightRadius, Real bottomRightRadius, Real bottomLeftRadius );

	public:
		virtual Tesselator::eType GetType(){ return Tesselator::kType_RoundedRect; }
		virtual Geometry::PrimitiveType GetFillPrimitive() const override;

	public:
		enum Corner
		{
			kTopLeftCorner = 0,
			kTopRightCorner,
			kBottomRightCorner,
			kBottomLeftCorner,

			kNumCorners
		};

	protected:
		void AppendRoundedRect( ArrayVertex2& vertices, Real halfW, Real halfH, bool closeLoop ) const;
		U32 PerimeterVertexCount() const;

	public:
		virtual void GenerateFill( ArrayVertex2& outVertices );
		virtual void GenerateFillTexture( ArrayVertex2& outTexCoords, const Transform& t );
		virtual void GenerateStroke( ArrayVertex2& outVertices );
		virtual void GetSelfBounds( Rect& rect );

		virtual U32 FillVertexCount() const override;
		virtual U32 StrokeVertexCount() const override;

	public:
		Real GetRadius() const { return fRadius; }
		void SetRadius( Real newValue );
		Real GetCornerRadius( Corner corner ) const { return fCornerRadii[corner]; }
		void SetCornerRadius( Corner corner, Real newValue );

	private:
		Real fRadius;
		Real fCornerRadii[kNumCorners];
};

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------

#endif // _Rtt_TesselatorRoundedRect_H__
