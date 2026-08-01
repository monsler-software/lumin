//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Corona game engine.
// For overview and more information on licensing please refer to README.md
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////

#ifndef _Rtt_ShaderEffect3D_H__
#define _Rtt_ShaderEffect3D_H__

#include "Core/Rtt_Types.h"

#include <string>
#include <vector>

// ----------------------------------------------------------------------------

namespace Rtt
{

class Texture3D;

// ----------------------------------------------------------------------------

// A custom shading program for 3D objects, and the render state it draws with.
//
// What an effect replaces is the fragment stage: the caller writes how a surface
// is coloured, and keeps everything the pipeline already does around it -- the
// vertex transform, skinning, the varyings, and the standard uniforms, all of
// which are declared for the snippet before it is compiled.
//
// The vertex stage is deliberately not the caller's. Skinning lives there, and
// there are two variants of it; letting content replace it would mean either
// giving up skinned effects or asking every effect author to reimplement the bone
// palette. Effects that need to move vertices are better served by newCustomMesh.
//
// Reference counted and shared, as materials are: one effect is normally set on
// several objects.
class ShaderEffect3D
{
	public:
		typedef ShaderEffect3D Self;

		// How back faces are treated, which is the whole of what an inverted-hull
		// outline needs from an effect: a black shell drawn with the front faces
		// culled instead of the back ones is exactly an outline.
		enum CullMode
		{
			kCullBack,
			kCullFront,
			kCullNone
		};

		// A named value the snippet can read. Always a vec4 -- one uniform type
		// covers scalars, colours and vectors, and saves the caller declaring
		// which they meant. A scalar is set into x and read as `.x`.
		struct Uniform
		{
			std::string fName;
			float fValue[4];
		};

		// More than this and the effect is better served by a texture. The cap
		// exists because each one is a separate bgfx uniform handle.
		enum { kMaxUniforms = 8 };

	public:
		ShaderEffect3D();
		~ShaderEffect3D();

		void Retain() { ++fRefCount; }
		void Release() { if ( --fRefCount == 0 ) { delete this; } }

	public:
		const std::string& GetName() const { return fName; }
		void SetName( const char* name ) { fName = ( name != NULL ) ? name : ""; }

		// The body of the fragment stage: GLSL that assigns to gl_FragColor.
		//
		// Compiled once, on the first draw that uses the effect, and only for the
		// vertex variant that draw needs. A failure is reported with the
		// compiler's own diagnostics and the effect then draws nothing, rather
		// than falling back to the standard shading and looking like it worked.
		const std::string& GetFragmentSource() const { return fFragmentSource; }
		void SetFragmentSource( const char* source ) { fFragmentSource = ( source != NULL ) ? source : ""; }

		CullMode GetCullMode() const { return fCullMode; }
		void SetCullMode( CullMode value ) { fCullMode = value; }

		bool IsTranslucent() const { return fIsTranslucent; }
		void SetTranslucent( bool value ) { fIsTranslucent = value; }

		// Whether the effect contributes to the depth buffer. An outline shell
		// wants this off so it never occludes the model it outlines.
		bool GetWritesDepth() const { return fWritesDepth; }
		void SetWritesDepth( bool value ) { fWritesDepth = value; }

	public:
		const std::vector< Uniform >& GetUniforms() const { return fUniforms; }

		// Declares a uniform, or replaces the value of one already declared.
		// Returns false once kMaxUniforms are in use.
		bool SetUniform( const char* name, const float* value );

		// -1 when the effect has no uniform by that name, which is how the Lua
		// binding tells a typo from an assignment.
		int FindUniform( const char* name ) const;

	public:
		// The handle the renderer stashed for this effect's programs, or
		// kInvalidGpuId before it has been compiled. Opaque here, as Mesh3D's and
		// Texture3D's are.
		enum { kInvalidGpuId = ~0u };

		U32 GetGpuId() const { return fGpuId; }
		void SetGpuId( U32 id ) { fGpuId = id; }

	private:
		std::string fName;
		std::string fFragmentSource;
		std::vector< Uniform > fUniforms;
		CullMode fCullMode;
		bool fIsTranslucent;
		bool fWritesDepth;
		U32 fGpuId;
		int fRefCount;
};

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------

#endif // _Rtt_ShaderEffect3D_H__
