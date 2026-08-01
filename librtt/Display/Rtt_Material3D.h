//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Corona game engine.
// For overview and more information on licensing please refer to README.md
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////

#ifndef _Rtt_Material3D_H__
#define _Rtt_Material3D_H__

#include "Core/Rtt_Types.h"

// ----------------------------------------------------------------------------

namespace Rtt
{

class Texture3D;

// ----------------------------------------------------------------------------

// The surface parameters the 3D shader takes, in the metallic-roughness form
// glTF and every PBR authoring tool use.
//
// Shared and reference counted, like meshes: one material is normally set on
// many objects, and the uniform values it holds are the same for all of them.
class Material3D
{
	public:
		typedef Material3D Self;

	public:
		Material3D();
		~Material3D();

		void Retain() { ++fRefCount; }
		void Release() { if ( --fRefCount == 0 ) { delete this; } }

	public:
		void SetAlbedo( float r, float g, float b, float a );
		void GetAlbedo( float& r, float& g, float& b, float& a ) const;

		float GetRoughness() const { return fRoughness; }

		// Clamped rather than rejected: zero roughness is a mathematical point
		// light reflection that aliases into fireflies, and values outside 0..1
		// have no meaning in the BRDF at all.
		void SetRoughness( float value );

		float GetMetallic() const { return fMetallic; }
		void SetMetallic( float value );

		void SetEmissive( float r, float g, float b );
		void GetEmissive( float& r, float& g, float& b ) const;

	public:
		// The maps, any of which may be NULL, in which case the factor above acts
		// alone. Where both are set they multiply, which is what glTF specifies
		// and what makes a white factor mean "the texture as authored".
		//
		// Each setter retains what it is given and releases what it replaces, so a
		// material can be re-textured at any time without the caller tracking
		// lifetimes.
		Texture3D* GetAlbedoMap() const { return fAlbedoMap; }
		void SetAlbedoMap( Texture3D* texture );

		// Roughness in green, metallic in blue -- the ORM packing glTF mandates,
		// so one fetch covers both.
		Texture3D* GetMetallicRoughnessMap() const { return fMetallicRoughnessMap; }
		void SetMetallicRoughnessMap( Texture3D* texture );

		Texture3D* GetEmissiveMap() const { return fEmissiveMap; }
		void SetEmissiveMap( Texture3D* texture );

	public:
		// Whether both faces of the surface are real.
		//
		// A single-sided surface has its back faces culled, which is what a closed
		// solid wants and what halves its fragment cost. Cloth, leaves and
		// anything else modelled as a sheet with no thickness needs both, and
		// culling one leaves holes wherever the sheet turns away. glTF states this
		// per material, so it is a property of the material here too.
		//
		// Defaults to double-sided, which is never wrong-looking: the cost of
		// drawing a hidden face is a face drawn twice, while the cost of culling a
		// visible one is a hole.
		bool IsDoubleSided() const { return fIsDoubleSided; }
		void SetDoubleSided( bool value ) { fIsDoubleSided = value; }

		// Whether the surface blends with what is behind it.
		//
		// Opaque geometry is drawn with blending off, which is not merely faster:
		// blending an opaque surface makes the result depend on the order objects
		// happen to be drawn in, so a model with interior detail shows seams that
		// move as the camera does.
		bool IsTranslucent() const { return fIsTranslucent; }
		void SetTranslucent( bool value ) { fIsTranslucent = value; }

	private:
		float fAlbedo[4];
		float fEmissive[3];
		float fRoughness;
		float fMetallic;
		Texture3D* fAlbedoMap;
		Texture3D* fMetallicRoughnessMap;
		Texture3D* fEmissiveMap;
		bool fIsDoubleSided;
		bool fIsTranslucent;
		int fRefCount;
};

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------

#endif // _Rtt_Material3D_H__
