//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Corona game engine.
// For overview and more information on licensing please refer to README.md
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////

#include "Renderer/Rtt_BgfxProgram.h"

#include "Core/Rtt_Assert.h"

#include "Renderer/Rtt_BgfxShaderCompiler.h"

#include <string.h>
#include <string>
#include <vector>

// Where bgfx_shader.sh and the rest of bgfx's shader headers live, so the
// compiler can resolve a kernel's #include directives. Supplied by the build.
#ifndef LUMIN_BGFX_SHADER_INCLUDE_DIR
	#define LUMIN_BGFX_SHADER_INCLUDE_DIR ""
#endif

// ----------------------------------------------------------------------------

namespace Rtt
{

// ----------------------------------------------------------------------------

// The interface between Corona's vertex data and its kernels, in the form
// bgfx's compiler wants. It mirrors BgfxGeometry::VertexLayout on the input
// side and shell_default_bgfx on the varying side.
//
// The names carry an "In" suffix because bgfx passes varyings as parameters of
// main() on its HLSL, SPIR-V and Metal paths, which puts them out of scope
// inside a kernel. The shell copies them into globals under the names Corona
// kernels actually use (v_TexCoord and friends).
static const char kVaryingDef[] =
	"vec2 v_TexCoordIn    : TEXCOORD1;\n"
	"vec4 v_ColorScaleIn  : COLOR0;\n"
	"vec4 v_UserDataIn    : TEXCOORD2;\n"
	"vec2 v_PositionIn    : TEXCOORD3;\n"
	"\n"
	"vec3 a_position  : POSITION;\n"
	"vec3 a_texcoord0 : TEXCOORD0;\n"
	"vec4 a_color0    : COLOR0;\n"
	"vec4 a_texcoord1 : TEXCOORD1;\n"
	;

// The profile to compile for, which follows the backend bgfx actually picked
// rather than anything chosen at build time: the same binary can come up on
// OpenGL on one machine and Vulkan on the next.
static const char*
ProfileForRenderer( char shaderType )
{
	switch ( bgfx::getRendererType() )
	{
		case bgfx::RendererType::OpenGL:
			return "120";

		case bgfx::RendererType::OpenGLES:
			return "300_es";

		case bgfx::RendererType::Vulkan:
			return "spirv";

		case bgfx::RendererType::Metal:
			return "metal";

		case bgfx::RendererType::Direct3D11:
		case bgfx::RendererType::Direct3D12:
			return 'v' == shaderType ? "s_5_0" : "p_5_0";

		default:
			Rtt_TRACE( ( "ERROR: no shader profile for bgfx renderer '%s'\n",
				bgfx::getRendererName( bgfx::getRendererType() ) ) );
			return "120";
	}
}

// Compiles one shader stage. The compiler itself lives in
// Rtt_BgfxShaderCompiler, which is built in a namespace of its own; see the
// note at the top of that file for why it cannot be called directly from here.
static const bgfx::Memory*
CompileStage( const std::string& source, BgfxShaderCompiler::Stage stage, const char* debugName )
{
	std::vector< U8 > blob;
	std::string messages;

	const bool compiled = BgfxShaderCompiler::Compile(
		  source.c_str()
		, kVaryingDef
		, stage
		, ProfileForRenderer( BgfxShaderCompiler::kVertex == stage ? 'v' : 'f' )
		, LUMIN_BGFX_SHADER_INCLUDE_DIR
		, debugName
		, blob
		, messages
		);

	if ( !messages.empty() )
	{
		// Content wrote this shader, so its diagnostics belong in the log.
		Rtt_LogException( "%s: %s\n", debugName, messages.c_str() );
	}

	if ( !compiled )
	{
		return NULL;
	}

	return bgfx::copy( &blob[0], U32( blob.size() ) );
}

// ----------------------------------------------------------------------------

BgfxProgram::VersionData::VersionData()
:	fProgram( BGFX_INVALID_HANDLE )
{
	for ( U32 i = 0; i < Uniform::kNumBuiltInVariables; ++i )
	{
		fUniforms[i] = BGFX_INVALID_HANDLE;
		fTimestamps[i] = 0;
	}
}

BgfxProgram::BgfxProgram()
{
}

bool
BgfxProgram::Build( Program& program, Program::Version version, VersionData& data )
{
	const char* header = program.GetHeaderSource();
	const char* vertexSource = program.GetVertexShaderSource();
	const char* fragmentSource = program.GetFragmentShaderSource();

	if ( !vertexSource || !fragmentSource )
	{
		return false;
	}

	// Same variant scheme as the GL backend: the mask count is a define, so one
	// kernel serves every masking case without the author knowing about it.
	std::string maskDefine( "#define MASK_COUNT 0\n" );

	switch ( version )
	{
		case Program::kMaskCount1:	maskDefine = "#define MASK_COUNT 1\n"; break;
		case Program::kMaskCount2:	maskDefine = "#define MASK_COUNT 2\n"; break;
		case Program::kMaskCount3:	maskDefine = "#define MASK_COUNT 3\n"; break;
		default: break;
	}

	const std::string prefix = maskDefine + ( header ? header : "" );

	const bgfx::Memory* vertexBlob = CompileStage( prefix + vertexSource, BgfxShaderCompiler::kVertex, "corona.vs" );

	if ( !vertexBlob )
	{
		return false;
	}

	const bgfx::Memory* fragmentBlob = CompileStage( prefix + fragmentSource, BgfxShaderCompiler::kFragment, "corona.fs" );

	if ( !fragmentBlob )
	{
		// bgfx frees the memory it is handed when the shader is created; since
		// no shader will be created from this one, release it by making a
		// throwaway shader rather than leaking the block.
		bgfx::destroy( bgfx::createShader( vertexBlob ) );
		return false;
	}

	bgfx::ShaderHandle vertexShader = bgfx::createShader( vertexBlob );
	bgfx::ShaderHandle fragmentShader = bgfx::createShader( fragmentBlob );

	if ( !bgfx::isValid( vertexShader ) || !bgfx::isValid( fragmentShader ) )
	{
		return false;
	}

	// true: bgfx destroys the shaders with the program.
	data.fProgram = bgfx::createProgram( vertexShader, fragmentShader, true );

	return bgfx::isValid( data.fProgram );
}

void
BgfxProgram::Create( CPUResource* resource )
{
	Rtt_ASSERT( CPUResource::kProgram == resource->GetType() );
	Program* program = static_cast< Program* >( resource );

	// Only the unmasked variant is built up front. The others are compiled the
	// first time masking actually asks for them, which matters more here than
	// under GL: every variant is a full run of the compiler.
	Build( *program, Program::kMaskCount0, fData[Program::kMaskCount0] );
}

void
BgfxProgram::Update( CPUResource* resource )
{
	Rtt_ASSERT( CPUResource::kProgram == resource->GetType() );
	Program* program = static_cast< Program* >( resource );

	// The source changed, so every variant already built is stale.
	for ( U32 i = 0; i < Program::kNumVersions; ++i )
	{
		if ( bgfx::isValid( fData[i].fProgram ) )
		{
			bgfx::destroy( fData[i].fProgram );
			fData[i].fProgram = BGFX_INVALID_HANDLE;

			Build( *program, Program::Version( i ), fData[i] );
		}
	}
}

void
BgfxProgram::Destroy()
{
	for ( U32 i = 0; i < Program::kNumVersions; ++i )
	{
		if ( bgfx::isValid( fData[i].fProgram ) )
		{
			bgfx::destroy( fData[i].fProgram );
			fData[i].fProgram = BGFX_INVALID_HANDLE;
		}

		for ( U32 j = 0; j < Uniform::kNumBuiltInVariables; ++j )
		{
			if ( bgfx::isValid( fData[i].fUniforms[j] ) )
			{
				bgfx::destroy( fData[i].fUniforms[j] );
				fData[i].fUniforms[j] = BGFX_INVALID_HANDLE;
			}
		}
	}
}

bgfx::ProgramHandle
BgfxProgram::GetProgram( Program::Version version ) const
{
	Rtt_ASSERT( version < Program::kNumVersions );
	return fData[version].fProgram;
}

bgfx::UniformHandle
BgfxProgram::GetUniform( U32 index, Program::Version version ) const
{
	Rtt_ASSERT( version < Program::kNumVersions );
	Rtt_ASSERT( index < Uniform::kNumBuiltInVariables );
	return fData[version].fUniforms[index];
}

U32
BgfxProgram::GetUniformTimestamp( U32 index, Program::Version version ) const
{
	Rtt_ASSERT( version < Program::kNumVersions );
	Rtt_ASSERT( index < Uniform::kNumBuiltInVariables );
	return fData[version].fTimestamps[index];
}

void
BgfxProgram::SetUniformTimestamp( U32 index, Program::Version version, U32 timestamp )
{
	Rtt_ASSERT( version < Program::kNumVersions );
	Rtt_ASSERT( index < Uniform::kNumBuiltInVariables );
	fData[version].fTimestamps[index] = timestamp;
}

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------
