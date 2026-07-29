//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Corona game engine.
// For overview and more information on licensing please refer to README.md
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////

#include "Renderer/Rtt_BgfxGeometry.h"

#include "Renderer/Rtt_FormatExtensionList.h"

#include "Corona/CoronaGraphics.h"

#include "Core/Rtt_Assert.h"

#include <string.h>

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

// What bgfx calls an attribute of a given Corona type. bgfx has no 32-bit
// integer vertex type, so an integer attribute is the one case with nothing to
// map onto; the caller reports that rather than silently reinterpreting bits.
static bool
ExtensionAttribType( const FormatExtensionList::Attribute& attribute, bgfx::AttribType::Enum& type )
{
	switch ( (CoronaVertexExtensionAttributeType)attribute.type )
	{
		case kAttributeType_Byte:
			type = bgfx::AttribType::Uint8;
			return true;

		case kAttributeType_Float:
			type = bgfx::AttribType::Float;
			return true;

		default:
			return false;
	}
}

void
BgfxGeometry::BuildVertexLayout( bgfx::VertexLayout& layout, const FormatExtensionList* list, U32 vertexSize )
{
	// The slots left for an effect's own per-vertex attributes: the stock
	// vertex takes Position, TexCoord0, Color0 and TexCoord1, and bgfx's
	// instance data claims TexCoord7 and up.
	static const bgfx::Attrib::Enum kSlots[kMaxVertexExtensionAttributes] =
	{
		  bgfx::Attrib::TexCoord2
		, bgfx::Attrib::TexCoord3
		, bgfx::Attrib::TexCoord4
		, bgfx::Attrib::TexCoord5
		, bgfx::Attrib::TexCoord6
	};

	layout.begin();

	// Geometry::Vertex, field for field; see VertexLayout().
	layout
		.add( bgfx::Attrib::Position,  3, bgfx::AttribType::Float )
		.add( bgfx::Attrib::TexCoord0, 3, bgfx::AttribType::Float )
		.add( bgfx::Attrib::Color0,    4, bgfx::AttribType::Uint8, true )
		.add( bgfx::Attrib::TexCoord1, 4, bgfx::AttribType::Float );

	U32 used = sizeof( Geometry::Vertex );
	U32 slot = 0;

	// Only vertex-rate attributes belong here; per-instance ones travel in
	// bgfx's instance data buffer instead. They live in the first group when
	// there is one at all (see FormatExtensionList::HasVertexRateData), and
	// Corona lays them out immediately after the vertex.
	for ( U32 i = 0; list && i < list->GetAttributeCount(); ++i )
	{
		const FormatExtensionList::Group& group = list->GetGroups()[list->FindGroup( i )];

		if ( group.IsInstanceRate() )
		{
			continue;
		}

		const FormatExtensionList::Attribute& attribute = list->GetAttributes()[i];

		bgfx::AttribType::Enum type;

		if ( slot >= kMaxVertexExtensionAttributes )
		{
			Rtt_TRACE( ( "WARNING: an effect declares more than the %u per-vertex extension attributes the bgfx backend carries\n",
				U32( kMaxVertexExtensionAttributes ) ) );
			break;
		}

		// The slot has to be spent either way: BgfxProgram walks the same
		// attributes in the same order to decide which one a kernel's name
		// refers to, and skipping here would put the two out of step.
		const U32 mySlot = slot++;

		if ( !ExtensionAttribType( attribute, type ) )
		{
			Rtt_TRACE( ( "WARNING: the bgfx backend has no vertex attribute type for a per-vertex extension attribute; leaving it unbound\n" ) );
			continue;
		}

		const U32 offset = sizeof( Geometry::Vertex ) + attribute.offset;

		if ( offset < used )
		{
			// Attributes are declared in ascending offset order within their
			// group, so this only happens on a malformed extension.
			Rtt_TRACE( ( "WARNING: per-vertex extension attributes are out of order; leaving one unbound\n" ) );
			continue;
		}

		layout.skip( U8( offset - used ) );

		layout.add(
			  kSlots[mySlot]
			, U8( attribute.components > 0 ? attribute.components : 4 )
			, type
			, 0 != attribute.normalized
			);

		used = offset + attribute.GetSize();
	}

	// Whatever padding Corona left at the end of the vertex, so that the stride
	// bgfx computes is the one the data was written at.
	if ( vertexSize > used )
	{
		layout.skip( U8( vertexSize - used ) );
	}

	layout.end();
}

BgfxGeometry::BgfxGeometry()
:	fVertexBuffer( BGFX_INVALID_HANDLE ),
	fIndexBuffer( BGFX_INVALID_HANDLE ),
	fLayoutHash( 0 ),
	fVertexCapacity( 0 ),
	fIndexCapacity( 0 )
{
}

void
BgfxGeometry::Create( CPUResource* resource )
{
	Rtt_ASSERT( CPUResource::kGeometry == resource->GetType() );
	Geometry* geometry = static_cast< Geometry* >( resource );

	if ( !geometry->GetStoredOnGPU() )
	{
		// Corona re-fills this geometry every frame out of its GeometryPool.
		// bgfx has transient buffers for exactly that, but they can only be
		// allocated during the frame that consumes them, so the upload happens
		// at draw time in BgfxCommandBuffer rather than here.
		return;
	}

	const FormatExtensionList* list = geometry->GetExtensionList();

	if ( list && list->HasVertexRateData() )
	{
		// The stride depends on which of the geometry's extension attributes
		// the effect about to draw actually asked for, which is not known until
		// the draw; EnsureBuffers builds it then.
		return;
	}

	EnsureBuffers( geometry, VertexLayout() );
}

// Brings the buffers into agreement with the geometry and the format the draw
// about to happen needs.
//
// This is reached from Corona's create queue, from every BindGeometry and from
// the draw itself, so it has to be idempotent -- and the geometry it is asked
// about changes underneath it: a path that is re-tesselated grows, and one that
// only now has an effect with per-vertex attributes changes shape. bgfx fixes a
// buffer's size and layout when it is created, so either means a new buffer.
void
BgfxGeometry::EnsureBuffers( Geometry* geometry, const bgfx::VertexLayout& layout )
{
	const U32 vertexCount = geometry->GetVerticesAllocated();
	const U32 indexCount = geometry->GetIndicesAllocated();

	if ( 0 == vertexCount )
	{
		return;
	}

	bool dirty = false;

	if ( !bgfx::isValid( fVertexBuffer ) || fLayoutHash != layout.m_hash || fVertexCapacity < vertexCount )
	{
		if ( bgfx::isValid( fVertexBuffer ) )
		{
			bgfx::destroy( fVertexBuffer );
		}

		// Dynamic rather than static: Corona mutates stored geometry in place (a
		// moved display object, a re-tesselated path) and calls Update() for it.
		fVertexBuffer = bgfx::createDynamicVertexBuffer( vertexCount, layout );
		fVertexCapacity = vertexCount;
		fLayoutHash = layout.m_hash;

		dirty = true;
	}

	// Indices arrive later than the geometry does: a mesh is queued for creation
	// before it has been tesselated, so a buffer built once and left alone would
	// never get one -- and the draw would fall back to reading the vertices in
	// order, which shows up as one triangle of the mesh.
	if ( indexCount > 0 && ( !bgfx::isValid( fIndexBuffer ) || fIndexCapacity < indexCount ) )
	{
		if ( bgfx::isValid( fIndexBuffer ) )
		{
			bgfx::destroy( fIndexBuffer );
		}

		fIndexBuffer = bgfx::createDynamicIndexBuffer( indexCount );
		fIndexCapacity = indexCount;

		dirty = true;
	}

	if ( dirty )
	{
		Update( geometry );
	}
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
		const FormatExtensionList* list = geometry->GetExtensionList();
		const bool extended = list && list->HasVertexRateData();

		const Geometry::Vertex* source = geometry->GetVertexData();
		U32 stride = sizeof( Geometry::Vertex );

		if ( extended )
		{
			// Corona keeps a stored geometry's per-vertex extension data in a
			// second array whose slots are already interleaved -- one empty
			// vertex, then that vertex's extension values, and so on -- and
			// leaves filling the empty ones to whoever uploads. See the note on
			// Geometry::ExtensionBlock::UpdateData.
			const U32 total = 1 + list->ExtraVertexCount();

			S32 length = 0;
			Geometry::Vertex* extendedData = geometry->GetWritableExtendedVertexData( &length );

			if ( !extendedData || U32( length ) < verticesUsed * total )
			{
				return;
			}

			for ( U32 i = 0; i < verticesUsed; ++i )
			{
				extendedData[i * total] = source[i];
			}

			source = extendedData;
			stride *= total;
		}

		// bgfx copies out of the memory it is handed before the frame ends, so
		// the copy here is what makes it safe for Corona to keep editing its
		// own vertex array afterwards.
		const bgfx::Memory* memory = bgfx::copy( source, verticesUsed * stride );

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

	fLayoutHash = 0;
	fVertexCapacity = 0;
	fIndexCapacity = 0;
}

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------
