//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Corona game engine.
// For overview and more information on licensing please refer to README.md
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////

#ifndef _Rtt_BgfxShaderCompiler_H__
#define _Rtt_BgfxShaderCompiler_H__

#include "Core/Rtt_Types.h"

#include <string>
#include <vector>

// ----------------------------------------------------------------------------

namespace Rtt
{

// ----------------------------------------------------------------------------

// Compiles shader source into a bgfx shader blob, at run time, in memory.
//
// This exists as its own module because shaderc cannot simply be linked beside
// bgfx: both define bgfx::g_allocator, bgfx::trace, bgfx::s_uniformTypeName and
// more, since shaderc ships as a standalone tool that never links against the
// renderer. The implementation is therefore compiled with the whole bgfx
// namespace renamed out of the way (see platform/linux/bgfx.cmake), which in
// turn means nothing here may expose a bgfx type: the interface deals in bytes,
// and the caller hands those to bgfx itself.
class BgfxShaderCompiler
{
	public:
		enum Stage
		{
			kVertex,
			kFragment,
		};

		// profile is a shaderc profile name -- "120", "300_es", "spirv",
		// "metal", "s_5_0". Callers pick it from the backend bgfx chose at run
		// time rather than at build time.
		//
		// On success fills outBlob and returns true. On failure returns false
		// and puts the compiler's diagnostics in outMessages, which is what
		// content authors need to see when a defineEffect kernel does not
		// compile.
		static bool Compile(
			const char* source,
			const char* varyingDef,
			Stage stage,
			const char* profile,
			const char* shaderIncludeDir,
			const char* debugName,
			std::vector< U8 >& outBlob,
			std::string& outMessages );
};

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------

#endif // _Rtt_BgfxShaderCompiler_H__
