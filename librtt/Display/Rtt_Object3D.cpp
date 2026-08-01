//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Corona game engine.
// For overview and more information on licensing please refer to README.md
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////

#include "Core/Rtt_Build.h"

#include "Display/Rtt_Object3D.h"

#include "Display/Rtt_Camera3D.h"
#include "Display/Rtt_Display.h"

#include "Display/Rtt_LuaLibRender.h"
#include "Display/Rtt_Material3D.h"
#include "Display/Rtt_Mesh3D.h"
#include "Display/Rtt_ShaderEffect3D.h"
#include "Display/Rtt_Scene3D.h"
#include "Renderer/Rtt_Renderer.h"
#include "Rtt_Lua.h"
#include "Rtt_LuaProxy.h"
#include "Rtt_LuaProxyVTable.h"

#include "CoronaLua.h"

#include <cmath>
#include <cstring>

// ----------------------------------------------------------------------------

namespace Rtt
{

// ----------------------------------------------------------------------------

static const float kDegreesToRadians = 3.14159265358979323846f / 180.0f;

Object3D::Object3D( Scene3D& scene, Mesh3D* mesh )
:	Super(),
	fScene( scene ),
	fMesh( mesh ),
	fMaterial( NULL ),
	fEffect( NULL ),
	fHasOrientation( false )
{
	if ( fMesh != NULL )
	{
		fMesh->Retain();
	}

	fPosition[0] = fPosition[1] = fPosition[2] = 0.0f;
	fRotation[0] = fRotation[1] = fRotation[2] = 0.0f;
	fScale[0] = fScale[1] = fScale[2] = 1.0f;

	fOrientation[0] = fOrientation[1] = fOrientation[2] = 0.0f;
	fOrientation[3] = 1.0f;
}

Object3D::~Object3D()
{
	if ( fMesh != NULL )
	{
		fMesh->Release();
	}

	if ( fMaterial != NULL )
	{
		fMaterial->Release();
	}

	if ( fEffect != NULL )
	{
		fEffect->Release();
	}
}

void
Object3D::SetEffect( ShaderEffect3D* effect )
{
	// Retained before the old one is released, as SetMaterial does and for the
	// same reason.
	if ( effect != NULL )
	{
		effect->Retain();
	}

	if ( fEffect != NULL )
	{
		fEffect->Release();
	}

	fEffect = effect;

	InvalidateDisplay();
}

// ----------------------------------------------------------------------------

void
Object3D::GetPosition3D( float& x, float& y, float& z ) const
{
	x = fPosition[0];
	y = fPosition[1];
	z = fPosition[2];
}

void
Object3D::SetPosition3D( float x, float y, float z )
{
	fPosition[0] = x;
	fPosition[1] = y;
	fPosition[2] = z;

	InvalidateDisplay();
}

void
Object3D::Translate3D( float dx, float dy, float dz )
{
	SetPosition3D( fPosition[0] + dx, fPosition[1] + dy, fPosition[2] + dz );
}

void
Object3D::GetRotation3D( float& x, float& y, float& z ) const
{
	x = fRotation[0];
	y = fRotation[1];
	z = fRotation[2];
}

void
Object3D::SetRotation3D( float x, float y, float z )
{
	fRotation[0] = x;
	fRotation[1] = y;
	fRotation[2] = z;

	// Setting angles is a statement that angles are what this object is oriented
	// by, so any quaternion previously set stops applying.
	fHasOrientation = false;

	InvalidateDisplay();
}

void
Object3D::Rotate3D( float dx, float dy, float dz )
{
	SetRotation3D( fRotation[0] + dx, fRotation[1] + dy, fRotation[2] + dz );
}

void
Object3D::SetOrientation( float x, float y, float z, float w )
{
	// Normalised on the way in: a solver's output drifts by a little each step,
	// and an unnormalised quaternion scales the object as well as rotating it.
	const float length = std::sqrt( x * x + y * y + z * z + w * w );

	if ( length > 0.0f )
	{
		fOrientation[0] = x / length;
		fOrientation[1] = y / length;
		fOrientation[2] = z / length;
		fOrientation[3] = w / length;
	}
	else
	{
		fOrientation[0] = fOrientation[1] = fOrientation[2] = 0.0f;
		fOrientation[3] = 1.0f;
	}

	fHasOrientation = true;

	InvalidateDisplay();
}

void
Object3D::GetOrientation( float& x, float& y, float& z, float& w ) const
{
	x = fOrientation[0];
	y = fOrientation[1];
	z = fOrientation[2];
	w = fOrientation[3];
}

void
Object3D::GetScale3D( float& x, float& y, float& z ) const
{
	x = fScale[0];
	y = fScale[1];
	z = fScale[2];
}

void
Object3D::SetScale3D( float x, float y, float z )
{
	fScale[0] = x;
	fScale[1] = y;
	fScale[2] = z;

	InvalidateDisplay();
}

void
Object3D::SetMaterial( Material3D* material )
{
	// Retained first, for the same reason the active camera is: setting the
	// material an object already has must not free it in between.
	if ( material != NULL )
	{
		material->Retain();
	}

	if ( fMaterial != NULL )
	{
		fMaterial->Release();
	}

	fMaterial = material;

	InvalidateDisplay();
}

// ----------------------------------------------------------------------------

void
Object3D::GetWorldMatrix( float* out ) const
{
	// A quaternion orientation, when one has been set, instead of the angles. The
	// composition is the same either way: scale in object space, then rotate, then
	// translate.
	if ( fHasOrientation )
	{
		const float x = fOrientation[0], y = fOrientation[1], z = fOrientation[2], w = fOrientation[3];

		const float xx = x * x, yy = y * y, zz = z * z;
		const float xy = x * y, xz = x * z, yz = y * z;
		const float wx = w * x, wy = w * y, wz = w * z;

		out[0] = ( 1.0f - 2.0f * ( yy + zz ) ) * fScale[0];
		out[1] = ( 2.0f * ( xy + wz ) ) * fScale[0];
		out[2] = ( 2.0f * ( xz - wy ) ) * fScale[0];
		out[3] = 0.0f;

		out[4] = ( 2.0f * ( xy - wz ) ) * fScale[1];
		out[5] = ( 1.0f - 2.0f * ( xx + zz ) ) * fScale[1];
		out[6] = ( 2.0f * ( yz + wx ) ) * fScale[1];
		out[7] = 0.0f;

		out[8] = ( 2.0f * ( xz + wy ) ) * fScale[2];
		out[9] = ( 2.0f * ( yz - wx ) ) * fScale[2];
		out[10] = ( 1.0f - 2.0f * ( xx + yy ) ) * fScale[2];
		out[11] = 0.0f;

		out[12] = fPosition[0];
		out[13] = fPosition[1];
		out[14] = fPosition[2];
		out[15] = 1.0f;

		return;
	}

	const float rx = fRotation[0] * kDegreesToRadians;
	const float ry = fRotation[1] * kDegreesToRadians;
	const float rz = fRotation[2] * kDegreesToRadians;

	const float sx = std::sin( rx ), cx = std::cos( rx );
	const float sy = std::sin( ry ), cy = std::cos( ry );
	const float sz = std::sin( rz ), cz = std::cos( rz );

	// The product Rz * Ry * Rx, written out rather than composed from three
	// matrices: this runs once per object per frame, and the multiplications
	// that would be spent on the zeros of the individual rotations are most of
	// the work.
	const float m00 = cy * cz;
	const float m01 = cy * sz;
	const float m02 = -sy;

	const float m10 = sx * sy * cz - cx * sz;
	const float m11 = sx * sy * sz + cx * cz;
	const float m12 = sx * cy;

	const float m20 = cx * sy * cz + sx * sz;
	const float m21 = cx * sy * sz - sx * cz;
	const float m22 = cx * cy;

	// Scale is applied in object space, before the rotation, which is what
	// makes scaleX stretch the object along its own X rather than the world's.
	out[0] = m00 * fScale[0];  out[1] = m01 * fScale[0];  out[2] = m02 * fScale[0];  out[3] = 0.0f;
	out[4] = m10 * fScale[1];  out[5] = m11 * fScale[1];  out[6] = m12 * fScale[1];  out[7] = 0.0f;
	out[8] = m20 * fScale[2];  out[9] = m21 * fScale[2];  out[10] = m22 * fScale[2]; out[11] = 0.0f;

	out[12] = fPosition[0];
	out[13] = fPosition[1];
	out[14] = fPosition[2];
	out[15] = 1.0f;
}

// ----------------------------------------------------------------------------

void
Object3D::Prepare( const Display& display )
{
	Super::Prepare( display );

	// Objects start out render-dirty and stay that way until whatever builds
	// their geometry says otherwise -- and ShouldDraw() refuses to draw a dirty
	// object. A 3D object has no per-frame geometry to build: its vertices went
	// to the GPU once, and everything that changes about it is a uniform. So
	// there is nothing to do here except say it is ready, which without this
	// nothing ever would, and no 3D object would be drawn at all.
	SetValid();
}

void
Object3D::Draw( Renderer& renderer ) const
{
	// ShouldDraw() is what `isVisible` and `alpha` come out as: a hidden object
	// is not merely drawn transparent, it never reaches the backend at all,
	// which is the exclusion from rendering the API promises.
	if ( ! ShouldDraw() || fMesh == NULL )
	{
		return;
	}

	Camera3D* camera = fScene.GetActiveCamera();

	// Without a camera there is no way to project anything. Silently skipping
	// is right rather than asserting: a project that creates geometry before
	// its camera is mid-setup, not broken, and will start drawing once
	// setActiveCamera is called.
	if ( camera == NULL )
	{
		return;
	}

	Draw3DCommand command;
	memset( &command, 0, sizeof( command ) );

	command.fMesh = fMesh;
	command.fCamera = camera;

	GetWorldMatrix( command.fTransform );

	if ( fMaterial != NULL )
	{
		fMaterial->GetAlbedo( command.fAlbedo[0], command.fAlbedo[1], command.fAlbedo[2], command.fAlbedo[3] );
		fMaterial->GetEmissive( command.fEmissive[0], command.fEmissive[1], command.fEmissive[2] );
		command.fRoughness = fMaterial->GetRoughness();
		command.fMetallic = fMaterial->GetMetallic();
		command.fAlbedoMap = fMaterial->GetAlbedoMap();
		command.fMetallicRoughnessMap = fMaterial->GetMetallicRoughnessMap();
		command.fEmissiveMap = fMaterial->GetEmissiveMap();
		command.fIsDoubleSided = fMaterial->IsDoubleSided();
		command.fIsTranslucent = fMaterial->IsTranslucent();
	}
	else
	{
		// An object with no material set still has to be visible, so it gets
		// the same neutral surface a default-constructed Material3D describes.
		command.fAlbedo[0] = command.fAlbedo[1] = command.fAlbedo[2] = command.fAlbedo[3] = 1.0f;
		command.fRoughness = 0.5f;
		command.fMetallic = 0.0f;

		// The primitives are closed solids wound consistently, so culling their
		// back faces is both safe and half the fragment work.
		command.fIsDoubleSided = false;
		command.fIsTranslucent = false;
	}

	command.fEffect = fEffect;

	// AlphaCumulative rather than Alpha, so that fading a group fades the 3D
	// objects inside it the way it fades the 2D ones.
	command.fAlpha = (float) AlphaCumulative() / 255.0f;

	// The shadow light is resolved first so the gather can report where it landed.
	Light3D* shadowLight = fScene.GetShadowLight();

	command.fLightCount = fScene.GatherLights( command.fLights, shadowLight, &command.fShadowLightIndex );
	fScene.GetAmbient( command.fAmbient[0], command.fAmbient[1], command.fAmbient[2] );

	command.fEnvironmentMap = fScene.GetEnvironmentMap();
	command.fIrradiance = fScene.GetIrradiance();
	command.fEnvironmentIntensity = fScene.GetEnvironmentIntensity();

	// The shadow-casting light, if any, and the point its map is centred on: what
	// the camera is aimed at, so the shadows are sharpest where the eye is.
	command.fCastsShadows = ( shadowLight != NULL );
	command.fShadowLight = shadowLight;

	if ( shadowLight != NULL )
	{
		camera->GetLookAt( command.fShadowCentre[0], command.fShadowCentre[1], command.fShadowCentre[2] );

		command.fShadowBias = shadowLight->GetShadowBias();
		command.fShadowStrength = shadowLight->GetShadowStrength();
	}

	renderer.Insert3D( command );
}

void
Object3D::GetSelfBounds( Rect& rect ) const
{
	// A 3D object has no meaningful extent in content space -- where it lands
	// on screen depends on the camera, and changes every time the camera moves.
	// An empty rect is the honest answer, and is what keeps the 2D culler and
	// hit tester from acting on a rectangle that means nothing.
	rect.SetEmpty();
}

// out = b applied after a, both column-major.
static void
MultiplyMatrix4x4( const float* a, const float* b, float* out )
{
	for ( int col = 0; col < 4; ++col )
	{
		for ( int row = 0; row < 4; ++row )
		{
			float sum = 0.0f;

			for ( int k = 0; k < 4; ++k )
			{
				sum += b[k * 4 + row] * a[col * 4 + k];
			}

			out[col * 4 + row] = sum;
		}
	}
}

bool
Object3D::GetScreenBounds( const Display& display, Rect& bounds ) const
{
	Camera3D* camera = fScene.GetActiveCamera();

	float minimum[3], maximum[3];

	if ( camera == NULL || ! GetLocalBounds( minimum, maximum ) )
	{
		return false;
	}

	float world[16], view[16], projection[16];

	GetWorldMatrix( world );
	camera->GetViewMatrix( view );

	// Homogeneous depth or not only changes how z maps into clip space, and
	// nothing here reads z beyond the sign of w, so either convention gives the
	// same rectangle.
	camera->GetProjectionMatrix( projection, U32( display.ScreenWidth() ), U32( display.ScreenHeight() ), true );

	float viewProjection[16], transform[16];

	MultiplyMatrix4x4( view, projection, viewProjection );
	MultiplyMatrix4x4( world, viewProjection, transform );

	const Rect& screen = display.GetScreenContentBounds();

	const Real screenWidth = screen.xMax - screen.xMin;
	const Real screenHeight = screen.yMax - screen.yMin;

	Rect result;
	result.SetEmpty();

	for ( int corner = 0; corner < 8; ++corner )
	{
		const float point[3] =
		{
			( corner & 1 ) ? maximum[0] : minimum[0],
			( corner & 2 ) ? maximum[1] : minimum[1],
			( corner & 4 ) ? maximum[2] : minimum[2]
		};

		float clip[4];

		for ( int row = 0; row < 4; ++row )
		{
			// Column-major, so each of the matrix's columns is four elements
			// apart and the last of them is the translation the point picks up
			// for having an implicit w of one.
			clip[row] = transform[row] * point[0]
				+ transform[4 + row] * point[1]
				+ transform[8 + row] * point[2]
				+ transform[12 + row];
		}

		// Behind the eye, where the perspective divide turns the projection
		// inside out. One such corner is enough to make the enclosing rectangle
		// meaningless, so the caller is told there is none rather than handed a
		// box turned the wrong way round.
		if ( clip[3] <= 0.0f )
		{
			return false;
		}

		const float ndcX = clip[0] / clip[3];
		const float ndcY = clip[1] / clip[3];

		Vertex2 vertex;

		vertex.x = screen.xMin + Rtt_RealMul( Rtt_FloatToReal( ndcX * 0.5f + 0.5f ), screenWidth );

		// Clip space counts y upwards and content space counts it downwards.
		vertex.y = screen.yMin + Rtt_RealMul( Rtt_FloatToReal( 0.5f - ndcY * 0.5f ), screenHeight );

		result.Union( vertex );
	}

	if ( result.IsEmpty() )
	{
		return false;
	}

	bounds = result;

	return true;
}

bool
Object3D::HitTest( Real contentX, Real contentY )
{
	// Touch on 3D objects is what render.raycast() is for, once it exists.
	// Returning false here means a 3D object never swallows a touch that a 2D
	// object underneath it should have received.
	return false;
}

bool
Object3D::CanHitTest() const
{
	return false;
}

bool
Object3D::CanCull() const
{
	// The 2D culler works on stage bounds, which for a 3D object are empty and
	// would cull it from every frame.
	return false;
}

Object3D*
Object3D::AsObject3D()
{
	return this;
}

// ----------------------------------------------------------------------------
// Ray casting.
//
// The ray is brought into the mesh's object space rather than the triangles
// being brought into the world: one inverse per object against a transform per
// vertex, on meshes of tens of thousands of them.
// ----------------------------------------------------------------------------

// Inverts an affine transform -- rotation, scale and translation, no projection.
//
// Returns false for a singular one, which is what a zero scale on any axis
// gives: such an object is flat to nothing and cannot be hit.
bool
Object3D::InvertAffine( const float* m, float* out )
{
	// Cofactors of the upper-left 3x3.
	const float a = m[0], b = m[4], c = m[8];
	const float d = m[1], e = m[5], f = m[9];
	const float g = m[2], h = m[6], i = m[10];

	const float A =  ( e * i - f * h );
	const float B = -( d * i - f * g );
	const float C =  ( d * h - e * g );

	const float determinant = a * A + b * B + c * C;

	if ( determinant > -1e-12f && determinant < 1e-12f )
	{
		return false;
	}

	const float inverse = 1.0f / determinant;

	// The adjugate over the determinant, written out transposed into the
	// column-major slots.
	out[0] = A * inverse;
	out[1] = B * inverse;
	out[2] = C * inverse;
	out[3] = 0.0f;

	out[4] = -( b * i - c * h ) * inverse;
	out[5] =  ( a * i - c * g ) * inverse;
	out[6] = -( a * h - b * g ) * inverse;
	out[7] = 0.0f;

	out[8] =  ( b * f - c * e ) * inverse;
	out[9] = -( a * f - c * d ) * inverse;
	out[10] = ( a * e - b * d ) * inverse;
	out[11] = 0.0f;

	// The inverse translation is the inverse rotation applied to the negated one.
	out[12] = -( out[0] * m[12] + out[4] * m[13] + out[8] * m[14] );
	out[13] = -( out[1] * m[12] + out[5] * m[13] + out[9] * m[14] );
	out[14] = -( out[2] * m[12] + out[6] * m[13] + out[10] * m[14] );
	out[15] = 1.0f;

	return true;
}

static void
TransformPoint( const float* m, const float* v, float* out )
{
	out[0] = m[0] * v[0] + m[4] * v[1] + m[8] * v[2] + m[12];
	out[1] = m[1] * v[0] + m[5] * v[1] + m[9] * v[2] + m[13];
	out[2] = m[2] * v[0] + m[6] * v[1] + m[10] * v[2] + m[14];
}

static void
TransformDirection( const float* m, const float* v, float* out )
{
	out[0] = m[0] * v[0] + m[4] * v[1] + m[8] * v[2];
	out[1] = m[1] * v[0] + m[5] * v[1] + m[9] * v[2];
	out[2] = m[2] * v[0] + m[6] * v[1] + m[10] * v[2];
}

// Whether the ray comes within `radius` of the origin, used to throw out a whole
// mesh before any of its triangles are looked at.
//
// Only the sphere being missed is conclusive; a ray that passes through it still
// has to be tested properly.
static bool
RayHitsSphere( const float* origin, const float* direction, float radius )
{
	const float b = origin[0] * direction[0] + origin[1] * direction[1] + origin[2] * direction[2];
	const float c = origin[0] * origin[0] + origin[1] * origin[1] + origin[2] * origin[2] - radius * radius;

	// Starting inside counts as a hit whichever way the ray points.
	if ( c <= 0.0f )
	{
		return true;
	}

	// Pointing away from a sphere it starts outside of.
	if ( b > 0.0f )
	{
		return false;
	}

	const float dd = direction[0] * direction[0] + direction[1] * direction[1] + direction[2] * direction[2];

	return ( b * b - dd * c ) >= 0.0f;
}

// Moller-Trumbore. Returns the ray parameter of the hit, or a negative number for
// a miss, and fills the barycentric coordinates so the caller can interpolate.
//
// Two-sided: a triangle is hit from either face. A one-sided test would make a
// tap miss anything the camera happens to be looking at the back of, and whether
// a surface is culled when drawn says nothing about whether it can be picked.
static float
RayHitsTriangle(
	  const float* origin
	, const float* direction
	, const float* a
	, const float* b
	, const float* c
	, float& outU
	, float& outV )
{
	const float e1[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] };
	const float e2[3] = { c[0] - a[0], c[1] - a[1], c[2] - a[2] };

	const float p[3] =
	{
		direction[1] * e2[2] - direction[2] * e2[1],
		direction[2] * e2[0] - direction[0] * e2[2],
		direction[0] * e2[1] - direction[1] * e2[0]
	};

	const float determinant = e1[0] * p[0] + e1[1] * p[1] + e1[2] * p[2];

	// Parallel to the triangle's plane, or degenerate.
	if ( determinant > -1e-9f && determinant < 1e-9f )
	{
		return -1.0f;
	}

	const float inverse = 1.0f / determinant;

	const float t[3] = { origin[0] - a[0], origin[1] - a[1], origin[2] - a[2] };

	const float u = ( t[0] * p[0] + t[1] * p[1] + t[2] * p[2] ) * inverse;

	if ( u < 0.0f || u > 1.0f )
	{
		return -1.0f;
	}

	const float q[3] =
	{
		t[1] * e1[2] - t[2] * e1[1],
		t[2] * e1[0] - t[0] * e1[2],
		t[0] * e1[1] - t[1] * e1[0]
	};

	const float v = ( direction[0] * q[0] + direction[1] * q[1] + direction[2] * q[2] ) * inverse;

	if ( v < 0.0f || u + v > 1.0f )
	{
		return -1.0f;
	}

	outU = u;
	outV = v;

	return ( e2[0] * q[0] + e2[1] * q[1] + e2[2] * q[2] ) * inverse;
}

bool
Object3D::RaycastMesh(
	  const Mesh3D& mesh
	, const float* worldMatrix
	, const float* origin
	, const float* direction
	, const float* bonePalette
	, U32 boneCount
	, Raycast3DHit& hit )
{
	float inverseMatrix[16];

	if ( !InvertAffine( worldMatrix, inverseMatrix ) )
	{
		return false;
	}

	float localOrigin[3];
	float localDirection[3];

	TransformPoint( inverseMatrix, origin, localOrigin );
	TransformDirection( inverseMatrix, direction, localDirection );

	const std::vector< Vertex3D >& vertices = mesh.GetVertices();
	const std::vector< U32 >& indices = mesh.GetIndices();

	// The bounding radius describes the bind pose, and posing moves vertices
	// outside it, so a skinned mesh is given room rather than being rejected
	// early on a radius that no longer bounds it.
	const float radius = mesh.GetBoundingRadius() * ( bonePalette != NULL ? 3.0f : 1.0f );

	if ( !RayHitsSphere( localOrigin, localDirection, radius ) )
	{
		return false;
	}

	bool found = false;
	float nearest = 0.0f;

	for ( size_t i = 0, iMax = indices.size(); i + 2 < iMax; i += 3 )
	{
		float p[3][3];

		for ( int corner = 0; corner < 3; ++corner )
		{
			const Vertex3D& vertex = vertices[indices[i + corner]];

			p[corner][0] = vertex.x;
			p[corner][1] = vertex.y;
			p[corner][2] = vertex.z;
		}

		// A skinned mesh is stored in its bind pose, so testing it as stored
		// would pick against a figure standing still while the visible one moves.
		// The palette is applied to the three corners here instead -- only for
		// the meshes that got past the sphere, and only when a ray is actually
		// being cast.
		if ( bonePalette != NULL && mesh.IsSkinned() )
		{
			const std::vector< SkinVertex3D >& skin = mesh.GetSkin();

			for ( int corner = 0; corner < 3; ++corner )
			{
				const SkinVertex3D& influences = skin[indices[i + corner]];

				float posed[3] = { 0.0f, 0.0f, 0.0f };

				for ( int k = 0; k < kMaxVertexBones; ++k )
				{
					const float weight = influences.weights[k];

					if ( weight <= 0.0f )
					{
						continue;
					}

					const U32 bone = (U32) influences.indices[k];

					if ( bone >= boneCount )
					{
						continue;
					}

					// Three rows of four, as the palette is packed for the shader.
					const float* rows = bonePalette + bone * kDraw3DBoneStride;

					for ( int row = 0; row < 3; ++row )
					{
						posed[row] += weight * (
							  rows[row * 4 + 0] * p[corner][0]
							+ rows[row * 4 + 1] * p[corner][1]
							+ rows[row * 4 + 2] * p[corner][2]
							+ rows[row * 4 + 3] );
					}
				}

				p[corner][0] = posed[0];
				p[corner][1] = posed[1];
				p[corner][2] = posed[2];
			}
		}

		float u, v;

		const float distance = RayHitsTriangle( localOrigin, localDirection, p[0], p[1], p[2], u, v );

		// Behind the ray's origin, or further than something already found.
		if ( distance < 0.0f || ( found && distance >= nearest ) )
		{
			continue;
		}

		nearest = distance;
		found = true;

		// The face normal rather than an interpolated vertex one: this is the
		// geometry that was hit, and a caller reflecting off it or placing
		// something on it wants the surface, not the shading.
		const float e1[3] = { p[1][0] - p[0][0], p[1][1] - p[0][1], p[1][2] - p[0][2] };
		const float e2[3] = { p[2][0] - p[0][0], p[2][1] - p[0][1], p[2][2] - p[0][2] };

		float localNormal[3] =
		{
			e1[1] * e2[2] - e1[2] * e2[1],
			e1[2] * e2[0] - e1[0] * e2[2],
			e1[0] * e2[1] - e1[1] * e2[0]
		};

		// Back to the world. The point transforms by the matrix; the normal by
		// the inverse transpose, which for the inverse already in hand is a
		// transposed multiply.
		const float localPoint[3] =
		{
			localOrigin[0] + localDirection[0] * distance,
			localOrigin[1] + localDirection[1] * distance,
			localOrigin[2] + localDirection[2] * distance
		};

		TransformPoint( worldMatrix, localPoint, hit.fPoint );

		hit.fNormal[0] = inverseMatrix[0] * localNormal[0] + inverseMatrix[1] * localNormal[1] + inverseMatrix[2] * localNormal[2];
		hit.fNormal[1] = inverseMatrix[4] * localNormal[0] + inverseMatrix[5] * localNormal[1] + inverseMatrix[6] * localNormal[2];
		hit.fNormal[2] = inverseMatrix[8] * localNormal[0] + inverseMatrix[9] * localNormal[1] + inverseMatrix[10] * localNormal[2];

		const float length = std::sqrt(
			hit.fNormal[0] * hit.fNormal[0] +
			hit.fNormal[1] * hit.fNormal[1] +
			hit.fNormal[2] * hit.fNormal[2] );

		if ( length > 0.0f )
		{
			hit.fNormal[0] /= length;
			hit.fNormal[1] /= length;
			hit.fNormal[2] /= length;
		}

		// In world units: the local direction was the world one put through the
		// inverse, so its length carries the object's scale and the parameter
		// along it is in world terms already.
		hit.fDistance = distance;
	}

	return found;
}

// The bounds of one mesh, walked once. Shared with ModelObject3D, which unions
// this over its parts.
static bool
AccumulateMeshBounds( const Mesh3D& mesh, const float* nodeMatrix, float* minimum, float* maximum )
{
	const std::vector< Vertex3D >& vertices = mesh.GetVertices();

	if ( vertices.empty() )
	{
		return false;
	}

	for ( size_t i = 0, iMax = vertices.size(); i < iMax; ++i )
	{
		float p[3] = { vertices[i].x, vertices[i].y, vertices[i].z };

		if ( nodeMatrix != NULL )
		{
			const float x = p[0], y = p[1], z = p[2];

			p[0] = nodeMatrix[0] * x + nodeMatrix[4] * y + nodeMatrix[8] * z + nodeMatrix[12];
			p[1] = nodeMatrix[1] * x + nodeMatrix[5] * y + nodeMatrix[9] * z + nodeMatrix[13];
			p[2] = nodeMatrix[2] * x + nodeMatrix[6] * y + nodeMatrix[10] * z + nodeMatrix[14];
		}

		for ( int k = 0; k < 3; ++k )
		{
			if ( p[k] < minimum[k] ) { minimum[k] = p[k]; }
			if ( p[k] > maximum[k] ) { maximum[k] = p[k]; }
		}
	}

	return true;
}

bool
Object3D::GetMeshBounds( const Mesh3D& mesh, const float* nodeMatrix, float* minimum, float* maximum )
{
	return AccumulateMeshBounds( mesh, nodeMatrix, minimum, maximum );
}

bool
Object3D::GetLocalBounds( float* minimum, float* maximum ) const
{
	if ( fMesh == NULL )
	{
		return false;
	}

	minimum[0] = minimum[1] = minimum[2] = 1e30f;
	maximum[0] = maximum[1] = maximum[2] = -1e30f;

	return AccumulateMeshBounds( *fMesh, NULL, minimum, maximum );
}

bool
Object3D::Raycast( const float* origin, const float* direction, Raycast3DHit& hit ) const
{
	// Hidden objects are not pickable, matching the rule that a hidden object
	// leaves no trace in the rendered frame.
	if ( fMesh == NULL || !IsVisible() )
	{
		return false;
	}

	float world[16];
	GetWorldMatrix( world );

	if ( !RaycastMesh( *fMesh, world, origin, direction, NULL, 0, hit ) )
	{
		return false;
	}

	hit.fObject = const_cast< Object3D* >( this );
	hit.fMeshName = "";

	return true;
}

// ----------------------------------------------------------------------------

Light3D::Light3D( Scene3D& scene, Draw3DLight::Kind kind )
:	Super( scene, NULL ),
	fKind( kind ),
	fIntensity( 1.0f ),
	fRange( 100.0f ),
	fInnerAngle( 30.0f ),
	fOuterAngle( 45.0f ),
	fCastsShadows( false ),
	fShadowExtent( 20.0f ),
	fShadowBias( 0.05f ),
	fShadowStrength( 1.0f )
{
	fColor[0] = 1.0f;
	fColor[1] = 1.0f;
	fColor[2] = 1.0f;

	// A directional light with no rotation should shine somewhere useful. Down
	// and slightly forward is where a project would put the sun first anyway.
	if ( kind == Draw3DLight::kDirectional )
	{
		fRotation[0] = 45.0f;
		fRotation[1] = -30.0f;
	}

	fScene.AddLight( this );
}

Light3D::~Light3D()
{
	fScene.RemoveLight( this );
}

void
Light3D::Draw( Renderer& renderer ) const
{
	// Nothing: a light is not geometry. Its contribution is gathered by the
	// objects it lights, in Object3D::Draw.
}

void
Light3D::SetColor( float r, float g, float b )
{
	fColor[0] = r;
	fColor[1] = g;
	fColor[2] = b;
}

void
Light3D::GetColor( float& r, float& g, float& b ) const
{
	r = fColor[0];
	g = fColor[1];
	b = fColor[2];
}

void
Light3D::SetShadowStrength( float value )
{
	fShadowStrength = ( value < 0.0f ) ? 0.0f : ( ( value > 1.0f ) ? 1.0f : value );
}

// An orthographic box aimed down the light, which is the right projection for a
// directional light: its rays are parallel, so there is no centre of projection
// for a perspective one to sit at.
void
Light3D::GetShadowMatrix( const float* centre, bool homogeneousDepth, float* out ) const
{
	// The direction the light shines in, taken from its rotation exactly as
	// GetLightData does, so the shadows fall the way the shading says they should.
	float world[16];
	GetWorldMatrix( world );

	float fx = world[8], fy = world[9], fz = world[10];

	float length = std::sqrt( fx * fx + fy * fy + fz * fz );

	if ( length > 0.0f )
	{
		fx /= length;
		fy /= length;
		fz /= length;
	}
	else
	{
		fx = 0.0f;
		fy = -1.0f;
		fz = 0.0f;
	}

	// Backed off along the direction far enough that everything within the extent
	// is in front of the near plane.
	const float distance = fShadowExtent;

	const float eye[3] =
	{
		centre[0] - fx * distance,
		centre[1] - fy * distance,
		centre[2] - fz * distance
	};

	// Any up vector not parallel to the direction will do; +Y unless the light
	// points nearly straight up or down, in which case +Z.
	float upX = 0.0f, upY = 1.0f, upZ = 0.0f;

	if ( std::fabs( fy ) > 0.99f )
	{
		upX = 0.0f;
		upY = 0.0f;
		upZ = 1.0f;
	}

	// The same left-handed basis Camera3D builds, so the two agree about handedness.
	float rx = upY * fz - upZ * fy;
	float ry = upZ * fx - upX * fz;
	float rz = upX * fy - upY * fx;

	length = std::sqrt( rx * rx + ry * ry + rz * rz );

	if ( length > 0.0f )
	{
		rx /= length;
		ry /= length;
		rz /= length;
	}

	const float ux = fy * rz - fz * ry;
	const float uy = fz * rx - fx * rz;
	const float uz = fx * ry - fy * rx;

	float view[16];

	view[0] = rx;  view[1] = ux;  view[2] = fx;  view[3] = 0.0f;
	view[4] = ry;  view[5] = uy;  view[6] = fy;  view[7] = 0.0f;
	view[8] = rz;  view[9] = uz;  view[10] = fz; view[11] = 0.0f;

	view[12] = -( rx * eye[0] + ry * eye[1] + rz * eye[2] );
	view[13] = -( ux * eye[0] + uy * eye[1] + uz * eye[2] );
	view[14] = -( fx * eye[0] + fy * eye[1] + fz * eye[2] );
	view[15] = 1.0f;

	// The orthographic box: the extent across, and twice the extent deep so that
	// a caster behind the centre still reaches the map.
	const float half = fShadowExtent * 0.5f;
	const float near = 0.01f;
	const float far = fShadowExtent * 2.0f;

	float projection[16];

	for ( int i = 0; i < 16; ++i )
	{
		projection[i] = 0.0f;
	}

	projection[0] = 1.0f / half;
	projection[5] = 1.0f / half;
	projection[15] = 1.0f;

	// The same 0..1 or -1..1 depth split Camera3D honours, for the same reason:
	// which one applies is a property of the graphics API, not of the light.
	if ( homogeneousDepth )
	{
		projection[10] = 2.0f / ( far - near );
		projection[14] = -( far + near ) / ( far - near );
	}
	else
	{
		projection[10] = 1.0f / ( far - near );
		projection[14] = -near / ( far - near );
	}

	// out = projection * view.
	for ( int col = 0; col < 4; ++col )
	{
		for ( int row = 0; row < 4; ++row )
		{
			float sum = 0.0f;

			for ( int k = 0; k < 4; ++k )
			{
				sum += projection[k * 4 + row] * view[col * 4 + k];
			}

			out[col * 4 + row] = sum;
		}
	}
}

void
Light3D::SetInnerAngle( float degrees )
{
	fInnerAngle = degrees;
}

void
Light3D::SetOuterAngle( float degrees )
{
	fOuterAngle = degrees;
}

bool
Light3D::GetLightData( Draw3DLight& out ) const
{
	if ( ! IsVisible() )
	{
		return false;
	}

	out.fKind = (U32) fKind;

	out.fPosition[0] = fPosition[0];
	out.fPosition[1] = fPosition[1];
	out.fPosition[2] = fPosition[2];

	// A light's rotation is what aims it: the direction is its local +Z put
	// through the same rotation an object's geometry would get, so rotating a
	// light behaves the way rotating anything else in the scene does.
	float world[16];
	GetWorldMatrix( world );

	out.fDirection[0] = world[8];
	out.fDirection[1] = world[9];
	out.fDirection[2] = world[10];

	// Undo the scale, which GetWorldMatrix folded in and which would otherwise
	// read as a brighter or dimmer light once the shader normalises against it.
	const float length = std::sqrt(
		out.fDirection[0] * out.fDirection[0] +
		out.fDirection[1] * out.fDirection[1] +
		out.fDirection[2] * out.fDirection[2] );

	if ( length > 0.0f )
	{
		out.fDirection[0] /= length;
		out.fDirection[1] /= length;
		out.fDirection[2] /= length;
	}

	out.fColor[0] = fColor[0];
	out.fColor[1] = fColor[1];
	out.fColor[2] = fColor[2];
	out.fIntensity = fIntensity;
	out.fRange = fRange;

	out.fInnerCosine = std::cos( fInnerAngle * kDegreesToRadians * 0.5f );
	out.fOuterCosine = std::cos( fOuterAngle * kDegreesToRadians * 0.5f );

	// An outer cone inside the inner one makes the falloff divide by a negative
	// span and turns the light inside out. Pushing them apart by a hair is
	// kinder than refusing the values, which are usually a typo in one number.
	if ( out.fOuterCosine >= out.fInnerCosine )
	{
		out.fOuterCosine = out.fInnerCosine - 0.001f;
	}

	return true;
}

// ----------------------------------------------------------------------------

const LuaProxyVTable&
Object3D::ProxyVTable() const
{
	return LuaObject3DProxyVTable::Constant();
}

const LuaProxyVTable&
Light3D::ProxyVTable() const
{
	return LuaLight3DProxyVTable::Constant();
}

// ----------------------------------------------------------------------------

// The 3D vtables match keys with a strcmp chain rather than the perfect hash
// the older vtables use. The key sets here are small enough that the difference
// does not show up in a frame, and a perfect hash has to be regenerated -- with
// new magic seeds -- every time a property is added, which for a module still
// growing towards the full spec would be most commits.

static int
PushObject3DMethod( lua_State *L, lua_CFunction f )
{
	lua_pushcfunction( L, f );

	return 1;
}

static int
Object3DSetMaterial( lua_State *L )
{
	Object3D* o = (Object3D*) LuaProxy::GetProxyableObject( L, 1 );
	Rtt_WARN_SIM_PROXY_TYPE( L, 1, Object3D );

	if ( o != NULL )
	{
		// Passing nil is how a material is taken back off an object, which
		// returns it to the default surface rather than making it invisible.
		Material3D* material = LuaLibRender3D::ToMaterial( L, 2 );

		o->SetMaterial( material );
	}

	return 0;
}

static int
Object3DSetShaderEffect( lua_State *L )
{
	Object3D* o = (Object3D*) LuaProxy::GetProxyableObject( L, 1 );
	Rtt_WARN_SIM_PROXY_TYPE( L, 1, Object3D );

	if ( o != NULL )
	{
		// Passing nil returns the object to the standard shading.
		o->SetEffect( LuaLibRender3D::ToShaderEffect( L, 2 ) );
	}

	return 0;
}

static int
Object3DTranslate( lua_State *L )
{
	Object3D* o = (Object3D*) LuaProxy::GetProxyableObject( L, 1 );
	Rtt_WARN_SIM_PROXY_TYPE( L, 1, Object3D );

	if ( o != NULL )
	{
		o->Translate3D(
			(float) luaL_optnumber( L, 2, 0.0 ),
			(float) luaL_optnumber( L, 3, 0.0 ),
			(float) luaL_optnumber( L, 4, 0.0 ) );
	}

	return 0;
}

static int
Object3DRotate( lua_State *L )
{
	Object3D* o = (Object3D*) LuaProxy::GetProxyableObject( L, 1 );
	Rtt_WARN_SIM_PROXY_TYPE( L, 1, Object3D );

	if ( o != NULL )
	{
		o->Rotate3D(
			(float) luaL_optnumber( L, 2, 0.0 ),
			(float) luaL_optnumber( L, 3, 0.0 ),
			(float) luaL_optnumber( L, 4, 0.0 ) );
	}

	return 0;
}

static int
Object3DSetScale( lua_State *L )
{
	Object3D* o = (Object3D*) LuaProxy::GetProxyableObject( L, 1 );
	Rtt_WARN_SIM_PROXY_TYPE( L, 1, Object3D );

	if ( o != NULL )
	{
		// A single argument scales uniformly, which is what most calls want and
		// what saves writing the same number three times.
		const float x = (float) luaL_optnumber( L, 2, 1.0 );

		o->SetScale3D(
			x,
			(float) luaL_optnumber( L, 3, x ),
			(float) luaL_optnumber( L, 4, x ) );
	}

	return 0;
}

const LuaObject3DProxyVTable&
LuaObject3DProxyVTable::Constant()
{
	static const Self kVTable;

	return kVTable;
}

const LuaProxyVTable&
LuaObject3DProxyVTable::Parent() const
{
	return Super::Constant();
}

int
LuaObject3DProxyVTable::ValueForKey( lua_State *L, const MLuaProxyable& object, const char key[], bool overrideRestriction ) const
{
	if ( key == NULL )
	{
		return 0;
	}

	const Object3D& o = static_cast< const Object3D& >( object );
	Rtt_WARN_SIM_PROXY_TYPE( L, 1, Object3D );

	float x, y, z;

	if ( strcmp( key, "x" ) == 0 || strcmp( key, "y" ) == 0 || strcmp( key, "z" ) == 0 )
	{
		o.GetPosition3D( x, y, z );

		lua_pushnumber( L, ( key[0] == 'x' ) ? x : ( key[0] == 'y' ) ? y : z );

		return 1;
	}

	if ( strcmp( key, "rotationX" ) == 0 || strcmp( key, "rotationY" ) == 0 || strcmp( key, "rotationZ" ) == 0 )
	{
		o.GetRotation3D( x, y, z );

		const char axis = key[8];

		lua_pushnumber( L, ( axis == 'X' ) ? x : ( axis == 'Y' ) ? y : z );

		return 1;
	}

	if ( strcmp( key, "scaleX" ) == 0 || strcmp( key, "scaleY" ) == 0 || strcmp( key, "scaleZ" ) == 0 )
	{
		o.GetScale3D( x, y, z );

		const char axis = key[5];

		lua_pushnumber( L, ( axis == 'X' ) ? x : ( axis == 'Y' ) ? y : z );

		return 1;
	}

	if ( strcmp( key, "setMaterial" ) == 0 )
	{
		return PushObject3DMethod( L, Object3DSetMaterial );
	}

	if ( strcmp( key, "setShaderEffect" ) == 0 )
	{
		return PushObject3DMethod( L, Object3DSetShaderEffect );
	}

	if ( strcmp( key, "translate" ) == 0 )
	{
		return PushObject3DMethod( L, Object3DTranslate );
	}

	if ( strcmp( key, "rotate" ) == 0 )
	{
		return PushObject3DMethod( L, Object3DRotate );
	}

	if ( strcmp( key, "setScale" ) == 0 )
	{
		return PushObject3DMethod( L, Object3DSetScale );
	}

	return Super::ValueForKey( L, object, key, overrideRestriction );
}

bool
LuaObject3DProxyVTable::SetValueForKey( lua_State *L, MLuaProxyable& object, const char key[], int valueIndex ) const
{
	if ( key == NULL )
	{
		return false;
	}

	Object3D& o = static_cast< Object3D& >( object );
	Rtt_WARN_SIM_PROXY_TYPE( L, 1, Object3D );

	float x, y, z;

	if ( strcmp( key, "x" ) == 0 || strcmp( key, "y" ) == 0 || strcmp( key, "z" ) == 0 )
	{
		o.GetPosition3D( x, y, z );

		const float value = (float) lua_tonumber( L, valueIndex );

		if ( key[0] == 'x' ) { x = value; }
		else if ( key[0] == 'y' ) { y = value; }
		else { z = value; }

		o.SetPosition3D( x, y, z );

		return true;
	}

	if ( strcmp( key, "rotationX" ) == 0 || strcmp( key, "rotationY" ) == 0 || strcmp( key, "rotationZ" ) == 0 )
	{
		o.GetRotation3D( x, y, z );

		const float value = (float) lua_tonumber( L, valueIndex );
		const char axis = key[8];

		if ( axis == 'X' ) { x = value; }
		else if ( axis == 'Y' ) { y = value; }
		else { z = value; }

		o.SetRotation3D( x, y, z );

		return true;
	}

	if ( strcmp( key, "scaleX" ) == 0 || strcmp( key, "scaleY" ) == 0 || strcmp( key, "scaleZ" ) == 0 )
	{
		o.GetScale3D( x, y, z );

		const float value = (float) lua_tonumber( L, valueIndex );
		const char axis = key[5];

		if ( axis == 'X' ) { x = value; }
		else if ( axis == 'Y' ) { y = value; }
		else { z = value; }

		o.SetScale3D( x, y, z );

		return true;
	}

	return Super::SetValueForKey( L, object, key, valueIndex );
}

// ----------------------------------------------------------------------------

static int
Light3DSetColor( lua_State *L )
{
	Light3D* o = (Light3D*) LuaProxy::GetProxyableObject( L, 1 );
	Rtt_WARN_SIM_PROXY_TYPE( L, 1, Light3D );

	if ( o != NULL )
	{
		o->SetColor(
			(float) luaL_optnumber( L, 2, 1.0 ),
			(float) luaL_optnumber( L, 3, 1.0 ),
			(float) luaL_optnumber( L, 4, 1.0 ) );
	}

	return 0;
}

const LuaLight3DProxyVTable&
LuaLight3DProxyVTable::Constant()
{
	static const Self kVTable;

	return kVTable;
}

const LuaProxyVTable&
LuaLight3DProxyVTable::Parent() const
{
	return Super::Constant();
}

int
LuaLight3DProxyVTable::ValueForKey( lua_State *L, const MLuaProxyable& object, const char key[], bool overrideRestriction ) const
{
	if ( key == NULL )
	{
		return 0;
	}

	const Light3D& o = static_cast< const Light3D& >( object );
	Rtt_WARN_SIM_PROXY_TYPE( L, 1, Light3D );

	if ( strcmp( key, "intensity" ) == 0 )
	{
		lua_pushnumber( L, o.GetIntensity() );

		return 1;
	}

	if ( strcmp( key, "range" ) == 0 )
	{
		lua_pushnumber( L, o.GetRange() );

		return 1;
	}

	if ( strcmp( key, "innerAngle" ) == 0 )
	{
		lua_pushnumber( L, o.GetInnerAngle() );

		return 1;
	}

	if ( strcmp( key, "outerAngle" ) == 0 )
	{
		lua_pushnumber( L, o.GetOuterAngle() );

		return 1;
	}

	if ( strcmp( key, "castsShadows" ) == 0 )
	{
		lua_pushboolean( L, o.CastsShadows() ? 1 : 0 );

		return 1;
	}

	if ( strcmp( key, "shadowExtent" ) == 0 )
	{
		lua_pushnumber( L, o.GetShadowExtent() );

		return 1;
	}

	if ( strcmp( key, "shadowBias" ) == 0 )
	{
		lua_pushnumber( L, o.GetShadowBias() );

		return 1;
	}

	if ( strcmp( key, "shadowStrength" ) == 0 )
	{
		lua_pushnumber( L, o.GetShadowStrength() );

		return 1;
	}

	if ( strcmp( key, "setColor" ) == 0 )
	{
		return PushObject3DMethod( L, Light3DSetColor );
	}

	return Super::ValueForKey( L, object, key, overrideRestriction );
}

bool
LuaLight3DProxyVTable::SetValueForKey( lua_State *L, MLuaProxyable& object, const char key[], int valueIndex ) const
{
	if ( key == NULL )
	{
		return false;
	}

	Light3D& o = static_cast< Light3D& >( object );
	Rtt_WARN_SIM_PROXY_TYPE( L, 1, Light3D );

	if ( strcmp( key, "intensity" ) == 0 )
	{
		o.SetIntensity( (float) lua_tonumber( L, valueIndex ) );

		return true;
	}

	if ( strcmp( key, "range" ) == 0 )
	{
		o.SetRange( (float) lua_tonumber( L, valueIndex ) );

		return true;
	}

	if ( strcmp( key, "innerAngle" ) == 0 )
	{
		o.SetInnerAngle( (float) lua_tonumber( L, valueIndex ) );

		return true;
	}

	if ( strcmp( key, "outerAngle" ) == 0 )
	{
		o.SetOuterAngle( (float) lua_tonumber( L, valueIndex ) );

		return true;
	}

	// Only a directional light casts, and only one of them does -- the first that
	// asks. Setting this on a point light is allowed and simply never comes up: see
	// Scene3D::GetShadowLight.
	if ( strcmp( key, "castsShadows" ) == 0 )
	{
		o.SetCastsShadows( lua_toboolean( L, valueIndex ) != 0 );

		return true;
	}

	// How far around what the camera is looking at the shadow map reaches. A larger
	// extent shadows more of the scene out of one map, and each of its texels covers
	// more ground, so the shadows are blockier.
	if ( strcmp( key, "shadowExtent" ) == 0 )
	{
		o.SetShadowExtent( (float) lua_tonumber( L, valueIndex ) );

		return true;
	}

	if ( strcmp( key, "shadowBias" ) == 0 )
	{
		o.SetShadowBias( (float) lua_tonumber( L, valueIndex ) );

		return true;
	}

	if ( strcmp( key, "shadowStrength" ) == 0 )
	{
		o.SetShadowStrength( (float) lua_tonumber( L, valueIndex ) );

		return true;
	}

	return Super::SetValueForKey( L, object, key, valueIndex );
}

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------
