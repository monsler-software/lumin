//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Corona game engine.
// For overview and more information on licensing please refer to README.md
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////

#ifndef _Rtt_RendererFactory_H__
#define _Rtt_RendererFactory_H__

#include "Core/Rtt_Types.h"
#include "Renderer/Rtt_Program.h"

// ----------------------------------------------------------------------------

struct Rtt_Allocator;

namespace Rtt
{

class Renderer;

// ----------------------------------------------------------------------------

// Creates the Renderer for a named rendering backend.
//
// Backends are looked up by the same names build.settings and the simulator
// already use ("glBackend", "vulkanBackend"). Previously Display::Initialize()
// hardcoded the mapping in an #if defined( Rtt_WIN_ENV ) block, so a new
// backend meant editing Display and every platform saw a different set of
// choices. Registration keeps that knowledge in the backend's own translation
// unit and lets a platform offer whatever it was built with.
class RendererFactory
{
	public:
		// Extra data a backend needs to reach the window/surface it renders
		// into. Its meaning is backend-specific and it may be NULL.
		typedef void *BackendContext;

		// Called when the backend should redraw; may be NULL.
		typedef void (*InvalidateCallback)( void *display );

		typedef Renderer * (*Creator)(
			Rtt_Allocator *allocator,
			BackendContext context,
			InvalidateCallback invalidate,
			void *display );

	public:
		// Names of the backends built into this binary. kDefault is the name
		// Create() uses when asked for NULL or an empty name.
		static const char kOpenGL[];	// "glBackend"
		static const char kVulkan[];	// "vulkanBackend"
		static const char kBgfx[];	// "bgfxBackend"

		// Associates name with creator, replacing any previous entry. Names are
		// compared case-sensitively. Returns false if the table is full.
		//
		// language is the shader dialect the backend's kernels are written in;
		// ShaderFactory asks for it by backend name instead of testing for one
		// particular backend, as it used to.
		static bool Register( const char *name, Creator creator, Program::Language language = Program::kDefault );

		// The shader dialect of the named backend, or that of the default
		// backend when name is unknown or NULL.
		static Program::Language GetShaderLanguage( const char *name );

		// True if name was registered and can be created on this build.
		static bool IsAvailable( const char *name );

		// The backend used when none is requested. OpenGL unless a platform
		// registers something else as its default.
		static const char *GetDefaultName();
		static void SetDefaultName( const char *name );

		// Creates the named backend, or the default one when name is NULL or
		// empty. Returns NULL if the name is unknown to this build, leaving the
		// caller to report it rather than assert; callers that must have a
		// renderer should fall back to GetDefaultName().
		static Renderer *Create(
			const char *name,
			Rtt_Allocator *allocator,
			BackendContext context = NULL,
			InvalidateCallback invalidate = NULL,
			void *display = NULL );

	private:
		enum { kMaxBackends = 8 };

		struct Entry
		{
			const char *fName;
			Creator fCreator;
			Program::Language fLanguage;
		};

		static Entry sEntries[kMaxBackends];
		static const char *sDefaultName;

		// Registers the backends this binary was built with. Idempotent.
		static void EnsureBuiltInsRegistered();

		static Creator Find( const char *name );
};

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------

#endif // _Rtt_RendererFactory_H__
