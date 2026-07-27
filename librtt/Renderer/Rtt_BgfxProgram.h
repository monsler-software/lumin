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
		bgfx::ProgramHandle GetProgram( Program::Version version ) const;

		bgfx::UniformHandle GetUniform( U32 index, Program::Version version ) const;

		U32 GetUniformTimestamp( U32 index, Program::Version version ) const;
		void SetUniformTimestamp( U32 index, Program::Version version, U32 timestamp );

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
};

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------

#endif // _Rtt_BgfxProgram_H__
