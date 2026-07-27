//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Corona game engine.
// For overview and more information on licensing please refer to README.md
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////

#ifndef _Rtt_RendererCapabilities_H__
#define _Rtt_RendererCapabilities_H__

#include "Core/Rtt_Types.h"

#include <stddef.h>

// ----------------------------------------------------------------------------

namespace Rtt
{

// ----------------------------------------------------------------------------

// Device limits and driver strings, as reported by whichever rendering backend
// is currently live.
//
// Historically these were static members of CommandBuffer, implemented once in
// Rtt_GLCommandBuffer.cpp. That made the answers a link-time decision: a build
// containing a non-GL backend still resolved them to the GL implementations,
// which then issued GL calls with no GL context bound. Backends now install an
// implementation of this interface at Initialize() time, so the answers follow
// the backend that is actually rendering.
//
// The strings below are the backend-neutral keys. GetString() is expected to
// return "" (never NULL) for a key the backend cannot answer.
class RendererCapabilities
{
	public:
		// Keys accepted by GetString().
		static const char kVendor[];			// "vendor"
		static const char kRenderer[];			// "renderer"
		static const char kVersion[];			// "version"
		static const char kShaderVersion[];		// "shaderVersion"
		static const char kExtensions[];		// "extensions"

		// Translates a legacy GL_* key ("GL_VENDOR", ...) to one of the keys
		// above. Returns NULL if the string is not a recognized GL_* key.
		static const char *KeyFromGlString( const char *s );

		// vec4 slots Corona reserves for its own vertex uniforms, and which
		// GetMaxUniformVectorsCount() must therefore leave out of its answer:
		//   4  kViewProjectionMatrix
		//   12 kMaskMatrix*, assuming 3 vectors each
		//   2  kTotalTime, kDeltaTime
		//   1  kTexelSize
		//   1  kContentScale
		static const size_t kReservedUniformVectors = 20;

	public:
		virtual ~RendererCapabilities();

		// Number of vec4 uniform slots a vertex shader may use, after the
		// slots Corona reserves for its own uniforms have been deducted.
		virtual size_t GetMaxUniformVectorsCount() const = 0;

		// Texture units addressable from a vertex shader.
		virtual size_t GetMaxVertexTextureUnits() const = 0;

		// Largest supported 2D texture dimension, in texels.
		virtual size_t GetMaxTextureSize() const = 0;

		// Driver / device description strings; see the k* keys above.
		virtual const char *GetString( const char *key ) const = 0;

		// Whether "highp" is usable in fragment shaders.
		virtual bool GetSupportsHighPrecisionFragmentShaders() const = 0;

	public:
		// The capabilities of the live backend. Never NULL: until a backend
		// installs its own, this is a conservative fallback (see
		// Rtt_CommandBuffer.cpp) that reports minimum guaranteed limits and
		// empty strings, which keeps content loading rather than crashing on a
		// backend that has not been taught to answer yet.
		static const RendererCapabilities& GetCurrent();

		// Installs the capabilities of a backend that has just initialized.
		// Passing NULL restores the fallback. The caller retains ownership and
		// must call Set( NULL ) before destroying the instance.
		static void Set( const RendererCapabilities *capabilities );

	private:
		static const RendererCapabilities *sCurrent;
};

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------

#endif // _Rtt_RendererCapabilities_H__
