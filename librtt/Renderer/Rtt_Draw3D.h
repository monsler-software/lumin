//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Corona game engine.
// For overview and more information on licensing please refer to README.md
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////

#ifndef _Rtt_Draw3D_H__
#define _Rtt_Draw3D_H__

#include "Core/Rtt_Types.h"

// ----------------------------------------------------------------------------

namespace Rtt
{

class Camera3D;
class Light3D;
class Mesh3D;
class ShaderEffect3D;
class Texture3D;

// ----------------------------------------------------------------------------

// Everything one 3D object contributes to a draw call, resolved at the moment
// the display list reaches it.
//
// Mesh and camera are borrowed pointers, which is safe here in a way it would
// not be under the GL or Vulkan command buffers: those record a frame and
// replay it during the next one, so anything they held would have to outlive
// the frame that built it. The bgfx buffer translates each call into bgfx calls
// as it arrives (see the note atop Rtt_BgfxCommandBuffer.h), so the pointers
// are read before Draw3D returns and the caller still owns them throughout.
struct Draw3DLight
{
	enum Kind
	{
		kDirectional,
		kPoint,
		kSpot
	};

	U32 fKind;

	// For a directional light this is the direction it shines in and the
	// position is unused; for the others it is the position and, for a spot,
	// the direction is used as well.
	float fPosition[3];
	float fDirection[3];
	float fColor[3];
	float fIntensity;

	// Beyond this distance a point or spot light contributes nothing, which is
	// what keeps a scene's light count from being a per-pixel cost.
	float fRange;

	// Cosines of the half-angles bounding a spot light's falloff, precomputed
	// because the shader would otherwise take two transcendentals per pixel per
	// light to recover them.
	float fInnerCosine;
	float fOuterCosine;
};

enum { kMaxDraw3DLights = 8 };

// The bone palette one skinned draw can carry.
//
// Bounded by bgfx, not by the GPU: bgfx records a uniform's register count in a
// single byte, so an array longer than 255 vec4 silently wraps modulo 256 and
// the shader is handed a fraction of what was asked for. Declaring 480 registers
// yields 224, which is the kind of failure that shows up as a model with spikes
// shooting out of it rather than as an error.
//
// 84 bones is 252 registers, just inside that ceiling. A bone is stored as three
// rows rather than as a 4x4 to get even that far: the bottom row of a joint
// transform is always (0,0,0,1), and keeping it would cost a quarter of the
// budget to send a constant.
//
// This is a limit per *draw*, not per skeleton, and the loader keeps every draw
// inside it by giving each mesh part a palette holding only the joints that part
// actually references -- and by splitting a part that needs more (see
// ModelPart::fJointMap). A 134-joint rig draws correctly because no one part of
// it uses more than a few dozen joints.
enum { kMaxDraw3DBones = 84 };

// Floats per bone in that packing: three rows of four.
enum { kDraw3DBoneStride = 12 };

struct Draw3DCommand
{
	const Mesh3D* fMesh;

	// Object-to-world, column major.
	float fTransform[16];

	// The eye to render from. The view and projection matrices are built by the
	// backend rather than passed in, because the projection depends on the
	// surface size and on whether the API bgfx chose uses a 0..1 or -1..1 depth
	// range -- neither of which the display list knows.
	const Camera3D* fCamera;

	float fAlbedo[4];
	float fEmissive[3];
	float fRoughness;
	float fMetallic;

	// The material's maps, each NULL when it has none, in which case the factor
	// beside it acts alone. Borrowed, like fMesh and fCamera and for the same
	// reason: the bgfx buffer reads them before Draw3D returns.
	const Texture3D* fAlbedoMap;
	const Texture3D* fMetallicRoughnessMap;
	const Texture3D* fEmissiveMap;

	// Render state the material decides rather than the pipeline: whether back
	// faces are culled, and whether the surface blends. See Material3D for why
	// each has to be per material and not a fixed choice.
	bool fIsDoubleSided;
	bool fIsTranslucent;

	// A custom shading program to draw with instead of the standard one, or NULL
	// for the standard one. Borrowed, like the maps and the mesh.
	//
	// An effect also carries its own culling, blending and depth-write choices,
	// which override the material's: an effect exists precisely to say how
	// something is drawn, so it has the last word.
	const ShaderEffect3D* fEffect;

	// The display list's accumulated alpha for this object, kept apart from the
	// material's own so that fading an object does not edit the material every
	// other object using it also sees.
	float fAlpha;

	Draw3DLight fLights[kMaxDraw3DLights];
	U32 fLightCount;

	// Ambient light, applied uniformly, so that the side of an object facing
	// away from every light is dark rather than pure black.
	float fAmbient[3];

	// The scene's environment: the map itself, for reflections, and the spherical
	// harmonic irradiance projected from it, for the diffuse term. NULL and NULL
	// when no environment has been set, in which case fAmbient acts alone.
	//
	// Borrowed, like everything else here.
	const Texture3D* fEnvironmentMap;

	// Nine vec4s, as Scene3D::GetIrradiance lays them out.
	const float* fIrradiance;

	float fEnvironmentIntensity;

	// The shadow pass. fCastsShadows says whether there is one this frame; the
	// rest is only meaningful when it is true.
	//
	// The light's view-projection is computed by the display list rather than the
	// backend, because it depends on the light's rotation and on what the camera is
	// looking at -- neither of which the renderer knows. The depth range split it
	// needs, though, is the backend's, so the matrix is filled in by the command
	// buffer just before it is used.
	bool fCastsShadows;

	// Which light, so the backend can build the matrix with the depth convention it
	// alone knows. Borrowed, like the camera.
	const Light3D* fShadowLight;

	// Where that light sits in fLights, so the shader darkens the light that casts
	// and leaves the others alone. kMaxDraw3DLights when it is not in the array at
	// all, which reads as "no light matches" in the loop and so shadows nothing.
	U32 fShadowLightIndex;

	// What the shadow map is centred on: the point the camera is aimed at.
	float fShadowCentre[3];

	float fShadowBias;
	float fShadowStrength;

	// The skinning palette: fBoneCount bones of kDraw3DBoneStride floats each,
	// every bone being the three rows of an affine transform taking a vertex
	// from the mesh's bind pose to where the pose puts it, in object space.
	//
	// NULL, with a count of zero, for everything that is not a posed skinned
	// mesh -- which is every primitive and every static model.
	//
	// Borrowed rather than copied, for the reason given at the top of this file:
	// the bgfx buffer reads it before Draw3D returns. Copying it would put
	// several kilobytes on the stack of every draw call, skinned or not.
	const float* fBoneTransforms;
	U32 fBoneCount;
};

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------

#endif // _Rtt_Draw3D_H__
