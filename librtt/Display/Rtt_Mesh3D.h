//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Corona game engine.
// For overview and more information on licensing please refer to README.md
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////

#ifndef _Rtt_Mesh3D_H__
#define _Rtt_Mesh3D_H__

#include "Core/Rtt_Types.h"

#include <vector>

// ----------------------------------------------------------------------------

namespace Rtt
{

// ----------------------------------------------------------------------------

// One vertex of a 3D mesh, in the layout the 3D pipeline declares to bgfx.
//
// Deliberately not Geometry::Vertex: that carries the 2D pipeline's texcoord
// q, byte colour and four userdata floats -- 44 bytes of which 3D uses eight --
// and has no room for a normal that is not stolen from the userdata slots.
struct Vertex3D
{
	float x, y, z;
	float nx, ny, nz;
	float u, v;
};

// The skinning influences of one vertex, in a stream of their own.
//
// Kept out of Vertex3D and uploaded as a second vertex buffer, because only
// loaded models with a skeleton have them: folding these 32 bytes into every
// vertex would double the size of every box and sphere in the scene to carry
// eight floats none of them use.
//
// Four influences is the glTF-mandated maximum per JOINTS_n set, and the number
// every authoring tool exports by default. Indices are floats rather than
// integers because the GLSL 1.20 profile the OpenGL backend compiles for has no
// integer vertex attributes.
enum { kMaxVertexBones = 4 };

struct SkinVertex3D
{
	float indices[kMaxVertexBones];
	float weights[kMaxVertexBones];
};

// Geometry held CPU-side, in object space, indexed as a triangle list.
//
// Meshes are immutable once built and are shared: two boxes of the same size
// are two Object3Ds pointing at one Mesh3D, so the vertex and index buffers are
// uploaded once. Ownership is by reference count because the last object to go
// away is the one that must release the GPU buffers, and which object that is
// is not known until it happens.
class Mesh3D
{
	public:
		typedef Mesh3D Self;

	public:
		Mesh3D();
		~Mesh3D();

		void Retain() { ++fRefCount; }
		void Release() { if ( --fRefCount == 0 ) { delete this; } }

		// Radius of the origin-centred sphere containing every vertex, used to
		// cull an object without walking its vertices again.
		float GetBoundingRadius() const { return fBoundingRadius; }

		const std::vector< Vertex3D >& GetVertices() const { return fVertices; }

		// 32-bit throughout, even though every primitive below fits in 16.
		// Loaded models routinely do not -- a single glTF primitive of 80,000
		// vertices is unremarkable -- and one index width for all meshes is
		// worth more than the bandwidth a cube would save by narrowing.
		const std::vector< U32 >& GetIndices() const { return fIndices; }

		// Empty unless the mesh came from a skinned model, in which case it has
		// exactly one entry per vertex. Callers must treat those two states
		// differently rather than reading a zeroth entry that is not there.
		const std::vector< SkinVertex3D >& GetSkin() const { return fSkin; }
		bool IsSkinned() const { return !fSkin.empty(); }

		// The handle the renderer stashed for this mesh's GPU buffers, or
		// kInvalidGpuId if it has none yet. Opaque here so that Display code
		// does not have to see bgfx.
		enum { kInvalidGpuId = ~0u };

		U32 GetGpuId() const { return fGpuId; }
		void SetGpuId( U32 id ) { fGpuId = id; }

	public:
		// Each returns a mesh with one reference already taken, which the
		// caller owns and must Release().
		//
		// Winding is counter-clockwise when seen from outside, and normals
		// point outwards, so back-face culling holds for all of them.
		static Mesh3D* NewBox( float width, float height, float depth );

		// segments is the longitudinal division count; half as many are used
		// latitudinally, matching the usual UV-sphere proportions.
		static Mesh3D* NewSphere( float radius, U32 segments );

		// Lies in the XZ plane, facing +Y, so it is a floor without being
		// rotated first -- which is what a plane is nearly always for.
		static Mesh3D* NewPlane( float width, float depth );

		// A cone is the degenerate case where one radius is zero, which is why
		// this takes two rather than a radius and a flag.
		static Mesh3D* NewCylinder( float radiusTop, float radiusBottom, float height, U32 segments );

		// Geometry built elsewhere -- by a model loader, or by render.newCustomMesh
		// from Lua tables.
		//
		// The three vectors are taken by swap, not copied: a loaded mesh can be
		// several megabytes, and the caller has no use for them afterwards. They
		// come back empty. Pass NULL for skin when the mesh has no skeleton.
		//
		// Returns NULL if the geometry is unusable -- no vertices, no indices, an
		// index past the end of the vertex array, or a skin whose length does not
		// match. Those are the ways a malformed file reaches here, and every one
		// of them would otherwise be a read out of bounds on the GPU.
		static Mesh3D* NewFromGeometry(
			  std::vector< Vertex3D >& vertices
			, std::vector< U32 >& indices
			, std::vector< SkinVertex3D >* skin
			);

		// Replaces every vertex normal with the area-weighted average of the
		// faces meeting at it, for geometry that arrived without any -- an OBJ
		// with no `vn` lines, or a newCustomMesh call that left them out.
		//
		// Must be called before NewFromGeometry, which is what makes the mesh
		// immutable. Public because both the model loaders and the Lua binding
		// need it, and neither should carry its own copy.
		static void GenerateNormals( std::vector< Vertex3D >& vertices, const std::vector< U32 >& indices );

	private:
		void AddTriangle( U32 a, U32 b, U32 c );
		void ComputeBounds();

		std::vector< Vertex3D > fVertices;
		std::vector< U32 > fIndices;
		std::vector< SkinVertex3D > fSkin;
		float fBoundingRadius;
		U32 fGpuId;
		int fRefCount;
};

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------

#endif // _Rtt_Mesh3D_H__
