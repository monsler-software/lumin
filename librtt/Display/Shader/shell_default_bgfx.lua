local shell = {}

shell.language = "glsl"

shell.category = "default"

shell.name = "default"

-- The shell that adapts Corona kernels to bgfx's shader dialect. Kernels
-- themselves are unchanged: everything they rely on -- P_COLOR and friends,
-- v_ColorScale, u_FillSampler0, texture2D -- is declared here, exactly as it
-- is for the GL and Vulkan backends.
--
-- Two differences from those shells drive most of what follows.
--
-- Varyings. bgfx declares them through $input/$output, and on its HLSL,
-- SPIR-V and Metal paths it passes them as parameters of main(), which puts
-- them out of scope inside a kernel. So they arrive under an "In" suffix and
-- main() copies them into globals named the way kernels expect. Those globals
-- have to be "static" on the HLSL path, or its compiler treats a file-scope
-- variable as a uniform and refuses the assignment.
--
-- Precision. bgfx handles precision qualifiers itself, so the P_* macros that
-- mean something to GLSL ES become empty here.

local precision =
[[
#define P_DEFAULT
#define P_RANDOM
#define P_POSITION
#define P_NORMAL
#define P_UV
#define P_COLOR

// Only plain GLSL keeps file-scope variables as variables. Every other target
// shaderc emits -- HLSL, and SPIR-V and Metal, which it reaches through HLSL --
// treats one as a uniform and rejects assigning to it, so it has to be static
// there.
#if BGFX_SHADER_LANGUAGE_GLSL
#	define CORONA_GLOBAL
#else
#	define CORONA_GLOBAL static
#endif

// Hooks BgfxProgram fills in for whatever the kernels themselves declared:
// varyings of their own, and per-vertex extension attributes. Both are things
// bgfx has no keyword for -- what crosses the stages is fixed by the
// $input/$output lists, and attributes come from bgfx's own named set -- so
// the program splices those lists and defines these to match. Empty here,
// which is what an effect that declares neither compiles with.
#ifndef CORONA_KERNEL_VARYING_DECLS
#	define CORONA_KERNEL_VARYING_DECLS
#endif

#ifndef CORONA_KERNEL_VARYING_WRITES
#	define CORONA_KERNEL_VARYING_WRITES
#endif

#ifndef CORONA_KERNEL_VARYING_READS
#	define CORONA_KERNEL_VARYING_READS
#endif

#ifndef CORONA_KERNEL_ATTRIB_DECLS
#	define CORONA_KERNEL_ATTRIB_DECLS
#endif

#ifndef CORONA_KERNEL_ATTRIB_COPIES
#	define CORONA_KERNEL_ATTRIB_COPIES
#endif
]]

shell.vertex =
[[
$input a_position, a_texcoord0, a_color0, a_texcoord1
$output v_TexCoordIn, v_ColorScaleIn, v_UserDataIn, v_PositionIn, v_MaskUV0In, v_MaskUV1In, v_MaskUV2In

#include <bgfx_shader.sh>
]] .. precision ..
[[
uniform vec4 u_TotalTime;
uniform vec4 u_DeltaTime;
uniform vec4 u_TexelSize;
uniform vec4 u_ContentScale;

uniform mat4 u_ViewProjectionMatrix;

// Corona binds these as 3x3 matrices, the same as it does for the other
// backends: a mask is an affine transform of the vertex position into the
// mask texture's own space.
#if MASK_COUNT > 0
	uniform mat3 u_MaskMatrix0;
#endif

#if MASK_COUNT > 1
	uniform mat3 u_MaskMatrix1;
#endif

#if MASK_COUNT > 2
	uniform mat3 u_MaskMatrix2;
#endif

// Per-instance data, which bgfx delivers as vec4s named i_data0 and up. How
// many of them there are is decided per program, by the vertex extension the
// effect was defined with; BgfxProgram both sets this and splices the matching
// names into the $input line above.
//
// They are copied into globals for the same reason the varyings are: on the
// HLSL, SPIR-V and Metal paths bgfx passes attributes as parameters of main(),
// which puts them out of reach of a kernel. The Corona<Name> macros a kernel
// uses point at these copies.
#ifndef CORONA_INSTANCE_VECTORS
#	define CORONA_INSTANCE_VECTORS 0
#endif

#if CORONA_INSTANCE_VECTORS > 0
	CORONA_GLOBAL vec4 CoronaInstanceData0;
#endif

#if CORONA_INSTANCE_VECTORS > 1
	CORONA_GLOBAL vec4 CoronaInstanceData1;
#endif

#if CORONA_INSTANCE_VECTORS > 2
	CORONA_GLOBAL vec4 CoronaInstanceData2;
#endif

#if CORONA_INSTANCE_VECTORS > 3
	CORONA_GLOBAL vec4 CoronaInstanceData3;
#endif

#if CORONA_INSTANCE_VECTORS > 4
	CORONA_GLOBAL vec4 CoronaInstanceData4;
#endif

// The vertex attributes, under the names kernels reach them by. Two sets of
// names are in use: the Corona<Name> macros, and the a_* attribute names the
// GL and Vulkan shells declare, which the built-in filters (wobble, pixelate,
// scatter and the rest) still write directly.
//
// Both point at globals rather than at the attributes themselves, for the same
// reason the varyings do: on the HLSL, SPIR-V and Metal paths bgfx passes
// attributes as parameters of main(), which puts them out of scope inside a
// kernel. main() copies them across before calling VertexKernel.
// A vertex kernel is allowed to override the texture coordinate the fragment
// stage will see -- filter.straighten does exactly that -- by assigning to
// v_TexCoord, which the GL shell declares as a varying. Here it is a global
// that main() seeds before the kernel runs and hands to the $output after.
CORONA_GLOBAL vec2 v_TexCoord;

// Whatever this effect declared for itself, if anything.
CORONA_KERNEL_VARYING_DECLS
CORONA_KERNEL_ATTRIB_DECLS

CORONA_GLOBAL vec2 CoronaAttribPosition;
CORONA_GLOBAL vec3 CoronaAttribTexCoord;
CORONA_GLOBAL vec4 CoronaAttribColor;
CORONA_GLOBAL vec4 CoronaAttribUserData;

#define a_Position CoronaAttribPosition
#define a_TexCoord CoronaAttribTexCoord
#define a_Color CoronaAttribColor
#define a_UserData CoronaAttribUserData

#define CoronaVertexUserData CoronaAttribUserData
#define CoronaTexCoord CoronaAttribTexCoord.xy

// Single-float built-ins travel in the x component, since bgfx has no scalar
// uniform type.
#define CoronaTotalTime u_TotalTime.x
#define CoronaDeltaTime u_DeltaTime.x
#define CoronaTexelSize u_TexelSize
#define CoronaContentScale u_ContentScale.xy

P_POSITION vec2 VertexKernel( P_POSITION vec2 position );

void main()
{
	CoronaAttribPosition = a_position.xy;
	CoronaAttribTexCoord = a_texcoord0;
	CoronaAttribColor = a_color0;
	CoronaAttribUserData = a_texcoord1;

	v_TexCoord = a_texcoord0.xy;

	v_ColorScaleIn = a_color0;
	v_UserDataIn = a_texcoord1;

	#if CORONA_INSTANCE_VECTORS > 0
		CoronaInstanceData0 = i_data0;
	#endif

	#if CORONA_INSTANCE_VECTORS > 1
		CoronaInstanceData1 = i_data1;
	#endif

	#if CORONA_INSTANCE_VECTORS > 2
		CoronaInstanceData2 = i_data2;
	#endif

	#if CORONA_INSTANCE_VECTORS > 3
		CoronaInstanceData3 = i_data3;
	#endif

	#if CORONA_INSTANCE_VECTORS > 4
		CoronaInstanceData4 = i_data4;
	#endif

	CORONA_KERNEL_ATTRIB_COPIES

	P_POSITION vec2 position = VertexKernel( CoronaAttribPosition );

	// After the kernel, since it may have overridden it -- which is the whole
	// point of a varying the kernel declared itself.
	v_TexCoordIn = v_TexCoord;

	CORONA_KERNEL_VARYING_WRITES

	v_PositionIn = position;

	// Unused mask channels still have to be written: leaving an output
	// undefined is not something every backend tolerates.
	v_MaskUV0In = vec2( 0.0, 0.0 );
	v_MaskUV1In = vec2( 0.0, 0.0 );
	v_MaskUV2In = vec2( 0.0, 0.0 );

	#if MASK_COUNT > 0
		v_MaskUV0In = mul( u_MaskMatrix0, vec3( position, 1.0 ) ).xy;
	#endif

	#if MASK_COUNT > 1
		v_MaskUV1In = mul( u_MaskMatrix1, vec3( position, 1.0 ) ).xy;
	#endif

	#if MASK_COUNT > 2
		v_MaskUV2In = mul( u_MaskMatrix2, vec3( position, 1.0 ) ).xy;
	#endif

	gl_Position = mul( u_ViewProjectionMatrix, vec4( position, 0.0, 1.0 ) );
}
]]

shell.fragment =
[[
$input v_TexCoordIn, v_ColorScaleIn, v_UserDataIn, v_PositionIn, v_MaskUV0In, v_MaskUV1In, v_MaskUV2In

#include <bgfx_shader.sh>
]] .. precision ..
[[
SAMPLER2D(u_FillSampler0, 0);
SAMPLER2D(u_FillSampler1, 1);

#if MASK_COUNT > 0
	SAMPLER2D(u_MaskSampler0, 2);
#endif

#if MASK_COUNT > 1
	SAMPLER2D(u_MaskSampler1, 3);
#endif

#if MASK_COUNT > 2
	SAMPLER2D(u_MaskSampler2, 4);
#endif

uniform vec4 u_TotalTime;
uniform vec4 u_DeltaTime;
uniform vec4 u_TexelSize;
uniform vec4 u_ContentScale;

// The varyings kernels reference by name. See the note at the top of this file
// on why they are globals rather than the $input names themselves.
CORONA_GLOBAL vec2 v_TexCoord;
CORONA_GLOBAL vec4 v_ColorScale;
CORONA_GLOBAL vec4 v_UserData;
CORONA_GLOBAL vec2 v_Position;

// Whatever this effect declared for itself, if anything.
CORONA_KERNEL_VARYING_DECLS

#define CoronaColorScale( color ) ((color) * v_ColorScale)
#define CoronaVertexUserData v_UserData

#define CoronaTotalTime u_TotalTime.x
#define CoronaDeltaTime u_DeltaTime.x
#define CoronaTexelSize u_TexelSize
#define CoronaContentScale u_ContentScale.xy

#define CoronaSampler0 u_FillSampler0
#define CoronaSampler1 u_FillSampler1

P_COLOR vec4 FragmentKernel( P_UV vec2 texCoord );

void main()
{
	v_TexCoord = v_TexCoordIn;
	v_ColorScale = v_ColorScaleIn;
	v_UserData = v_UserDataIn;
	v_Position = v_PositionIn;

	CORONA_KERNEL_VARYING_READS

	P_COLOR vec4 result = FragmentKernel( v_TexCoord );

	// A mask texture is single-channel: its red channel scales the result, so
	// the transparent parts of the mask erase what the kernel produced.
	#if MASK_COUNT > 0
		result *= texture2D( u_MaskSampler0, v_MaskUV0In ).r;
	#endif

	#if MASK_COUNT > 1
		result *= texture2D( u_MaskSampler1, v_MaskUV1In ).r;
	#endif

	#if MASK_COUNT > 2
		result *= texture2D( u_MaskSampler2, v_MaskUV2In ).r;
	#endif

	gl_FragColor = result;
}
]]

return shell
