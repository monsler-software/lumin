//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Corona game engine.
// For overview and more information on licensing please refer to README.md
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////

#include "Core/Rtt_Build.h"

#include "Display/Rtt_ModelObject3D.h"

#include "Display/Rtt_Camera3D.h"
#include "Display/Rtt_Display.h"
#include "Display/Rtt_LuaLibRender.h"
#include "Display/Rtt_Material3D.h"
#include "Display/Rtt_Mesh3D.h"
#include "Display/Rtt_Model3D.h"
#include "Display/Rtt_ShaderEffect3D.h"
#include "Display/Rtt_Scene3D.h"
#include "Renderer/Rtt_Renderer.h"
#include "Rtt_Lua.h"
#include "Rtt_LuaProxy.h"

#include "CoronaLua.h"

#include <cmath>
#include <cstring>

// ----------------------------------------------------------------------------

namespace Rtt
{

// ----------------------------------------------------------------------------

ModelPart3D::ModelPart3D( Mesh3D* mesh, Material3D* fileMaterial, const char* name, int node, int skin )
:	fMesh( mesh ),
	fFileMaterial( fileMaterial ),
	fOverrideMaterial( NULL ),
	fName( name != NULL ? name : "" ),
	fNode( node ),
	fSkin( skin ),
	fIsVisible( true ),
	fRefCount( 1 )
{
	if ( fMesh != NULL )
	{
		fMesh->Retain();
	}

	if ( fFileMaterial != NULL )
	{
		fFileMaterial->Retain();
	}
}

ModelPart3D::~ModelPart3D()
{
	if ( fMesh != NULL )
	{
		fMesh->Release();
	}

	if ( fFileMaterial != NULL )
	{
		fFileMaterial->Release();
	}

	if ( fOverrideMaterial != NULL )
	{
		fOverrideMaterial->Release();
	}
}

void
ModelPart3D::SetOverrideMaterial( Material3D* material )
{
	// Retained before the old one is released, so that setting the material a
	// part already has does not free it in between.
	if ( material != NULL )
	{
		material->Retain();
	}

	if ( fOverrideMaterial != NULL )
	{
		fOverrideMaterial->Release();
	}

	fOverrideMaterial = material;
}

// ----------------------------------------------------------------------------

ModelObject3D::ModelObject3D( Scene3D& scene, Model3D* model )
:	Super( scene, NULL ),
	fModel( model ),
	fAnimation( -1 ),
	fTime( 0.0f ),
	fSpeed( 1.0f ),
	fIsLooping( false ),
	fIsPlaying( false ),
	fLastTimeMs( 0 ),
	fHasLastTime( false )
{
	if ( fModel != NULL )
	{
		fModel->Retain();

		const std::vector< ModelPart >& parts = fModel->GetParts();

		fParts.reserve( parts.size() );

		for ( size_t i = 0, iMax = parts.size(); i < iMax; ++i )
		{
			fParts.push_back( new ModelPart3D(
				  parts[i].fMesh
				, parts[i].fMaterial
				, parts[i].fName.c_str()
				, parts[i].fNode
				, parts[i].fSkin
				) );
		}

		// Posed once here rather than waiting for the first Prepare: a model with
		// a hierarchy is not at the origin in its bind pose, and a frame drawn
		// before the first Prepare would show it collapsed there.
		UpdatePose();
	}
}

ModelObject3D::~ModelObject3D()
{
	for ( size_t i = 0, iMax = fParts.size(); i < iMax; ++i )
	{
		fParts[i]->Release();
	}

	if ( fModel != NULL )
	{
		fModel->Release();
	}
}

// ----------------------------------------------------------------------------

ModelPart3D*
ModelObject3D::GetPart( U32 index ) const
{
	return ( index < fParts.size() ) ? fParts[index] : NULL;
}

ModelPart3D*
ModelObject3D::FindPart( const char* name ) const
{
	if ( name == NULL )
	{
		return NULL;
	}

	for ( size_t i = 0, iMax = fParts.size(); i < iMax; ++i )
	{
		if ( fParts[i]->GetName() == name )
		{
			return fParts[i];
		}
	}

	return NULL;
}

// ----------------------------------------------------------------------------

bool
ModelObject3D::PlayAnimation( const char* name, bool loop, float speed )
{
	if ( fModel == NULL )
	{
		return false;
	}

	const int animation = fModel->FindAnimation( name );

	if ( animation < 0 )
	{
		return false;
	}

	fAnimation = animation;
	fIsLooping = loop;
	fSpeed = speed;
	fIsPlaying = true;

	// A clip running backwards starts at its end, which is the only reading of
	// "play this backwards" that shows the whole clip.
	const float duration = fModel->GetAnimations()[animation].fDuration;

	fTime = ( speed < 0.0f ) ? duration : 0.0f;

	UpdatePose();
	InvalidateDisplay();

	return true;
}

void
ModelObject3D::StopAnimation()
{
	fIsPlaying = false;
}

const char*
ModelObject3D::GetAnimationName() const
{
	if ( fModel == NULL || fAnimation < 0 || fAnimation >= (int) fModel->GetAnimations().size() )
	{
		return NULL;
	}

	return fModel->GetAnimations()[fAnimation].fName.c_str();
}

void
ModelObject3D::SetAnimationTime( float seconds )
{
	fTime = seconds;

	UpdatePose();
	InvalidateDisplay();
}

// ----------------------------------------------------------------------------

void
ModelObject3D::UpdatePose()
{
	if ( fModel == NULL )
	{
		return;
	}

	if ( !fModel->GetNodes().empty() )
	{
		fModel->GetPose( fAnimation, fTime, fPose );
	}

	// One palette per part, not per skin: a part's palette holds only the joints
	// that part uses, which is what keeps every draw inside kMaxDraw3DBones on a
	// rig with far more joints than that. Parts of one skin therefore have
	// different palettes and cannot share.
	const size_t partCount = fParts.size();

	fPaletteCounts.assign( partCount, 0 );

	if ( partCount == 0 || fModel->GetSkins().empty() )
	{
		return;
	}

	fPalettes.resize( partCount * kMaxDraw3DBones * kDraw3DBoneStride );

	for ( size_t i = 0; i < partCount; ++i )
	{
		if ( fParts[i]->GetSkin() < 0 )
		{
			continue;
		}

		fPaletteCounts[i] = fModel->GetPartPalette(
			  (int) i
			, fPose
			, &fPalettes[i * kMaxDraw3DBones * kDraw3DBoneStride]
			, kMaxDraw3DBones
			);
	}
}

void
ModelObject3D::Prepare( const Display& display )
{
	const U64 now = Rtt_AbsoluteToMilliseconds( display.GetElapsedTime() );

	if ( !fHasLastTime )
	{
		fLastTimeMs = now;
		fHasLastTime = true;
	}

	if ( fIsPlaying && fAnimation >= 0 && fModel != NULL )
	{
		const float delta = (float) ( now - fLastTimeMs ) / 1000.0f;
		const float duration = fModel->GetAnimations()[fAnimation].fDuration;

		fTime += delta * fSpeed;

		if ( duration <= 0.0f )
		{
			// A clip with no extent -- one key per channel -- is a pose, not an
			// animation. It is applied and finished with.
			fTime = 0.0f;
			fIsPlaying = false;
		}
		else if ( fIsLooping )
		{
			// fmod leaves a negative remainder for a clip running backwards, so
			// the duration is added back to land inside the clip either way.
			fTime = std::fmod( fTime, duration );

			if ( fTime < 0.0f )
			{
				fTime += duration;
			}
		}
		else if ( fTime >= duration )
		{
			fTime = duration;
			fIsPlaying = false;
		}
		else if ( fTime <= 0.0f )
		{
			fTime = 0.0f;
			fIsPlaying = false;
		}

		UpdatePose();
	}

	fLastTimeMs = now;

	Super::Prepare( display );

	// Ask for another frame while the clip runs. Prepare is otherwise only
	// reached when something marked the object dirty, and an animation advancing
	// on its own is exactly the case where nothing did.
	if ( fIsPlaying )
	{
		InvalidateDisplay();
	}
}

// ----------------------------------------------------------------------------

void
ModelObject3D::Draw( Renderer& renderer ) const
{
	if ( ! ShouldDraw() || fModel == NULL )
	{
		return;
	}

	Camera3D* camera = fScene.GetActiveCamera();

	if ( camera == NULL )
	{
		return;
	}

	// Everything below the parts loop is the same for all of them, so it is built
	// once: the lights, the ambient term and the object's own transform do not
	// vary between two parts of one model.
	Draw3DCommand command;
	memset( &command, 0, sizeof( command ) );

	command.fCamera = camera;
	command.fAlpha = (float) AlphaCumulative() / 255.0f;
	// The shadow light is resolved first so the gather can report where it landed.
	Light3D* shadowLight = fScene.GetShadowLight();

	command.fLightCount = fScene.GatherLights( command.fLights, shadowLight, &command.fShadowLightIndex );
	command.fEffect = fEffect;

	fScene.GetAmbient( command.fAmbient[0], command.fAmbient[1], command.fAmbient[2] );

	command.fEnvironmentMap = fScene.GetEnvironmentMap();
	command.fIrradiance = fScene.GetIrradiance();
	command.fEnvironmentIntensity = fScene.GetEnvironmentIntensity();

	// The shadow-casting light, if any, and the point its map is centred on: what
	// the camera is aimed at, so the shadows are sharpest where the eye is.
	command.fCastsShadows = ( shadowLight != NULL );
	command.fShadowLight = shadowLight;

	if ( shadowLight != NULL )
	{
		camera->GetLookAt( command.fShadowCentre[0], command.fShadowCentre[1], command.fShadowCentre[2] );

		command.fShadowBias = shadowLight->GetShadowBias();
		command.fShadowStrength = shadowLight->GetShadowStrength();
	}

	float objectMatrix[16];
	GetWorldMatrix( objectMatrix );

	for ( size_t i = 0, iMax = fParts.size(); i < iMax; ++i )
	{
		const ModelPart3D& part = *fParts[i];

		if ( !part.IsVisible() || part.GetMesh() == NULL )
		{
			continue;
		}

		command.fMesh = part.GetMesh();

		const int skin = part.GetSkin();
		const int node = part.GetNode();

		command.fBoneTransforms = NULL;
		command.fBoneCount = 0;

		if ( skin >= 0 && i < fPaletteCounts.size() && fPaletteCounts[i] > 0 )
		{
			command.fBoneTransforms = &fPalettes[i * kMaxDraw3DBones * kDraw3DBoneStride];
			command.fBoneCount = fPaletteCounts[i];

			// A skinned part is placed entirely by its joints: glTF requires the
			// mesh node's own transform to be ignored, and applying it as well
			// would move the part twice. The object's transform still applies --
			// that is where the model stands in the world.
			memcpy( command.fTransform, objectMatrix, sizeof( objectMatrix ) );
		}
		else if ( node >= 0 && (size_t) ( node + 1 ) * 16 <= fPose.size() )
		{
			// out = object * node, so the node's place in the model is applied
			// first and the model's place in the world second.
			const float* nodeMatrix = &fPose[(size_t) node * 16];

			for ( int col = 0; col < 4; ++col )
			{
				for ( int row = 0; row < 4; ++row )
				{
					float sum = 0.0f;

					for ( int k = 0; k < 4; ++k )
					{
						sum += objectMatrix[k * 4 + row] * nodeMatrix[col * 4 + k];
					}

					command.fTransform[col * 4 + row] = sum;
				}
			}
		}
		else
		{
			memcpy( command.fTransform, objectMatrix, sizeof( objectMatrix ) );
		}

		// Which material wins, most specific first: one set on this part, then
		// one set on the model as a whole, then what the file described. A model
		// with none of the three is drawn in the default neutral surface, as any
		// other 3D object with no material is.
		const Material3D* material = part.GetOverrideMaterial();

		if ( material == NULL )
		{
			material = fMaterial;
		}

		if ( material == NULL )
		{
			material = part.GetFileMaterial();
		}

		if ( material != NULL )
		{
			material->GetAlbedo( command.fAlbedo[0], command.fAlbedo[1], command.fAlbedo[2], command.fAlbedo[3] );
			material->GetEmissive( command.fEmissive[0], command.fEmissive[1], command.fEmissive[2] );
			command.fRoughness = material->GetRoughness();
			command.fMetallic = material->GetMetallic();
			command.fAlbedoMap = material->GetAlbedoMap();
			command.fMetallicRoughnessMap = material->GetMetallicRoughnessMap();
			command.fEmissiveMap = material->GetEmissiveMap();
			command.fIsDoubleSided = material->IsDoubleSided();
			command.fIsTranslucent = material->IsTranslucent();
		}
		else
		{
			command.fAlbedo[0] = command.fAlbedo[1] = command.fAlbedo[2] = command.fAlbedo[3] = 1.0f;
			command.fEmissive[0] = command.fEmissive[1] = command.fEmissive[2] = 0.0f;
			command.fRoughness = 0.5f;
			command.fMetallic = 0.0f;
			command.fAlbedoMap = NULL;
			command.fMetallicRoughnessMap = NULL;
			command.fEmissiveMap = NULL;
			command.fIsDoubleSided = true;
			command.fIsTranslucent = false;
		}

		renderer.Insert3D( command );
	}
}

// ----------------------------------------------------------------------------

const LuaProxyVTable&
ModelObject3D::ProxyVTable() const
{
	return LuaModelObject3DProxyVTable::Constant();
}

// ----------------------------------------------------------------------------

bool
ModelObject3D::GetLocalBounds( float* minimum, float* maximum ) const
{
	minimum[0] = minimum[1] = minimum[2] = 1e30f;
	maximum[0] = maximum[1] = maximum[2] = -1e30f;

	bool any = false;

	for ( size_t i = 0, iMax = fParts.size(); i < iMax; ++i )
	{
		const ModelPart3D& part = *fParts[i];

		if ( !part.IsVisible() || part.GetMesh() == NULL )
		{
			continue;
		}

		// Each part placed by its node, so the union describes the assembled model
		// rather than a pile of pieces all at the origin. A skinned part is left in
		// its bind pose: its bounds change every frame as it animates, and a
		// collision shape that resized with the animation would shove the body
		// around by itself.
		const int node = part.GetNode();

		const float* nodeMatrix = NULL;

		if ( part.GetSkin() < 0 && node >= 0 && (size_t) ( node + 1 ) * 16 <= fPose.size() )
		{
			nodeMatrix = &fPose[(size_t) node * 16];
		}

		if ( GetMeshBounds( *part.GetMesh(), nodeMatrix, minimum, maximum ) )
		{
			any = true;
		}
	}

	return any;
}

bool
ModelObject3D::Raycast( const float* origin, const float* direction, Raycast3DHit& hit ) const
{
	if ( fModel == NULL || !IsVisible() )
	{
		return false;
	}

	float objectMatrix[16];
	GetWorldMatrix( objectMatrix );

	bool found = false;

	for ( size_t i = 0, iMax = fParts.size(); i < iMax; ++i )
	{
		const ModelPart3D& part = *fParts[i];

		if ( !part.IsVisible() || part.GetMesh() == NULL )
		{
			continue;
		}

		const int skin = part.GetSkin();
		const int node = part.GetNode();

		const float* palette = NULL;
		U32 boneCount = 0;

		float partMatrix[16];

		// The same three cases Draw distinguishes, and they have to agree: a ray
		// tested against a transform the object is not drawn with would pick
		// something the user cannot see.
		if ( skin >= 0 && i < fPaletteCounts.size() && fPaletteCounts[i] > 0 )
		{
			palette = &fPalettes[i * kMaxDraw3DBones * kDraw3DBoneStride];
			boneCount = fPaletteCounts[i];

			memcpy( partMatrix, objectMatrix, sizeof( partMatrix ) );
		}
		else if ( node >= 0 && (size_t) ( node + 1 ) * 16 <= fPose.size() )
		{
			const float* nodeMatrix = &fPose[(size_t) node * 16];

			for ( int col = 0; col < 4; ++col )
			{
				for ( int row = 0; row < 4; ++row )
				{
					float sum = 0.0f;

					for ( int k = 0; k < 4; ++k )
					{
						sum += objectMatrix[k * 4 + row] * nodeMatrix[col * 4 + k];
					}

					partMatrix[col * 4 + row] = sum;
				}
			}
		}
		else
		{
			memcpy( partMatrix, objectMatrix, sizeof( partMatrix ) );
		}

		Raycast3DHit partHit;

		if ( !RaycastMesh( *part.GetMesh(), partMatrix, origin, direction, palette, boneCount, partHit ) )
		{
			continue;
		}

		if ( found && partHit.fDistance >= hit.fDistance )
		{
			continue;
		}

		hit = partHit;
		hit.fObject = const_cast< ModelObject3D* >( this );
		hit.fMeshName = part.GetName().c_str();

		found = true;
	}

	return found;
}

// ----------------------------------------------------------------------------

static int
Model3DPlayAnimation( lua_State *L )
{
	ModelObject3D* o = (ModelObject3D*) LuaProxy::GetProxyableObject( L, 1 );
	Rtt_WARN_SIM_PROXY_TYPE( L, 1, ModelObject3D );

	if ( o == NULL )
	{
		return 0;
	}

	const char* name = lua_tostring( L, 2 );

	bool loop = false;
	float speed = 1.0f;

	if ( lua_istable( L, 3 ) )
	{
		lua_getfield( L, 3, "loop" );
		loop = lua_toboolean( L, -1 ) != 0;
		lua_pop( L, 1 );

		lua_getfield( L, 3, "speed" );
		if ( !lua_isnil( L, -1 ) ) { speed = (float) lua_tonumber( L, -1 ); }
		lua_pop( L, 1 );
	}

	const bool started = o->PlayAnimation( name, loop, speed );

	if ( !started )
	{
		// Named, and listing what there was instead: the mistake is nearly always
		// a name that differs from the file's by a case or a separator, and the
		// answer is in the file the caller cannot see from Lua.
		Model3D* model = o->GetModel();

		std::string available;

		if ( model != NULL )
		{
			const std::vector< ModelAnimation >& animations = model->GetAnimations();

			for ( size_t i = 0, iMax = animations.size(); i < iMax; ++i )
			{
				if ( i > 0 )
				{
					available += ", ";
				}

				available += animations[i].fName;
			}
		}

		CoronaLuaWarning( L, "model:playAnimation() -- this model has no animation named '%s'; it has: %s",
			name != NULL ? name : "",
			available.empty() ? "(none)" : available.c_str() );
	}

	lua_pushboolean( L, started ? 1 : 0 );

	return 1;
}

static int
Model3DStopAnimation( lua_State *L )
{
	ModelObject3D* o = (ModelObject3D*) LuaProxy::GetProxyableObject( L, 1 );
	Rtt_WARN_SIM_PROXY_TYPE( L, 1, ModelObject3D );

	if ( o != NULL )
	{
		o->StopAnimation();
	}

	return 0;
}

static int
Model3DGetMesh( lua_State *L )
{
	ModelObject3D* o = (ModelObject3D*) LuaProxy::GetProxyableObject( L, 1 );
	Rtt_WARN_SIM_PROXY_TYPE( L, 1, ModelObject3D );

	if ( o == NULL )
	{
		return 0;
	}

	ModelPart3D* part = NULL;

	// By name, or by 1-based index -- the latter because a model whose parts the
	// exporter left unnamed cannot be reached any other way, and because walking
	// every part of a model is a thing worth being able to do.
	if ( lua_isnumber( L, 2 ) )
	{
		const int index = (int) lua_tointeger( L, 2 );

		if ( index >= 1 )
		{
			part = o->GetPart( (U32) ( index - 1 ) );
		}
	}
	else
	{
		part = o->FindPart( lua_tostring( L, 2 ) );
	}

	if ( part == NULL )
	{
		return 0;
	}

	LuaLibRender3D::PushModelPart( L, part );

	return 1;
}

// ----------------------------------------------------------------------------

const LuaModelObject3DProxyVTable&
LuaModelObject3DProxyVTable::Constant()
{
	static const Self kVTable;

	return kVTable;
}

const LuaProxyVTable&
LuaModelObject3DProxyVTable::Parent() const
{
	return Super::Constant();
}

int
LuaModelObject3DProxyVTable::ValueForKey( lua_State *L, const MLuaProxyable& object, const char key[], bool overrideRestriction ) const
{
	if ( key == NULL )
	{
		return 0;
	}

	const ModelObject3D& o = static_cast< const ModelObject3D& >( object );
	Rtt_WARN_SIM_PROXY_TYPE( L, 1, ModelObject3D );

	if ( strcmp( key, "playAnimation" ) == 0 )
	{
		lua_pushcfunction( L, Model3DPlayAnimation );

		return 1;
	}

	if ( strcmp( key, "stopAnimation" ) == 0 )
	{
		lua_pushcfunction( L, Model3DStopAnimation );

		return 1;
	}

	if ( strcmp( key, "getMesh" ) == 0 )
	{
		lua_pushcfunction( L, Model3DGetMesh );

		return 1;
	}

	if ( strcmp( key, "animation" ) == 0 )
	{
		const char* name = o.GetAnimationName();

		if ( name == NULL )
		{
			lua_pushnil( L );
		}
		else
		{
			lua_pushstring( L, name );
		}

		return 1;
	}

	if ( strcmp( key, "isPlaying" ) == 0 )
	{
		lua_pushboolean( L, o.IsAnimationPlaying() ? 1 : 0 );

		return 1;
	}

	if ( strcmp( key, "animationTime" ) == 0 )
	{
		lua_pushnumber( L, o.GetAnimationTime() );

		return 1;
	}

	if ( strcmp( key, "animationSpeed" ) == 0 )
	{
		lua_pushnumber( L, o.GetAnimationSpeed() );

		return 1;
	}

	if ( strcmp( key, "loop" ) == 0 )
	{
		lua_pushboolean( L, o.IsAnimationLooping() ? 1 : 0 );

		return 1;
	}

	if ( strcmp( key, "meshCount" ) == 0 )
	{
		lua_pushinteger( L, (int) o.GetPartCount() );

		return 1;
	}

	// The clips the file contains, so that a project can offer them without
	// having been told what they are.
	if ( strcmp( key, "animations" ) == 0 )
	{
		Model3D* model = o.GetModel();

		if ( model == NULL )
		{
			lua_newtable( L );

			return 1;
		}

		const std::vector< ModelAnimation >& animations = model->GetAnimations();

		lua_createtable( L, (int) animations.size(), 0 );

		for ( size_t i = 0, iMax = animations.size(); i < iMax; ++i )
		{
			lua_pushstring( L, animations[i].fName.c_str() );
			lua_rawseti( L, -2, (int) i + 1 );
		}

		return 1;
	}

	return Super::ValueForKey( L, object, key, overrideRestriction );
}

bool
LuaModelObject3DProxyVTable::SetValueForKey( lua_State *L, MLuaProxyable& object, const char key[], int valueIndex ) const
{
	if ( key == NULL )
	{
		return false;
	}

	ModelObject3D& o = static_cast< ModelObject3D& >( object );
	Rtt_WARN_SIM_PROXY_TYPE( L, 1, ModelObject3D );

	if ( strcmp( key, "animationTime" ) == 0 )
	{
		o.SetAnimationTime( (float) lua_tonumber( L, valueIndex ) );

		return true;
	}

	if ( strcmp( key, "animationSpeed" ) == 0 )
	{
		o.SetAnimationSpeed( (float) lua_tonumber( L, valueIndex ) );

		return true;
	}

	if ( strcmp( key, "loop" ) == 0 )
	{
		o.SetAnimationLooping( lua_toboolean( L, valueIndex ) != 0 );

		return true;
	}

	return Super::SetValueForKey( L, object, key, valueIndex );
}

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------
