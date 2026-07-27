//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Corona game engine.
// For overview and more information on licensing please refer to README.md
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////

// This translation unit is built with -Dbgfx=bgfx_shaderc, which moves every
// symbol shaderc declares into a namespace of its own. Without it, linking
// shaderc beside bgfx fails on duplicate definitions of bgfx::g_allocator,
// bgfx::trace, bgfx::s_uniformTypeName and others -- shaderc is a standalone
// tool upstream and never shares a binary with the renderer.
//
// Consequences, both enforced by keeping the interface free of bgfx types:
//
//   * nothing here may hand a bgfx object back to the rest of the engine, as
//     "bgfx::Foo" here and "bgfx::Foo" elsewhere are different types;
//   * this file must not include <bgfx/bgfx.h> for the renderer's sake.

#include "Renderer/Rtt_BgfxShaderCompiler.h"

#include <bx/allocator.h>
#include <bx/readerwriter.h>

#include <string.h>

#include "shaderc.h"

namespace bgfx
{
	// Defined in shaderc.cpp but, unlike its per-target siblings, not declared
	// in shaderc.h. This is the only entry point that compiles from memory.
	bool compileShader(
		  const char* _varying
		, const char* _comment
		, char* _shader
		, uint32_t _shaderLen
		, const Options& _options
		, bx::WriterI* _shaderWriter
		, bx::WriterI* _messageWriter
		);
}

// ----------------------------------------------------------------------------

namespace Rtt
{

// ----------------------------------------------------------------------------

bool
BgfxShaderCompiler::Compile(
	const char* source,
	const char* varyingDef,
	Stage stage,
	const char* profile,
	const char* shaderIncludeDir,
	const char* debugName,
	std::vector< U8 >& outBlob,
	std::string& outMessages )
{
	outBlob.clear();
	outMessages.clear();

	if ( !source || !profile )
	{
		return false;
	}

	bgfx::Options options;
	options.shaderType = ( kVertex == stage ) ? 'v' : 'f';
	options.profile = profile;
	options.inputFilePath = debugName ? debugName : "<memory>";
	options.outputFilePath = options.inputFilePath;

	if ( shaderIncludeDir && '\0' != shaderIncludeDir[0] )
	{
		// Where bgfx_shader.sh lives, so a kernel's #include resolves.
		options.includeDirs.push_back( shaderIncludeDir );
	}

	static bx::DefaultAllocator sAllocator;

	bx::MemoryBlock shaderBlock( &sAllocator );
	bx::MemoryBlock messageBlock( &sAllocator );
	bx::MemoryWriter shaderWriter( &shaderBlock );
	bx::MemoryWriter messageWriter( &messageBlock );

	// Two contracts of compileShader, neither documented, both of which corrupt
	// the heap when missed:
	//
	//   * it takes ownership of this buffer and ends with "delete [] data"
	//     (shaderc.cpp:1662), so it must come from new char[] and must not be
	//     freed here;
	//   * it preprocesses in place and reads past the source, so it needs the
	//     same 16 KB of slack the tool itself allocates, and a trailing
	//     newline.
	const uint32_t sourceLen = uint32_t( strlen( source ) );
	const uint32_t kSlack = 16384;

	char* buffer = new char[ sourceLen + kSlack ];
	memcpy( buffer, source, sourceLen );
	buffer[sourceLen] = '\n';
	memset( buffer + sourceLen + 1, 0, kSlack - 1 );

	const bool compiled = bgfx::compileShader(
		  varyingDef ? varyingDef : ""
		, ""
		, buffer // ownership passes here
		, sourceLen
		, options
		, &shaderWriter
		, &messageWriter
		);

	const uint32_t messageSize = messageBlock.getSize();

	if ( messageSize > 0 )
	{
		outMessages.assign( (const char*)messageBlock.more( 0 ), messageSize );
	}

	if ( !compiled )
	{
		return false;
	}

	const uint32_t blobSize = shaderBlock.getSize();

	if ( 0 == blobSize )
	{
		return false;
	}

	outBlob.resize( blobSize );
	memcpy( &outBlob[0], shaderBlock.more( 0 ), blobSize );

	return true;
}

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------
