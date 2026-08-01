//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Corona game engine.
// For overview and more information on licensing please refer to README.md
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////

#include "Core/Rtt_Build.h"

#include "Display/Rtt_Mesh3D.h"

#include <cmath>

// ----------------------------------------------------------------------------

namespace Rtt
{

// ----------------------------------------------------------------------------

static const float kPi = 3.14159265358979323846f;

Mesh3D::Mesh3D()
:	fBoundingRadius( 0.0f ),
	fGpuId( kInvalidGpuId ),
	fRefCount( 1 )
{
}

Mesh3D::~Mesh3D()
{
}

void
Mesh3D::AddTriangle( U32 a, U32 b, U32 c )
{
	fIndices.push_back( a );
	fIndices.push_back( b );
	fIndices.push_back( c );
}

void
Mesh3D::ComputeBounds()
{
	float maxLengthSquared = 0.0f;

	for ( size_t i = 0, iMax = fVertices.size(); i < iMax; ++i )
	{
		const Vertex3D& v = fVertices[i];
		const float lengthSquared = v.x * v.x + v.y * v.y + v.z * v.z;

		if ( lengthSquared > maxLengthSquared )
		{
			maxLengthSquared = lengthSquared;
		}
	}

	fBoundingRadius = std::sqrt( maxLengthSquared );
}

// ----------------------------------------------------------------------------

Mesh3D*
Mesh3D::NewBox( float width, float height, float depth )
{
	Mesh3D* mesh = new Mesh3D;

	const float x = width * 0.5f;
	const float y = height * 0.5f;
	const float z = depth * 0.5f;

	// Each face gets its own four vertices rather than sharing the eight
	// corners: a shared corner would have to average three face normals, which
	// rounds the edges off a shape whose whole point is that they are sharp.
	struct Face
	{
		float nx, ny, nz;    // outward normal
		float ux, uy, uz;    // unit step across the face, left to right
		float vx, vy, vz;    // unit step up the face
	};

	static const Face kFaces[6] =
	{
		{  0,  0,  1,   1,  0,  0,   0,  1,  0 }, // +Z, front
		{  0,  0, -1,  -1,  0,  0,   0,  1,  0 }, // -Z, back
		{  1,  0,  0,   0,  0, -1,   0,  1,  0 }, // +X, right
		{ -1,  0,  0,   0,  0,  1,   0,  1,  0 }, // -X, left
		{  0,  1,  0,   1,  0,  0,   0,  0, -1 }, // +Y, top
		{  0, -1,  0,   1,  0,  0,   0,  0,  1 }, // -Y, bottom
	};

	for ( int f = 0; f < 6; ++f )
	{
		const Face& face = kFaces[f];

		// The half-extent along each axis, so a face's own two axes scale to
		// that face's dimensions without a per-face table of sizes.
		const float ux = face.ux * x, uy = face.uy * y, uz = face.uz * z;
		const float vx = face.vx * x, vy = face.vy * y, vz = face.vz * z;
		const float cx = face.nx * x, cy = face.ny * y, cz = face.nz * z;

		const U32 base = (U32) mesh->fVertices.size();

		static const float kCorners[4][2] = { { -1, -1 }, { 1, -1 }, { 1, 1 }, { -1, 1 } };

		for ( int c = 0; c < 4; ++c )
		{
			const float s = kCorners[c][0];
			const float t = kCorners[c][1];

			Vertex3D v;
			v.x = cx + ux * s + vx * t;
			v.y = cy + uy * s + vy * t;
			v.z = cz + uz * s + vz * t;
			v.nx = face.nx;
			v.ny = face.ny;
			v.nz = face.nz;
			v.u = ( s + 1.0f ) * 0.5f;
			v.v = 1.0f - ( t + 1.0f ) * 0.5f;

			mesh->fVertices.push_back( v );
		}

		mesh->AddTriangle( base + 0, base + 1, base + 2 );
		mesh->AddTriangle( base + 0, base + 2, base + 3 );
	}

	mesh->ComputeBounds();

	return mesh;
}

Mesh3D*
Mesh3D::NewSphere( float radius, U32 segments )
{
	if ( segments < 3 )
	{
		segments = 3;
	}

	// A UV sphere with fewer rings than it has segments looks stretched at the
	// equator, and with as many looks over-tessellated at the poles where the
	// rings crowd together; half is the usual compromise.
	U32 rings = segments / 2;

	if ( rings < 2 )
	{
		rings = 2;
	}

	Mesh3D* mesh = new Mesh3D;

	// The seam is duplicated -- the vertex at u = 0 and the one at u = 1 are at
	// the same place but must carry different texture coordinates, or the last
	// column of the mesh samples the whole texture backwards.
	for ( U32 ring = 0; ring <= rings; ++ring )
	{
		const float phi = kPi * (float) ring / (float) rings;
		const float sinPhi = std::sin( phi );
		const float cosPhi = std::cos( phi );

		for ( U32 seg = 0; seg <= segments; ++seg )
		{
			const float theta = 2.0f * kPi * (float) seg / (float) segments;

			Vertex3D v;
			v.nx = sinPhi * std::cos( theta );
			v.ny = cosPhi;
			v.nz = sinPhi * std::sin( theta );
			v.x = v.nx * radius;
			v.y = v.ny * radius;
			v.z = v.nz * radius;
			v.u = (float) seg / (float) segments;
			v.v = (float) ring / (float) rings;

			mesh->fVertices.push_back( v );
		}
	}

	const U32 stride = segments + 1;

	for ( U32 ring = 0; ring < rings; ++ring )
	{
		for ( U32 seg = 0; seg < segments; ++seg )
		{
			const U32 a = (U32) ( ring * stride + seg );
			const U32 b = (U32) ( a + stride );

			// The pole rings degenerate to a point, so one of the two triangles
			// in those quads has zero area. Skipping them keeps the index
			// buffer honest rather than relying on the rasteriser to drop them.
			if ( ring != 0 )
			{
				mesh->AddTriangle( a, b, (U32) ( a + 1 ) );
			}

			if ( ring != rings - 1 )
			{
				mesh->AddTriangle( (U32) ( a + 1 ), b, (U32) ( b + 1 ) );
			}
		}
	}

	mesh->ComputeBounds();

	return mesh;
}

Mesh3D*
Mesh3D::NewPlane( float width, float depth )
{
	Mesh3D* mesh = new Mesh3D;

	const float x = width * 0.5f;
	const float z = depth * 0.5f;

	static const float kCorners[4][2] = { { -1, -1 }, { 1, -1 }, { 1, 1 }, { -1, 1 } };

	for ( int c = 0; c < 4; ++c )
	{
		Vertex3D v;
		v.x = kCorners[c][0] * x;
		v.y = 0.0f;
		v.z = kCorners[c][1] * z;
		v.nx = 0.0f;
		v.ny = 1.0f;
		v.nz = 0.0f;
		v.u = ( kCorners[c][0] + 1.0f ) * 0.5f;
		v.v = ( kCorners[c][1] + 1.0f ) * 0.5f;

		mesh->fVertices.push_back( v );
	}

	// Wound so the front face is the one seen from +Y, where the camera
	// normally is when a plane is being used as the ground.
	mesh->AddTriangle( 0, 2, 1 );
	mesh->AddTriangle( 0, 3, 2 );

	mesh->ComputeBounds();

	return mesh;
}

Mesh3D*
Mesh3D::NewCylinder( float radiusTop, float radiusBottom, float height, U32 segments )
{
	if ( segments < 3 )
	{
		segments = 3;
	}

	Mesh3D* mesh = new Mesh3D;

	const float halfHeight = height * 0.5f;

	// The side normal is not horizontal unless the two radii match: it follows
	// the slope of the wall, so a cone shades as a cone rather than as a tube
	// that happens to narrow.
	const float slope = ( radiusBottom - radiusTop ) / ( height != 0.0f ? height : 1.0f );
	const float normalScale = 1.0f / std::sqrt( 1.0f + slope * slope );

	const U32 sideBase = (U32) mesh->fVertices.size();

	for ( int end = 0; end < 2; ++end )
	{
		const float y = ( end == 0 ) ? halfHeight : -halfHeight;
		const float radius = ( end == 0 ) ? radiusTop : radiusBottom;

		for ( U32 seg = 0; seg <= segments; ++seg )
		{
			const float theta = 2.0f * kPi * (float) seg / (float) segments;
			const float cosTheta = std::cos( theta );
			const float sinTheta = std::sin( theta );

			Vertex3D v;
			v.x = cosTheta * radius;
			v.y = y;
			v.z = sinTheta * radius;
			v.nx = cosTheta * normalScale;
			v.ny = slope * normalScale;
			v.nz = sinTheta * normalScale;
			v.u = (float) seg / (float) segments;
			v.v = (float) end;

			mesh->fVertices.push_back( v );
		}
	}

	const U32 stride = segments + 1;

	for ( U32 seg = 0; seg < segments; ++seg )
	{
		const U32 top = (U32) ( sideBase + seg );
		const U32 bottom = (U32) ( top + stride );

		// A tip -- radius zero at one end -- collapses that edge to a point,
		// leaving one degenerate triangle per segment, as with the sphere poles.
		if ( radiusTop != 0.0f )
		{
			mesh->AddTriangle( top, bottom, (U32) ( top + 1 ) );
		}

		if ( radiusBottom != 0.0f )
		{
			mesh->AddTriangle( (U32) ( top + 1 ), bottom, (U32) ( bottom + 1 ) );
		}
	}

	// Caps are separate fans with their own vertices, since their normals point
	// along Y and cannot be shared with the wall.
	for ( int end = 0; end < 2; ++end )
	{
		const float radius = ( end == 0 ) ? radiusTop : radiusBottom;

		if ( radius == 0.0f )
		{
			continue;
		}

		const float y = ( end == 0 ) ? halfHeight : -halfHeight;
		const float ny = ( end == 0 ) ? 1.0f : -1.0f;

		const U32 centre = (U32) mesh->fVertices.size();

		Vertex3D c;
		c.x = 0.0f;
		c.y = y;
		c.z = 0.0f;
		c.nx = 0.0f;
		c.ny = ny;
		c.nz = 0.0f;
		c.u = 0.5f;
		c.v = 0.5f;

		mesh->fVertices.push_back( c );

		for ( U32 seg = 0; seg <= segments; ++seg )
		{
			const float theta = 2.0f * kPi * (float) seg / (float) segments;
			const float cosTheta = std::cos( theta );
			const float sinTheta = std::sin( theta );

			Vertex3D v;
			v.x = cosTheta * radius;
			v.y = y;
			v.z = sinTheta * radius;
			v.nx = 0.0f;
			v.ny = ny;
			v.nz = 0.0f;
			v.u = ( cosTheta + 1.0f ) * 0.5f;
			v.v = ( sinTheta + 1.0f ) * 0.5f;

			mesh->fVertices.push_back( v );
		}

		for ( U32 seg = 0; seg < segments; ++seg )
		{
			const U32 a = (U32) ( centre + 1 + seg );
			const U32 b = (U32) ( a + 1 );

			// The two caps face opposite ways, so their fans wind opposite ways
			// too or one of them is culled.
			if ( end == 0 )
			{
				mesh->AddTriangle( centre, a, b );
			}
			else
			{
				mesh->AddTriangle( centre, b, a );
			}
		}
	}

	mesh->ComputeBounds();

	return mesh;
}

// ----------------------------------------------------------------------------

// The cross product of two edges is twice the triangle's area in length, so
// accumulating it unnormalised weights each face by its size -- which is what
// keeps a large face from being outvoted by the several slivers beside it.
void
Mesh3D::GenerateNormals( std::vector< Vertex3D >& vertices, const std::vector< U32 >& indices )
{
	for ( size_t i = 0, iMax = vertices.size(); i < iMax; ++i )
	{
		vertices[i].nx = vertices[i].ny = vertices[i].nz = 0.0f;
	}

	for ( size_t i = 0, iMax = indices.size(); i + 2 < iMax; i += 3 )
	{
		// Guarded because this runs before NewFromGeometry has had a chance to
		// reject an out-of-range index.
		if ( indices[i] >= vertices.size()
			|| indices[i + 1] >= vertices.size()
			|| indices[i + 2] >= vertices.size() )
		{
			continue;
		}

		Vertex3D& a = vertices[indices[i]];
		Vertex3D& b = vertices[indices[i + 1]];
		Vertex3D& c = vertices[indices[i + 2]];

		const float e1x = b.x - a.x, e1y = b.y - a.y, e1z = b.z - a.z;
		const float e2x = c.x - a.x, e2y = c.y - a.y, e2z = c.z - a.z;

		const float nx = e1y * e2z - e1z * e2y;
		const float ny = e1z * e2x - e1x * e2z;
		const float nz = e1x * e2y - e1y * e2x;

		a.nx += nx; a.ny += ny; a.nz += nz;
		b.nx += nx; b.ny += ny; b.nz += nz;
		c.nx += nx; c.ny += ny; c.nz += nz;
	}

	for ( size_t i = 0, iMax = vertices.size(); i < iMax; ++i )
	{
		Vertex3D& v = vertices[i];

		const float length = std::sqrt( v.nx * v.nx + v.ny * v.ny + v.nz * v.nz );

		if ( length > 0.0f )
		{
			v.nx /= length;
			v.ny /= length;
			v.nz /= length;
		}
		else
		{
			// A vertex used only by degenerate triangles. Any unit vector will
			// do; up is the least surprising in a lit scene.
			v.ny = 1.0f;
		}
	}
}

// ----------------------------------------------------------------------------

Mesh3D*
Mesh3D::NewFromGeometry(
	  std::vector< Vertex3D >& vertices
	, std::vector< U32 >& indices
	, std::vector< SkinVertex3D >* skin )
{
	if ( vertices.empty() || indices.empty() || ( indices.size() % 3 ) != 0 )
	{
		return NULL;
	}

	if ( skin != NULL && !skin->empty() && skin->size() != vertices.size() )
	{
		return NULL;
	}

	// Every index is checked once here rather than trusted. The alternative is
	// a GPU-side read past the end of the vertex buffer, which on some drivers
	// is a black mesh and on others is a lost device -- and the input is a file
	// off disk, so it is not this code's to assume well-formed.
	const U32 vertexCount = (U32) vertices.size();

	for ( size_t i = 0, iMax = indices.size(); i < iMax; ++i )
	{
		if ( indices[i] >= vertexCount )
		{
			return NULL;
		}
	}

	Mesh3D* mesh = new Mesh3D;

	mesh->fVertices.swap( vertices );
	mesh->fIndices.swap( indices );

	if ( skin != NULL )
	{
		mesh->fSkin.swap( *skin );
	}

	mesh->ComputeBounds();

	return mesh;
}

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------
