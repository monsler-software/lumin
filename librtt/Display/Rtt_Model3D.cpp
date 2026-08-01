//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Corona game engine.
// For overview and more information on licensing please refer to README.md
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////

#include "Core/Rtt_Build.h"

#include "Display/Rtt_Model3D.h"

#include "Display/Rtt_Material3D.h"
#include "Display/Rtt_Mesh3D.h"
#include "Display/Rtt_Texture3D.h"
#include "Renderer/Rtt_Draw3D.h"

// cgltf is vendored with bgfx, which is where the include path comes from. It is
// header-only, so exactly one translation unit has to instantiate it, and this
// is the only one that uses it.
//
// The write half is not wanted -- nothing here saves a model -- and excluding it
// keeps a few hundred unused functions out of the object file.
#define CGLTF_IMPLEMENTATION
#include "cgltf.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>

// ----------------------------------------------------------------------------

namespace Rtt
{

// ----------------------------------------------------------------------------

// Defined with the OBJ loader below, which is its other caller: the glTF loader
// needs it to resolve external images against the model's own directory.
static std::string DirectoryOf( const char* path );

// ----------------------------------------------------------------------------

// Column-major throughout, indexed out[column * 4 + row], matching Object3D's
// world matrix and what the shaders are handed.

static void
SetIdentity( float* out )
{
	memset( out, 0, 16 * sizeof( float ) );

	out[0] = out[5] = out[10] = out[15] = 1.0f;
}

// out = a * b, meaning b is applied to a vertex first. Distinct from the
// pipeline's MultiplyMatrix, which takes its arguments the other way round.
static void
Multiply( const float* a, const float* b, float* out )
{
	for ( int col = 0; col < 4; ++col )
	{
		for ( int row = 0; row < 4; ++row )
		{
			float sum = 0.0f;

			for ( int k = 0; k < 4; ++k )
			{
				sum += a[k * 4 + row] * b[col * 4 + k];
			}

			out[col * 4 + row] = sum;
		}
	}
}

// Composes translation, a unit quaternion and scale into a matrix, in the order
// glTF specifies: scale, then rotate, then translate.
static void
ComposeTransform( const float* translation, const float* rotation, const float* scale, float* out )
{
	const float x = rotation[0], y = rotation[1], z = rotation[2], w = rotation[3];

	const float xx = x * x, yy = y * y, zz = z * z;
	const float xy = x * y, xz = x * z, yz = y * z;
	const float wx = w * x, wy = w * y, wz = w * z;

	out[0] = ( 1.0f - 2.0f * ( yy + zz ) ) * scale[0];
	out[1] = ( 2.0f * ( xy + wz ) ) * scale[0];
	out[2] = ( 2.0f * ( xz - wy ) ) * scale[0];
	out[3] = 0.0f;

	out[4] = ( 2.0f * ( xy - wz ) ) * scale[1];
	out[5] = ( 1.0f - 2.0f * ( xx + zz ) ) * scale[1];
	out[6] = ( 2.0f * ( yz + wx ) ) * scale[1];
	out[7] = 0.0f;

	out[8] = ( 2.0f * ( xz + wy ) ) * scale[2];
	out[9] = ( 2.0f * ( yz - wx ) ) * scale[2];
	out[10] = ( 1.0f - 2.0f * ( xx + yy ) ) * scale[2];
	out[11] = 0.0f;

	out[12] = translation[0];
	out[13] = translation[1];
	out[14] = translation[2];
	out[15] = 1.0f;
}

// Splits a matrix back into translation, rotation and scale.
//
// Needed because a glTF node may carry either a TRS triple or a bare matrix, and
// animation can only interpolate the triple. A matrix with shear or a negative
// determinant cannot be expressed as one, and those come out as the nearest
// thing that can -- which is wrong, but is wrong for a file that could not be
// animated meaningfully anyway.
static void
DecomposeTransform( const float* m, float* translation, float* rotation, float* scale )
{
	translation[0] = m[12];
	translation[1] = m[13];
	translation[2] = m[14];

	float column[3][3];

	for ( int col = 0; col < 3; ++col )
	{
		for ( int row = 0; row < 3; ++row )
		{
			column[col][row] = m[col * 4 + row];
		}

		scale[col] = std::sqrt(
			column[col][0] * column[col][0] +
			column[col][1] * column[col][1] +
			column[col][2] * column[col][2] );

		if ( scale[col] > 0.0f )
		{
			for ( int row = 0; row < 3; ++row )
			{
				column[col][row] /= scale[col];
			}
		}
	}

	// Shepperd's method: build the quaternion from whichever of the four
	// components the trace shows to be largest, which is the one whose formula
	// does not divide by something near zero.
	const float trace = column[0][0] + column[1][1] + column[2][2];

	if ( trace > 0.0f )
	{
		const float s = std::sqrt( trace + 1.0f ) * 2.0f;

		rotation[3] = 0.25f * s;
		rotation[0] = ( column[1][2] - column[2][1] ) / s;
		rotation[1] = ( column[2][0] - column[0][2] ) / s;
		rotation[2] = ( column[0][1] - column[1][0] ) / s;
	}
	else if ( column[0][0] > column[1][1] && column[0][0] > column[2][2] )
	{
		const float s = std::sqrt( 1.0f + column[0][0] - column[1][1] - column[2][2] ) * 2.0f;

		rotation[3] = ( column[1][2] - column[2][1] ) / s;
		rotation[0] = 0.25f * s;
		rotation[1] = ( column[1][0] + column[0][1] ) / s;
		rotation[2] = ( column[2][0] + column[0][2] ) / s;
	}
	else if ( column[1][1] > column[2][2] )
	{
		const float s = std::sqrt( 1.0f + column[1][1] - column[0][0] - column[2][2] ) * 2.0f;

		rotation[3] = ( column[2][0] - column[0][2] ) / s;
		rotation[0] = ( column[1][0] + column[0][1] ) / s;
		rotation[1] = 0.25f * s;
		rotation[2] = ( column[2][1] + column[1][2] ) / s;
	}
	else
	{
		const float s = std::sqrt( 1.0f + column[2][2] - column[0][0] - column[1][1] ) * 2.0f;

		rotation[3] = ( column[0][1] - column[1][0] ) / s;
		rotation[0] = ( column[2][0] + column[0][2] ) / s;
		rotation[1] = ( column[2][1] + column[1][2] ) / s;
		rotation[2] = 0.25f * s;
	}
}

// Shortest-arc interpolation between two unit quaternions.
//
// Falls back to a normalised linear blend when they are nearly parallel, where
// the sine the spherical form divides by goes to zero. Animation keys are dense
// enough that this is most pairs, so the cheap path is also the common one.
static void
Slerp( const float* a, const float* b, float t, float* out )
{
	float dot = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];

	float end[4] = { b[0], b[1], b[2], b[3] };

	// q and -q are the same rotation, so the sign is chosen to take the short
	// way round rather than spinning most of the way backwards.
	if ( dot < 0.0f )
	{
		dot = -dot;

		for ( int i = 0; i < 4; ++i )
		{
			end[i] = -end[i];
		}
	}

	float wa, wb;

	if ( dot > 0.9995f )
	{
		wa = 1.0f - t;
		wb = t;
	}
	else
	{
		const float angle = std::acos( dot );
		const float sinAngle = std::sin( angle );

		wa = std::sin( ( 1.0f - t ) * angle ) / sinAngle;
		wb = std::sin( t * angle ) / sinAngle;
	}

	float length = 0.0f;

	for ( int i = 0; i < 4; ++i )
	{
		out[i] = a[i] * wa + end[i] * wb;
		length += out[i] * out[i];
	}

	length = std::sqrt( length );

	if ( length > 0.0f )
	{
		for ( int i = 0; i < 4; ++i )
		{
			out[i] /= length;
		}
	}
	else
	{
		out[0] = out[1] = out[2] = 0.0f;
		out[3] = 1.0f;
	}
}

// ----------------------------------------------------------------------------

Model3D::Model3D()
:	fRefCount( 1 )
{
}

Model3D::~Model3D()
{
	for ( size_t i = 0, iMax = fParts.size(); i < iMax; ++i )
	{
		if ( fParts[i].fMesh != NULL )
		{
			fParts[i].fMesh->Release();
		}

		if ( fParts[i].fMaterial != NULL )
		{
			fParts[i].fMaterial->Release();
		}
	}
}

int
Model3D::FindPart( const char* name ) const
{
	if ( name == NULL )
	{
		return -1;
	}

	for ( size_t i = 0, iMax = fParts.size(); i < iMax; ++i )
	{
		if ( fParts[i].fName == name )
		{
			return (int) i;
		}
	}

	return -1;
}

int
Model3D::FindAnimation( const char* name ) const
{
	if ( name == NULL )
	{
		return -1;
	}

	for ( size_t i = 0, iMax = fAnimations.size(); i < iMax; ++i )
	{
		if ( fAnimations[i].fName == name )
		{
			return (int) i;
		}
	}

	return -1;
}

// ----------------------------------------------------------------------------

// Reads one channel at the given time into out, which is three floats for a
// translation or scale track and four for a rotation.
static void
SampleChannel( const ModelAnimationChannel& channel, float time, float* out )
{
	const size_t keyCount = channel.fTimes.size();
	const int components = ( channel.fPath == ModelAnimationChannel::kRotation ) ? 4 : 3;

	if ( keyCount == 0 )
	{
		return;
	}

	// Before the first key and after the last, the track holds its end value.
	// glTF says so explicitly, and it is also what stops a clip whose channels
	// have different lengths from snapping parts of the model to the origin.
	if ( time <= channel.fTimes[0] || keyCount == 1 )
	{
		memcpy( out, &channel.fValues[0], components * sizeof( float ) );

		return;
	}

	if ( time >= channel.fTimes[keyCount - 1] )
	{
		memcpy( out, &channel.fValues[( keyCount - 1 ) * components], components * sizeof( float ) );

		return;
	}

	// The last key at or before `time`. Binary search rather than a linear scan:
	// this runs once per channel per frame, and a long clip has thousands of
	// keys per channel.
	size_t low = 0;
	size_t high = keyCount - 1;

	while ( high - low > 1 )
	{
		const size_t mid = ( low + high ) / 2;

		if ( channel.fTimes[mid] <= time )
		{
			low = mid;
		}
		else
		{
			high = mid;
		}
	}

	const float* a = &channel.fValues[low * components];
	const float* b = &channel.fValues[high * components];

	if ( channel.fStep )
	{
		memcpy( out, a, components * sizeof( float ) );

		return;
	}

	const float span = channel.fTimes[high] - channel.fTimes[low];
	const float t = ( span > 0.0f ) ? ( time - channel.fTimes[low] ) / span : 0.0f;

	if ( channel.fPath == ModelAnimationChannel::kRotation )
	{
		Slerp( a, b, t, out );
	}
	else
	{
		for ( int i = 0; i < components; ++i )
		{
			out[i] = a[i] + ( b[i] - a[i] ) * t;
		}
	}
}

void
Model3D::GetPose( int animation, float time, std::vector< float >& out ) const
{
	const size_t nodeCount = fNodes.size();

	out.resize( nodeCount * 16 );

	if ( nodeCount == 0 )
	{
		return;
	}

	// The nodes' own transforms, which an animation overrides per channel rather
	// than wholesale: a clip that rotates one bone leaves every other node, and
	// that bone's translation and scale, exactly as the file posed them.
	std::vector< float > translation( nodeCount * 3 );
	std::vector< float > rotation( nodeCount * 4 );
	std::vector< float > scale( nodeCount * 3 );

	for ( size_t i = 0; i < nodeCount; ++i )
	{
		memcpy( &translation[i * 3], fNodes[i].fTranslation, 3 * sizeof( float ) );
		memcpy( &rotation[i * 4], fNodes[i].fRotation, 4 * sizeof( float ) );
		memcpy( &scale[i * 3], fNodes[i].fScale, 3 * sizeof( float ) );
	}

	if ( animation >= 0 && animation < (int) fAnimations.size() )
	{
		const ModelAnimation& clip = fAnimations[animation];

		for ( size_t i = 0, iMax = clip.fChannels.size(); i < iMax; ++i )
		{
			const ModelAnimationChannel& channel = clip.fChannels[i];

			if ( channel.fNode < 0 || channel.fNode >= (int) nodeCount )
			{
				continue;
			}

			switch ( channel.fPath )
			{
				case ModelAnimationChannel::kTranslation:
					SampleChannel( channel, time, &translation[channel.fNode * 3] );
					break;

				case ModelAnimationChannel::kRotation:
					SampleChannel( channel, time, &rotation[channel.fNode * 4] );
					break;

				case ModelAnimationChannel::kScale:
					SampleChannel( channel, time, &scale[channel.fNode * 3] );
					break;

				default:
					break;
			}
		}
	}

	// One forward pass: nodes were reordered at load time so that fParent is
	// always an index below the node's own, which means the parent's accumulated
	// matrix is already final by the time it is read.
	for ( size_t i = 0; i < nodeCount; ++i )
	{
		float local[16];
		ComposeTransform( &translation[i * 3], &rotation[i * 4], &scale[i * 3], local );

		const int parent = fNodes[i].fParent;

		if ( parent >= 0 && parent < (int) i )
		{
			Multiply( &out[parent * 16], local, &out[i * 16] );
		}
		else
		{
			memcpy( &out[i * 16], local, sizeof( local ) );
		}
	}
}

U32
Model3D::GetPartPalette( int part, const std::vector< float >& pose, float* out, U32 maxBones ) const
{
	if ( part < 0 || part >= (int) fParts.size() )
	{
		return 0;
	}

	const ModelPart& p = fParts[part];

	if ( p.fSkin < 0 || p.fSkin >= (int) fSkins.size() )
	{
		return 0;
	}

	const ModelSkin& s = fSkins[p.fSkin];

	U32 count = 0;

	for ( size_t slot = 0, slotMax = p.fJointMap.size(); slot < slotMax && count < maxBones; ++slot )
	{
		const int joint = p.fJointMap[slot];
		const int node = ( joint >= 0 && joint < (int) s.fJoints.size() ) ? s.fJoints[joint] : -1;

		float bone[16];

		if ( node < 0 || (size_t) ( node + 1 ) * 16 > pose.size() )
		{
			// A joint naming a node that is not there: leave it as an identity
			// so the vertices it influences stay in their bind pose, rather than
			// reading a matrix that belongs to nothing.
			SetIdentity( bone );
		}
		else
		{
			Multiply( &pose[node * 16], &s.fInverseBind[joint * 16], bone );
		}

		// Column major in, three rows out: row r is every column's rth element,
		// so it gathers with a stride of four.
		float* rows = &out[count * kDraw3DBoneStride];

		for ( int row = 0; row < 3; ++row )
		{
			for ( int col = 0; col < 4; ++col )
			{
				rows[row * 4 + col] = bone[col * 4 + row];
			}
		}

		++count;
	}

	return count;
}

// ----------------------------------------------------------------------------

// Normalises a vertex's four bone weights so the shader can sum them without
// rescaling, and gives a vertex with no influence at all a full-weight one on
// bone zero -- without which its weights sum to zero, the blended matrix is all
// zeroes, and the vertex collapses to the origin, dragging a spike across the
// model.
static void
NormalizeSkinWeights( SkinVertex3D& skin )
{
	float total = 0.0f;

	for ( int i = 0; i < kMaxVertexBones; ++i )
	{
		// A negative weight is meaningless and would fight the others.
		if ( skin.weights[i] < 0.0f )
		{
			skin.weights[i] = 0.0f;
		}

		total += skin.weights[i];
	}

	if ( total > 0.0f )
	{
		for ( int i = 0; i < kMaxVertexBones; ++i )
		{
			skin.weights[i] /= total;
		}
	}
	else
	{
		skin.indices[0] = 0.0f;
		skin.weights[0] = 1.0f;
	}
}

// ----------------------------------------------------------------------------

Model3D*
Model3D::NewFromFile( const char* path )
{
	if ( path == NULL || *path == '\0' )
	{
		return NULL;
	}

	const char* dot = strrchr( path, '.' );

	if ( dot == NULL )
	{
		Rtt_LogException( "ERROR: render.newModel: '%s' has no extension, so its format cannot be told; expected .gltf, .glb or .obj\n", path );

		return NULL;
	}

	// Extensions are matched case-insensitively: files arrive from artists'
	// tools and from case-preserving filesystems, and a model called Robot.GLB
	// is not a different format.
	std::string extension( dot + 1 );

	for ( size_t i = 0, iMax = extension.size(); i < iMax; ++i )
	{
		if ( extension[i] >= 'A' && extension[i] <= 'Z' )
		{
			extension[i] = (char) ( extension[i] - 'A' + 'a' );
		}
	}

	Model3D* model = new Model3D;

	bool loaded = false;

	if ( extension == "gltf" || extension == "glb" )
	{
		loaded = model->LoadGltf( path );
	}
	else if ( extension == "obj" )
	{
		loaded = model->LoadObj( path );
	}
	else
	{
		Rtt_LogException( "ERROR: render.newModel: '%s' is not a supported model format; expected .gltf, .glb or .obj\n", path );
	}

	if ( !loaded || model->fParts.empty() )
	{
		if ( loaded )
		{
			Rtt_LogException( "ERROR: render.newModel: '%s' loaded but contains no drawable geometry\n", path );
		}

		model->Release();

		return NULL;
	}

	return model;
}

// ----------------------------------------------------------------------------
// glTF and GLB, through cgltf.
// ----------------------------------------------------------------------------

// Decodes the base64 payload of a data: URI in place of a file.
//
// Exporters that write a single self-contained .gltf inline their images this
// way, so without this those models load with every map missing.
static bool
DecodeBase64( const char* text, std::vector< U8 >& out )
{
	// Reverse of the standard alphabet, built once. -1 marks a character that is
	// not part of the encoding; '=' padding and any whitespace the writer wrapped
	// lines with are simply skipped.
	static signed char kValue[256];
	static bool built = false;

	if ( !built )
	{
		static const char kAlphabet[] =
			"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

		for ( int i = 0; i < 256; ++i )
		{
			kValue[i] = -1;
		}

		for ( int i = 0; i < 64; ++i )
		{
			kValue[(unsigned char) kAlphabet[i]] = (signed char) i;
		}

		built = true;
	}

	U32 accumulator = 0;
	int bits = 0;

	for ( const char* c = text; *c != '\0'; ++c )
	{
		const signed char value = kValue[(unsigned char) *c];

		if ( value < 0 )
		{
			continue;
		}

		accumulator = ( accumulator << 6 ) | (U32) value;
		bits += 6;

		// A byte emerges every four characters; the leftover bits of a partial
		// group are padding and are dropped.
		if ( bits >= 8 )
		{
			bits -= 8;
			out.push_back( (U8) ( ( accumulator >> bits ) & 0xFF ) );
		}
	}

	return !out.empty();
}

// Loads one glTF image, from wherever that file chose to keep it.
//
// Results are cached by image index for the life of the load, so a file whose
// ten materials share one image decodes it once and hands out one Texture3D.
// Returns NULL if the image cannot be read, having said so.
static Texture3D*
TextureFromGltfImage(
	  const cgltf_image* image
	, const cgltf_data* data
	, const std::string& directory
	, std::map< const cgltf_image*, Texture3D* >& cache )
{
	if ( image == NULL )
	{
		return NULL;
	}

	std::map< const cgltf_image*, Texture3D* >::const_iterator found = cache.find( image );

	if ( found != cache.end() )
	{
		return found->second;
	}

	Texture3D* texture = NULL;

	if ( image->buffer_view != NULL && image->buffer_view->buffer != NULL
		&& image->buffer_view->buffer->data != NULL )
	{
		// A .glb keeps every image in its binary chunk, which is this case and the
		// common one.
		const U8* bytes = (const U8*) image->buffer_view->buffer->data + image->buffer_view->offset;

		texture = Texture3D::NewFromMemory(
			  bytes
			, (U32) image->buffer_view->size
			, image->name != NULL ? image->name : "<embedded>"
			);
	}
	else if ( image->uri != NULL )
	{
		if ( strncmp( image->uri, "data:", 5 ) == 0 )
		{
			const char* comma = strchr( image->uri, ',' );

			if ( comma != NULL )
			{
				std::vector< U8 > bytes;

				if ( DecodeBase64( comma + 1, bytes ) )
				{
					texture = Texture3D::NewFromMemory( &bytes[0], (U32) bytes.size(), "<data uri>" );
				}
			}
		}
		else
		{
			// A relative path, resolved against the model rather than the current
			// directory -- the same rule the .bin beside a .gltf follows.
			//
			// Percent escapes are not undone. A texture whose name contains one
			// will not be found, which is a narrower failure than mangling every
			// path that legitimately contains a '%'.
			texture = Texture3D::NewFromFile( ( directory + image->uri ).c_str() );
		}
	}

	// Cached even when NULL, so a broken image is reported once rather than once
	// per material that names it.
	cache[image] = texture;

	return texture;
}

// Turns one glTF material into the engine's, which is the easy direction: both
// describe metallic-roughness PBR with the same factor names and ranges, because
// Material3D was built to match glTF.
//
// Returns NULL when the primitive had no material, which the caller reads as
// "let the object's own material apply".
static Material3D*
MaterialFromGltf(
	  const cgltf_material* source
	, const cgltf_data* data
	, const std::string& directory
	, std::map< const cgltf_image*, Texture3D* >& textures
	, bool& warnedAboutNormalMaps )
{
	if ( source == NULL )
	{
		return NULL;
	}

	Material3D* material = new Material3D;

	material->SetDoubleSided( source->double_sided != 0 );

	// MASK is treated as OPAQUE rather than as BLEND: the cutout it asks for is a
	// discard against a threshold, which the shader does not do yet, and blending
	// it instead would make a masked surface order-dependent for no benefit.
	material->SetTranslucent( source->alpha_mode == cgltf_alpha_mode_blend );

	if ( source->has_pbr_metallic_roughness )
	{
		const cgltf_pbr_metallic_roughness& pbr = source->pbr_metallic_roughness;

		material->SetAlbedo(
			  pbr.base_color_factor[0]
			, pbr.base_color_factor[1]
			, pbr.base_color_factor[2]
			, pbr.base_color_factor[3]
			);

		material->SetRoughness( pbr.roughness_factor );
		material->SetMetallic( pbr.metallic_factor );

		if ( pbr.base_color_texture.texture != NULL )
		{
			material->SetAlbedoMap( TextureFromGltfImage(
				pbr.base_color_texture.texture->image, data, directory, textures ) );
		}

		if ( pbr.metallic_roughness_texture.texture != NULL )
		{
			material->SetMetallicRoughnessMap( TextureFromGltfImage(
				pbr.metallic_roughness_texture.texture->image, data, directory, textures ) );
		}
	}

	material->SetEmissive( source->emissive_factor[0], source->emissive_factor[1], source->emissive_factor[2] );

	if ( source->emissive_texture.texture != NULL )
	{
		material->SetEmissiveMap( TextureFromGltfImage(
			source->emissive_texture.texture->image, data, directory, textures ) );
	}

	// Normal maps are read as a tangent-space perturbation, and applying one needs
	// per-vertex tangents that Vertex3D does not carry and that most files do not
	// supply either -- they would have to be derived from the UVs. Until that
	// exists, saying so once is better than a surface that looks subtly flat for
	// no visible reason.
	if ( source->normal_texture.texture != NULL && !warnedAboutNormalMaps )
	{
		warnedAboutNormalMaps = true;

		Rtt_LogException( "WARNING: render.newModel: this model's materials reference normal maps, which are not supported yet; its surfaces will be shaded from their vertex normals alone\n" );
	}

	return material;
}

// Reads a whole accessor as floats, three or four at a time.
//
// cgltf_accessor_read_float handles every component type and normalisation rule
// the format allows, so going through it rather than at the buffer directly is
// what makes a file with, say, normalised short texcoords load correctly.
static bool
ReadAccessorFloats( const cgltf_accessor* accessor, int components, std::vector< float >& out )
{
	if ( accessor == NULL || accessor->count == 0 )
	{
		return false;
	}

	out.resize( accessor->count * components );

	for ( cgltf_size i = 0; i < accessor->count; ++i )
	{
		if ( !cgltf_accessor_read_float( accessor, i, &out[i * components], components ) )
		{
			return false;
		}
	}

	return true;
}

bool
Model3D::LoadGltf( const char* path )
{
	cgltf_options options;
	memset( &options, 0, sizeof( options ) );

	cgltf_data* data = NULL;

	cgltf_result result = cgltf_parse_file( &options, path, &data );

	if ( result != cgltf_result_success )
	{
		Rtt_LogException( "ERROR: render.newModel: '%s' could not be parsed as glTF\n", path );

		return false;
	}

	// The external .bin and any external images the file refers to, resolved
	// relative to the file itself -- which is why the path is passed again.
	result = cgltf_load_buffers( &options, data, path );

	if ( result != cgltf_result_success )
	{
		Rtt_LogException( "ERROR: render.newModel: '%s' parsed, but its buffer data could not be loaded; a .gltf needs its .bin alongside it\n", path );

		cgltf_free( data );

		return false;
	}

	if ( cgltf_validate( data ) != cgltf_result_success )
	{
		Rtt_LogException( "ERROR: render.newModel: '%s' is not a valid glTF file\n", path );

		cgltf_free( data );

		return false;
	}

	// ------------------------------------------------------------------------
	// The node hierarchy, reordered so parents precede children.
	//
	// glTF puts nodes in whatever order the exporter wrote them, and a pose is
	// resolved by accumulating each node's transform onto its parent's. Sorting
	// once here turns that into a single forward pass every frame, instead of a
	// recursion with a visited set per frame.
	// ------------------------------------------------------------------------

	const cgltf_size sourceNodeCount = data->nodes_count;

	std::vector< int > remap( sourceNodeCount, -1 );
	std::vector< const cgltf_node* > ordered;
	ordered.reserve( sourceNodeCount );

	std::vector< const cgltf_node* > stack;

	for ( cgltf_size i = 0; i < sourceNodeCount; ++i )
	{
		if ( data->nodes[i].parent == NULL )
		{
			stack.push_back( &data->nodes[i] );
		}
	}

	while ( !stack.empty() )
	{
		const cgltf_node* node = stack.back();
		stack.pop_back();

		const cgltf_size index = cgltf_size( node - data->nodes );

		if ( index >= sourceNodeCount || remap[index] >= 0 )
		{
			continue;
		}

		remap[index] = (int) ordered.size();
		ordered.push_back( node );

		for ( cgltf_size i = 0; i < node->children_count; ++i )
		{
			stack.push_back( node->children[i] );
		}
	}

	// Anything the walk did not reach is in a parent cycle -- malformed, but
	// cheap to survive. Such a node is kept as a root so that a mesh hanging off
	// it still draws, somewhere, rather than the file being rejected outright.
	for ( cgltf_size i = 0; i < sourceNodeCount; ++i )
	{
		if ( remap[i] < 0 )
		{
			remap[i] = (int) ordered.size();
			ordered.push_back( &data->nodes[i] );
		}
	}

	fNodes.resize( ordered.size() );

	for ( size_t i = 0, iMax = ordered.size(); i < iMax; ++i )
	{
		const cgltf_node* node = ordered[i];
		ModelNode& out = fNodes[i];

		out.fName = ( node->name != NULL ) ? node->name : "";

		out.fParent = -1;

		if ( node->parent != NULL )
		{
			const cgltf_size parentIndex = cgltf_size( node->parent - data->nodes );

			if ( parentIndex < sourceNodeCount )
			{
				out.fParent = remap[parentIndex];
			}
		}

		if ( node->has_matrix )
		{
			DecomposeTransform( node->matrix, out.fTranslation, out.fRotation, out.fScale );
		}
		else
		{
			out.fTranslation[0] = out.fTranslation[1] = out.fTranslation[2] = 0.0f;
			out.fRotation[0] = out.fRotation[1] = out.fRotation[2] = 0.0f;
			out.fRotation[3] = 1.0f;
			out.fScale[0] = out.fScale[1] = out.fScale[2] = 1.0f;

			if ( node->has_translation )
			{
				memcpy( out.fTranslation, node->translation, sizeof( out.fTranslation ) );
			}

			if ( node->has_rotation )
			{
				memcpy( out.fRotation, node->rotation, sizeof( out.fRotation ) );
			}

			if ( node->has_scale )
			{
				memcpy( out.fScale, node->scale, sizeof( out.fScale ) );
			}
		}
	}

	// ------------------------------------------------------------------------
	// Skins.
	// ------------------------------------------------------------------------

	fSkins.resize( data->skins_count );

	for ( cgltf_size i = 0; i < data->skins_count; ++i )
	{
		const cgltf_skin& source = data->skins[i];
		ModelSkin& skin = fSkins[i];

		skin.fJoints.resize( source.joints_count );

		for ( cgltf_size j = 0; j < source.joints_count; ++j )
		{
			const cgltf_size index = cgltf_size( source.joints[j] - data->nodes );

			skin.fJoints[j] = ( index < sourceNodeCount ) ? remap[index] : -1;
		}

		skin.fInverseBind.assign( source.joints_count * 16, 0.0f );

		for ( cgltf_size j = 0; j < source.joints_count; ++j )
		{
			SetIdentity( &skin.fInverseBind[j * 16] );
		}

		// The inverse bind matrices are optional: a file that omits them means
		// the joints' bind transforms are identity, which the identities just
		// written already say.
		if ( source.inverse_bind_matrices != NULL )
		{
			std::vector< float > matrices;

			if ( ReadAccessorFloats( source.inverse_bind_matrices, 16, matrices )
				&& matrices.size() >= source.joints_count * 16 )
			{
				memcpy( &skin.fInverseBind[0], &matrices[0], source.joints_count * 16 * sizeof( float ) );
			}
		}

		// No complaint about a skin with more joints than kMaxDraw3DBones: that
		// limit is per draw, and the geometry pass below keeps each part inside it
		// by giving parts palettes of their own, splitting one if it needs to.
	}

	// ------------------------------------------------------------------------
	// Geometry: one part per primitive of every mesh-bearing node.
	//
	// Walking nodes rather than data->meshes is what gives each part its place
	// in the hierarchy. A mesh instanced by two nodes becomes two parts, which
	// duplicates the upload but keeps a part a thing with one transform -- and
	// instanced meshes are rare next to the cost of explaining otherwise.
	// ------------------------------------------------------------------------

	bool warnedAboutNormalMaps = false;

	// The images this file has already yielded, so a map shared by several
	// materials is decoded and uploaded once.
	std::map< const cgltf_image*, Texture3D* > imageCache;

	// Where external images are resolved from: beside the model, not beside the
	// process.
	const std::string modelDirectory = DirectoryOf( path );
	bool warnedAboutPrimitiveType = false;

	for ( size_t nodeIndex = 0, nodeMax = fNodes.size(); nodeIndex < nodeMax; ++nodeIndex )
	{
		const cgltf_node* node = ordered[nodeIndex];

		if ( node->mesh == NULL )
		{
			continue;
		}

		int skinIndex = -1;

		if ( node->skin != NULL )
		{
			const cgltf_size index = cgltf_size( node->skin - data->skins );

			if ( index < data->skins_count )
			{
				skinIndex = (int) index;
			}
		}

		for ( cgltf_size p = 0; p < node->mesh->primitives_count; ++p )
		{
			const cgltf_primitive& primitive = node->mesh->primitives[p];

			if ( primitive.type != cgltf_primitive_type_triangles )
			{
				if ( !warnedAboutPrimitiveType )
				{
					warnedAboutPrimitiveType = true;

					Rtt_LogException( "WARNING: render.newModel: '%s' contains non-triangle primitives, which are skipped\n", path );
				}

				continue;
			}

			// Positions are the one attribute a primitive cannot omit, and the
			// one that fixes how many vertices there are.
			const cgltf_accessor* positions = NULL;
			const cgltf_accessor* normals = NULL;
			const cgltf_accessor* texcoords = NULL;
			const cgltf_accessor* joints = NULL;
			const cgltf_accessor* weights = NULL;

			for ( cgltf_size a = 0; a < primitive.attributes_count; ++a )
			{
				const cgltf_attribute& attribute = primitive.attributes[a];

				// Only the zeroth set of each is read. A second UV set or a
				// second group of four bone influences needs shader support that
				// does not exist yet, and silently mixing set 1 into set 0 would
				// look like a bug in the model.
				if ( attribute.index != 0 )
				{
					continue;
				}

				switch ( attribute.type )
				{
					case cgltf_attribute_type_position: positions = attribute.data; break;
					case cgltf_attribute_type_normal:   normals = attribute.data;   break;
					case cgltf_attribute_type_texcoord: texcoords = attribute.data; break;
					case cgltf_attribute_type_joints:   joints = attribute.data;    break;
					case cgltf_attribute_type_weights:  weights = attribute.data;   break;
					default: break;
				}
			}

			if ( positions == NULL )
			{
				continue;
			}

			const cgltf_size vertexCount = positions->count;

			std::vector< float > positionData;
			std::vector< float > normalData;
			std::vector< float > texcoordData;

			if ( !ReadAccessorFloats( positions, 3, positionData ) )
			{
				continue;
			}

			const bool hasNormals = normals != NULL
				&& normals->count == vertexCount
				&& ReadAccessorFloats( normals, 3, normalData );

			const bool hasTexcoords = texcoords != NULL
				&& texcoords->count == vertexCount
				&& ReadAccessorFloats( texcoords, 2, texcoordData );

			std::vector< Vertex3D > vertices( vertexCount );

			for ( cgltf_size v = 0; v < vertexCount; ++v )
			{
				Vertex3D& vertex = vertices[v];

				vertex.x = positionData[v * 3 + 0];
				vertex.y = positionData[v * 3 + 1];
				vertex.z = positionData[v * 3 + 2];

				if ( hasNormals )
				{
					vertex.nx = normalData[v * 3 + 0];
					vertex.ny = normalData[v * 3 + 1];
					vertex.nz = normalData[v * 3 + 2];
				}
				else
				{
					vertex.nx = vertex.ny = vertex.nz = 0.0f;
				}

				vertex.u = hasTexcoords ? texcoordData[v * 2 + 0] : 0.0f;
				vertex.v = hasTexcoords ? texcoordData[v * 2 + 1] : 0.0f;
			}

			std::vector< U32 > indices;

			if ( primitive.indices != NULL )
			{
				indices.resize( primitive.indices->count );

				for ( cgltf_size i = 0; i < primitive.indices->count; ++i )
				{
					indices[i] = (U32) cgltf_accessor_read_index( primitive.indices, i );
				}
			}
			else
			{
				// A non-indexed primitive is a plain triangle list, so the
				// indices it did not supply are the vertex order itself.
				indices.resize( vertexCount );

				for ( cgltf_size i = 0; i < vertexCount; ++i )
				{
					indices[i] = (U32) i;
				}
			}

			if ( !hasNormals )
			{
				Mesh3D::GenerateNormals( vertices, indices );
			}

			// A primitive has no name of its own in glTF, only its mesh does, so
			// that is what getMesh() matches on. With several primitives under
			// one mesh the index disambiguates them, and the first keeps the
			// bare name -- which is the one a single-material mesh has.
			const char* meshName = ( node->mesh->name != NULL ) ? node->mesh->name : NULL;

			if ( meshName == NULL )
			{
				meshName = ( node->name != NULL ) ? node->name : "";
			}

			std::string partName( meshName );

			if ( p > 0 )
			{
				char suffix[24];
				snprintf( suffix, sizeof( suffix ), ".%d", (int) p );

				partName += suffix;
			}

			{
				float u0 = 1e9f, u1 = -1e9f, v0 = 1e9f, v1 = -1e9f;

				for ( size_t k = 0; k < vertices.size(); ++k )
				{
					if ( vertices[k].u < u0 ) { u0 = vertices[k].u; }
					if ( vertices[k].u > u1 ) { u1 = vertices[k].u; }
					if ( vertices[k].v < v0 ) { v0 = vertices[k].v; }
					if ( vertices[k].v > v1 ) { v1 = vertices[k].v; }
				}

				Material3D* diag = MaterialFromGltf( primitive.material, data, modelDirectory, imageCache, warnedAboutNormalMaps );

				float ar = -1, ag = -1, ab = -1, aa = -1;

				if ( diag != NULL )
				{
					diag->GetAlbedo( ar, ag, ab, aa );
				}

				Rtt_LogException( "DIAG '%s': hasUV=%d u=[%.3f..%.3f] v=[%.3f..%.3f] map=%p albedo=(%.2f %.2f %.2f) 2sided=%d\n",
					partName.c_str(), hasTexcoords ? 1 : 0, u0, u1, v0, v1,
					diag != NULL ? (void*) diag->GetAlbedoMap() : NULL, ar, ag, ab,
					diag != NULL ? ( diag->IsDoubleSided() ? 1 : 0 ) : -1 );

				if ( diag != NULL )
				{
					diag->Release();
				}
			}

			// The bone influences, read only when the primitive carries both
			// halves of them: indices without weights, or the reverse, cannot
			// pose anything and would draw the mesh crumpled at the origin.
			std::vector< U32 > rawJoints;
			std::vector< float > rawWeights;

			if ( skinIndex >= 0
				&& joints != NULL
				&& weights != NULL
				&& joints->count == vertexCount
				&& weights->count == vertexCount
				&& ReadAccessorFloats( weights, 4, rawWeights ) )
			{
				const U32 jointCount = (U32) fSkins[skinIndex].fJoints.size();

				rawJoints.resize( vertexCount * kMaxVertexBones );

				for ( cgltf_size v = 0; v < vertexCount; ++v )
				{
					cgltf_uint indexValues[kMaxVertexBones] = { 0, 0, 0, 0 };

					// Joint indices are integers in the file -- usually unsigned
					// bytes or shorts -- and read_uint is what unpacks them
					// without going through a float that could round.
					cgltf_accessor_read_uint( joints, v, indexValues, kMaxVertexBones );

					for ( int i = 0; i < kMaxVertexBones; ++i )
					{
						// An index naming a joint the skin does not have is
						// dropped rather than trusted; it would otherwise pick up
						// whatever matrix happened to be in that slot.
						if ( indexValues[i] >= jointCount )
						{
							rawJoints[v * kMaxVertexBones + i] = 0;
							rawWeights[v * kMaxVertexBones + i] = 0.0f;
						}
						else
						{
							rawJoints[v * kMaxVertexBones + i] = (U32) indexValues[i];
						}
					}
				}
			}

			if ( rawJoints.empty() )
			{
				// Rigid: the geometry goes to the GPU as it was read, with no
				// palette and no repacking.
				Mesh3D* mesh = Mesh3D::NewFromGeometry( vertices, indices, NULL );

				if ( mesh == NULL )
				{
					continue;
				}

				ModelPart part;

				part.fMesh = mesh;
				part.fMaterial = MaterialFromGltf( primitive.material, data, modelDirectory, imageCache, warnedAboutNormalMaps );
				part.fNode = (int) nodeIndex;
				part.fSkin = -1;
				part.fName = partName;

				fParts.push_back( part );

				continue;
			}

			// ----------------------------------------------------------------
			// Skinned: partition the triangles into groups whose joints fit in
			// one draw's palette.
			//
			// Greedy and in index order, which is enough because the joints a
			// mesh uses vary smoothly across its surface -- neighbouring
			// triangles share bones -- so consecutive triangles nearly always
			// land in the same group. Nearly every part comes out as one group;
			// only a mesh spanning more than kMaxDraw3DBones joints, such as a
			// full head of rigged hair, is split at all.
			// ----------------------------------------------------------------

			struct SkinGroup
			{
				// Palette layout: slot -> index into the skin's joint list.
				std::vector< int > fJointMap;

				// The reverse, to answer "does this group already have this
				// joint, and in which slot" without scanning fJointMap per
				// vertex.
				std::map< U32, U32 > fSlotOf;

				// Indices into the primitive's original vertex array.
				std::vector< U32 > fIndices;
			};

			std::vector< SkinGroup > groups;
			groups.push_back( SkinGroup() );

			for ( size_t t = 0; t + 2 < indices.size(); t += 3 )
			{
				// The joints this triangle actually needs: at most twelve, and
				// usually three or four once the duplicates are dropped.
				U32 needed[3 * kMaxVertexBones];
				U32 neededCount = 0;

				for ( int corner = 0; corner < 3; ++corner )
				{
					const U32 vertex = indices[t + corner];

					for ( int i = 0; i < kMaxVertexBones; ++i )
					{
						if ( rawWeights[vertex * kMaxVertexBones + i] <= 0.0f )
						{
							continue;
						}

						const U32 joint = rawJoints[vertex * kMaxVertexBones + i];

						bool already = false;

						for ( U32 k = 0; k < neededCount; ++k )
						{
							if ( needed[k] == joint ) { already = true; break; }
						}

						if ( !already )
						{
							needed[neededCount++] = joint;
						}
					}
				}

				U32 additions = 0;

				for ( U32 k = 0; k < neededCount; ++k )
				{
					if ( groups.back().fSlotOf.find( needed[k] ) == groups.back().fSlotOf.end() )
					{
						++additions;
					}
				}

				// One triangle needs twelve joints at the very most, so a fresh
				// group can always take it and this cannot loop forever.
				if ( groups.back().fJointMap.size() + additions > (size_t) kMaxDraw3DBones
					&& !groups.back().fIndices.empty() )
				{
					groups.push_back( SkinGroup() );
				}

				SkinGroup& group = groups.back();

				for ( U32 k = 0; k < neededCount; ++k )
				{
					if ( group.fSlotOf.find( needed[k] ) == group.fSlotOf.end() )
					{
						group.fSlotOf[needed[k]] = (U32) group.fJointMap.size();
						group.fJointMap.push_back( (int) needed[k] );
					}
				}

				group.fIndices.push_back( indices[t + 0] );
				group.fIndices.push_back( indices[t + 1] );
				group.fIndices.push_back( indices[t + 2] );
			}

			for ( size_t g = 0; g < groups.size(); ++g )
			{
				SkinGroup& group = groups[g];

				if ( group.fIndices.empty() )
				{
					continue;
				}

				// Each group gets only the vertices it references, renumbered.
				// A vertex on a seam between two groups is duplicated, which is
				// the cost of splitting and is a handful of vertices.
				std::map< U32, U32 > renumbered;

				std::vector< Vertex3D > groupVertices;
				std::vector< U32 > groupIndices;
				std::vector< SkinVertex3D > groupSkin;

				groupIndices.reserve( group.fIndices.size() );

				for ( size_t i = 0, iMax = group.fIndices.size(); i < iMax; ++i )
				{
					const U32 old = group.fIndices[i];

					std::map< U32, U32 >::const_iterator found = renumbered.find( old );

					if ( found != renumbered.end() )
					{
						groupIndices.push_back( found->second );

						continue;
					}

					const U32 fresh = (U32) groupVertices.size();

					renumbered[old] = fresh;
					groupVertices.push_back( vertices[old] );

					SkinVertex3D skin;

					for ( int k = 0; k < kMaxVertexBones; ++k )
					{
						const float weight = rawWeights[old * kMaxVertexBones + k];

						// Slots are this group's, not the skin's: the whole point
						// of the split is that a vertex's bone index becomes small
						// enough to reach.
						std::map< U32, U32 >::const_iterator slot =
							group.fSlotOf.find( rawJoints[old * kMaxVertexBones + k] );

						if ( weight > 0.0f && slot != group.fSlotOf.end() )
						{
							skin.indices[k] = (float) slot->second;
							skin.weights[k] = weight;
						}
						else
						{
							skin.indices[k] = 0.0f;
							skin.weights[k] = 0.0f;
						}
					}

					NormalizeSkinWeights( skin );

					groupSkin.push_back( skin );
					groupIndices.push_back( fresh );
				}

				Mesh3D* mesh = Mesh3D::NewFromGeometry( groupVertices, groupIndices, &groupSkin );

				if ( mesh == NULL )
				{
					continue;
				}

				ModelPart part;

				part.fMesh = mesh;
				part.fMaterial = MaterialFromGltf( primitive.material, data, modelDirectory, imageCache, warnedAboutNormalMaps );
				part.fNode = (int) nodeIndex;
				part.fSkin = skinIndex;
				part.fJointMap = group.fJointMap;
				part.fName = partName;

				// The first piece of a split part keeps the plain name, so
				// getMesh() still finds the part by the name the file gave it.
				if ( g > 0 )
				{
					char suffix[24];
					snprintf( suffix, sizeof( suffix ), "#%d", (int) g );

					part.fName += suffix;
				}

				fParts.push_back( part );
			}
		}
	}

	// The cache's own references. Every material that uses an image retained it,
	// so what this drops is either a shared image's spare reference or an image
	// the file declared and no material named.
	for ( std::map< const cgltf_image*, Texture3D* >::iterator i = imageCache.begin();
		i != imageCache.end(); ++i )
	{
		if ( i->second != NULL )
		{
			i->second->Release();
		}
	}

	// ------------------------------------------------------------------------
	// Animations.
	// ------------------------------------------------------------------------

	fAnimations.resize( data->animations_count );

	for ( cgltf_size i = 0; i < data->animations_count; ++i )
	{
		const cgltf_animation& source = data->animations[i];
		ModelAnimation& clip = fAnimations[i];

		clip.fDuration = 0.0f;

		// An unnamed clip is given its index as a name, so that playAnimation
		// has something to ask for. glTF does not require names, and exporters
		// that write a single clip often leave it blank.
		if ( source.name != NULL && *source.name != '\0' )
		{
			clip.fName = source.name;
		}
		else
		{
			char generated[24];
			snprintf( generated, sizeof( generated ), "animation%d", (int) i );

			clip.fName = generated;
		}

		for ( cgltf_size c = 0; c < source.channels_count; ++c )
		{
			const cgltf_animation_channel& sourceChannel = source.channels[c];

			if ( sourceChannel.target_node == NULL || sourceChannel.sampler == NULL )
			{
				continue;
			}

			U32 pathKind;

			switch ( sourceChannel.target_path )
			{
				case cgltf_animation_path_type_translation:
					pathKind = ModelAnimationChannel::kTranslation;
					break;

				case cgltf_animation_path_type_rotation:
					pathKind = ModelAnimationChannel::kRotation;
					break;

				case cgltf_animation_path_type_scale:
					pathKind = ModelAnimationChannel::kScale;
					break;

				default:
					// Morph target weights, and anything an extension added.
					// Skipped rather than approximated: there is nothing in the
					// pipeline they could drive.
					continue;
			}

			const cgltf_size nodeIndex = cgltf_size( sourceChannel.target_node - data->nodes );

			if ( nodeIndex >= sourceNodeCount )
			{
				continue;
			}

			const cgltf_animation_sampler* sampler = sourceChannel.sampler;

			ModelAnimationChannel channel;

			channel.fNode = remap[nodeIndex];
			channel.fPath = pathKind;
			channel.fStep = ( sampler->interpolation == cgltf_interpolation_type_step );

			if ( !ReadAccessorFloats( sampler->input, 1, channel.fTimes ) )
			{
				continue;
			}

			const int components = ( pathKind == ModelAnimationChannel::kRotation ) ? 4 : 3;

			if ( sampler->interpolation == cgltf_interpolation_type_cubic_spline )
			{
				// Cubic spline keys are stored as in-tangent, value, out-tangent
				// triples. The tangents are dropped and the values interpolated
				// linearly: a visible loss only where a clip leans on strong
				// easing between sparse keys, and far better than reading the
				// tangents as though they were values, which would make the
				// model twitch.
				std::vector< float > packed;

				if ( !ReadAccessorFloats( sampler->output, components * 3, packed ) )
				{
					continue;
				}

				const size_t keyCount = packed.size() / ( components * 3 );

				channel.fValues.resize( keyCount * components );

				for ( size_t k = 0; k < keyCount; ++k )
				{
					memcpy(
						  &channel.fValues[k * components]
						, &packed[k * components * 3 + components]
						, components * sizeof( float )
						);
				}
			}
			else if ( !ReadAccessorFloats( sampler->output, components, channel.fValues ) )
			{
				continue;
			}

			// A channel with fewer values than times would be read past its end
			// when sampled near the end of the clip.
			if ( channel.fValues.size() < channel.fTimes.size() * components )
			{
				continue;
			}

			if ( !channel.fTimes.empty() )
			{
				const float last = channel.fTimes.back();

				if ( last > clip.fDuration )
				{
					clip.fDuration = last;
				}
			}

			clip.fChannels.push_back( channel );
		}
	}

	cgltf_free( data );

	return true;
}

// ----------------------------------------------------------------------------
// Wavefront OBJ, and the MTL library beside it.
//
// Hand-written rather than through a library: the format is a dozen line kinds,
// and the two general-purpose OBJ readers worth using both pull in a mesh
// framework of their own.
// ----------------------------------------------------------------------------

// Reads a whole file into memory.
//
// Models are read once at load and are megabytes rather than gigabytes, so the
// whole file at once is both simpler and faster than streaming it -- and lets
// the parser work on a flat buffer with no chunk boundaries to straddle.
static bool
ReadWholeFile( const char* path, std::vector< char >& out )
{
	FILE* file = fopen( path, "rb" );

	if ( file == NULL )
	{
		return false;
	}

	fseek( file, 0, SEEK_END );
	const long size = ftell( file );
	fseek( file, 0, SEEK_SET );

	if ( size < 0 )
	{
		fclose( file );

		return false;
	}

	out.resize( (size_t) size + 1 );

	const size_t read = ( size > 0 ) ? fread( &out[0], 1, (size_t) size, file ) : 0;

	fclose( file );

	out.resize( read + 1 );

	// A terminator, so the line splitting below can treat the buffer as a string.
	out[read] = '\0';

	return true;
}

// The directory part of a path, with its trailing separator, or empty if the
// path names a file in the current directory. Used to resolve an OBJ's mtllib,
// which is written relative to the OBJ.
static std::string
DirectoryOf( const char* path )
{
	const char* lastSlash = strrchr( path, '/' );

#if defined( Rtt_WIN_ENV )
	const char* lastBackslash = strrchr( path, '\\' );

	if ( lastBackslash != NULL && ( lastSlash == NULL || lastBackslash > lastSlash ) )
	{
		lastSlash = lastBackslash;
	}
#endif

	if ( lastSlash == NULL )
	{
		return std::string();
	}

	return std::string( path, lastSlash - path + 1 );
}

// One material out of an MTL library, keyed by the name `usemtl` uses.
typedef std::map< std::string, Material3D* > MtlLibrary;

static void
ParseMtl( const char* path, MtlLibrary& out, bool& warnedAboutTextures )
{
	std::vector< char > buffer;

	if ( !ReadWholeFile( path, buffer ) )
	{
		// Not an error worth failing the model over: an OBJ whose MTL is missing
		// still has all its geometry, and draws in the default surface.
		Rtt_LogException( "WARNING: render.newModel: the material library '%s' could not be opened; the model's parts will use the default surface\n", path );

		return;
	}

	Material3D* current = NULL;

	// Map paths in an .mtl are relative to the .mtl, which is not necessarily
	// where the .obj is.
	const std::string directory = DirectoryOf( path );

	char* cursor = &buffer[0];

	while ( *cursor != '\0' )
	{
		char* line = cursor;

		while ( *cursor != '\0' && *cursor != '\n' && *cursor != '\r' )
		{
			++cursor;
		}

		while ( *cursor == '\n' || *cursor == '\r' )
		{
			*cursor = '\0';
			++cursor;
		}

		while ( *line == ' ' || *line == '\t' )
		{
			++line;
		}

		if ( *line == '\0' || *line == '#' )
		{
			continue;
		}

		char name[256];
		float x, y, z;

		if ( sscanf( line, "newmtl %255s", name ) == 1 )
		{
			// A redefinition keeps the first: that is what a duplicate name in
			// an MTL almost always is, and swapping to the later one would
			// change what parts already parsed refer to.
			if ( out.find( name ) == out.end() )
			{
				current = new Material3D;
				out[name] = current;
			}
			else
			{
				current = out[name];
			}

			continue;
		}

		if ( current == NULL )
		{
			continue;
		}

		if ( sscanf( line, "Kd %f %f %f", &x, &y, &z ) == 3 )
		{
			float r, g, b, a;
			current->GetAlbedo( r, g, b, a );

			// The alpha already there is kept, since `d` may have been read
			// before `Kd` -- the format fixes no order between them.
			current->SetAlbedo( x, y, z, a );

			continue;
		}

		if ( sscanf( line, "Ke %f %f %f", &x, &y, &z ) == 3 )
		{
			current->SetEmissive( x, y, z );

			continue;
		}

		// Pr and Pm are the metallic-roughness extension several exporters now
		// write, and are preferred over deriving roughness from Ns because they
		// mean exactly what this pipeline's uniforms mean.
		if ( sscanf( line, "Pr %f", &x ) == 1 )
		{
			current->SetRoughness( x );

			continue;
		}

		if ( sscanf( line, "Pm %f", &x ) == 1 )
		{
			current->SetMetallic( x );

			continue;
		}

		if ( sscanf( line, "Ns %f", &x ) == 1 )
		{
			// Blinn-Phong specular exponent to GGX roughness. The usual
			// approximation, and about as good as the mapping between two
			// unrelated lighting models gets: an exponent of 0 is fully rough,
			// and it tightens from there.
			if ( x < 0.0f )
			{
				x = 0.0f;
			}

			current->SetRoughness( std::sqrt( 2.0f / ( x + 2.0f ) ) );

			continue;
		}

		if ( sscanf( line, "d %f", &x ) == 1 )
		{
			float r, g, b, a;
			current->GetAlbedo( r, g, b, a );
			current->SetAlbedo( r, g, b, x );

			continue;
		}

		if ( sscanf( line, "Tr %f", &x ) == 1 )
		{
			// Tr is the complement of d. Files carry one or the other, and
			// occasionally both, in which case the later line wins -- which is
			// the best a format with two spellings of one value allows.
			float r, g, b, a;
			current->GetAlbedo( r, g, b, a );
			current->SetAlbedo( r, g, b, 1.0f - x );

			continue;
		}

		// The maps, resolved against the .mtl's own directory. Only the ones the
		// shader has a slot for; the rest of the format's dozen map kinds are
		// passed over in silence, since naming each would be noise.
		char mapPath[512];

		if ( sscanf( line, "map_Kd %511s", mapPath ) == 1 )
		{
			Texture3D* texture = Texture3D::NewFromFile( ( directory + mapPath ).c_str() );

			if ( texture != NULL )
			{
				current->SetAlbedoMap( texture );
				texture->Release();
			}

			continue;
		}

		if ( sscanf( line, "map_Ke %511s", mapPath ) == 1 )
		{
			Texture3D* texture = Texture3D::NewFromFile( ( directory + mapPath ).c_str() );

			if ( texture != NULL )
			{
				current->SetEmissiveMap( texture );
				texture->Release();
			}

			continue;
		}

		if ( strncmp( line, "map_Bump", 8 ) == 0 || strncmp( line, "bump", 4 ) == 0 )
		{
			if ( !warnedAboutTextures )
			{
				warnedAboutTextures = true;

				Rtt_LogException( "WARNING: render.newModel: this model's material library references bump or normal maps, which are not supported yet\n" );
			}
		}
	}
}

// ----------------------------------------------------------------------------

// A vertex of an OBJ face, as written: indices into the three independent
// position, texcoord and normal arrays.
//
// OBJ indexes those three separately, and a GPU vertex buffer cannot -- one
// index has to name one whole vertex. So each distinct triple becomes one
// vertex, which is what this is the key of.
struct ObjVertexKey
{
	int fPosition;
	int fTexcoord;
	int fNormal;

	bool operator<( const ObjVertexKey& other ) const
	{
		if ( fPosition != other.fPosition ) { return fPosition < other.fPosition; }
		if ( fTexcoord != other.fTexcoord ) { return fTexcoord < other.fTexcoord; }

		return fNormal < other.fNormal;
	}
};

// One part being accumulated: everything between two `usemtl` lines.
struct ObjPart
{
	std::string fName;
	std::string fMaterialName;
	std::vector< Vertex3D > fVertices;
	std::vector< U32 > fIndices;
	std::map< ObjVertexKey, U32 > fLookup;
	bool fHasNormals;
};

// Resolves an OBJ index, which is 1-based and may be negative to count back from
// the end of what has been declared so far. Returns -1 for an index naming
// nothing, which a face containing it is then dropped for.
static int
ResolveObjIndex( int written, size_t declared )
{
	if ( written > 0 )
	{
		return ( (size_t) written <= declared ) ? written - 1 : -1;
	}

	if ( written < 0 )
	{
		const long resolved = (long) declared + written;

		return ( resolved >= 0 ) ? (int) resolved : -1;
	}

	return -1;
}

bool
Model3D::LoadObj( const char* path )
{
	std::vector< char > buffer;

	if ( !ReadWholeFile( path, buffer ) )
	{
		Rtt_LogException( "ERROR: render.newModel: '%s' could not be opened\n", path );

		return false;
	}

	std::vector< float > positions;
	std::vector< float > texcoords;
	std::vector< float > normals;

	std::vector< ObjPart > parts;
	MtlLibrary materials;

	bool warnedAboutTextures = false;

	// The name the next part takes, set by `o` or `g` and consumed when geometry
	// arrives. Held rather than applied immediately because a group line with no
	// faces after it should not produce an empty part.
	std::string pendingName;
	std::string currentMaterial;
	int currentPart = -1;

	char* cursor = &buffer[0];

	while ( *cursor != '\0' )
	{
		char* line = cursor;

		while ( *cursor != '\0' && *cursor != '\n' && *cursor != '\r' )
		{
			++cursor;
		}

		while ( *cursor == '\n' || *cursor == '\r' )
		{
			*cursor = '\0';
			++cursor;
		}

		while ( *line == ' ' || *line == '\t' )
		{
			++line;
		}

		if ( *line == '\0' || *line == '#' )
		{
			continue;
		}

		float x, y, z;

		if ( sscanf( line, "v %f %f %f", &x, &y, &z ) == 3 )
		{
			positions.push_back( x );
			positions.push_back( y );
			positions.push_back( z );

			continue;
		}

		if ( sscanf( line, "vn %f %f %f", &x, &y, &z ) == 3 )
		{
			normals.push_back( x );
			normals.push_back( y );
			normals.push_back( z );

			continue;
		}

		if ( sscanf( line, "vt %f %f", &x, &y ) >= 2 )
		{
			// OBJ's V runs up from the bottom of the image and glTF's -- and this
			// pipeline's -- runs down from the top, so it is flipped here rather
			// than at sample time, where it would have to be flipped for one
			// format and not the other.
			texcoords.push_back( x );
			texcoords.push_back( 1.0f - y );

			continue;
		}

		char name[256];

		if ( sscanf( line, "usemtl %255s", name ) == 1 )
		{
			currentMaterial = name;

			// A material switch starts a new part, since a part is what carries
			// one material. The name follows the material unless a group named
			// itself more recently.
			if ( pendingName.empty() )
			{
				pendingName = name;
			}

			currentPart = -1;

			continue;
		}

		if ( sscanf( line, "mtllib %255s", name ) == 1 )
		{
			ParseMtl( ( DirectoryOf( path ) + name ).c_str(), materials, warnedAboutTextures );

			continue;
		}

		if ( sscanf( line, "o %255s", name ) == 1 || sscanf( line, "g %255s", name ) == 1 )
		{
			pendingName = name;
			currentPart = -1;

			continue;
		}

		if ( line[0] != 'f' || ( line[1] != ' ' && line[1] != '\t' ) )
		{
			continue;
		}

		if ( currentPart < 0 )
		{
			ObjPart part;

			part.fName = pendingName.empty() ? "mesh" : pendingName;
			part.fMaterialName = currentMaterial;
			part.fHasNormals = true;

			currentPart = (int) parts.size();
			parts.push_back( part );

			pendingName.clear();
		}

		ObjPart& part = parts[currentPart];

		// The face's vertices, gathered before triangulating: a face may have any
		// number of them and is fanned from the first.
		std::vector< U32 > face;

		char* token = line + 1;

		while ( *token != '\0' )
		{
			while ( *token == ' ' || *token == '\t' )
			{
				++token;
			}

			if ( *token == '\0' )
			{
				break;
			}

			// The four spellings the format allows: v, v/vt, v//vn and v/vt/vn.
			int written[3] = { 0, 0, 0 };
			int component = 0;

			while ( *token != '\0' && *token != ' ' && *token != '\t' && component < 3 )
			{
				if ( *token == '/' )
				{
					++component;
					++token;

					continue;
				}

				const bool negative = ( *token == '-' );

				if ( negative || *token == '+' )
				{
					++token;
				}

				int value = 0;
				bool any = false;

				while ( *token >= '0' && *token <= '9' )
				{
					value = value * 10 + ( *token - '0' );
					++token;
					any = true;
				}

				if ( any )
				{
					written[component] = negative ? -value : value;
				}
				else if ( !negative )
				{
					// Neither a digit nor a separator: a malformed field. Stop
					// reading this vertex rather than spinning on the character.
					break;
				}
			}

			while ( *token != '\0' && *token != ' ' && *token != '\t' )
			{
				++token;
			}

			ObjVertexKey key;

			key.fPosition = ResolveObjIndex( written[0], positions.size() / 3 );
			key.fTexcoord = ResolveObjIndex( written[1], texcoords.size() / 2 );
			key.fNormal = ResolveObjIndex( written[2], normals.size() / 3 );

			if ( key.fPosition < 0 )
			{
				continue;
			}

			std::map< ObjVertexKey, U32 >::const_iterator found = part.fLookup.find( key );

			if ( found != part.fLookup.end() )
			{
				face.push_back( found->second );

				continue;
			}

			Vertex3D vertex;

			vertex.x = positions[key.fPosition * 3 + 0];
			vertex.y = positions[key.fPosition * 3 + 1];
			vertex.z = positions[key.fPosition * 3 + 2];

			if ( key.fNormal >= 0 )
			{
				vertex.nx = normals[key.fNormal * 3 + 0];
				vertex.ny = normals[key.fNormal * 3 + 1];
				vertex.nz = normals[key.fNormal * 3 + 2];
			}
			else
			{
				vertex.nx = vertex.ny = vertex.nz = 0.0f;

				// One vertex without a normal makes the whole part's normals
				// untrustworthy, so they are all recomputed. Mixing supplied and
				// generated normals across one surface shows as a seam.
				part.fHasNormals = false;
			}

			if ( key.fTexcoord >= 0 )
			{
				vertex.u = texcoords[key.fTexcoord * 2 + 0];
				vertex.v = texcoords[key.fTexcoord * 2 + 1];
			}
			else
			{
				vertex.u = vertex.v = 0.0f;
			}

			const U32 index = (U32) part.fVertices.size();

			part.fVertices.push_back( vertex );
			part.fLookup[key] = index;

			face.push_back( index );
		}

		// Fan triangulation, which is correct for the convex polygons OBJ faces
		// are in practice and produces overlapping triangles for a concave one.
		// Splitting concave polygons properly needs an ear-clipping pass that no
		// exporter's output has ever needed here.
		for ( size_t i = 2, iMax = face.size(); i < iMax; ++i )
		{
			part.fIndices.push_back( face[0] );
			part.fIndices.push_back( face[i - 1] );
			part.fIndices.push_back( face[i] );
		}
	}

	// ------------------------------------------------------------------------
	// Turn the accumulated parts into meshes.
	// ------------------------------------------------------------------------

	for ( size_t i = 0, iMax = parts.size(); i < iMax; ++i )
	{
		ObjPart& part = parts[i];

		if ( part.fVertices.empty() || part.fIndices.empty() )
		{
			continue;
		}

		if ( !part.fHasNormals )
		{
			Mesh3D::GenerateNormals( part.fVertices, part.fIndices );
		}

		Mesh3D* mesh = Mesh3D::NewFromGeometry( part.fVertices, part.fIndices, NULL );

		if ( mesh == NULL )
		{
			continue;
		}

		ModelPart out;

		out.fMesh = mesh;
		out.fMaterial = NULL;
		out.fName = part.fName;

		// OBJ has no hierarchy and no skeleton: every part sits at the origin in
		// the file's own coordinates, and the object's transform is the only one
		// that applies.
		out.fNode = -1;
		out.fSkin = -1;

		MtlLibrary::const_iterator found = materials.find( part.fMaterialName );

		if ( found != materials.end() )
		{
			out.fMaterial = found->second;
			out.fMaterial->Retain();
		}

		fParts.push_back( out );
	}

	// The library's own references. Anything a part took is retained above, so
	// what this drops is either a shared material's spare reference or a material
	// the file defined and never used.
	for ( MtlLibrary::iterator i = materials.begin(); i != materials.end(); ++i )
	{
		i->second->Release();
	}

	return true;
}

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------
