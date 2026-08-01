//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Corona game engine.
// For overview and more information on licensing please refer to README.md
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////

#include "Core/Rtt_Build.h"

#include "Renderer/Rtt_Bgfx3DPipeline.h"

#include "Display/Rtt_Camera3D.h"
#include "Display/Rtt_Mesh3D.h"
#include "Display/Rtt_Object3D.h"
#include "Display/Rtt_ShaderEffect3D.h"
#include "Display/Rtt_Texture3D.h"
#include "Renderer/Rtt_BgfxShaderCompiler.h"

#include <cstring>
#include <string>

// ----------------------------------------------------------------------------

namespace Rtt
{

// ----------------------------------------------------------------------------

// The attributes and varyings the two shaders below agree on. bgfx's compiler
// takes this as a separate input rather than reading it out of the source.
static const char kVaryingDef[] =
	"vec3 v_worldPos   : TEXCOORD0;\n"
	"vec3 v_normal     : TEXCOORD1;\n"
	"vec2 v_texcoord0  : TEXCOORD2;\n"
	"\n"
	"vec3 a_position   : POSITION;\n"
	"vec3 a_normal     : NORMAL;\n"
	"vec2 a_texcoord0  : TEXCOORD0;\n";

// The skinned program's layout adds the two attributes carrying the bone
// palette lookup, which live in a vertex stream of their own (see SkinVertex3D).
static const char kSkinnedVaryingDef[] =
	"vec3 v_worldPos   : TEXCOORD0;\n"
	"vec3 v_normal     : TEXCOORD1;\n"
	"vec2 v_texcoord0  : TEXCOORD2;\n"
	"\n"
	"vec3 a_position   : POSITION;\n"
	"vec3 a_normal     : NORMAL;\n"
	"vec2 a_texcoord0  : TEXCOORD0;\n"
	"vec4 a_indices    : BLENDINDICES;\n"
	"vec4 a_weight     : BLENDWEIGHT;\n";

static const char kVertexShader[] =
	"$input a_position, a_normal, a_texcoord0\n"
	"$output v_worldPos, v_normal, v_texcoord0\n"
	"\n"
	"#include <bgfx_shader.sh>\n"
	"\n"
	"uniform mat4 u_model3D;\n"
	"uniform mat4 u_viewProj3D;\n"
	"\n"
	"void main()\n"
	"{\n"
	"    vec4 world = mul(u_model3D, vec4(a_position, 1.0));\n"
	"    v_worldPos = world.xyz;\n"
	// Rotating the normal by the model matrix is correct for rotation and for
	// uniform scale, and wrong for non-uniform scale, which needs the inverse
	// transpose. Normalising in the fragment shader hides the error for the
	// scales content actually uses; a scaleX of 5 against a scaleY of 1 will
	// shade slightly off until the inverse transpose is passed down too.
	"    v_normal = mul(u_model3D, vec4(a_normal, 0.0)).xyz;\n"
	"    v_texcoord0 = a_texcoord0;\n"
	"    gl_Position = mul(u_viewProj3D, world);\n"
	"}\n";

// The same vertex stage with the skinning palette applied first.
//
// A separate program rather than a branch in the one above: a uniform branch
// would still cost every static object the bone array's uniform space and its
// upload, and skinned and static geometry do not share a vertex layout anyway.
//
// The fragment stage is shared -- skinning changes where a vertex is, not how
// the surface is lit -- so only this stage has a variant.
static const char kSkinnedVertexShader[] =
	"$input a_position, a_normal, a_texcoord0, a_indices, a_weight\n"
	"$output v_worldPos, v_normal, v_texcoord0\n"
	"\n"
	"#include <bgfx_shader.sh>\n"
	"\n"
	// Three vec4 rows per bone rather than a mat4 -- see kMaxDraw3DBones.
	"#define MAX_BONES 84\n"
	"\n"
	"uniform mat4 u_model3D;\n"
	"uniform mat4 u_viewProj3D;\n"
	"uniform vec4 u_bones3D[MAX_BONES * 3];\n"
	"\n"
	"void main()\n"
	"{\n"
	// Linear blend skinning: the influencing bones' transforms are summed by
	// weight into one transform, which gives the same result as transforming the
	// vertex by each and blending the positions, for a quarter of the work.
	//
	// The rows are blended directly instead of being reassembled into matrices
	// first. Nothing here needs a matrix: a row dotted with the vertex is one
	// component of the result, which is all three lines below are doing.
	//
	// The weights are normalised by the loader, so no rescaling is needed here.
	// A vertex with no influences at all would collapse to the origin, so the
	// loader gives those a single full-weight influence on bone zero instead.
	"    int i0 = int(a_indices.x) * 3;\n"
	"    int i1 = int(a_indices.y) * 3;\n"
	"    int i2 = int(a_indices.z) * 3;\n"
	"    int i3 = int(a_indices.w) * 3;\n"
	"\n"
	"    vec4 row0 = u_bones3D[i0 + 0] * a_weight.x + u_bones3D[i1 + 0] * a_weight.y + u_bones3D[i2 + 0] * a_weight.z + u_bones3D[i3 + 0] * a_weight.w;\n"
	"    vec4 row1 = u_bones3D[i0 + 1] * a_weight.x + u_bones3D[i1 + 1] * a_weight.y + u_bones3D[i2 + 1] * a_weight.z + u_bones3D[i3 + 1] * a_weight.w;\n"
	"    vec4 row2 = u_bones3D[i0 + 2] * a_weight.x + u_bones3D[i1 + 2] * a_weight.y + u_bones3D[i2 + 2] * a_weight.z + u_bones3D[i3 + 2] * a_weight.w;\n"
	"\n"
	"    vec4 bindPosition = vec4(a_position, 1.0);\n"
	"\n"
	"    vec3 posed = vec3(dot(row0, bindPosition), dot(row1, bindPosition), dot(row2, bindPosition));\n"
	// A normal is a direction, so the translation column is left out of its
	// transform rather than added to it.
	"    vec3 posedNormal = vec3(dot(row0.xyz, a_normal), dot(row1.xyz, a_normal), dot(row2.xyz, a_normal));\n"
	"\n"
	"    vec4 world = mul(u_model3D, vec4(posed, 1.0));\n"
	"    v_worldPos = world.xyz;\n"
	"    v_normal = mul(u_model3D, vec4(posedNormal, 0.0)).xyz;\n"
	"    v_texcoord0 = a_texcoord0;\n"
	"    gl_Position = mul(u_viewProj3D, world);\n"
	"}\n";

// The shadow pass: position only, into a depth-only target.
//
// The fragment stage writes nothing -- the depth buffer is the whole output -- but
// it has to exist, because a program needs both stages.
static const char kShadowVertexShader[] =
	"$input a_position\n"
	"\n"
	"#include <bgfx_shader.sh>\n"
	"\n"
	"uniform mat4 u_model3D;\n"
	"uniform mat4 u_shadowMatrix3D;\n"
	"\n"
	"void main()\n"
	"{\n"
	"    gl_Position = mul(u_shadowMatrix3D, mul(u_model3D, vec4(a_position, 1.0)));\n"
	"}\n";

static const char kShadowSkinnedVertexShader[] =
	"$input a_position, a_indices, a_weight\n"
	"\n"
	"#include <bgfx_shader.sh>\n"
	"\n"
	"#define MAX_BONES 84\n"
	"\n"
	"uniform mat4 u_model3D;\n"
	"uniform mat4 u_shadowMatrix3D;\n"
	"uniform vec4 u_bones3D[MAX_BONES * 3];\n"
	"\n"
	"void main()\n"
	"{\n"
	// The same blend the shading pass does, because a caster has to be posed the
	// way it is drawn or its shadow will not match it.
	"    int i0 = int(a_indices.x) * 3;\n"
	"    int i1 = int(a_indices.y) * 3;\n"
	"    int i2 = int(a_indices.z) * 3;\n"
	"    int i3 = int(a_indices.w) * 3;\n"
	"\n"
	"    vec4 row0 = u_bones3D[i0 + 0] * a_weight.x + u_bones3D[i1 + 0] * a_weight.y + u_bones3D[i2 + 0] * a_weight.z + u_bones3D[i3 + 0] * a_weight.w;\n"
	"    vec4 row1 = u_bones3D[i0 + 1] * a_weight.x + u_bones3D[i1 + 1] * a_weight.y + u_bones3D[i2 + 1] * a_weight.z + u_bones3D[i3 + 1] * a_weight.w;\n"
	"    vec4 row2 = u_bones3D[i0 + 2] * a_weight.x + u_bones3D[i1 + 2] * a_weight.y + u_bones3D[i2 + 2] * a_weight.z + u_bones3D[i3 + 2] * a_weight.w;\n"
	"\n"
	"    vec4 bindPosition = vec4(a_position, 1.0);\n"
	"    vec3 posed = vec3(dot(row0, bindPosition), dot(row1, bindPosition), dot(row2, bindPosition));\n"
	"\n"
	"    gl_Position = mul(u_shadowMatrix3D, mul(u_model3D, vec4(posed, 1.0)));\n"
	"}\n";

static const char kShadowFragmentShader[] =
	"#include <bgfx_shader.sh>\n"
	"\n"
	"void main()\n"
	"{\n"
	// Never read: the target has no colour attachment. Written anyway because a
	// fragment stage that assigns nothing is not valid GLSL.
	"    gl_FragColor = vec4_splat(1.0);\n"
	"}\n";

// A metallic-roughness PBR shader: GGX for the distribution, Smith for the
// geometry term, Schlick for Fresnel. The same model glTF describes, so a
// material authored against any PBR tool lands where its author expected.
//
// Assembled rather than used directly: SHADOWS_ENABLED is decided from the
// backend's capabilities at run time, and a backend without depth-compare
// sampling must not even declare the shadow sampler -- an unbound sampler is a
// draw-time error in bgfx, and there would be nothing valid to bind.
static const char kFragmentShader[] =
	"#include <bgfx_shader.sh>\n"
	"\n"
	"#define MAX_LIGHTS 8\n"
	"\n"
	"uniform vec4 u_albedo3D;\n"
	// x roughness, y metallic, z alpha, w light count
	"uniform vec4 u_material3D;\n"
	"uniform vec4 u_emissive3D;\n"
	"uniform vec4 u_ambient3D;\n"
	"uniform vec4 u_cameraPos3D;\n"
	// xyz position, w kind (0 directional, 1 point, 2 spot)
	"uniform vec4 u_lightPosition3D[MAX_LIGHTS];\n"
	"uniform vec4 u_lightDirection3D[MAX_LIGHTS];\n"
	"uniform vec4 u_lightColor3D[MAX_LIGHTS];\n"
	// x intensity, y range, z inner cosine, w outer cosine
	"uniform vec4 u_lightParams3D[MAX_LIGHTS];\n"
	// x albedo, y metallic-roughness, z emissive, w environment: 1 where a real map
	// is bound.
	"uniform vec4 u_mapFlags3D;\n"
	// Nine spherical harmonic coefficients of the environment's diffuse
	// irradiance, and its intensity in the tenth's x.
	"uniform vec4 u_irradiance3D[9];\n"
	"uniform vec4 u_envParams3D;\n"
	"\n"
	"SAMPLER2D(s_albedo3D, 0);\n"
	"SAMPLER2D(s_metallicRoughness3D, 1);\n"
	"SAMPLER2D(s_emissive3D, 2);\n"
	"SAMPLER2D(s_environment3D, 3);\n"
	"\n"
	"#if SHADOWS_ENABLED\n"
	// The light's view-projection with the map's texel-space bias folded in, so
	// that dividing by w here lands directly on a texture coordinate and a 0..1
	// depth. The fold is done on the CPU because it depends on which way the
	// backend's textures run and on its clip-space depth range, and neither is
	// worth a shader variant.
	"uniform mat4 u_shadowMatrix3D;\n"
	// [0] x one texel of the map in uv, y depth bias, z strength, w the world-space
	// size of one texel; [1] x the index of the casting light in the arrays above.
	"uniform vec4 u_shadowParams3D[2];\n"
	"SAMPLER2DSHADOW(s_shadowMap3D, 4);\n"
	"#endif\n"
	"\n"
	"#define PI 3.14159265359\n"
	"\n"
	"float DistributionGGX(float NdotH, float roughness)\n"
	"{\n"
	"    float a = roughness * roughness;\n"
	"    float a2 = a * a;\n"
	"    float d = NdotH * NdotH * (a2 - 1.0) + 1.0;\n"
	"    return a2 / max(PI * d * d, 0.0001);\n"
	"}\n"
	"\n"
	"float GeometrySmith(float NdotV, float NdotL, float roughness)\n"
	"{\n"
	"    float r = roughness + 1.0;\n"
	"    float k = (r * r) / 8.0;\n"
	"    float gv = NdotV / (NdotV * (1.0 - k) + k);\n"
	"    float gl = NdotL / (NdotL * (1.0 - k) + k);\n"
	"    return gv * gl;\n"
	"}\n"
	"\n"
	// A direction to the equirectangular texel it names. The inverse of the mapping
	// Scene3D::ProjectIrradiance samples with, so the two agree about which part of
	// the image lies in which direction.
	"vec2 DirectionToEquirect(vec3 d)\n"
	"{\n"
	"    float phi = atan2(d.x, -d.z);\n"
	"    float theta = acos(clamp(d.y, -1.0, 1.0));\n"
	"    return vec2(phi / (2.0 * PI) + 0.5, theta / PI);\n"
	"}\n"
	"\n"
	// The irradiance arriving at a surface facing n, evaluated from the nine
	// coefficients. Cheaper than a single texture fetch and smooth by construction,
	// which is what makes it the right representation for the diffuse term.
	"vec3 EvaluateIrradiance(vec3 n)\n"
	"{\n"
	"    vec3 result = u_irradiance3D[0].rgb * 0.282095;\n"
	"    result += u_irradiance3D[1].rgb * (0.488603 * n.y);\n"
	"    result += u_irradiance3D[2].rgb * (0.488603 * n.z);\n"
	"    result += u_irradiance3D[3].rgb * (0.488603 * n.x);\n"
	"    result += u_irradiance3D[4].rgb * (1.092548 * n.x * n.y);\n"
	"    result += u_irradiance3D[5].rgb * (1.092548 * n.y * n.z);\n"
	"    result += u_irradiance3D[6].rgb * (0.315392 * (3.0 * n.z * n.z - 1.0));\n"
	"    result += u_irradiance3D[7].rgb * (1.092548 * n.x * n.z);\n"
	"    result += u_irradiance3D[8].rgb * (0.546274 * (n.x * n.x - n.y * n.y));\n"
	"    return max(result, vec3_splat(0.0));\n"
	"}\n"
	"\n"
	"#if SHADOWS_ENABLED\n"
	// How much of the light at this point survives the shadow map: 1 fully lit, 0
	// fully shadowed, and the values between are the map's edges softened.
	"float ShadowFactor(vec3 worldPos, vec3 N, vec3 L)\n"
	"{\n"
	"    if (u_shadowParams3D[0].z <= 0.0) { return 1.0; }\n"
	"\n"
	// Normal-offset bias: the point is moved off the surface along its own normal
	// before being looked up, by about a texel's worth of world space. A depth bias
	// alone has to grow with the surface's slope to stop it shadowing itself, and by
	// the time it is large enough for a grazing surface it has detached the contact
	// shadows of every other one. Moving along the normal costs nothing at normal
	// incidence and does the most exactly where the slope needs it.
	"    float slope = clamp(1.0 - dot(N, L), 0.0, 1.0);\n"
	"    vec3 offsetPos = worldPos + N * (u_shadowParams3D[0].w * (0.5 + slope * 2.0));\n"
	"\n"
	"    vec4 coord = mul(u_shadowMatrix3D, vec4(offsetPos, 1.0));\n"
	"\n"
	"    if (coord.w <= 0.0) { return 1.0; }\n"
	"\n"
	"    vec3 uvz = coord.xyz / coord.w;\n"
	"\n"
	// Anything the map does not cover is treated as lit. The map spans a box around
	// what the camera is looking at, so the alternative -- clamping to the edge --
	// would smear the outermost texel across the rest of the world as a shadow.
	"    if (uvz.x < 0.0 || uvz.x > 1.0 || uvz.y < 0.0 || uvz.y > 1.0 || uvz.z > 1.0) { return 1.0; }\n"
	"\n"
	"    float depth = uvz.z - u_shadowParams3D[0].y;\n"
	"    float texel = u_shadowParams3D[0].x;\n"
	"\n"
	// Nine comparisons on a 3x3 grid. Each one is a hardware depth compare, and on
	// most GPUs it is bilinear across four texels, so this is a good deal smoother
	// than nine samples of the depth would be.
	"    float sum = 0.0;\n"
	"\n"
	"    for (int y = -1; y <= 1; ++y)\n"
	"    {\n"
	"        for (int x = -1; x <= 1; ++x)\n"
	"        {\n"
	"            vec2 offset = vec2(float(x), float(y)) * texel;\n"
	"            sum += shadow2D(s_shadowMap3D, vec3(uvz.xy + offset, depth));\n"
	"        }\n"
	"    }\n"
	"\n"
	// Strength scales how dark the shadow gets rather than how much of it there is,
	// so half strength is a shadow half as deep and not a half-dissolved one.
	"    return mix(1.0, sum / 9.0, u_shadowParams3D[0].z);\n"
	"}\n"
	"#endif\n"
	"\n"
	"void main()\n"
	"{\n"
	"    vec3 N = normalize(v_normal);\n"
	"    vec3 V = normalize(u_cameraPos3D.xyz - v_worldPos);\n"
	"    float NdotV = max(dot(N, V), 0.0001);\n"
	"\n"
	"    vec3 albedo = u_albedo3D.rgb;\n"
	"    float roughness = u_material3D.x;\n"
	"    float metallic = u_material3D.y;\n"
	"    float alpha = u_albedo3D.a;\n"
	"\n"
	// Where a map is present it multiplies the factor, which is what glTF says
	// and what makes a factor of white mean "the texture as authored". The flag
	// selects rather than branches: a mip-sampled fetch inside a conditional has
	// undefined derivatives, and the fetch of the 1x1 white dummy is free.
	"    vec4 albedoTexel = texture2D(s_albedo3D, v_texcoord0);\n"
	"    albedo *= mix(vec3_splat(1.0), albedoTexel.rgb, u_mapFlags3D.x);\n"
	"    alpha *= mix(1.0, albedoTexel.a, u_mapFlags3D.x);\n"
	"\n"
	// glTF packs occlusion, roughness and metallic into one texture's red, green
	// and blue. Occlusion is ignored for now; there is no ambient term detailed
	// enough for it to modulate.
	"    vec4 ormTexel = texture2D(s_metallicRoughness3D, v_texcoord0);\n"
	"    roughness *= mix(1.0, ormTexel.g, u_mapFlags3D.y);\n"
	"    metallic *= mix(1.0, ormTexel.b, u_mapFlags3D.y);\n"
	"\n"
	// Clamped after the multiply: a roughness of zero is a mathematical point
	// reflection that aliases into fireflies, and a map's black pixels would
	// otherwise reintroduce exactly what Material3D::SetRoughness guards against.
	"    roughness = clamp(roughness, 0.04, 1.0);\n"
	"    metallic = clamp(metallic, 0.0, 1.0);\n"
	"\n"
	// Dielectrics reflect about 4% head on; metals reflect their own colour and
	// have no diffuse term at all. That is the whole of what "metallic" means.
	"    vec3 F0 = mix(vec3_splat(0.04), albedo, metallic);\n"
	"    vec3 diffuseColor = albedo * (1.0 - metallic);\n"
	"\n"
	"    vec3 emissive = u_emissive3D.rgb;\n"
	"    emissive *= mix(vec3_splat(1.0), texture2D(s_emissive3D, v_texcoord0).rgb, u_mapFlags3D.z);\n"
	"\n"
	// The environment replaces the flat ambient term rather than adding to it: both
	// stand for the same thing -- light arriving from everywhere that is not one of
	// the lights -- and counting it twice makes a scene with an environment
	// noticeably washed out.
	"    vec3 ambient = u_ambient3D.rgb;\n"
	"    vec3 envSpecular = vec3_splat(0.0);\n"
	"\n"
	"    if (u_mapFlags3D.w > 0.5)\n"
	"    {\n"
	"        ambient = EvaluateIrradiance(N) * u_envParams3D.x;\n"
	"\n"
	// A single sample down the reflection vector, blended towards the diffuse
	// irradiance as the surface roughens. A correct specular probe needs the
	// environment prefiltered into mip levels per roughness; this stands in for it,
	// and is right at both ends -- a mirror reflects the image, a fully rough
	// surface reflects the average.
	"        vec3 R = reflect(-V, N);\n"
	"        vec3 sharp = texture2D(s_environment3D, DirectionToEquirect(R)).rgb;\n"
	"        envSpecular = mix(sharp, ambient, roughness) * u_envParams3D.x;\n"
	"    }\n"
	"\n"
	"    vec3 color = ambient * albedo + emissive;\n"
	"\n"
	// The environment's specular, with the same Fresnel weighting the lights get,
	// so a grazing view reflects more of it than a head-on one.
	"    if (u_mapFlags3D.w > 0.5)\n"
	"    {\n"
	"        vec3 envFresnel = F0 + (vec3_splat(1.0) - F0) * pow(1.0 - NdotV, 5.0);\n"
	"        color += envSpecular * envFresnel;\n"
	"    }\n"
	"\n"
	"    int count = int(u_material3D.w);\n"
	"\n"
	"    for (int i = 0; i < MAX_LIGHTS; ++i)\n"
	"    {\n"
	"        if (i >= count) { break; }\n"
	"\n"
	"        float kind = u_lightPosition3D[i].w;\n"
	"        vec3 L;\n"
	"        float attenuation = 1.0;\n"
	"\n"
	"        if (kind < 0.5)\n"
	"        {\n"
	// Directional: the light shines along its direction, so the vector towards
	// it is the reverse.
	"            L = -normalize(u_lightDirection3D[i].xyz);\n"
	"        }\n"
	"        else\n"
	"        {\n"
	"            vec3 toLight = u_lightPosition3D[i].xyz - v_worldPos;\n"
	"            float dist = length(toLight);\n"
	"            L = toLight / max(dist, 0.0001);\n"
	"\n"
	"            float range = u_lightParams3D[i].y;\n"
	// Inverse-square falloff, windowed so that it reaches exactly zero at the
	// range rather than trailing off forever and making the range meaningless.
	"            float window = clamp(1.0 - pow(dist / max(range, 0.0001), 4.0), 0.0, 1.0);\n"
	"            attenuation = (window * window) / max(dist * dist, 0.0001);\n"
	"\n"
	"            if (kind > 1.5)\n"
	"            {\n"
	"                float cosAngle = dot(-L, normalize(u_lightDirection3D[i].xyz));\n"
	"                float inner = u_lightParams3D[i].z;\n"
	"                float outer = u_lightParams3D[i].w;\n"
	"                attenuation *= clamp((cosAngle - outer) / max(inner - outer, 0.0001), 0.0, 1.0);\n"
	"            }\n"
	"        }\n"
	"\n"
	"        float NdotL = max(dot(N, L), 0.0);\n"
	"        if (NdotL <= 0.0 || attenuation <= 0.0) { continue; }\n"
	"\n"
	"        vec3 H = normalize(V + L);\n"
	"        float NdotH = max(dot(N, H), 0.0);\n"
	"        float VdotH = max(dot(V, H), 0.0);\n"
	"\n"
	"        float D = DistributionGGX(NdotH, roughness);\n"
	"        float G = GeometrySmith(NdotV, NdotL, roughness);\n"
	"        vec3 F = F0 + (vec3_splat(1.0) - F0) * pow(1.0 - VdotH, 5.0);\n"
	"\n"
	"        vec3 specular = (D * G * F) / max(4.0 * NdotV * NdotL, 0.0001);\n"
	// Energy conservation: what the surface reflects specularly is not
	// available to scatter diffusely.
	"        vec3 kD = (vec3_splat(1.0) - F);\n"
	"\n"
	"        vec3 radiance = u_lightColor3D[i].rgb * u_lightParams3D[i].x * attenuation;\n"
	"\n"
	"#if SHADOWS_ENABLED\n"
	// One light casts, so only that one is darkened. The others keep lighting what
	// its shadow covers, which is what makes a fill light read as a fill light.
	"        if (float(i) == u_shadowParams3D[1].x)\n"
	"        {\n"
	"            radiance *= ShadowFactor(v_worldPos, N, L);\n"
	"        }\n"
	"#endif\n"
	"\n"
	"        color += (kD * diffuseColor / PI + specular) * radiance * NdotL;\n"
	"    }\n"
	"\n"
	// Reinhard, then gamma. Without a tone map the specular highlights of a
	// bright light clip to flat white patches; without the gamma the whole
	// image reads too dark, since the lighting above is done in linear space
	// and the framebuffer is not.
	"    color = color / (color + vec3_splat(1.0));\n"
	"    color = pow(color, vec3_splat(1.0 / 2.2));\n"
	"\n"
	"    gl_FragColor = vec4(color, alpha * u_material3D.z);\n"
	"}\n";

// ----------------------------------------------------------------------------

// Kept in step with the same function in Rtt_BgfxProgram.cpp. Duplicated rather
// than shared because that one is a file-local detail of how Corona's 2D
// kernels are built, and giving it external linkage would make it part of that
// module's interface for the sake of six lines.
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
			return "120";
	}
}

// The shading fragment stage as it is actually compiled.
//
// bgfx's preprocessor wants the $input line first, so the define that selects the
// shadow path is inserted between it and the body rather than prepended.
static std::string
ShadingFragmentSource( bool shadows )
{
	std::string source( "$input v_worldPos, v_normal, v_texcoord0\n\n" );

	source += shadows ? "#define SHADOWS_ENABLED 1\n\n" : "#define SHADOWS_ENABLED 0\n\n";
	source += kFragmentShader;

	return source;
}

static bgfx::ShaderHandle
CompileStage( const char* source, BgfxShaderCompiler::Stage stage, const char* debugName )
{
	std::vector< U8 > blob;
	std::string messages;

	const bool compiled = BgfxShaderCompiler::Compile(
		  source
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
		Rtt_LogException( "%s: %s\n", debugName, messages.c_str() );
	}

	if ( !compiled || blob.empty() )
	{
		return BGFX_INVALID_HANDLE;
	}

	return bgfx::createShader( bgfx::copy( &blob[0], U32( blob.size() ) ) );
}

// ----------------------------------------------------------------------------

Bgfx3DPipeline::Bgfx3DPipeline()
:	fInitialized( false ),
	fFailed( false ),
	fSkinnedTried( false ),
	fShadowsSupported( false ),
	fShadowFramePrepared( false ),
	fShadowTried( false ),
	fShadowSkinnedTried( false ),
	fShadowSize( 1024 ),
	fShadowFrameBuffer( BGFX_INVALID_HANDLE ),
	fShadowTexture( BGFX_INVALID_HANDLE ),
	fShadowProgram( BGFX_INVALID_HANDLE ),
	fShadowSkinnedProgram( BGFX_INVALID_HANDLE ),
	fShadowSampler( BGFX_INVALID_HANDLE ),
	fShadowMatrixUniform( BGFX_INVALID_HANDLE ),
	fShadowParamsUniform( BGFX_INVALID_HANDLE ),
	fProgram( BGFX_INVALID_HANDLE ),
	fSkinnedProgram( BGFX_INVALID_HANDLE ),
	fBonesUniform( BGFX_INVALID_HANDLE ),
	fWhiteTexture( BGFX_INVALID_HANDLE ),
	fAlbedoSampler( BGFX_INVALID_HANDLE ),
	fMetallicRoughnessSampler( BGFX_INVALID_HANDLE ),
	fEmissiveSampler( BGFX_INVALID_HANDLE ),
	fMapFlagsUniform( BGFX_INVALID_HANDLE ),
	fEnvironmentSampler( BGFX_INVALID_HANDLE ),
	fIrradianceUniform( BGFX_INVALID_HANDLE ),
	fEnvParamsUniform( BGFX_INVALID_HANDLE ),
	fModelUniform( BGFX_INVALID_HANDLE ),
	fViewProjUniform( BGFX_INVALID_HANDLE ),
	fCameraUniform( BGFX_INVALID_HANDLE ),
	fAlbedoUniform( BGFX_INVALID_HANDLE ),
	fMaterialUniform( BGFX_INVALID_HANDLE ),
	fEmissiveUniform( BGFX_INVALID_HANDLE ),
	fAmbientUniform( BGFX_INVALID_HANDLE ),
	fLightPositionUniform( BGFX_INVALID_HANDLE ),
	fLightDirectionUniform( BGFX_INVALID_HANDLE ),
	fLightColorUniform( BGFX_INVALID_HANDLE ),
	fLightParamsUniform( BGFX_INVALID_HANDLE )
{
}

Bgfx3DPipeline::~Bgfx3DPipeline()
{
	Release();
}

static void
DestroyUniform( bgfx::UniformHandle& handle )
{
	if ( bgfx::isValid( handle ) )
	{
		bgfx::destroy( handle );
		handle = BGFX_INVALID_HANDLE;
	}
}

void
Bgfx3DPipeline::Release()
{
	for ( size_t i = 0, iMax = fMeshBuffers.size(); i < iMax; ++i )
	{
		if ( bgfx::isValid( fMeshBuffers[i].fVertexBuffer ) )
		{
			bgfx::destroy( fMeshBuffers[i].fVertexBuffer );
		}

		if ( bgfx::isValid( fMeshBuffers[i].fSkinBuffer ) )
		{
			bgfx::destroy( fMeshBuffers[i].fSkinBuffer );
		}

		if ( bgfx::isValid( fMeshBuffers[i].fIndexBuffer ) )
		{
			bgfx::destroy( fMeshBuffers[i].fIndexBuffer );
		}
	}

	fMeshBuffers.clear();

	for ( size_t i = 0, iMax = fTextures.size(); i < iMax; ++i )
	{
		if ( bgfx::isValid( fTextures[i] ) )
		{
			bgfx::destroy( fTextures[i] );
		}
	}

	fTextures.clear();

	if ( bgfx::isValid( fWhiteTexture ) )
	{
		bgfx::destroy( fWhiteTexture );
		fWhiteTexture = BGFX_INVALID_HANDLE;
	}

	for ( size_t i = 0, iMax = fEffects.size(); i < iMax; ++i )
	{
		if ( bgfx::isValid( fEffects[i].fProgram ) )
		{
			bgfx::destroy( fEffects[i].fProgram );
		}

		if ( bgfx::isValid( fEffects[i].fSkinnedProgram ) )
		{
			bgfx::destroy( fEffects[i].fSkinnedProgram );
		}

		for ( size_t u = 0, uMax = fEffects[i].fUniforms.size(); u < uMax; ++u )
		{
			DestroyUniform( fEffects[i].fUniforms[u] );
		}
	}

	fEffects.clear();

	if ( bgfx::isValid( fProgram ) )
	{
		bgfx::destroy( fProgram );
		fProgram = BGFX_INVALID_HANDLE;
	}

	if ( bgfx::isValid( fSkinnedProgram ) )
	{
		bgfx::destroy( fSkinnedProgram );
		fSkinnedProgram = BGFX_INVALID_HANDLE;
	}

	// Unlike fFailed below, this is cleared: the skinned variant is compiled on
	// demand, so leaving it set would mean a project that reopens never gets its
	// skinned program back even though the static one is rebuilt.
	fSkinnedTried = false;

	// The framebuffer owns the texture it was created with, so destroying it is
	// what releases both; the handle is only cleared here.
	if ( bgfx::isValid( fShadowFrameBuffer ) )
	{
		bgfx::destroy( fShadowFrameBuffer );
		fShadowFrameBuffer = BGFX_INVALID_HANDLE;
	}

	fShadowTexture = BGFX_INVALID_HANDLE;

	if ( bgfx::isValid( fShadowProgram ) )
	{
		bgfx::destroy( fShadowProgram );
		fShadowProgram = BGFX_INVALID_HANDLE;
	}

	if ( bgfx::isValid( fShadowSkinnedProgram ) )
	{
		bgfx::destroy( fShadowSkinnedProgram );
		fShadowSkinnedProgram = BGFX_INVALID_HANDLE;
	}

	DestroyUniform( fShadowSampler );
	DestroyUniform( fShadowMatrixUniform );
	DestroyUniform( fShadowParamsUniform );

	// Cleared for the same reason fSkinnedTried is: these are built on demand, and
	// a project that reopens has to be able to build them again.
	fShadowTried = false;
	fShadowSkinnedTried = false;
	fShadowsSupported = false;
	fShadowFramePrepared = false;

	DestroyUniform( fBonesUniform );
	DestroyUniform( fAlbedoSampler );
	DestroyUniform( fMetallicRoughnessSampler );
	DestroyUniform( fEmissiveSampler );
	DestroyUniform( fMapFlagsUniform );
	DestroyUniform( fEnvironmentSampler );
	DestroyUniform( fIrradianceUniform );
	DestroyUniform( fEnvParamsUniform );
	DestroyUniform( fModelUniform );
	DestroyUniform( fViewProjUniform );
	DestroyUniform( fCameraUniform );
	DestroyUniform( fAlbedoUniform );
	DestroyUniform( fMaterialUniform );
	DestroyUniform( fEmissiveUniform );
	DestroyUniform( fAmbientUniform );
	DestroyUniform( fLightPositionUniform );
	DestroyUniform( fLightDirectionUniform );
	DestroyUniform( fLightColorUniform );
	DestroyUniform( fLightParamsUniform );

	fInitialized = false;

	// Deliberately not clearing fFailed: a release happens when bgfx is being
	// shut down, and re-running a compile that has already been shown not to
	// work would only log the same errors again.
}

bool
Bgfx3DPipeline::EnsureInitialized()
{
	if ( fInitialized )
	{
		return true;
	}

	if ( fFailed )
	{
		return false;
	}

	// Decided before the shading program is compiled, because it is compiled around
	// the answer. A backend without hardware depth comparison gets a shader with no
	// shadow path at all rather than one that samples a map it cannot read.
	{
		const bgfx::Caps* caps = bgfx::getCaps();

		fShadowsSupported = caps != NULL
			&& 0 != ( caps->supported & BGFX_CAPS_TEXTURE_COMPARE_LEQUAL )
			&& bgfx::isTextureValid( 0, false, 1, bgfx::TextureFormat::D16, BGFX_TEXTURE_RT );

		if ( !fShadowsSupported )
		{
			Rtt_LogException( "WARNING: this graphics backend cannot sample depth textures, so 3D lights will not cast shadows\n" );
		}
	}

	// Before the shading program, because that program is compiled around whether
	// this worked: a failure here has to leave a shader with no shadow path rather
	// than one whose sampler could never be bound.
	if ( fShadowsSupported && !EnsureShadowResources() )
	{
		fShadowsSupported = false;
	}

	bgfx::ShaderHandle vertex = CompileStage( kVertexShader, BgfxShaderCompiler::kVertex, "render3d.vs" );
	bgfx::ShaderHandle fragment = CompileStage( ShadingFragmentSource( fShadowsSupported ).c_str(), BgfxShaderCompiler::kFragment, "render3d.fs" );

	if ( !bgfx::isValid( vertex ) || !bgfx::isValid( fragment ) )
	{
		if ( bgfx::isValid( vertex ) )
		{
			bgfx::destroy( vertex );
		}

		if ( bgfx::isValid( fragment ) )
		{
			bgfx::destroy( fragment );
		}

		Rtt_LogException( "ERROR: the 3D shaders could not be compiled; render.* objects will not be drawn\n" );

		fFailed = true;

		return false;
	}

	// Destroying the shaders with the program is what bgfx expects: the program
	// holds its own references, so the handles here are done with once it is
	// made.
	fProgram = bgfx::createProgram( vertex, fragment, true );

	if ( !bgfx::isValid( fProgram ) )
	{
		fFailed = true;

		return false;
	}

	fLayout
		.begin()
		.add( bgfx::Attrib::Position, 3, bgfx::AttribType::Float )
		.add( bgfx::Attrib::Normal, 3, bgfx::AttribType::Float )
		.add( bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float )
		.end();

	// The second stream, declared here even when nothing skinned is ever drawn:
	// a layout is a description, not a resource, so building it costs nothing
	// and having it always valid saves the mesh upload path a second check.
	fSkinLayout
		.begin()
		.add( bgfx::Attrib::Indices, 4, bgfx::AttribType::Float )
		.add( bgfx::Attrib::Weight, 4, bgfx::AttribType::Float )
		.end();

	fModelUniform = bgfx::createUniform( "u_model3D", bgfx::UniformType::Mat4 );
	fViewProjUniform = bgfx::createUniform( "u_viewProj3D", bgfx::UniformType::Mat4 );
	fCameraUniform = bgfx::createUniform( "u_cameraPos3D", bgfx::UniformType::Vec4 );
	fAlbedoUniform = bgfx::createUniform( "u_albedo3D", bgfx::UniformType::Vec4 );
	fMaterialUniform = bgfx::createUniform( "u_material3D", bgfx::UniformType::Vec4 );
	fEmissiveUniform = bgfx::createUniform( "u_emissive3D", bgfx::UniformType::Vec4 );
	fAmbientUniform = bgfx::createUniform( "u_ambient3D", bgfx::UniformType::Vec4 );
	fMapFlagsUniform = bgfx::createUniform( "u_mapFlags3D", bgfx::UniformType::Vec4 );

	// Sampler uniforms name a texture unit rather than carrying a value, which is
	// why they are created as Sampler and set with setTexture below.
	fAlbedoSampler = bgfx::createUniform( "s_albedo3D", bgfx::UniformType::Sampler );
	fMetallicRoughnessSampler = bgfx::createUniform( "s_metallicRoughness3D", bgfx::UniformType::Sampler );
	fEmissiveSampler = bgfx::createUniform( "s_emissive3D", bgfx::UniformType::Sampler );
	fEnvironmentSampler = bgfx::createUniform( "s_environment3D", bgfx::UniformType::Sampler );

	if ( fShadowsSupported )
	{
		// Created alongside the program that declares them rather than with the map
		// itself: bgfx wants every sampler a program declares bound on every submit,
		// so these have to exist from the first 3D draw whether or not a light is
		// casting yet.
		fShadowSampler = bgfx::createUniform( "s_shadowMap3D", bgfx::UniformType::Sampler );
		fShadowMatrixUniform = bgfx::createUniform( "u_shadowMatrix3D", bgfx::UniformType::Mat4 );
		fShadowParamsUniform = bgfx::createUniform( "u_shadowParams3D", bgfx::UniformType::Vec4, 2 );
	}

	fIrradianceUniform = bgfx::createUniform( "u_irradiance3D", bgfx::UniformType::Vec4, 9 );
	fEnvParamsUniform = bgfx::createUniform( "u_envParams3D", bgfx::UniformType::Vec4 );

	fLightPositionUniform = bgfx::createUniform( "u_lightPosition3D", bgfx::UniformType::Vec4, kMaxDraw3DLights );
	fLightDirectionUniform = bgfx::createUniform( "u_lightDirection3D", bgfx::UniformType::Vec4, kMaxDraw3DLights );
	fLightColorUniform = bgfx::createUniform( "u_lightColor3D", bgfx::UniformType::Vec4, kMaxDraw3DLights );
	fLightParamsUniform = bgfx::createUniform( "u_lightParams3D", bgfx::UniformType::Vec4, kMaxDraw3DLights );

	fInitialized = true;

	return true;
}

bool
Bgfx3DPipeline::EnsureSkinnedProgram()
{
	if ( fSkinnedTried )
	{
		return bgfx::isValid( fSkinnedProgram );
	}

	fSkinnedTried = true;

	std::vector< U8 > blob;
	std::string messages;

	const bool compiled = BgfxShaderCompiler::Compile(
		  kSkinnedVertexShader
		, kSkinnedVaryingDef
		, BgfxShaderCompiler::kVertex
		, ProfileForRenderer( 'v' )
		, LUMIN_BGFX_SHADER_INCLUDE_DIR
		, "render3d_skinned.vs"
		, blob
		, messages
		);

	if ( !messages.empty() )
	{
		Rtt_LogException( "render3d_skinned.vs: %s\n", messages.c_str() );
	}

	if ( !compiled || blob.empty() )
	{
		Rtt_LogException( "ERROR: the skinned 3D vertex shader could not be compiled; animated models will draw in their bind pose\n" );

		return false;
	}

	bgfx::ShaderHandle vertex = bgfx::createShader( bgfx::copy( &blob[0], U32( blob.size() ) ) );

	// The fragment stage is recompiled rather than shared with fProgram: bgfx
	// destroys a program's shaders with it when created that way, so handing the
	// same fragment handle to two programs would have the second one holding a
	// destroyed shader as soon as the first went away.
	bgfx::ShaderHandle fragment = CompileStage( ShadingFragmentSource( fShadowsSupported ).c_str(), BgfxShaderCompiler::kFragment, "render3d_skinned.fs" );

	if ( !bgfx::isValid( vertex ) || !bgfx::isValid( fragment ) )
	{
		if ( bgfx::isValid( vertex ) )
		{
			bgfx::destroy( vertex );
		}

		if ( bgfx::isValid( fragment ) )
		{
			bgfx::destroy( fragment );
		}

		return false;
	}

	fSkinnedProgram = bgfx::createProgram( vertex, fragment, true );

	if ( !bgfx::isValid( fSkinnedProgram ) )
	{
		return false;
	}

	// Three vec4 registers per bone, matching how the palette is packed and how
	// the shader above indexes it.
	fBonesUniform = bgfx::createUniform( "u_bones3D", bgfx::UniformType::Vec4, kMaxDraw3DBones * 3 );

	return true;
}

// ----------------------------------------------------------------------------

bool
Bgfx3DPipeline::EnsureShadowResources()
{
	if ( fShadowTried )
	{
		return bgfx::isValid( fShadowFrameBuffer ) && bgfx::isValid( fShadowProgram );
	}

	fShadowTried = true;

	// Depth only, and read back through a comparison sampler: the shading pass never
	// wants the stored depth itself, only the answer to "is this point behind what
	// the light can see", and asking the sampler that is both cheaper and filtered.
	fShadowTexture = bgfx::createTexture2D(
		  fShadowSize, fShadowSize, false, 1, bgfx::TextureFormat::D16
		, BGFX_TEXTURE_RT | BGFX_SAMPLER_COMPARE_LEQUAL
			| BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP
		);

	if ( !bgfx::isValid( fShadowTexture ) )
	{
		Rtt_LogException( "ERROR: the shadow map could not be created; 3D lights will not cast shadows\n" );

		return false;
	}

	// true: the framebuffer takes ownership, so the texture goes away with it.
	fShadowFrameBuffer = bgfx::createFrameBuffer( 1, &fShadowTexture, true );

	if ( !bgfx::isValid( fShadowFrameBuffer ) )
	{
		bgfx::destroy( fShadowTexture );
		fShadowTexture = BGFX_INVALID_HANDLE;

		Rtt_LogException( "ERROR: the shadow map could not be created; 3D lights will not cast shadows\n" );

		return false;
	}

	bgfx::ShaderHandle vertex = CompileStage( kShadowVertexShader, BgfxShaderCompiler::kVertex, "shadow3d.vs" );
	bgfx::ShaderHandle fragment = CompileStage( kShadowFragmentShader, BgfxShaderCompiler::kFragment, "shadow3d.fs" );

	if ( !bgfx::isValid( vertex ) || !bgfx::isValid( fragment ) )
	{
		if ( bgfx::isValid( vertex ) )
		{
			bgfx::destroy( vertex );
		}

		if ( bgfx::isValid( fragment ) )
		{
			bgfx::destroy( fragment );
		}

		Rtt_LogException( "ERROR: the shadow pass shaders could not be compiled; 3D lights will not cast shadows\n" );

		return false;
	}

	fShadowProgram = bgfx::createProgram( vertex, fragment, true );

	return bgfx::isValid( fShadowProgram );
}

bool
Bgfx3DPipeline::EnsureShadowSkinnedProgram()
{
	if ( fShadowSkinnedTried )
	{
		return bgfx::isValid( fShadowSkinnedProgram );
	}

	fShadowSkinnedTried = true;

	std::vector< U8 > blob;
	std::string messages;

	// Compiled against the skinned varying definition rather than through
	// CompileStage, which only knows the static one -- the bone attributes have to
	// be declared or the stage cannot name them.
	const bool compiled = BgfxShaderCompiler::Compile(
		  kShadowSkinnedVertexShader
		, kSkinnedVaryingDef
		, BgfxShaderCompiler::kVertex
		, ProfileForRenderer( 'v' )
		, LUMIN_BGFX_SHADER_INCLUDE_DIR
		, "shadow3d_skinned.vs"
		, blob
		, messages
		);

	if ( !messages.empty() )
	{
		Rtt_LogException( "shadow3d_skinned.vs: %s\n", messages.c_str() );
	}

	if ( !compiled || blob.empty() )
	{
		Rtt_LogException( "ERROR: the skinned shadow shader could not be compiled; animated models will cast their bind pose\n" );

		return false;
	}

	bgfx::ShaderHandle vertex = bgfx::createShader( bgfx::copy( &blob[0], U32( blob.size() ) ) );

	// Recompiled rather than shared with fShadowProgram, for the reason given in
	// EnsureSkinnedProgram: a program destroys the shaders it was created from.
	bgfx::ShaderHandle fragment = CompileStage( kShadowFragmentShader, BgfxShaderCompiler::kFragment, "shadow3d_skinned.fs" );

	if ( !bgfx::isValid( vertex ) || !bgfx::isValid( fragment ) )
	{
		if ( bgfx::isValid( vertex ) )
		{
			bgfx::destroy( vertex );
		}

		if ( bgfx::isValid( fragment ) )
		{
			bgfx::destroy( fragment );
		}

		return false;
	}

	fShadowSkinnedProgram = bgfx::createProgram( vertex, fragment, true );

	return bgfx::isValid( fShadowSkinnedProgram );
}

// ----------------------------------------------------------------------------

const Bgfx3DPipeline::MeshBuffers*
Bgfx3DPipeline::GetMeshBuffers( const Mesh3D& mesh )
{
	const U32 id = mesh.GetGpuId();

	if ( id != Mesh3D::kInvalidGpuId && id < fMeshBuffers.size() )
	{
		return &fMeshBuffers[id];
	}

	const std::vector< Vertex3D >& vertices = mesh.GetVertices();
	const std::vector< U32 >& indices = mesh.GetIndices();

	if ( vertices.empty() || indices.empty() )
	{
		return NULL;
	}

	MeshBuffers buffers;
	buffers.fSkinBuffer = BGFX_INVALID_HANDLE;

	// bgfx::copy rather than makeRef: the mesh's vectors belong to Lua's
	// lifetime, and makeRef would have bgfx read them on its render thread
	// after the mesh could already have been collected.
	buffers.fVertexBuffer = bgfx::createVertexBuffer(
		  bgfx::copy( &vertices[0], U32( vertices.size() * sizeof( Vertex3D ) ) )
		, fLayout
		);

	// BGFX_BUFFER_INDEX32 because Mesh3D indices are 32-bit for every mesh, not
	// only the large ones -- see the note on Mesh3D::GetIndices.
	buffers.fIndexBuffer = bgfx::createIndexBuffer(
		  bgfx::copy( &indices[0], U32( indices.size() * sizeof( U32 ) ) )
		, BGFX_BUFFER_INDEX32
		);

	buffers.fIndexCount = U32( indices.size() );

	if ( mesh.IsSkinned() )
	{
		const std::vector< SkinVertex3D >& skin = mesh.GetSkin();

		buffers.fSkinBuffer = bgfx::createVertexBuffer(
			  bgfx::copy( &skin[0], U32( skin.size() * sizeof( SkinVertex3D ) ) )
			, fSkinLayout
			);
	}

	if ( !bgfx::isValid( buffers.fVertexBuffer ) || !bgfx::isValid( buffers.fIndexBuffer ) )
	{
		if ( bgfx::isValid( buffers.fVertexBuffer ) )
		{
			bgfx::destroy( buffers.fVertexBuffer );
		}

		if ( bgfx::isValid( buffers.fIndexBuffer ) )
		{
			bgfx::destroy( buffers.fIndexBuffer );
		}

		if ( bgfx::isValid( buffers.fSkinBuffer ) )
		{
			bgfx::destroy( buffers.fSkinBuffer );
		}

		return NULL;
	}

	const_cast< Mesh3D& >( mesh ).SetGpuId( U32( fMeshBuffers.size() ) );

	fMeshBuffers.push_back( buffers );

	return &fMeshBuffers.back();
}

// ----------------------------------------------------------------------------

bgfx::TextureHandle
Bgfx3DPipeline::EnsureWhiteTexture()
{
	if ( bgfx::isValid( fWhiteTexture ) )
	{
		return fWhiteTexture;
	}

	static const U8 kWhite[4] = { 0xFF, 0xFF, 0xFF, 0xFF };

	fWhiteTexture = bgfx::createTexture2D(
		  1, 1, false, 1, bgfx::TextureFormat::RGBA8
		, BGFX_SAMPLER_NONE
		, bgfx::copy( kWhite, sizeof( kWhite ) )
		);

	return fWhiteTexture;
}

// The declarations an effect's fragment snippet is compiled against.
//
// Everything the standard shader has, so a snippet can light a surface the usual
// way, sample the material's maps, or ignore all of it -- plus whatever uniforms
// the effect declared. Prepended rather than documented as "write these
// yourself": an effect that had to redeclare the light arrays would break the
// moment kMaxDraw3DLights changed.
static const char kEffectFragmentPrologue[] =
	"$input v_worldPos, v_normal, v_texcoord0\n"
	"\n"
	"#include <bgfx_shader.sh>\n"
	"\n"
	"#define MAX_LIGHTS 8\n"
	"\n"
	"uniform vec4 u_albedo3D;\n"
	"uniform vec4 u_material3D;\n"
	"uniform vec4 u_emissive3D;\n"
	"uniform vec4 u_ambient3D;\n"
	"uniform vec4 u_cameraPos3D;\n"
	"uniform vec4 u_lightPosition3D[MAX_LIGHTS];\n"
	"uniform vec4 u_lightDirection3D[MAX_LIGHTS];\n"
	"uniform vec4 u_lightColor3D[MAX_LIGHTS];\n"
	"uniform vec4 u_lightParams3D[MAX_LIGHTS];\n"
	"uniform vec4 u_mapFlags3D;\n"
	"uniform vec4 u_irradiance3D[9];\n"
	"uniform vec4 u_envParams3D;\n"
	"\n"
	"SAMPLER2D(s_albedo3D, 0);\n"
	"SAMPLER2D(s_metallicRoughness3D, 1);\n"
	"SAMPLER2D(s_emissive3D, 2);\n"
	"SAMPLER2D(s_environment3D, 3);\n"
	"\n";

bgfx::TextureHandle
Bgfx3DPipeline::GetTexture( const Texture3D& texture )
{
	const U32 id = texture.GetGpuId();

	if ( id != Texture3D::kInvalidGpuId && id < fTextures.size() )
	{
		return fTextures[id];
	}

	const std::vector< U8 >& pixels = texture.GetPixels();

	if ( pixels.empty() || texture.GetWidth() == 0 || texture.GetHeight() == 0 )
	{
		return BGFX_INVALID_HANDLE;
	}

	// sRGB for colour images so the sampler linearises on read. The shader lights
	// in linear space and gamma-encodes at the end, so handing it gamma-encoded
	// texels without this would light the wrong numbers and read washed out.
	U64 flags = BGFX_TEXTURE_NONE;

	if ( texture.IsColorData() )
	{
		flags |= BGFX_TEXTURE_SRGB;
	}

	bgfx::TextureHandle handle = bgfx::createTexture2D(
		  (U16) texture.GetWidth()
		, (U16) texture.GetHeight()
		, false
		, 1
		, bgfx::TextureFormat::RGBA8
		, flags
		, bgfx::copy( &pixels[0], U32( pixels.size() ) )
		);

	if ( !bgfx::isValid( handle ) )
	{
		return BGFX_INVALID_HANDLE;
	}

	const_cast< Texture3D& >( texture ).SetGpuId( U32( fTextures.size() ) );

	fTextures.push_back( handle );

	// bgfx::copy above took its own copy, so the decoded image has no further
	// reader. Releasing it here is what keeps a model's textures from costing
	// twice their size for the life of the project.
	const_cast< Texture3D& >( texture ).DiscardPixels();

	return handle;
}

// ----------------------------------------------------------------------------

const Bgfx3DPipeline::EffectPrograms*
Bgfx3DPipeline::GetEffectPrograms( const ShaderEffect3D& effect, bool skinned )
{
	U32 id = effect.GetGpuId();

	if ( id == ShaderEffect3D::kInvalidGpuId || id >= fEffects.size() )
	{
		EffectPrograms fresh;

		fresh.fProgram = BGFX_INVALID_HANDLE;
		fresh.fSkinnedProgram = BGFX_INVALID_HANDLE;
		fresh.fTried = false;
		fresh.fSkinnedTried = false;

		id = U32( fEffects.size() );

		fEffects.push_back( fresh );

		const_cast< ShaderEffect3D& >( effect ).SetGpuId( id );
	}

	EffectPrograms& programs = fEffects[id];

	const bool tried = skinned ? programs.fSkinnedTried : programs.fTried;
	bgfx::ProgramHandle& handle = skinned ? programs.fSkinnedProgram : programs.fProgram;

	if ( tried )
	{
		return bgfx::isValid( handle ) ? &programs : NULL;
	}

	if ( skinned )
	{
		programs.fSkinnedTried = true;
	}
	else
	{
		programs.fTried = true;
	}

	// The snippet, compiled behind the standard declarations. Its own uniforms are
	// declared between the two, so a snippet sees them without having to spell out
	// their types.
	std::string source( kEffectFragmentPrologue );

	const std::vector< ShaderEffect3D::Uniform >& uniforms = effect.GetUniforms();

	for ( size_t i = 0, iMax = uniforms.size(); i < iMax; ++i )
	{
		source += "uniform vec4 ";
		source += uniforms[i].fName;
		source += ";\n";
	}

	source += "\n";
	source += effect.GetFragmentSource();

	const std::string debugName = std::string( "effect_" ) + effect.GetName() + ( skinned ? "_skinned.fs" : ".fs" );

	std::vector< U8 > blob;
	std::string messages;

	const bool compiled = BgfxShaderCompiler::Compile(
		  source.c_str()
		, skinned ? kSkinnedVaryingDef : kVaryingDef
		, BgfxShaderCompiler::kFragment
		, ProfileForRenderer( 'f' )
		, LUMIN_BGFX_SHADER_INCLUDE_DIR
		, debugName.c_str()
		, blob
		, messages
		);

	if ( !messages.empty() )
	{
		// The compiler's own diagnostics, verbatim: line numbers in them refer to
		// the assembled source, which is the prologue plus the snippet, so they are
		// off by the prologue's length. Reporting them anyway beats reporting
		// nothing, and the message text names the construct at fault.
		Rtt_LogException( "render.newShaderEffect '%s': %s\n", effect.GetName().c_str(), messages.c_str() );
	}

	if ( !compiled || blob.empty() )
	{
		Rtt_LogException( "ERROR: render.newShaderEffect '%s': the fragment shader could not be compiled; objects using this effect will not be drawn\n",
			effect.GetName().c_str() );

		return NULL;
	}

	bgfx::ShaderHandle fragment = bgfx::createShader( bgfx::copy( &blob[0], U32( blob.size() ) ) );

	// The vertex stage is the pipeline's, recompiled for this program rather than
	// shared: bgfx destroys a program's shaders with it, so handing one handle to
	// two programs would leave the second holding a destroyed shader.
	bgfx::ShaderHandle vertex = BGFX_INVALID_HANDLE;

	if ( skinned )
	{
		std::vector< U8 > vertexBlob;
		std::string vertexMessages;

		if ( BgfxShaderCompiler::Compile(
			  kSkinnedVertexShader
			, kSkinnedVaryingDef
			, BgfxShaderCompiler::kVertex
			, ProfileForRenderer( 'v' )
			, LUMIN_BGFX_SHADER_INCLUDE_DIR
			, "effect_skinned.vs"
			, vertexBlob
			, vertexMessages )
			&& !vertexBlob.empty() )
		{
			vertex = bgfx::createShader( bgfx::copy( &vertexBlob[0], U32( vertexBlob.size() ) ) );
		}
	}
	else
	{
		vertex = CompileStage( kVertexShader, BgfxShaderCompiler::kVertex, "effect.vs" );
	}

	if ( !bgfx::isValid( vertex ) || !bgfx::isValid( fragment ) )
	{
		if ( bgfx::isValid( vertex ) )
		{
			bgfx::destroy( vertex );
		}

		if ( bgfx::isValid( fragment ) )
		{
			bgfx::destroy( fragment );
		}

		return NULL;
	}

	handle = bgfx::createProgram( vertex, fragment, true );

	if ( !bgfx::isValid( handle ) )
	{
		return NULL;
	}

	// Created once for the effect, not once per program: the two programs declare
	// the same uniforms, and a bgfx uniform is identified by its name.
	if ( programs.fUniforms.empty() )
	{
		for ( size_t i = 0, iMax = uniforms.size(); i < iMax; ++i )
		{
			programs.fUniforms.push_back(
				bgfx::createUniform( uniforms[i].fName.c_str(), bgfx::UniformType::Vec4 ) );
		}
	}

	return &programs;
}

// ----------------------------------------------------------------------------

static void
MultiplyMatrix( const float* a, const float* b, float* out )
{
	// out = b * a, in the sense that a is applied first -- the convention the
	// rest of this file uses, where the view matrix is applied before the
	// projection.
	for ( int col = 0; col < 4; ++col )
	{
		for ( int row = 0; row < 4; ++row )
		{
			float sum = 0.0f;

			for ( int k = 0; k < 4; ++k )
			{
				sum += b[k * 4 + row] * a[col * 4 + k];
			}

			out[col * 4 + row] = sum;
		}
	}
}

// The matrix that turns the light's clip space into a lookup into the shadow map:
// the xy half-scaled and biased into 0..1 texture coordinates, and the depth
// mapped into 0..1 as well.
//
// Both halves depend on the backend -- which corner a texture's origin is in, and
// whether clip space depth runs -1..1 -- so this is folded into the matrix on the
// CPU and the shader is left backend-agnostic.
static void
TexelBiasMatrix( bool originBottomLeft, bool homogeneousDepth, float* out )
{
	for ( int i = 0; i < 16; ++i )
	{
		out[i] = 0.0f;
	}

	out[0] = 0.5f;

	// Flipped where the API's textures start at the top left, since clip space
	// counts y upwards either way.
	//
	// Getting this backwards does not look like a mirrored image, which is what
	// makes it worth naming: the map's depth varies across it, so a mirrored
	// lookup agrees with the truth only along the axis it mirrors about -- the row
	// through the centre, which is the point the camera is aimed at -- and reads
	// too near on one side of it. That half of the world falls into shadow, bounded
	// by a straight line through whatever the camera is looking at, and no amount
	// of depth bias touches it.
	out[5] = originBottomLeft ? 0.5f : -0.5f;
	out[12] = 0.5f;
	out[13] = 0.5f;

	out[10] = homogeneousDepth ? 0.5f : 1.0f;
	out[14] = homogeneousDepth ? 0.5f : 0.0f;

	out[15] = 1.0f;
}

void
Bgfx3DPipeline::BeginFrame()
{
	fShadowFramePrepared = false;
}

void
Bgfx3DPipeline::Draw( const Draw3DCommand& command, bgfx::ViewId view, bgfx::ViewId shadowView, U32 surfaceWidth, U32 surfaceHeight )
{
	if ( command.fMesh == NULL || command.fCamera == NULL )
	{
		return;
	}

	if ( !EnsureInitialized() )
	{
		return;
	}

	const MeshBuffers* buffers = GetMeshBuffers( *command.fMesh );

	if ( buffers == NULL )
	{
		return;
	}

	float viewMatrix[16];
	float projectionMatrix[16];

	command.fCamera->GetViewMatrix( viewMatrix );
	command.fCamera->GetProjectionMatrix(
		  projectionMatrix
		, surfaceWidth
		, surfaceHeight
		, bgfx::getCaps()->homogeneousDepth
		);

	float viewProjection[16];
	MultiplyMatrix( viewMatrix, projectionMatrix, viewProjection );

	// Skinning needs all three of a skinned mesh, a posed palette for it, and a
	// program that compiled. Any one missing draws the bind pose through the
	// static program, which is a still model rather than no model.
	//
	// Resolved here rather than beside the submit because the shadow pass below has
	// to pose its caster exactly the way the shading pass poses it.
	const bool skinned = command.fMesh->IsSkinned()
		&& command.fBoneTransforms != NULL
		&& command.fBoneCount > 0
		&& bgfx::isValid( buffers->fSkinBuffer )
		&& EnsureSkinnedProgram();

	const U32 boneCount = command.fBoneCount < kMaxDraw3DBones ? command.fBoneCount : kMaxDraw3DBones;

	// ----------------------------------------------------------------------------
	// The shadow pass.

	const bool shadows = fShadowsSupported
		&& command.fCastsShadows
		&& command.fShadowLight != NULL
		&& command.fShadowStrength > 0.0f
		&& bgfx::isValid( fShadowFrameBuffer );

	float shadowMatrix[16];
	float shadowLookup[16];

	if ( shadows )
	{
		const bool homogeneousDepth = bgfx::getCaps()->homogeneousDepth;

		command.fShadowLight->GetShadowMatrix( command.fShadowCentre, homogeneousDepth, shadowMatrix );

		float bias[16];
		TexelBiasMatrix( bgfx::getCaps()->originBottomLeft, homogeneousDepth, bias );

		MultiplyMatrix( shadowMatrix, bias, shadowLookup );

		// Once a frame, on the first object that needs it. A view keeps its settings
		// until something changes them, but the clear has to be reissued every frame
		// or the map would accumulate the depths of every frame since the first.
		if ( !fShadowFramePrepared )
		{
			fShadowFramePrepared = true;

			bgfx::setViewFrameBuffer( shadowView, fShadowFrameBuffer );
			bgfx::setViewRect( shadowView, 0, 0, fShadowSize, fShadowSize );

			// Far, so that a texel nothing was drawn into shadows nothing.
			bgfx::setViewClear( shadowView, BGFX_CLEAR_DEPTH, 0x00000000, 1.0f, 0 );

			// The transform is identity: the light's matrix travels as a uniform, the
			// same way the camera's does, so a view left over from 2D content cannot
			// put the casters somewhere else.
			bgfx::setViewTransform( shadowView, NULL, NULL );
		}

		// What a caster contributes is its depth, and a surface that is see-through
		// does not stop light -- so the things that blend are left out rather than
		// casting as though they were solid. An inverted-hull outline is left out for
		// a different reason: it is a shell slightly larger than the model it
		// surrounds, and its shadow would be the model's own, one size too big.
		const bool isOutline = command.fEffect != NULL
			&& command.fEffect->GetCullMode() == ShaderEffect3D::kCullFront;

		const bool casts = !isOutline
			&& !command.fIsTranslucent
			&& command.fAlpha >= 1.0f
			&& command.fAlbedo[3] >= 1.0f;

		if ( casts && ( skinned ? EnsureShadowSkinnedProgram() : true ) )
		{
			bgfx::setUniform( fModelUniform, command.fTransform );
			bgfx::setUniform( fShadowMatrixUniform, shadowMatrix );

			bgfx::setVertexBuffer( 0, buffers->fVertexBuffer );

			if ( skinned )
			{
				bgfx::setVertexBuffer( 1, buffers->fSkinBuffer );
				bgfx::setUniform( fBonesUniform, command.fBoneTransforms, boneCount * 3 );
			}

			bgfx::setIndexBuffer( buffers->fIndexBuffer );

			// Depth only, and no culling: a mesh with open sides -- which is most of
			// what a double-sided material is for -- would drop half its depth if the
			// pass culled, and the normal-offset bias in the shader is what keeps a
			// surface from shadowing itself instead.
			bgfx::setState( BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS );

			bgfx::submit( shadowView, skinned ? fShadowSkinnedProgram : fShadowProgram );
		}
	}

	bgfx::setUniform( fModelUniform, command.fTransform );
	bgfx::setUniform( fViewProjUniform, viewProjection );

	float cameraPosition[4];
	command.fCamera->GetPosition( cameraPosition[0], cameraPosition[1], cameraPosition[2] );
	cameraPosition[3] = 1.0f;
	bgfx::setUniform( fCameraUniform, cameraPosition );

	bgfx::setUniform( fAlbedoUniform, command.fAlbedo );

	const float material[4] =
	{
		command.fRoughness,
		command.fMetallic,
		command.fAlpha,
		(float) command.fLightCount
	};
	bgfx::setUniform( fMaterialUniform, material );

	const float emissive[4] = { command.fEmissive[0], command.fEmissive[1], command.fEmissive[2], 0.0f };
	bgfx::setUniform( fEmissiveUniform, emissive );

	const float ambient[4] = { command.fAmbient[0], command.fAmbient[1], command.fAmbient[2], 0.0f };
	bgfx::setUniform( fAmbientUniform, ambient );

	// Every sampler is bound on every draw, with the white dummy standing in
	// where the material has no map, because bgfx requires a program's samplers
	// all be set and leaving one over from the previous draw would texture this
	// object with the last one's image.
	const Texture3D* const maps[3] =
	{
		command.fAlbedoMap,
		command.fMetallicRoughnessMap,
		command.fEmissiveMap
	};

	bgfx::UniformHandle samplers[3] =
	{
		fAlbedoSampler,
		fMetallicRoughnessSampler,
		fEmissiveSampler
	};

	float mapFlags[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

	for ( int i = 0; i < 3; ++i )
	{
		bgfx::TextureHandle handle = BGFX_INVALID_HANDLE;

		if ( maps[i] != NULL )
		{
			handle = GetTexture( *maps[i] );
		}

		if ( bgfx::isValid( handle ) )
		{
			mapFlags[i] = 1.0f;
		}
		else
		{
			handle = EnsureWhiteTexture();
		}

		bgfx::setTexture( (U8) i, samplers[i], handle );
	}

	// The environment, on the stage after the material's maps, with the white dummy
	// standing in when there is none -- for the same reason those do.
	{
		bgfx::TextureHandle handle = BGFX_INVALID_HANDLE;

		if ( command.fEnvironmentMap != NULL )
		{
			handle = GetTexture( *command.fEnvironmentMap );
		}

		if ( bgfx::isValid( handle ) && command.fIrradiance != NULL )
		{
			mapFlags[3] = 1.0f;

			bgfx::setUniform( fIrradianceUniform, command.fIrradiance, 9 );
		}
		else
		{
			handle = EnsureWhiteTexture();

			// Zeroed rather than left stale: the shader only reads these when the
			// flag is set, but a uniform array holding the previous scene's
			// environment is the kind of thing that surfaces later as a bug.
			static const float kNoIrradiance[9 * 4] = { 0.0f };

			bgfx::setUniform( fIrradianceUniform, kNoIrradiance, 9 );
		}

		bgfx::setTexture( 3, fEnvironmentSampler, handle );
	}

	const float envParams[4] = { command.fEnvironmentIntensity, 0.0f, 0.0f, 0.0f };
	bgfx::setUniform( fEnvParamsUniform, envParams );

	bgfx::setUniform( fMapFlagsUniform, mapFlags );

	// The whole array is sent even when fewer lights are in use: bgfx uploads a
	// uniform array as one block, and leaving the tail stale would let a light
	// that has since been removed keep lighting the scene whenever the shader's
	// loop bound moved back up.
	float lightPosition[kMaxDraw3DLights * 4];
	float lightDirection[kMaxDraw3DLights * 4];
	float lightColor[kMaxDraw3DLights * 4];
	float lightParams[kMaxDraw3DLights * 4];

	memset( lightPosition, 0, sizeof( lightPosition ) );
	memset( lightDirection, 0, sizeof( lightDirection ) );
	memset( lightColor, 0, sizeof( lightColor ) );
	memset( lightParams, 0, sizeof( lightParams ) );

	for ( U32 i = 0; i < command.fLightCount && i < kMaxDraw3DLights; ++i )
	{
		const Draw3DLight& light = command.fLights[i];

		lightPosition[i * 4 + 0] = light.fPosition[0];
		lightPosition[i * 4 + 1] = light.fPosition[1];
		lightPosition[i * 4 + 2] = light.fPosition[2];
		lightPosition[i * 4 + 3] = (float) light.fKind;

		lightDirection[i * 4 + 0] = light.fDirection[0];
		lightDirection[i * 4 + 1] = light.fDirection[1];
		lightDirection[i * 4 + 2] = light.fDirection[2];

		lightColor[i * 4 + 0] = light.fColor[0];
		lightColor[i * 4 + 1] = light.fColor[1];
		lightColor[i * 4 + 2] = light.fColor[2];

		lightParams[i * 4 + 0] = light.fIntensity;
		lightParams[i * 4 + 1] = light.fRange;
		lightParams[i * 4 + 2] = light.fInnerCosine;
		lightParams[i * 4 + 3] = light.fOuterCosine;
	}

	bgfx::setUniform( fLightPositionUniform, lightPosition, kMaxDraw3DLights );
	bgfx::setUniform( fLightDirectionUniform, lightDirection, kMaxDraw3DLights );
	bgfx::setUniform( fLightColorUniform, lightColor, kMaxDraw3DLights );
	bgfx::setUniform( fLightParamsUniform, lightParams, kMaxDraw3DLights );

	// The shadow map, on the stage after the environment. Bound on every draw once
	// the pipeline has one, whether or not anything is casting: bgfx requires every
	// sampler the program declares to be set, and a strength of zero in the params
	// below is what tells the shader to ignore it.
	if ( fShadowsSupported )
	{
		bgfx::setTexture( 4, fShadowSampler, bgfx::getTexture( fShadowFrameBuffer ) );

		float shadowParams[8] = { 0.0f, 0.0f, 0.0f, 0.0f, (float) kMaxDraw3DLights, 0.0f, 0.0f, 0.0f };

		if ( shadows )
		{
			bgfx::setUniform( fShadowMatrixUniform, shadowLookup );

			shadowParams[0] = 1.0f / (float) fShadowSize;
			shadowParams[1] = command.fShadowBias;
			shadowParams[2] = command.fShadowStrength;

			// The world-space width of one texel: the map's box is the light's extent
			// across -- half either side of the centre, as GetShadowMatrix builds it --
			// spread over the map's resolution. This is what the shader's normal offset
			// is measured in, so a bigger extent, and so a coarser map, offsets
			// further, which is exactly what it needs to do.
			shadowParams[3] = command.fShadowLight->GetShadowExtent() / (float) fShadowSize;

			shadowParams[4] = (float) command.fShadowLightIndex;
		}
		else
		{
			// A matrix is sent even with the strength at zero: the shader's early out
			// makes it unread, but a uniform left holding the previous frame's value
			// is the kind of thing that surfaces later as a bug.
			static const float kIdentity[16] =
			{
				1.0f, 0.0f, 0.0f, 0.0f,
				0.0f, 1.0f, 0.0f, 0.0f,
				0.0f, 0.0f, 1.0f, 0.0f,
				0.0f, 0.0f, 0.0f, 1.0f
			};

			bgfx::setUniform( fShadowMatrixUniform, kIdentity );
		}

		bgfx::setUniform( fShadowParamsUniform, shadowParams, 2 );
	}

	bgfx::setVertexBuffer( 0, buffers->fVertexBuffer );

	if ( skinned )
	{
		bgfx::setVertexBuffer( 1, buffers->fSkinBuffer );

		// The count is in vec4 registers, which is three per bone.
		bgfx::setUniform( fBonesUniform, command.fBoneTransforms, boneCount * 3 );
	}

	bgfx::setIndexBuffer( buffers->fIndexBuffer );

	// Depth test and write are what make a 3D object solid, and are exactly
	// what the 2D pipeline leaves off.
	U64 state = BGFX_STATE_WRITE_RGB
		| BGFX_STATE_WRITE_A
		| BGFX_STATE_WRITE_Z
		| BGFX_STATE_DEPTH_TEST_LESS
		| BGFX_STATE_MSAA;

	// An effect's own state wins over the material's: an effect exists to say how
	// something is drawn, so it has the last word on culling, blending and depth.
	const ShaderEffect3D* effect = command.fEffect;

	if ( effect != NULL )
	{
		switch ( effect->GetCullMode() )
		{
			case ShaderEffect3D::kCullBack:
				state |= BGFX_STATE_CULL_CCW;
				break;

			// What an inverted-hull outline needs: the shell's near side is
			// discarded and only the part standing past the silhouette remains.
			case ShaderEffect3D::kCullFront:
				state |= BGFX_STATE_CULL_CW;
				break;

			default:
				break;
		}

		if ( effect->IsTranslucent() || command.fAlpha < 1.0f )
		{
			state |= BGFX_STATE_BLEND_FUNC( BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_INV_SRC_ALPHA );
		}

		if ( !effect->GetWritesDepth() )
		{
			state &= ~BGFX_STATE_WRITE_Z;
		}

		bgfx::setState( state );

		const EffectPrograms* programs = GetEffectPrograms( *effect, skinned );

		if ( programs == NULL )
		{
			// Nothing drawn, and the reason is already in the log. Discarding the
			// state bgfx has been given for this submit keeps it from leaking into
			// whatever is submitted next.
			bgfx::discard();

			return;
		}

		const std::vector< ShaderEffect3D::Uniform >& uniforms = effect->GetUniforms();

		for ( size_t i = 0, iMax = uniforms.size(); i < iMax && i < programs->fUniforms.size(); ++i )
		{
			bgfx::setUniform( programs->fUniforms[i], uniforms[i].fValue );
		}

		bgfx::submit( view, skinned ? programs->fSkinnedProgram : programs->fProgram );

		return;
	}

	// Culling follows the material, not the pipeline. A closed solid saves half
	// its fragment work by culling; a sheet with no thickness -- cloth, a leaf, a
	// skirt -- shows holes wherever it turns away if it is culled, which is why
	// glTF records this per material and why it is honoured here.
	//
	// CCW rather than CW, which is what discards the faces pointing away from the
	// eye once the projection below has had its say. The other way round discards
	// the faces pointing towards it, and the result is subtle enough to hide: a
	// closed convex shape drawn inside-out still reads as that shape under soft
	// lighting. What gives it away is an inverted-hull outline -- a black,
	// single-sided, reverse-wound copy of a mesh -- which stops being a silhouette
	// at the edges and becomes a shell covering the model entirely.
	if ( !command.fIsDoubleSided )
	{
		state |= BGFX_STATE_CULL_CCW;
	}

	// Blending only where the material asks for it, or where the display list has
	// faded the object. Blending an opaque surface makes the result depend on the
	// order things happened to be drawn in, which shows up as seams on a model
	// whose parts overlap.
	if ( command.fIsTranslucent || command.fAlpha < 1.0f || command.fAlbedo[3] < 1.0f )
	{
		state |= BGFX_STATE_BLEND_FUNC( BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_INV_SRC_ALPHA );
	}

	bgfx::setState( state );

	bgfx::submit( view, skinned ? fSkinnedProgram : fProgram );
}

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------
