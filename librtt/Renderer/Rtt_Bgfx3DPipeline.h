//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Corona game engine.
// For overview and more information on licensing please refer to README.md
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////

#ifndef _Rtt_Bgfx3DPipeline_H__
#define _Rtt_Bgfx3DPipeline_H__

#include "Renderer/Rtt_Draw3D.h"

#include <bgfx/bgfx.h>

#include <vector>

// ----------------------------------------------------------------------------

namespace Rtt
{

class Light3D;
class Mesh3D;
class Texture3D;

// ----------------------------------------------------------------------------

// The bgfx side of render.*: the vertex layout, the PBR program, and the GPU
// buffers meshes are uploaded into.
//
// Owned by BgfxRenderer rather than by a command buffer, for the reason given
// on BgfxDrawState: Corona alternates between two command buffers, and a
// program or a buffer cache held by one would be missing from the other on
// every second frame.
//
// Everything is built the first time a 3D object is drawn, not at startup. The
// shaders are compiled at run time, which is not free, and most projects are
// 2D and would pay for a pipeline they never use.
class Bgfx3DPipeline
{
	public:
		typedef Bgfx3DPipeline Self;

	public:
		Bgfx3DPipeline();
		~Bgfx3DPipeline();

		// Releases every bgfx handle held. Called before bgfx::shutdown(),
		// since opening a project tears bgfx down and brings it back up, and
		// handles kept across that would name someone else's resources.
		void Release();

		// Submits one object into the given view. The view's transform is left
		// alone: the camera matrices are folded into a uniform of the
		// pipeline's own instead, so that 3D objects can share a view -- and
		// therefore a submission order -- with the 2D content around them. A
		// view can only hold one transform, and 2D needs it to stay the
		// orthographic one Corona set.
		// shadowView is the id reserved for the depth pass, which bgfx has been told
		// to run before every other view (see BgfxRenderer::SetupShadowViewOrder), so
		// a caster submitted here is in the map by the time anything reads it.
		void Draw( const Draw3DCommand& command, bgfx::ViewId view, bgfx::ViewId shadowView, U32 surfaceWidth, U32 surfaceHeight );

		// Clears what only holds for one frame: whether the shadow view has had its
		// target and clear set. Called once per frame by the command buffer.
		void BeginFrame();

	private:
		// Returns false if the program could not be compiled, in which case
		// nothing should be drawn and the failure has already been logged.
		bool EnsureInitialized();

		// Compiles the skinned variant, on first sight of a skinned mesh rather
		// than alongside the static program: a project with no animated model
		// should not pay a shader compile for one. Returns false, once, if it
		// cannot be built, and skinned meshes then draw in their bind pose.
		bool EnsureSkinnedProgram();

		// The vertex and index buffers for a mesh, created on first sight and
		// kept for as long as the mesh lives.
		struct MeshBuffers
		{
			bgfx::VertexBufferHandle fVertexBuffer;

			// The bone index and weight stream, valid only for a skinned mesh.
			bgfx::VertexBufferHandle fSkinBuffer;

			bgfx::IndexBufferHandle fIndexBuffer;
			U32 fIndexCount;
		};

		// Returns NULL if the mesh has no geometry or its buffers could not be
		// created.
		const MeshBuffers* GetMeshBuffers( const Mesh3D& mesh );

		// The GPU texture for an image, uploaded on first sight and kept, with the
		// same id-stamped cache the meshes use. Returns an invalid handle if the
		// image has no pixels left and no texture yet, which cannot happen for a
		// texture that was uploaded before its pixels were discarded.
		bgfx::TextureHandle GetTexture( const Texture3D& texture );

		// A single opaque white pixel, bound wherever a material has no map.
		//
		// bgfx requires every sampler a program declares to be set before a
		// submit, so the alternative to a dummy is a shader variant per
		// combination of maps -- eight programs to avoid one 4-byte texture. White
		// is the identity for the multiply the shader does, so binding it is the
		// same as having no map at all.
		bgfx::TextureHandle EnsureWhiteTexture();

		// The programs and uniform handles one custom effect needs.
		//
		// Two programs because the vertex stage has a skinned variant, and an
		// effect set on a skinned model needs it: they share the effect's fragment
		// stage and differ only in how the vertex got where it is. Each is built
		// on the first draw that calls for it, so an effect used only on static
		// geometry never compiles the skinned one.
		struct EffectPrograms
		{
			bgfx::ProgramHandle fProgram;
			bgfx::ProgramHandle fSkinnedProgram;

			// Parallel to the effect's own uniform list.
			std::vector< bgfx::UniformHandle > fUniforms;

			// Set once a stage has been tried, so a broken snippet reports itself
			// once rather than once per object per frame.
			bool fTried;
			bool fSkinnedTried;
		};

		// Returns NULL if the effect's program could not be built, which is the
		// signal to draw nothing: falling back to the standard shading would make
		// a broken effect look like a working one.
		const EffectPrograms* GetEffectPrograms( const ShaderEffect3D& effect, bool skinned );

		bool fInitialized;

		// Set once compilation has been tried and failed, so that a broken
		// build reports itself once rather than once per object per frame.
		bool fFailed;

		// Set once the skinned variant has been tried, whether or not it worked,
		// alongside the handle that is only valid if it did.
		bool fSkinnedTried;

		bgfx::VertexLayout fLayout;
		bgfx::VertexLayout fSkinLayout;
		bgfx::ProgramHandle fProgram;
		bgfx::ProgramHandle fSkinnedProgram;

		bgfx::UniformHandle fBonesUniform;

		bgfx::UniformHandle fModelUniform;
		bgfx::UniformHandle fViewProjUniform;
		bgfx::UniformHandle fCameraUniform;
		bgfx::UniformHandle fAlbedoUniform;
		bgfx::UniformHandle fMaterialUniform;
		bgfx::UniformHandle fEmissiveUniform;
		bgfx::UniformHandle fAmbientUniform;
		bgfx::UniformHandle fLightPositionUniform;
		bgfx::UniformHandle fLightDirectionUniform;
		bgfx::UniformHandle fLightColorUniform;
		bgfx::UniformHandle fLightParamsUniform;

		// Indexed by the id stamped into the mesh, so a lookup is an array
		// access rather than a search. Entries are never reused: a mesh that
		// goes away leaves an invalid pair behind, which costs one slot and
		// avoids a freed id being handed to a mesh that then finds another
		// mesh's buffers waiting under it.
		std::vector< MeshBuffers > fMeshBuffers;

		// Indexed by the id stamped into the image, on the same terms as
		// fMeshBuffers above.
		std::vector< bgfx::TextureHandle > fTextures;

		bgfx::TextureHandle fWhiteTexture;

		// Indexed by the id stamped into the effect, on the same terms as
		// fMeshBuffers and fTextures above.
		std::vector< EffectPrograms > fEffects;

		bgfx::UniformHandle fAlbedoSampler;
		bgfx::UniformHandle fMetallicRoughnessSampler;
		bgfx::UniformHandle fEmissiveSampler;

		// x albedo, y metallic-roughness, z emissive: 1 where a map is bound and 0
		// where the white dummy is, so the shader multiplies by the texture only
		// where there is one. Needed because white is not the identity for every
		// use -- an unset roughness map would otherwise read as fully rough.
		bgfx::UniformHandle fMapFlagsUniform;

		// The shadow map and the two programs that fill it -- static and skinned,
		// for the same reason the shading programs come in pairs.
		//
		// Built on the first frame a light asks to cast, and false-and-tried after a
		// failure so a backend without depth-compare sampling reports itself once.
		bool EnsureShadowResources();

		// The skinned caster program, on the same on-demand terms as the skinned
		// shading program.
		bool EnsureShadowSkinnedProgram();

		// Whether the backend can compare-sample a depth texture. Decided once, on
		// the first 3D draw, and the shading shader is compiled around it: without it
		// there is no shadow path in the shader at all.
		bool fShadowsSupported;

		// Set once the shadow view has been aimed at the map and told to clear, which
		// has to happen once per frame and not once per caster.
		bool fShadowFramePrepared;

		bgfx::FrameBufferHandle fShadowFrameBuffer;
		bgfx::TextureHandle fShadowTexture;
		bgfx::ProgramHandle fShadowProgram;
		bgfx::ProgramHandle fShadowSkinnedProgram;
		bgfx::UniformHandle fShadowSampler;
		bgfx::UniformHandle fShadowMatrixUniform;

		// Two vec4: [0] is x one texel of the map in uv, y depth bias, z strength,
		// w the world size of one texel; [1].x is which light casts.
		bgfx::UniformHandle fShadowParamsUniform;

		bool fShadowTried;
		bool fShadowSkinnedTried;
		U16 fShadowSize;

		bgfx::UniformHandle fEnvironmentSampler;
		bgfx::UniformHandle fIrradianceUniform;
		bgfx::UniformHandle fEnvParamsUniform;
};

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------

#endif // _Rtt_Bgfx3DPipeline_H__
