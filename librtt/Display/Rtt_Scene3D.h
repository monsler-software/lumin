//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Corona game engine.
// For overview and more information on licensing please refer to README.md
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////

#ifndef _Rtt_Scene3D_H__
#define _Rtt_Scene3D_H__

#include "Renderer/Rtt_Draw3D.h"

#include <vector>

// ----------------------------------------------------------------------------

namespace Rtt
{

class Camera3D;
class Light3D;
class Texture3D;

// ----------------------------------------------------------------------------

// The 3D state that is a property of the runtime rather than of any one object:
// which camera is active, which lights exist, and the ambient term.
//
// One per Display, owned by the `render` library, rather than a singleton. The
// simulator runs a runtime per open project and tears one down to build the
// next, so state shared process-wide would leak a closed project's camera into
// the one that replaced it.
class Scene3D
{
	public:
		typedef Scene3D Self;

	public:
		Scene3D();
		~Scene3D();

	public:
		// Retains the camera and releases any previous one, so that the active
		// camera survives its Lua handle being collected.
		void SetActiveCamera( Camera3D* camera );
		Camera3D* GetActiveCamera() const { return fActiveCamera; }

	public:
		// Lights add and remove themselves as they are constructed and
		// destroyed; this is not an ownership relation, it is an index so that
		// drawing an object does not have to walk the whole display list
		// looking for what might be lighting it.
		void AddLight( Light3D* light );
		void RemoveLight( Light3D* light );

		// Copies the currently contributing lights into out, up to
		// kMaxDraw3DLights, and returns how many were written. Hidden lights
		// are skipped, so they cost nothing beyond being passed over.
		// Fills out with up to kMaxDraw3DLights lights and returns how many.
		//
		// When target is given, *targetIndex comes back as its position in what was
		// gathered, or kMaxDraw3DLights if it was not gathered at all. The shader
		// needs that index to shadow one light and not the others, and the position
		// is not the light's position in fLights: a light that contributes nothing
		// is skipped, so the two only agree until the first hidden one.
		U32 GatherLights( Draw3DLight* out, const Light3D* target = NULL, U32* targetIndex = NULL ) const;

		void SetAmbient( float r, float g, float b );
		void GetAmbient( float& r, float& g, float& b ) const;

	public:
		// The environment the scene is lit by and reflects, as an equirectangular
		// image -- the projection every HDRI and sky photograph comes in.
		//
		// Setting one retains it and releases any previous, and projects its
		// diffuse contribution onto spherical harmonics straight away; see
		// GetIrradiance. Passing NULL removes it and returns the scene to the flat
		// ambient term above.
		void SetEnvironmentMap( Texture3D* texture );
		Texture3D* GetEnvironmentMap() const { return fEnvironmentMap; }

		// Nine coefficients of three floats, padded to vec4 for the shader: the
		// second-order spherical harmonic projection of the environment's diffuse
		// irradiance.
		//
		// Computed once on the CPU when the map is set, because the alternative --
		// sampling the environment over the hemisphere per pixel -- is hundreds of
		// texture fetches for a result that nine numbers reproduce to within a few
		// percent. This is the standard way diffuse image-based lighting is done.
		const float* GetIrradiance() const { return fIrradiance; }

		enum { kIrradianceCoefficients = 9 };

		// Scales the environment's contribution, both diffuse and specular, so a
		// map can be dimmed without being reloaded.
		float GetEnvironmentIntensity() const { return fEnvironmentIntensity; }
		void SetEnvironmentIntensity( float value );

	public:
		// The light casting shadows this frame, or NULL if none asked to.
		//
		// The first directional light with castsShadows set: a shadow map per light
		// means a depth pass per light, so the scene commits to one rather than
		// letting a project quietly pay for four.
		Light3D* GetShadowLight() const;

	private:
		// Projects the environment image onto fIrradiance. Called by
		// SetEnvironmentMap while the decoded pixels are still there -- the
		// renderer frees them once it has uploaded the texture.
		void ProjectIrradiance();

		Camera3D* fActiveCamera;
		std::vector< Light3D* > fLights;
		float fAmbient[3];

		Texture3D* fEnvironmentMap;
		float fIrradiance[kIrradianceCoefficients * 4];
		float fEnvironmentIntensity;
};

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------

#endif // _Rtt_Scene3D_H__
