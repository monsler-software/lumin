//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Corona game engine.
// For overview and more information on licensing please refer to README.md
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////

#ifndef _Rtt_SimulatorMenus_H__
#define _Rtt_SimulatorMenus_H__

#include "Core/Rtt_Types.h"
#include "UI/Rtt_MenuBar.h"

#include <vector>

// ----------------------------------------------------------------------------

namespace Rtt
{

// ----------------------------------------------------------------------------

// What goes on the simulator's menu bar, and the keys that reach the same
// commands without it.
//
// There is one simulator, so there is one description of its menus. It used to
// exist once per platform -- an AppKit main menu on macOS, an MFC menu resource
// on Windows, a Dear ImGui bar on Linux -- and the three had drifted: items
// present in one and missing in another, and shortcuts listed on an item but
// wired to a different command elsewhere in the same file.
//
// Nothing here knows about a windowing system. A host translates its own key
// events into the neutral key and modifiers below and asks
// CommandForAccelerator what they mean; everything else is the same on every
// platform, including which chord an item advertises.
namespace SimulatorCommand
{
	// What a menu item asks for. Handed to MenuBar as MenuItem::fCommand and
	// handed back to the host's command handler, which is where it turns into
	// whatever that platform does about it.
	enum Type
	{
		kNone = 0,

		kNewProject,
		kOpenProject,
		kOpenInEditor,
		kShowProjectFiles,
		kShowProjectSandbox,
		kClearProjectSandbox,
		kRelaunch,
		kRelaunchLastProject,

		// Leaving the project: the Hardware menu's Back, File's Close Project
		// and View's Welcome screen are three ways of asking for it.
		kCloseProject,

		kOpenPreferences,
		kQuit,

		kBuildAndroid,
		kBuildHTML5,
		kBuildLinux,

		kRotateLeft,
		kRotateRight,
		kShake,

		// Suspend and Resume are one item whose label follows the state, so
		// they are one command too.
		kSuspendResume,

		kZoomIn,
		kZoomOut,
		kSetFocusConsole,
		kViewAs,

		kOpenDocumentation,
		kOpenSampleProjects,
		kAbout
	};
}

// The keys that carry an accelerator, named rather than numbered: every
// platform spells its keycodes differently, and the host is the only thing
// that should have to know how.
namespace SimulatorKey
{
	enum Type
	{
		kNone = 0,

		kB,
		kN,
		kO,
		kQ,
		kR,
		kW,

		kLeft,
		kRight,
		kUp,
		kDown,

		// Whatever key carries '+' and '-' unshifted, plus the keypad pair --
		// which is the host's problem, not this file's.
		kPlus,
		kMinus
	};
}

namespace SimulatorModifier
{
	enum
	{
		kNone = 0,

		// The modifier an accelerator is normally built on: Command on macOS,
		// Control everywhere else. Menus say so, and so does the host when it
		// reports which modifiers a key event carried.
		kPrimary = 0x01,

		kShift = 0x02,
		kAlt = 0x04
	};
}

// Fills menus in for what is loaded now, replacing whatever was there.
//
// isHomeScreen picks between the welcome screen's short bar and the fuller one
// a loaded project gets: with nothing loaded there is no sandbox to show,
// nothing to relaunch but the last project, and nothing to build.
//
// isSuspended only chooses the wording of one item. It is read here rather
// than left to the bar to consult while drawing, so the host rebuilds the
// menus when it changes -- which it already does when moving between the
// welcome screen and a project.
void BuildSimulatorMenus( bool isHomeScreen, bool isSuspended, std::vector< Menu >& menus );

// The command a chord names, or SimulatorCommand::kNone if it names none.
//
// key is a SimulatorKey and modifiers a mask of SimulatorModifier. The host
// gets no say in which chord means what: this and the shortcuts printed on the
// items come from one table, which is the point of them living together.
int CommandForAccelerator( bool isHomeScreen, int key, U32 modifiers );

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------

#endif // _Rtt_SimulatorMenus_H__
