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
]]

shell.vertex =
[[
$input a_position, a_texcoord0, a_color0, a_texcoord1
$output v_TexCoordIn, v_ColorScaleIn, v_UserDataIn, v_PositionIn

#include <bgfx_shader.sh>
]] .. precision ..
[[
uniform vec4 u_TotalTime;
uniform vec4 u_DeltaTime;
uniform vec4 u_TexelSize;
uniform vec4 u_ContentScale;

uniform mat4 u_ViewProjectionMatrix;

#if MASK_COUNT > 0
	uniform mat4 u_MaskMatrix0;
#endif

#if MASK_COUNT > 1
	uniform mat4 u_MaskMatrix1;
#endif

#if MASK_COUNT > 2
	uniform mat4 u_MaskMatrix2;
#endif

#define CoronaVertexUserData a_texcoord1
#define CoronaTexCoord a_texcoord0.xy

// Single-float built-ins travel in the x component, since bgfx has no scalar
// uniform type.
#define CoronaTotalTime u_TotalTime.x
#define CoronaDeltaTime u_DeltaTime.x
#define CoronaTexelSize u_TexelSize
#define CoronaContentScale u_ContentScale.xy

P_POSITION vec2 VertexKernel( P_POSITION vec2 position );

void main()
{
	v_TexCoordIn = a_texcoord0.xy;
	v_ColorScaleIn = a_color0;
	v_UserDataIn = a_texcoord1;

	P_POSITION vec2 position = VertexKernel( a_position.xy );

	v_PositionIn = position;

	gl_Position = mul( u_ViewProjectionMatrix, vec4( position, 0.0, 1.0 ) );
}
]]

shell.fragment =
[[
$input v_TexCoordIn, v_ColorScaleIn, v_UserDataIn, v_PositionIn

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

	P_COLOR vec4 result = FragmentKernel( v_TexCoord );

	gl_FragColor = result;
}
]]

return shell
