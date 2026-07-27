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
#include "Renderer/Rtt_FormatExtensionList.h"
#include "Display/Rtt_ShaderResource.h"

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
	// Declared for every variant, not just the masked ones: shaderc reads the
	// $input/$output lists before the preprocessor runs, so they cannot be put
	// behind MASK_COUNT. A variant that does not mask simply leaves them
	// unwritten.
	"vec2 v_MaskUV0In     : TEXCOORD4;\n"
	"vec2 v_MaskUV1In     : TEXCOORD5;\n"
	"vec2 v_MaskUV2In     : TEXCOORD6;\n"
	"\n"
	// Per-instance data. Only the programs that instance list these in their
	// $input, so declaring them all here costs nothing elsewhere.
	"vec4 i_data0 : TEXCOORD7;\n"
	"vec4 i_data1 : TEXCOORD8;\n"
	"vec4 i_data2 : TEXCOORD9;\n"
	"vec4 i_data3 : TEXCOORD10;\n"
	"vec4 i_data4 : TEXCOORD11;\n"
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

U32
BgfxProgram::GetInstanceGroupOffset( const FormatExtensionList* list, U32 groupIndex )
{
	U32 offset = 0;

	if ( !list )
	{
		return 0;
	}

	for ( U32 i = 0; i < list->GetGroupCount() && i < groupIndex; ++i )
	{
		const FormatExtensionList::Group& group = list->GetGroups()[i];

		if ( group.IsInstanceRate() )
		{
			// Rounded up, since each group starts at an i_data vector.
			offset += ( group.size + kInstanceVectorSize - 1 ) / kInstanceVectorSize * kInstanceVectorSize;
		}
	}

	return offset;
}

U32
BgfxProgram::GetInstanceStride( const FormatExtensionList* list )
{
	const U32 size = list ? GetInstanceGroupOffset( list, list->GetGroupCount() ) : 0;

	// An instanced draw always carries at least one vector: with nothing else
	// to send, it holds the instance index, which is how CoronaInstanceID is
	// reached on backends with no equivalent of gl_InstanceID.
	return size > 0 ? size : kInstanceVectorSize;
}

// The i_data vector and swizzle an extension attribute lands on, given where
// its group sits in the per-instance block.
static bool
InstanceAttributeReference( const FormatExtensionList* list, U32 attributeIndex, std::string& reference )
{
	const FormatExtensionList::Attribute& attribute = list->GetAttributes()[attributeIndex];
	const U32 groupIndex = list->FindGroup( attributeIndex );
	const U32 offset = BgfxProgram::GetInstanceGroupOffset( list, groupIndex ) + attribute.offset;

	const U32 vector = offset / BgfxProgram::kInstanceVectorSize;
	const U32 component = ( offset % BgfxProgram::kInstanceVectorSize ) / 4;
	const U32 components = attribute.components > 0 ? attribute.components : 4;

	if ( vector >= BgfxProgram::kMaxInstanceVectors || component + components > 4 )
	{
		return false;
	}

	char buf[64];
	static const char kComponentNames[] = "xyzw";

	char swizzle[5] = {};

	for ( U32 i = 0; i < components; ++i )
	{
		swizzle[i] = kComponentNames[component + i];
	}

	sprintf( buf, "CoronaInstanceData%u.%s", vector, swizzle );

	reference = buf;

	return true;
}

// What a kernel needs in order to see its per-instance values, in the terms
// bgfx works in: instance data arrives as vec4s named i_data0 and up, so each
// attribute becomes a swizzle of one of those rather than an attribute of its
// own, and the Corona<Name> macros kernels use are defined to point at it.
static std::string
InstanceDefines( const FormatExtensionList* list )
{
	std::string result;

	if ( !list || !list->IsInstanced() )
	{
		return result;
	}

	list->SortNames();

	for ( U32 i = 0; i < list->GetAttributeCount(); ++i )
	{
		const U32 groupIndex = list->FindGroup( i );

		if ( !list->GetGroups()[groupIndex].IsInstanceRate() )
		{
			continue; // vertex-rate extension attributes are a separate matter
		}

		std::string reference;

		const char* name = list->FindNameByAttribute( i );

		if ( !name || !InstanceAttributeReference( list, i, reference ) )
		{
			Rtt_TRACE( ( "ERROR: per-instance attribute '%s' does not fit in the instance data bgfx allows\n", name ? name : "?" ) );
			continue;
		}

		char buf[128];

		sprintf( buf, "#define Corona%c%s %s\n", toupper( name[0] ), name + 1, reference.c_str() );

		result += buf;
	}

	// bgfx has no gl_InstanceID it can promise on every backend, so the index
	// travels in the instance data like everything else. When there is no other
	// per-instance data, the command buffer writes it into the first vector.
	if ( list->IsInstancedByID() )
	{
		result += "#define CoronaInstanceFloat CoronaInstanceData0.x\n"
				  "#define CoronaInstanceID int(CoronaInstanceData0.x)\n";
	}

	return result;
}

// bgfx reads the $input list textually, before the preprocessor runs, so the
// shell cannot declare the instance attributes conditionally: they have to be
// spliced into the line for the programs that use them, and left out of the
// rest, where an unfilled attribute would be undefined.
static std::string
AddInstanceInputs( const std::string& source, U32 vectorCount )
{
	const size_t start = source.find( "$input " );

	if ( std::string::npos == start )
	{
		return source;
	}

	const size_t end = source.find( '\n', start );

	if ( std::string::npos == end )
	{
		return source;
	}

	std::string inputs;

	for ( U32 i = 0; i < vectorCount; ++i )
	{
		char buf[32];

		sprintf( buf, ", i_data%u", i );

		inputs += buf;
	}

	return source.substr( 0, end ) + inputs + source.substr( end );
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
:	fResource( NULL )
{
	for ( U32 i = 0; i < Program::kNumVersions; ++i )
	{
		fBuildFailed[i] = false;
	}
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

	std::string prefix = maskDefine + ( header ? header : "" );

	// Instancing: the kernel reaches its per-instance values, and the instance
	// index, through macros over bgfx's instance data vectors.
	const ShaderResource* shaderResource = program.GetShaderResource();
	const FormatExtensionList* extensionList = shaderResource ? shaderResource->GetExtensionList() : NULL;
	const bool isInstanced = extensionList && extensionList->IsInstanced();

	if ( isInstanced )
	{
		// The shell declares one global per vector and copies the attributes
		// into them; this is how it learns how many there are.
		char instanceVectors[64];

		sprintf( instanceVectors, "#define CORONA_INSTANCE_VECTORS %u\n", GetInstanceStride( extensionList ) / kInstanceVectorSize );

		prefix += instanceVectors;
		prefix += InstanceDefines( extensionList );
	}

	if ( extensionList && extensionList->HasVertexRateData() )
	{
		// Per-vertex extension attributes would mean a second vertex stream and
		// a layout built per format; nothing else here depends on it, so the
		// program still compiles and the values simply are not there.
		Rtt_TRACE( ( "WARNING: the bgfx backend does not supply per-vertex extension attributes yet\n" ) );
	}

	std::string vertexStage = prefix + vertexSource;

	if ( isInstanced )
	{
		vertexStage = AddInstanceInputs( vertexStage, GetInstanceStride( extensionList ) / kInstanceVectorSize );
	}

	const bgfx::Memory* vertexBlob = CompileStage( vertexStage, BgfxShaderCompiler::kVertex, "corona.vs" );

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

	fResource = program;

	// See the note in BgfxTexture::Create: a resource is built on demand when a
	// draw reaches it before Corona's create queue does, and then asked for
	// again when that queue comes round -- and, since BindProgram goes through
	// the same path, once per draw after that. Rebuilding would recompile the
	// shaders and leak the program every one of those times.
	if ( bgfx::isValid( fData[Program::kMaskCount0].fProgram ) || fBuildFailed[Program::kMaskCount0] )
	{
		return;
	}

	// Only the unmasked variant is built up front; GetProgram compiles the
	// masked ones the first time a draw asks for them.
	if ( !Build( *program, Program::kMaskCount0, fData[Program::kMaskCount0] ) )
	{
		fBuildFailed[Program::kMaskCount0] = true;
	}
}

void
BgfxProgram::Update( CPUResource* resource )
{
	Rtt_ASSERT( CPUResource::kProgram == resource->GetType() );
	Program* program = static_cast< Program* >( resource );

	fResource = program;

	// The source changed, so every variant already built is stale.
	for ( U32 i = 0; i < Program::kNumVersions; ++i )
	{
		// A variant that failed to compile gets another chance: the new source
		// may well be what fixes it.
		fBuildFailed[i] = false;

		if ( bgfx::isValid( fData[i].fProgram ) )
		{
			bgfx::destroy( fData[i].fProgram );
			fData[i].fProgram = BGFX_INVALID_HANDLE;

			if ( !Build( *program, Program::Version( i ), fData[i] ) )
			{
				fBuildFailed[i] = true;
			}
		}
	}
}

void
BgfxProgram::Destroy()
{
	fResource = NULL;

	for ( U32 i = 0; i < Program::kNumVersions; ++i )
	{
		fBuildFailed[i] = false;

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
BgfxProgram::GetProgram( Program::Version version )
{
	Rtt_ASSERT( version < Program::kNumVersions );

	// The masked variants are only worth their compile once something is
	// actually drawn through a mask, which is what this call means.
	if ( !bgfx::isValid( fData[version].fProgram ) && !fBuildFailed[version] && fResource )
	{
		if ( !Build( *fResource, version, fData[version] ) )
		{
			fBuildFailed[version] = true;
		}
	}

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
