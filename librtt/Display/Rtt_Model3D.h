//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Corona game engine.
// For overview and more information on licensing please refer to README.md
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////

#ifndef _Rtt_Model3D_H__
#define _Rtt_Model3D_H__

#include "Core/Rtt_Types.h"

#include <string>
#include <vector>

// ----------------------------------------------------------------------------

namespace Rtt
{

class Material3D;
class Mesh3D;

// ----------------------------------------------------------------------------

// One drawable piece of a loaded model.
//
// A model is a list of these rather than a single mesh because that is what the
// formats contain: a glTF mesh is a set of primitives, one per material, and an
// OBJ file is a stream of faces broken up by `usemtl`. Keeping them separate is
// also what makes model:getMesh("head_mesh") able to name one of them.
struct ModelPart
{
	// Both retained by the model.
	Mesh3D* fMesh;

	// The material the file described, or NULL if it described none, in which
	// case the object's own material -- or the default surface -- applies.
	Material3D* fMaterial;

	std::string fName;

	// The node this part hangs off, whose accumulated transform places it. -1
	// for a file with no node hierarchy at all, which is every OBJ.
	int fNode;

	// The skin posing this part, or -1 if it is rigid. A skinned part ignores
	// fNode when it is drawn: in glTF a skinned mesh is positioned entirely by
	// its joints, and applying the node transform as well would move it twice.
	int fSkin;

	// This part's own palette layout: entry i is the index, within the skin's
	// joint list, of the joint the mesh's bone index i refers to. Empty for a
	// rigid part.
	//
	// Parts get compact palettes of their own rather than sharing the skin's
	// because a draw can only carry kMaxDraw3DBones of them, while a rig can have
	// far more joints than that. What saves it is that a part uses very few: on a
	// 134-joint character, a boot is influenced by ten joints and a tooth by one.
	// So the mesh's bone indices are rewritten at load time to index this list,
	// and only these joints are ever sent.
	std::vector< int > fJointMap;
};

// ----------------------------------------------------------------------------

// One node of the model's transform hierarchy.
//
// Stored with rotation as a quaternion rather than as the Euler angles Object3D
// uses, because animation interpolates rotations and Euler angles cannot be
// interpolated without gimbal artefacts. It is also the form glTF stores.
//
// Nodes are reordered at load time so that a parent always comes before its
// children, which lets a pose be resolved in one forward pass.
struct ModelNode
{
	std::string fName;

	// -1 for a root.
	int fParent;

	float fTranslation[3];

	// x, y, z, w.
	float fRotation[4];

	float fScale[3];
};

// ----------------------------------------------------------------------------

// The joints posing a skinned part, and the matrices taking the mesh's bind
// pose into each joint's space.
struct ModelSkin
{
	// Node indices, in the order the mesh's bone attributes reference them.
	std::vector< int > fJoints;

	// 16 floats per joint, column major, parallel to fJoints.
	std::vector< float > fInverseBind;
};

// ----------------------------------------------------------------------------

// One animated property of one node over time.
struct ModelAnimationChannel
{
	enum Path
	{
		kTranslation,
		kRotation,
		kScale
	};

	int fNode;
	U32 fPath;

	// Seconds, ascending.
	std::vector< float > fTimes;

	// Three floats per key for translation and scale, four for rotation.
	std::vector< float > fValues;

	// STEP interpolation holds each key until the next instead of blending
	// towards it, which is how a file asks for a hard cut.
	bool fStep;
};

struct ModelAnimation
{
	std::string fName;

	// Seconds. The largest time any of its channels reaches, which is what a
	// loop wraps at and what a one-shot ends at.
	float fDuration;

	std::vector< ModelAnimationChannel > fChannels;
};

// ----------------------------------------------------------------------------

// A model file, loaded.
//
// Immutable and shared, like Mesh3D and for the same reasons: two instances of
// the same model are two display objects pointing at one Model3D, so the file is
// parsed and its geometry uploaded once. Everything that differs between
// instances -- where it stands, which animation it is playing, which material
// has been swapped onto which part -- lives on the object, not here.
class Model3D
{
	public:
		typedef Model3D Self;

	public:
		Model3D();
		~Model3D();

		void Retain() { ++fRefCount; }
		void Release() { if ( --fRefCount == 0 ) { delete this; } }

	public:
		// Loads from an absolute path, dispatching on the extension: .gltf and
		// .glb through cgltf, .obj through the loader below.
		//
		// Returns NULL on any failure, having written the reason to the log:
		// there are many ways a model file can be unusable and a caller can do
		// nothing useful with any of them beyond reporting it, which this has
		// already done in more detail than a return code could carry.
		//
		// The result has one reference, which the caller owns.
		static Model3D* NewFromFile( const char* path );

	public:
		const std::vector< ModelPart >& GetParts() const { return fParts; }
		const std::vector< ModelNode >& GetNodes() const { return fNodes; }
		const std::vector< ModelSkin >& GetSkins() const { return fSkins; }
		const std::vector< ModelAnimation >& GetAnimations() const { return fAnimations; }

		// -1 when nothing by that name exists. Names come from the file and are
		// matched exactly; two parts can share one, in which case the first is
		// found, which is the same rule glTF viewers use.
		int FindPart( const char* name ) const;
		int FindAnimation( const char* name ) const;

		bool IsSkinned() const { return !fSkins.empty(); }

	public:
		// Fills out with one 4x4 per node, column major, in node order:
		// each node's transform accumulated down from its root.
		//
		// animation is an index into GetAnimations(), or -1 for the bind pose --
		// the transforms the file's nodes carry with no animation applied, which
		// is what a model that is not playing anything should look like.
		//
		// time is in seconds and is used as given; wrapping it for a looping
		// animation, or clamping it for a one-shot, is the caller's business
		// because only the caller knows which it is.
		void GetPose( int animation, float time, std::vector< float >& out ) const;

		// Fills out with the given part's bone palette and returns how many bones
		// were written -- never more than maxBones.
		//
		// Each bone is kDraw3DBoneStride floats: the top three rows of the
		// transform taking a bind-pose vertex to where the pose puts it. The
		// fourth row is dropped because it is always (0,0,0,1); see the note on
		// kMaxDraw3DBones for why that is worth doing. out must have room for
		// maxBones * kDraw3DBoneStride floats.
		//
		// pose is what GetPose produced. Passing a pose from a different model
		// is not checked for and would produce nonsense.
		U32 GetPartPalette( int part, const std::vector< float >& pose, float* out, U32 maxBones ) const;

	private:
		// The two format loaders. Each fills the model in place and returns
		// false, having logged why, if the file cannot be used.
		bool LoadGltf( const char* path );
		bool LoadObj( const char* path );

		std::vector< ModelPart > fParts;
		std::vector< ModelNode > fNodes;
		std::vector< ModelSkin > fSkins;
		std::vector< ModelAnimation > fAnimations;

		int fRefCount;
};

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------

#endif // _Rtt_Model3D_H__
