//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Corona game engine.
// For overview and more information on licensing please refer to README.md
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////

#include "Core/Rtt_Build.h"

#include "Rtt_LinuxMenuBar.h"
#include "Rtt_LinuxApp.h"

namespace Rtt
{
	// Which of the keys an accelerator can be built on this is, if any.
	//
	// Zoom is spelled "Plus" and "Minus" on the menu; the plus is whatever key
	// carries it unshifted, which is '=' on most layouts, and the keypad pair
	// for anyone using it.
	static int KeyForKeysym(SDL_Keycode sym)
	{
		switch (sym)
		{
		case SDLK_b: return SimulatorKey::kB;
		case SDLK_n: return SimulatorKey::kN;
		case SDLK_o: return SimulatorKey::kO;
		case SDLK_q: return SimulatorKey::kQ;
		case SDLK_r: return SimulatorKey::kR;
		case SDLK_w: return SimulatorKey::kW;

		case SDLK_LEFT: return SimulatorKey::kLeft;
		case SDLK_RIGHT: return SimulatorKey::kRight;
		case SDLK_UP: return SimulatorKey::kUp;
		case SDLK_DOWN: return SimulatorKey::kDown;

		case SDLK_EQUALS:
		case SDLK_PLUS:
		case SDLK_KP_PLUS:
			return SimulatorKey::kPlus;

		case SDLK_MINUS:
		case SDLK_KP_MINUS:
			return SimulatorKey::kMinus;

		default:
			return SimulatorKey::kNone;
		}
	}

	int CommandForKeyEvent(const SDL_KeyboardEvent& key, bool isHomeScreen)
	{
		// Autorepeat would fire a command per repeat, which for something like
		// Relaunch is not what holding the key down means.
		if (key.repeat != 0)
		{
			return SimulatorCommand::kNone;
		}

		const SDL_Keymod mod = SDL_Keymod(key.keysym.mod);

		U32 modifiers = SimulatorModifier::kNone;

		// Control is the primary modifier everywhere but macOS, and this host
		// is not macOS.
		if (mod & KMOD_CTRL) modifiers |= SimulatorModifier::kPrimary;
		if (mod & KMOD_SHIFT) modifiers |= SimulatorModifier::kShift;
		if (mod & KMOD_ALT) modifiers |= SimulatorModifier::kAlt;

		return CommandForAccelerator(isHomeScreen, KeyForKeysym(key.keysym.sym), modifiers);
	}

	int SdlEventForCommand(int command)
	{
		switch (command)
		{
		case SimulatorCommand::kNewProject: return OnNewProject;
		case SimulatorCommand::kOpenProject: return OnOpenProject;
		case SimulatorCommand::kOpenInEditor: return OnOpenInEditor;
		case SimulatorCommand::kShowProjectFiles: return OnShowProjectFiles;
		case SimulatorCommand::kShowProjectSandbox: return OnShowProjectSandbox;
		case SimulatorCommand::kClearProjectSandbox: return OnClearProjectSandbox;
		case SimulatorCommand::kRelaunch: return OnRelaunch;
		case SimulatorCommand::kRelaunchLastProject: return OnRelaunchLastProject;
		case SimulatorCommand::kCloseProject: return OnCloseProject;
		case SimulatorCommand::kOpenPreferences: return OnOpenPreferences;
		case SimulatorCommand::kQuit: return SDL_QUIT;

		case SimulatorCommand::kBuildAndroid: return OnBuildAndroid;
		case SimulatorCommand::kBuildHTML5: return OnBuildHTML5;
		case SimulatorCommand::kBuildLinux: return OnBuildLinux;

		case SimulatorCommand::kRotateLeft: return OnRotateLeft;
		case SimulatorCommand::kRotateRight: return OnRotateRight;
		case SimulatorCommand::kShake: return OnShake;

		case SimulatorCommand::kZoomIn: return OnZoomIn;
		case SimulatorCommand::kZoomOut: return OnZoomOut;
		case SimulatorCommand::kSetFocusConsole: return OnSetFocusConsole;
		case SimulatorCommand::kViewAs: return OnViewAs;

		case SimulatorCommand::kOpenDocumentation: return OnOpenDocumentation;
		case SimulatorCommand::kOpenSampleProjects: return OnOpenSampleProjects;
		case SimulatorCommand::kAbout: return OnAbout;

		// Suspend/Resume is not pushed: the label on the item is the state, so
		// the app flips it and rebuilds the menus itself.
		default: return 0;
		}
	}
}
