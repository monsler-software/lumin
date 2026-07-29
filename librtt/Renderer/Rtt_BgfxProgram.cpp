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

#include "Renderer/Rtt_BgfxGeometry.h"
#include "Renderer/Rtt_BgfxShaderCompiler.h"
#include "Renderer/Rtt_FormatExtensionList.h"
#include "Display/Rtt_ShaderResource.h"

#include <ctype.h>
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

// The semantics left for whatever a kernel declares as a varying of its own.
// The shell's own varyings run up to TEXCOORD6, and the instance data vectors
// take TEXCOORD7 through TEXCOORD11.
static const char* kCustomVaryingSemantics[] =
{
	  "TEXCOORD12"
	, "TEXCOORD13"
	, "TEXCOORD14"
	, "TEXCOORD15"
	, "COLOR1"
	, "COLOR2"
	, "COLOR3"
};

enum { kMaxCustomVaryings = sizeof( kCustomVaryingSemantics ) / sizeof( kCustomVaryingSemantics[0] ) };

// A varying a kernel declared for itself, as opposed to the ones the shell
// provides.
struct KernelVarying
{
	std::string fType;
	std::string fName;
};

// The varyings the shell already declares. A kernel that names one of these is
// reaching for something it is being handed anyway -- the GL shell's own
// declaration makes that legal there -- so the declaration is dropped rather
// than turned into a varying of its own.
static bool
IsShellVarying( const std::string& name )
{
	static const char* kNames[] =
	{
		"v_Position", "v_TexCoord", "v_TexCoordZ", "v_ColorScale", "v_UserData",
		"v_MaskUV0", "v_MaskUV1", "v_MaskUV2"
	};

	for ( size_t i = 0; i < sizeof( kNames ) / sizeof( kNames[0] ); ++i )
	{
		if ( name == kNames[i] )
		{
			return true;
		}
	}

	return false;
}

static bool
IsIdentifierChar( char c )
{
	return 0 != isalnum( (unsigned char)c ) || '_' == c;
}

// Pulls every "varying <precision> <type> <name>;" out of a kernel and returns
// what it declared, leaving the source without those lines.
//
// bgfx has no varying keyword: what travels between the stages is fixed by the
// $input/$output lists and by the varying definition handed to the compiler,
// neither of which a kernel can reach. So a kernel's own varyings are lifted
// out here and re-created around it -- see BuildKernelVaryings.
static void
ExtractVaryings( std::string& source, std::vector< KernelVarying >& varyings )
{
	size_t pos = 0;

	while ( ( pos = source.find( "varying", pos ) ) != std::string::npos )
	{
		const size_t after = pos + 7;

		const bool isToken = ( 0 == pos || !IsIdentifierChar( source[pos - 1] ) )
			&& after < source.size()
			&& 0 != isspace( (unsigned char)source[after] );

		if ( !isToken )
		{
			pos = after;
			continue;
		}

		const size_t end = source.find( ';', after );

		if ( std::string::npos == end )
		{
			break; // not a declaration after all; nothing more to find either
		}

		const std::string declaration = source.substr( after, end - after );

		// A declaration is one line. Anything else is the word turning up in a
		// comment -- the shell's own commentary talks about varyings -- and
		// erasing to the next semicolon there would take the shader with it.
		if ( declaration.find( '\n' ) != std::string::npos )
		{
			pos = after;
			continue;
		}

		if ( declaration.find( '[' ) != std::string::npos )
		{
			// A varying array. bgfx passes varyings as whole values, so there is
			// no honest way to carry one.
			Rtt_TRACE( ( "WARNING: the bgfx backend cannot carry a varying array between kernel stages\n" ) );

			source.erase( pos, end + 1 - pos );
			continue;
		}

		// "P_UV vec2 name" or "P_UV vec2 name1, name2": the names come last, the
		// type just before the first of them, and any precision qualifier ahead
		// of that.
		std::vector< std::string > tokens;
		std::string token;

		for ( size_t i = 0; i <= declaration.size(); ++i )
		{
			const char c = ( i < declaration.size() ) ? declaration[i] : ' ';

			if ( IsIdentifierChar( c ) )
			{
				token += c;
			}
			else
			{
				if ( !token.empty() )
				{
					tokens.push_back( token );
					token.clear();
				}

				if ( ',' == c )
				{
					tokens.push_back( "," );
				}
			}
		}

		// Everything from the type onwards: the token before the first comma-
		// separated name, if there is one, is the type.
		size_t typeIndex = tokens.size();

		for ( size_t i = 0; i < tokens.size(); ++i )
		{
			if ( "," == tokens[i] )
			{
				typeIndex = ( i >= 2 ) ? i - 2 : tokens.size();
				break;
			}
		}

		if ( tokens.size() == typeIndex )
		{
			typeIndex = ( tokens.size() >= 2 ) ? tokens.size() - 2 : tokens.size();
		}

		if ( typeIndex + 1 < tokens.size() )
		{
			const std::string& type = tokens[typeIndex];

			for ( size_t i = typeIndex + 1; i < tokens.size(); ++i )
			{
				if ( "," == tokens[i] )
				{
					continue;
				}

				KernelVarying varying;

				varying.fType = type;
				varying.fName = tokens[i];

				varyings.push_back( varying );
			}
		}

		source.erase( pos, end + 1 - pos );
	}
}

// The declarations, the varying definition and the $input/$output entries a
// kernel's own varyings need, gathered from both stages.
//
// bgfx passes varyings as parameters of main() on its HLSL, SPIR-V and Metal
// paths, which puts them out of scope inside a kernel -- the same reason the
// shell's own varyings arrive under an "In" suffix and are copied into globals.
// Kernel varyings get the same treatment, through macros the shell expands: the
// declarations before main(), the copies inside it.
struct KernelVaryingCode
{
	std::string fVaryingDef;    // appended to what the compiler is handed
	std::string fVertexInputs;  // spliced into the vertex stage's $output
	std::string fFragmentInputs;// spliced into the fragment stage's $input
	std::string fVertexDefines;
	std::string fFragmentDefines;
};

static void
BuildKernelVaryings( std::string& vertexSource, std::string& fragmentSource, KernelVaryingCode& code )
{
	std::vector< KernelVarying > varyings;

	ExtractVaryings( vertexSource, varyings );

	const size_t fromVertex = varyings.size();

	ExtractVaryings( fragmentSource, varyings );

	std::string declarations, writes, reads;
	U32 count = 0;

	for ( size_t i = 0; i < varyings.size(); ++i )
	{
		const KernelVarying& varying = varyings[i];

		if ( IsShellVarying( varying.fName ) )
		{
			continue;
		}

		// The fragment stage declares the same varying the vertex stage does, so
		// the second sighting of a name is the same variable.
		bool seen = false;

		for ( size_t j = 0; j < i && !seen; ++j )
		{
			seen = ( varyings[j].fName == varying.fName ) && !IsShellVarying( varyings[j].fName );
		}

		if ( seen )
		{
			continue;
		}

		if ( count >= kMaxCustomVaryings )
		{
			Rtt_TRACE( ( "WARNING: this effect declares more than the %u varyings of its own the bgfx backend carries; '%s' is unavailable\n",
				U32( kMaxCustomVaryings ), varying.fName.c_str() ) );
			continue;
		}

		if ( i >= fromVertex )
		{
			// Declared by the fragment kernel alone, so nothing ever writes it.
			Rtt_TRACE( ( "WARNING: varying '%s' is declared by this effect's fragment kernel but not its vertex kernel, so it carries nothing\n",
				varying.fName.c_str() ) );
		}

		char buf[256];

		sprintf( buf, "%s %sIn : %s;\n", varying.fType.c_str(), varying.fName.c_str(), kCustomVaryingSemantics[count] );
		code.fVaryingDef += buf;

		sprintf( buf, ", %sIn", varying.fName.c_str() );
		code.fVertexInputs += buf;
		code.fFragmentInputs += buf;

		sprintf( buf, "CORONA_GLOBAL %s %s; ", varying.fType.c_str(), varying.fName.c_str() );
		declarations += buf;

		sprintf( buf, "%sIn = %s; ", varying.fName.c_str(), varying.fName.c_str() );
		writes += buf;

		sprintf( buf, "%s = %sIn; ", varying.fName.c_str(), varying.fName.c_str() );
		reads += buf;

		++count;
	}

	if ( 0 == count )
	{
		return;
	}

	// Single-line macro bodies, since these are defines: the shell expands the
	// declarations before main() and the copies inside it.
	code.fVertexDefines =
		  "#define CORONA_KERNEL_VARYING_DECLS " + declarations + "\n"
		+ "#define CORONA_KERNEL_VARYING_WRITES " + writes + "\n";

	code.fFragmentDefines =
		  "#define CORONA_KERNEL_VARYING_DECLS " + declarations + "\n"
		+ "#define CORONA_KERNEL_VARYING_READS " + reads + "\n";
}

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
CompileStage( const std::string& source, const std::string& varyingDef, BgfxShaderCompiler::Stage stage, const char* debugName )
{
	std::vector< U8 > blob;
	std::string messages;

	const bool compiled = BgfxShaderCompiler::Compile(
		  source.c_str()
		, varyingDef.c_str()
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

// How many bytes of one instance's data a group takes.
//
// A "windowed" group is the one case where this is not simply the group's own
// size: it gives each instance a sliding run of `count` values from a single
// array (see FormatExtensionList::Group::IsWindowed), and Corona leaves its
// size at zero because the array is shared. Each instance still sees the whole
// window, so that is what has to be sent.
U32
BgfxProgram::GetInstanceGroupSize( const FormatExtensionList* list, U32 groupIndex )
{
	const FormatExtensionList::Group& group = list->GetGroups()[groupIndex];

	if ( !group.IsWindowed() )
	{
		return group.size;
	}

	for ( U32 i = 0; i < list->GetAttributeCount(); ++i )
	{
		if ( list->FindGroup( i ) == groupIndex )
		{
			return list->GetAttributes()[i].GetSize() * group.count;
		}
	}

	return 0;
}

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
			const U32 size = GetInstanceGroupSize( list, i );

			offset += ( size + kInstanceVectorSize - 1 ) / kInstanceVectorSize * kInstanceVectorSize;
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

// bgfx reads the $input and $output lists as text, before the preprocessor
// runs, so the shell cannot declare these conditionally: they have to be
// spliced into the line for the programs that use them, and left out of the
// rest, where an unfilled attribute would be undefined.
static std::string
AddToDirective( const std::string& source, const char* directive, const std::string& entries )
{
	if ( entries.empty() )
	{
		return source;
	}

	const size_t start = source.find( directive );

	if ( std::string::npos == start )
	{
		return source;
	}

	const size_t end = source.find( '\n', start );

	if ( std::string::npos == end )
	{
		return source;
	}

	return source.substr( 0, end ) + entries + source.substr( end );
}

static std::string
InstanceInputs( U32 vectorCount )
{
	std::string inputs;

	for ( U32 i = 0; i < vectorCount; ++i )
	{
		char buf[32];

		sprintf( buf, ", i_data%u", i );

		inputs += buf;
	}

	return inputs;
}

// What a kernel needs in order to see its per-vertex extension attributes.
//
// Corona interleaves those after each vertex and lets the effect name them;
// bgfx wants named attributes from its own fixed set, so each one is given a
// slot -- a_texcoord2 and up, matching what BgfxGeometry::BuildVertexLayout
// binds -- and the Corona<Name> macro a kernel uses points at a global copy of
// it, for the same reason the varyings are copied.
struct VertexAttributeCode
{
	std::string fVaryingDef;
	std::string fInputs;
	std::string fDefines;
};

static void
BuildVertexAttributes( const FormatExtensionList* list, VertexAttributeCode& code )
{
	if ( !list || !list->HasVertexRateData() )
	{
		return;
	}

	static const char* kTypes[] = { "float", "vec2", "vec3", "vec4" };

	list->SortNames();

	std::string declarations, copies;
	U32 slot = 0;

	for ( U32 i = 0; i < list->GetAttributeCount(); ++i )
	{
		if ( list->GetGroups()[list->FindGroup( i )].IsInstanceRate() )
		{
			continue;
		}

		if ( slot >= BgfxGeometry::kMaxVertexExtensionAttributes )
		{
			Rtt_TRACE( ( "WARNING: this effect declares more than the %u per-vertex extension attributes the bgfx backend carries\n",
				U32( BgfxGeometry::kMaxVertexExtensionAttributes ) ) );
			break;
		}

		const FormatExtensionList::Attribute& attribute = list->GetAttributes()[i];
		const char* name = list->FindNameByAttribute( i );

		if ( !name )
		{
			++slot; // the layout spends the slot either way; see BuildVertexLayout
			continue;
		}

		const U32 components = attribute.components > 0 ? attribute.components : 4;
		const char* type = kTypes[( components > 4 ? 4 : components ) - 1];

		char buf[256];

		sprintf( buf, "%s a_texcoord%u : TEXCOORD%u;\n", type, slot + 2, slot + 2 );
		code.fVaryingDef += buf;

		sprintf( buf, ", a_texcoord%u", slot + 2 );
		code.fInputs += buf;

		sprintf( buf, "CORONA_GLOBAL %s CoronaVertexExt%u; ", type, slot );
		declarations += buf;

		sprintf( buf, "CoronaVertexExt%u = a_texcoord%u; ", slot, slot + 2 );
		copies += buf;

		sprintf( buf, "#define Corona%c%s CoronaVertexExt%u\n", toupper( name[0] ), name + 1, slot );
		code.fDefines += buf;

		++slot;
	}

	if ( 0 == slot )
	{
		return;
	}

	code.fDefines +=
		  "#define CORONA_KERNEL_ATTRIB_DECLS " + declarations + "\n"
		+ "#define CORONA_KERNEL_ATTRIB_COPIES " + copies + "\n";
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
:	fResource( NULL ),
	fInstancedByID( false ),
	fInstanceStride( 0 )
{
	for ( U32 i = 0; i < Program::kNumVersions; ++i )
	{
		fBuildFailed[i] = false;
	}
}

// What this effect asked for by way of instancing, which only its own extension
// list knows: the reconciled list a draw carries describes the geometry's
// attributes, and says nothing about whether the effect wanted the instance
// index.
void
BgfxProgram::ReadInstancing( Program& program )
{
	const ShaderResource* shaderResource = program.GetShaderResource();
	const FormatExtensionList* extensionList = shaderResource ? shaderResource->GetExtensionList() : NULL;

	fInstancedByID = extensionList && extensionList->IsInstancedByID();
	fInstanceStride = ( extensionList && extensionList->IsInstanced() ) ? GetInstanceStride( extensionList ) : 0;
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

	// Per-vertex extension attributes, which arrive interleaved with the vertex
	// itself and reach the kernel under the names the effect gave them.
	VertexAttributeCode attributes;

	BuildVertexAttributes( extensionList, attributes );

	prefix += attributes.fDefines;

	// Varyings the kernels declared for themselves, which bgfx has no keyword
	// for: they are lifted out of the sources and re-created around them.
	std::string vertexBody( vertexSource );
	std::string fragmentBody( fragmentSource );

	KernelVaryingCode varyings;

	BuildKernelVaryings( vertexBody, fragmentBody, varyings );

	const std::string varyingDef = std::string( kVaryingDef ) + attributes.fVaryingDef + varyings.fVaryingDef;

	std::string vertexStage = prefix + varyings.fVertexDefines + vertexBody;

	vertexStage = AddToDirective( vertexStage, "$input ", attributes.fInputs );
	vertexStage = AddToDirective( vertexStage, "$output ", varyings.fVertexInputs );

	if ( isInstanced )
	{
		vertexStage = AddToDirective( vertexStage, "$input ", InstanceInputs( GetInstanceStride( extensionList ) / kInstanceVectorSize ) );
	}

	const bgfx::Memory* vertexBlob = CompileStage( vertexStage, varyingDef, BgfxShaderCompiler::kVertex, "corona.vs" );

	if ( !vertexBlob )
	{
		return false;
	}

	std::string fragmentStage = prefix + varyings.fFragmentDefines + fragmentBody;

	fragmentStage = AddToDirective( fragmentStage, "$input ", varyings.fFragmentInputs );

	const bgfx::Memory* fragmentBlob = CompileStage( fragmentStage, varyingDef, BgfxShaderCompiler::kFragment, "corona.fs" );

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
		// One of the two may well have been created. No program will own it, so
		// nothing else would ever destroy it.
		if ( bgfx::isValid( vertexShader ) )
		{
			bgfx::destroy( vertexShader );
		}

		if ( bgfx::isValid( fragmentShader ) )
		{
			bgfx::destroy( fragmentShader );
		}

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

	ReadInstancing( *program );

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

	ReadInstancing( *program );

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
