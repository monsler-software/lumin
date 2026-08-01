//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Corona game engine.
// For overview and more information on licensing please refer to README.md
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////

#ifndef _Rtt_ModelObject3D_H__
#define _Rtt_ModelObject3D_H__

#include "Display/Rtt_Object3D.h"

#include <string>
#include <vector>

// ----------------------------------------------------------------------------

namespace Rtt
{

class Model3D;

// ----------------------------------------------------------------------------

// What one part of one model instance looks like right now.
//
// The geometry and the material a file described are on the Model3D and shared
// by every instance of it; what can differ between two instances of the same
// model -- a material swapped onto one of them, a part hidden on the other --
// lives here, one of these per part per instance.
//
// Reference counted so that the handle model:getMesh() returns can hold it
// without having to be told when the model object goes away. A handle outliving
// its object then refers to a part of a model that is no longer drawn, which
// does nothing, rather than to freed memory.
class ModelPart3D
{
	public:
		typedef ModelPart3D Self;

	public:
		// Retains both the mesh and the file's material.
		ModelPart3D( Mesh3D* mesh, Material3D* fileMaterial, const char* name, int node, int skin );
		~ModelPart3D();

		void Retain() { ++fRefCount; }
		void Release() { if ( --fRefCount == 0 ) { delete this; } }

	public:
		const std::string& GetName() const { return fName; }

		Mesh3D* GetMesh() const { return fMesh; }

		int GetNode() const { return fNode; }
		int GetSkin() const { return fSkin; }

		// The material set on this part alone, or NULL if none has been -- in
		// which case what the part is drawn with is decided by ModelObject3D,
		// which knows about the model-wide material and the file's.
		Material3D* GetOverrideMaterial() const { return fOverrideMaterial; }

		// Passing NULL returns the part to whatever it would have used.
		void SetOverrideMaterial( Material3D* material );

		Material3D* GetFileMaterial() const { return fFileMaterial; }

		bool IsVisible() const { return fIsVisible; }
		void SetVisible( bool value ) { fIsVisible = value; }

	private:
		Mesh3D* fMesh;
		Material3D* fFileMaterial;
		Material3D* fOverrideMaterial;
		std::string fName;
		int fNode;
		int fSkin;
		bool fIsVisible;
		int fRefCount;
};

// ----------------------------------------------------------------------------

// A loaded model in the display list: what render.newModel() returns.
//
// An Object3D with no mesh of its own that draws several instead. Everything
// positional is inherited unchanged -- a model is moved, rotated and scaled
// exactly as a box is -- and what it adds is the parts, and the clock that poses
// them.
class ModelObject3D : public Object3D
{
	public:
		typedef Object3D Super;
		typedef ModelObject3D Self;

	public:
		// Retains the model.
		ModelObject3D( Scene3D& scene, Model3D* model );
		virtual ~ModelObject3D();

	public:
		// MDrawable
		virtual void Prepare( const Display& display );
		virtual void Draw( Renderer& renderer ) const;

	public:
		virtual const LuaProxyVTable& ProxyVTable() const;

		// Tests every visible part, posed as it is drawn, and reports the nearest
		// hit along with which part it was.
		virtual bool Raycast( const float* origin, const float* direction, Raycast3DHit& hit ) const;

		// The union of every visible part's bounds, each placed by its node, so a
		// model's collision shape fits the model rather than one piece of it.
		virtual bool GetLocalBounds( float* minimum, float* maximum ) const;

	public:
		Model3D* GetModel() const { return fModel; }

		U32 GetPartCount() const { return (U32) fParts.size(); }

		// NULL for an index out of range, and for a name no part has.
		ModelPart3D* GetPart( U32 index ) const;
		ModelPart3D* FindPart( const char* name ) const;

	public:
		// Starts a named clip from the beginning. Returns false if the model has
		// no clip by that name, which is how the Lua binding knows to warn --
		// a mistyped animation name is otherwise a model that silently stands
		// still, and finding out why means reopening the file in a viewer.
		//
		// A speed of zero holds the first frame; a negative speed runs the clip
		// backwards, and loops round to the end rather than the beginning.
		bool PlayAnimation( const char* name, bool loop, float speed );

		// Leaves the model in the pose it had reached. Stopping is not the same
		// as returning to the bind pose, which is what the model looks like with
		// no animation ever played, and which a caller that wants it can get by
		// setting the pose time and speed to zero.
		void StopAnimation();

		// Empty when nothing has been played yet.
		const char* GetAnimationName() const;

		bool IsAnimationPlaying() const { return fIsPlaying; }

		float GetAnimationTime() const { return fTime; }
		void SetAnimationTime( float seconds );

		float GetAnimationSpeed() const { return fSpeed; }
		void SetAnimationSpeed( float value ) { fSpeed = value; }

		bool IsAnimationLooping() const { return fIsLooping; }
		void SetAnimationLooping( bool value ) { fIsLooping = value; }

	private:
		// Recomputes fPose and fPalettes for the current animation and time.
		// Called when the clock has moved and once at construction, so that a
		// model that never animates is still posed by its hierarchy.
		void UpdatePose();

		Model3D* fModel;
		std::vector< ModelPart3D* > fParts;

		// One 4x4 per node of the model, as Model3D::GetPose fills it. Empty for
		// a model with no hierarchy, where every part is placed by the object's
		// own transform alone.
		std::vector< float > fPose;

		// One bone palette per part, back to back, kMaxDraw3DBones bones apart --
		// so a part's palette is found by multiplying its own index rather than by
		// searching. Parallel to fPaletteCounts, which says how many of each
		// part's bones are real, and is zero for a rigid part.
		//
		// Per part rather than per skin because each part's palette is compacted
		// down to the joints that part uses; see ModelPart::fJointMap.
		std::vector< float > fPalettes;
		std::vector< U32 > fPaletteCounts;

		int fAnimation;
		float fTime;
		float fSpeed;
		bool fIsLooping;
		bool fIsPlaying;

		// The runtime clock as of the last Prepare, in milliseconds, and whether
		// it has been read even once.
		//
		// The runtime's elapsed time rather than the wall clock, so that an
		// animation does not jump forward by however long the app spent
		// suspended. The first frame has no previous reading to subtract, and
		// treating the whole elapsed runtime as one frame's delta would drop a
		// model created ten seconds in ten seconds into its animation.
		U64 fLastTimeMs;
		bool fHasLastTime;
};

// ----------------------------------------------------------------------------

// What model:playAnimation() and model:getMesh() resolve to, on top of
// everything an Object3D already answers.
class LuaModelObject3DProxyVTable : public LuaObject3DProxyVTable
{
	public:
		typedef LuaModelObject3DProxyVTable Self;
		typedef LuaObject3DProxyVTable Super;

	public:
		static const Self& Constant();

	protected:
		LuaModelObject3DProxyVTable() {}

	public:
		virtual int ValueForKey( lua_State *L, const MLuaProxyable& object, const char key[], bool overrideRestriction = false ) const;
		virtual bool SetValueForKey( lua_State *L, MLuaProxyable& object, const char key[], int valueIndex ) const;
		virtual const LuaProxyVTable& Parent() const;
};

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------

#endif // _Rtt_ModelObject3D_H__
