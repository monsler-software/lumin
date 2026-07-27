//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Corona game engine.
// For overview and more information on licensing please refer to README.md
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////

#include "Renderer/Rtt_BgfxGeometry.h"

#include "Core/Rtt_Assert.h"

// ----------------------------------------------------------------------------

namespace Rtt
{

// ----------------------------------------------------------------------------

const bgfx::VertexLayout&
BgfxGeometry::VertexLayout()
{
	// Mirrors Geometry::Vertex field for field, in declaration order, because
	// bgfx reads the vertex data straight out of Corona's own array:
	//
	//     Real x, y, z;        12 bytes
	//     Real u, v, q;        12 bytes
	//     U8   rs, gs, bs, as;  4 bytes
	//     Real ux, uy, uz, uw; 16 bytes
	//
	// The names line up with what shell_default_bgfx declares as $input.
	static bgfx::VertexLayout sLayout;
	static bool sInitialized = false;

	if ( !sInitialized )
	{
		sLayout.begin()
			.add( bgfx::Attrib::Position,  3, bgfx::AttribType::Float )
			.add( bgfx::Attrib::TexCoord0, 3, bgfx::AttribType::Float )
			.add( bgfx::Attrib::Color0,    4, bgfx::AttribType::Uint8, true )
			.add( bgfx::Attrib::TexCoord1, 4, bgfx::AttribType::Float )
			.end();

		Rtt_ASSERT( sLayout.getStride() == sizeof( Geometry::Vertex ) );

		sInitialized = true;
	}

	return sLayout;
}

BgfxGeometry::BgfxGeometry()
:	fVertexBuffer( BGFX_INVALID_HANDLE ),
	fIndexBuffer( BGFX_INVALID_HANDLE )
{
}

void
BgfxGeometry::Create( CPUResource* resource )
{
	Rtt_ASSERT( CPUResource::kGeometry == resource->GetType() );
	Geometry* geometry = static_cast< Geometry* >( resource );

	// See the note in BgfxTexture::Create: this is reached both from Corona's
	// create queue and from every BindGeometry, so without this the buffers
	// would be recreated -- and the old ones leaked -- once per draw.
	if ( bgfx::isValid( fVertexBuffer ) )
	{
		return;
	}

	if ( !geometry->GetStoredOnGPU() )
	{
		// Corona re-fills this geometry every frame out of its GeometryPool.
		// bgfx has transient buffers for exactly that, but they can only be
		// allocated during the frame that consumes them, so the upload happens
		// at draw time in BgfxCommandBuffer rather than here.
		return;
	}

	const U32 vertexCount = geometry->GetVerticesAllocated();
	const U32 indexCount = geometry->GetIndicesAllocated();

	if ( 0 == vertexCount )
	{
		return;
	}

	// Dynamic rather than static: Corona mutates stored geometry in place (a
	// moved display object, a re-tesselated path) and calls Update() for it.
	fVertexBuffer = bgfx::createDynamicVertexBuffer( vertexCount, VertexLayout() );

	if ( indexCount > 0 )
	{
		fIndexBuffer = bgfx::createDynamicIndexBuffer( indexCount );
	}

	Update( resource );
}

void
BgfxGeometry::Update( CPUResource* resource )
{
	Rtt_ASSERT( CPUResource::kGeometry == resource->GetType() );
	Geometry* geometry = static_cast< Geometry* >( resource );

	if ( !bgfx::isValid( fVertexBuffer ) )
	{
		return; // transient geometry; nothing is held on our side to refresh
	}

	const U32 verticesUsed = geometry->GetVerticesUsed();

	if ( verticesUsed > 0 )
	{
		// bgfx copies out of the memory it is handed before the frame ends, so
		// the copy here is what makes it safe for Corona to keep editing its
		// own vertex array afterwards.
		const bgfx::Memory* memory = bgfx::copy(
			  geometry->GetVertexData()
			, verticesUsed * sizeof( Geometry::Vertex )
			);

		bgfx::update( fVertexBuffer, 0, memory );
	}

	const U32 indicesUsed = geometry->GetIndicesUsed();

	if ( indicesUsed > 0 && bgfx::isValid( fIndexBuffer ) )
	{
		const bgfx::Memory* memory = bgfx::copy(
			  geometry->GetIndexData()
			, indicesUsed * sizeof( Geometry::Index )
			);

		bgfx::update( fIndexBuffer, 0, memory );
	}
}

void
BgfxGeometry::Destroy()
{
	if ( bgfx::isValid( fVertexBuffer ) )
	{
		bgfx::destroy( fVertexBuffer );
		fVertexBuffer = BGFX_INVALID_HANDLE;
	}

	if ( bgfx::isValid( fIndexBuffer ) )
	{
		bgfx::destroy( fIndexBuffer );
		fIndexBuffer = BGFX_INVALID_HANDLE;
	}
}

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------
