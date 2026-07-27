//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Corona game engine.
// For overview and more information on licensing please refer to README.md
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////

#ifndef _Rtt_BgfxProgram_H__
#define _Rtt_BgfxProgram_H__

#include "Renderer/Rtt_GPUResource.h"
#include "Renderer/Rtt_Program.h"
#include "Renderer/Rtt_Uniform.h"

#include <bgfx/bgfx.h>

// ----------------------------------------------------------------------------

namespace Rtt
{

class FormatExtensionList;

// ----------------------------------------------------------------------------

// A Corona program compiled for bgfx.
//
// Unlike GL, bgfx takes no shader source at run time, only its own compiled
// blobs. Solar2D lets content create shaders while it runs, through
// graphics.defineEffect, so the compiler has to be present in the app: this
// class drives bgfx's shaderc in-process, with no temp files and no
// subprocess, which is also the only form that works on iOS and Android.
class BgfxProgram : public GPUResource
{
	public:
		typedef GPUResource Super;
		typedef BgfxProgram Self;

	public:
		BgfxProgram();

		virtual void Create( CPUResource* resource );
		virtual void Update( CPUResource* resource );
		virtual void Destroy();

		// Corona compiles one variant per active mask count, so that a kernel
		// works under masking without the author writing anything for it.
		// Variants past the unmasked one are compiled the first time a masked
		// draw asks for them, which matters more here than under GL: each one
		// is a full run of the shader compiler.
		bgfx::ProgramHandle GetProgram( Program::Version version );

		bgfx::UniformHandle GetUniform( U32 index, Program::Version version ) const;

		U32 GetUniformTimestamp( U32 index, Program::Version version ) const;
		void SetUniformTimestamp( U32 index, Program::Version version, U32 timestamp );

		// How Corona's per-instance data is laid out for bgfx, which carries it
		// in a buffer of vec4s named i_data0 and up rather than in attributes of
		// the shader's own choosing. Both the shader (which needs names) and the
		// command buffer (which fills the buffer) derive their view from these,
		// so the two cannot drift apart.
		//
		// Each instanced group starts on a vec4 boundary, and an attribute sits
		// at its own offset inside its group.
		enum { kInstanceVectorSize = 16, kMaxInstanceVectors = 5 };

		static U32 GetInstanceGroupOffset( const FormatExtensionList* list, U32 groupIndex );
		static U32 GetInstanceStride( const FormatExtensionList* list );

	private:
		struct VersionData
		{
			VersionData();

			bgfx::ProgramHandle fProgram;
			bgfx::UniformHandle fUniforms[Uniform::kNumBuiltInVariables];
			U32 fTimestamps[Uniform::kNumBuiltInVariables];
		};

		// Compiles one variant. Returns false and logs the compiler's own
		// diagnostics on failure, leaving the variant unusable rather than
		// bringing the app down: a bad defineEffect is content's mistake and
		// should look like one.
		bool Build( Program& program, Program::Version version, VersionData& data );

		VersionData fData[Program::kNumVersions];

		// The source a variant is compiled from, kept so that variants can be
		// built on demand rather than all at once. Owned by the display's
		// resource pool, which outlives this.
		Program* fResource;

		// Set for a variant whose compile failed, so a draw that keeps asking
		// for it does not keep re-running the compiler.
		bool fBuildFailed[Program::kNumVersions];
};

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------

#endif // _Rtt_BgfxProgram_H__
