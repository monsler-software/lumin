//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Corona game engine.
// For overview and more information on licensing please refer to README.md
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////

#include "Core/Rtt_Build.h"

#include "UI/Rtt_SimulatorMenus.h"

// ----------------------------------------------------------------------------

namespace Rtt
{

// ----------------------------------------------------------------------------

namespace
{
	// An accelerator, named so a menu item can point at one instead of
	// repeating it. The item takes both its command and the chord it prints
	// from the entry, so an item and its shortcut cannot disagree, and neither
	// can a shortcut and the key that triggers it.
	enum AcceleratorId
	{
		kAccelNone = -1,

		kAccelNewProject,
		kAccelOpenProject,
		kAccelOpenInEditor,
		kAccelRelaunch,
		kAccelRelaunchLastProject,
		kAccelCloseProject,
		kAccelQuit,

		kAccelBuildAndroid,
		kAccelBuildHTML5,
		kAccelBuildLinux,

		kAccelRotateLeft,
		kAccelRotateRight,
		kAccelShake,
		kAccelBack,
		kAccelSuspendResume,

		kAccelZoomIn,
		kAccelZoomOut,

		kAcceleratorCount
	};

	// Where an accelerator applies. The welcome screen and a loaded project
	// disagree about one chord -- Ctrl+R relaunches the last project on one and
	// the current one on the other -- and the rest of a project's chords mean
	// nothing with nothing loaded.
	enum AcceleratorScope
	{
		kScopeBoth,
		kScopeHomeScreen,
		kScopeProject
	};

	struct Accelerator
	{
		int fCommand;
		int fKey;
		U32 fModifiers;
		int fScope;
	};

	const Accelerator kAccelerators[kAcceleratorCount] =
	{
		{ SimulatorCommand::kNewProject,          SimulatorKey::kN,     SimulatorModifier::kPrimary,                                                  kScopeBoth },
		{ SimulatorCommand::kOpenProject,         SimulatorKey::kO,     SimulatorModifier::kPrimary,                                                  kScopeBoth },
		{ SimulatorCommand::kOpenInEditor,        SimulatorKey::kO,     SimulatorModifier::kPrimary | SimulatorModifier::kShift,                      kScopeProject },
		{ SimulatorCommand::kRelaunch,            SimulatorKey::kR,     SimulatorModifier::kPrimary,                                                  kScopeProject },
		{ SimulatorCommand::kRelaunchLastProject, SimulatorKey::kR,     SimulatorModifier::kPrimary,                                                  kScopeHomeScreen },
		{ SimulatorCommand::kCloseProject,        SimulatorKey::kW,     SimulatorModifier::kPrimary,                                                  kScopeProject },
		{ SimulatorCommand::kQuit,                SimulatorKey::kQ,     SimulatorModifier::kPrimary,                                                  kScopeBoth },

		// The three build targets differ only in their modifiers.
		{ SimulatorCommand::kBuildAndroid,        SimulatorKey::kB,     SimulatorModifier::kPrimary,                                                  kScopeProject },
		{ SimulatorCommand::kBuildHTML5,          SimulatorKey::kB,     SimulatorModifier::kPrimary | SimulatorModifier::kAlt,                        kScopeProject },
		{ SimulatorCommand::kBuildLinux,          SimulatorKey::kB,     SimulatorModifier::kPrimary | SimulatorModifier::kShift | SimulatorModifier::kAlt, kScopeProject },

		{ SimulatorCommand::kRotateLeft,          SimulatorKey::kLeft,  SimulatorModifier::kPrimary,                                                  kScopeProject },
		{ SimulatorCommand::kRotateRight,         SimulatorKey::kRight, SimulatorModifier::kPrimary,                                                  kScopeProject },
		{ SimulatorCommand::kShake,               SimulatorKey::kUp,    SimulatorModifier::kPrimary,                                                  kScopeProject },

		// Back, the one chord that is not built on the primary modifier. It
		// asks for the same thing Close Project does; the two are separate
		// entries because they are separate items, printing separate chords.
		{ SimulatorCommand::kCloseProject,        SimulatorKey::kLeft,  SimulatorModifier::kAlt,                                                      kScopeProject },

		{ SimulatorCommand::kSuspendResume,       SimulatorKey::kDown,  SimulatorModifier::kPrimary,                                                  kScopeProject },

		{ SimulatorCommand::kZoomIn,              SimulatorKey::kPlus,  SimulatorModifier::kPrimary,                                                  kScopeProject },
		{ SimulatorCommand::kZoomOut,             SimulatorKey::kMinus, SimulatorModifier::kPrimary,                                                  kScopeProject }
	};

	bool AppliesTo( const Accelerator& accelerator, bool isHomeScreen )
	{
		switch ( accelerator.fScope )
		{
			case kScopeHomeScreen:	return isHomeScreen;
			case kScopeProject:		return !isHomeScreen;
			default:				return true;
		}
	}

	const char* KeyName( int key )
	{
		switch ( key )
		{
			case SimulatorKey::kB:		return "B";
			case SimulatorKey::kN:		return "N";
			case SimulatorKey::kO:		return "O";
			case SimulatorKey::kQ:		return "Q";
			case SimulatorKey::kR:		return "R";
			case SimulatorKey::kW:		return "W";
			case SimulatorKey::kLeft:	return "Left";
			case SimulatorKey::kRight:	return "Right";
			case SimulatorKey::kUp:		return "Up";
			case SimulatorKey::kDown:	return "Down";
			case SimulatorKey::kPlus:	return "Plus";
			case SimulatorKey::kMinus:	return "Minus";
			default:					return "";
		}
	}

	// How a chord is spelled on the item. The only place in here that is aware
	// of a platform at all, and it is aware of one thing: macOS writes its
	// modifiers as symbols, in its own order, and calls the primary one
	// Command.
	std::string AcceleratorLabel( const Accelerator& accelerator )
	{
		std::string label;

#if defined( Rtt_MAC_ENV )
		if ( accelerator.fModifiers & SimulatorModifier::kAlt )		label += "\xE2\x8C\xA5";	// U+2325 option
		if ( accelerator.fModifiers & SimulatorModifier::kShift )	label += "\xE2\x87\xA7";	// U+21E7 shift
		if ( accelerator.fModifiers & SimulatorModifier::kPrimary )	label += "\xE2\x8C\x98";	// U+2318 command

		switch ( accelerator.fKey )
		{
			case SimulatorKey::kLeft:	label += "\xE2\x86\x90"; return label;	// U+2190
			case SimulatorKey::kRight:	label += "\xE2\x86\x92"; return label;	// U+2192
			case SimulatorKey::kUp:		label += "\xE2\x86\x91"; return label;	// U+2191
			case SimulatorKey::kDown:	label += "\xE2\x86\x93"; return label;	// U+2193
			case SimulatorKey::kPlus:	label += "+"; return label;
			case SimulatorKey::kMinus:	label += "-"; return label;
			default:					break;
		}

		label += KeyName( accelerator.fKey );
#else
		if ( accelerator.fModifiers & SimulatorModifier::kPrimary )	label += "Ctrl+";
		if ( accelerator.fModifiers & SimulatorModifier::kShift )	label += "Shift+";
		if ( accelerator.fModifiers & SimulatorModifier::kAlt )		label += "Alt+";

		label += KeyName( accelerator.fKey );
#endif

		return label;
	}

	// A command item, spelled out rather than assembled through a builder:
	// what a menu bar is is a list, and the list reads better as one.
	MenuItem Command( const char* label, int command )
	{
		MenuItem item;

		item.fLabel = label;
		item.fCommand = command;

		return item;
	}

	// The same, for the items that have a chord. Both the command and the
	// printed shortcut come from the accelerator, so naming the accelerator is
	// the whole of it.
	MenuItem Command( const char* label, AcceleratorId id )
	{
		const Accelerator& accelerator = kAccelerators[id];

		MenuItem item;

		item.fLabel = label;
		item.fShortcut = AcceleratorLabel( accelerator );
		item.fCommand = accelerator.fCommand;

		return item;
	}

	MenuItem Separator()
	{
		MenuItem item;

		item.fSeparator = true;

		return item;
	}
}

// ----------------------------------------------------------------------------

void
BuildSimulatorMenus( bool isHomeScreen, bool isSuspended, std::vector< Menu >& menus )
{
	menus.clear();

	Menu file;
	Menu help;

	file.fTitle = "File";
	help.fTitle = "Help";

	help.fItems.push_back( Command( "Online Documentation...", SimulatorCommand::kOpenDocumentation ) );
	help.fItems.push_back( Command( "Sample projects...", SimulatorCommand::kOpenSampleProjects ) );
	help.fItems.push_back( Separator() );
	help.fItems.push_back( Command( "About Simulator...", SimulatorCommand::kAbout ) );

	if ( isHomeScreen )
	{
		// Nothing is loaded, so everything about a project is missing: there is
		// no sandbox to show, nothing to relaunch but the last one, and nothing
		// to build.
		file.fItems.push_back( Command( "New Project...", kAccelNewProject ) );
		file.fItems.push_back( Command( "Open Project...", kAccelOpenProject ) );
		file.fItems.push_back( Separator() );
		file.fItems.push_back( Command( "Relaunch Last Project", kAccelRelaunchLastProject ) );
		file.fItems.push_back( Separator() );
		file.fItems.push_back( Command( "Preferences...", SimulatorCommand::kOpenPreferences ) );
		file.fItems.push_back( Separator() );
		file.fItems.push_back( Command( "Exit", kAccelQuit ) );

		menus.push_back( file );
		menus.push_back( help );

		return;
	}

	MenuItem build;

	build.fLabel = "Build";
	build.fItems.push_back( Command( "Android...", kAccelBuildAndroid ) );
	build.fItems.push_back( Command( "HTML5...", kAccelBuildHTML5 ) );
	build.fItems.push_back( Command( "Linux...", kAccelBuildLinux ) );

	file.fItems.push_back( Command( "New Project...", kAccelNewProject ) );
	file.fItems.push_back( Command( "Open Project...", kAccelOpenProject ) );
	file.fItems.push_back( Separator() );
	file.fItems.push_back( build );
	file.fItems.push_back( Command( "Open in Editor", kAccelOpenInEditor ) );
	file.fItems.push_back( Command( "Show Project Files", SimulatorCommand::kShowProjectFiles ) );
	file.fItems.push_back( Command( "Show Project Sandbox", SimulatorCommand::kShowProjectSandbox ) );
	file.fItems.push_back( Separator() );
	file.fItems.push_back( Command( "Clear Project Sandbox", SimulatorCommand::kClearProjectSandbox ) );
	file.fItems.push_back( Separator() );
	file.fItems.push_back( Command( "Relaunch", kAccelRelaunch ) );
	file.fItems.push_back( Command( "Close Project", kAccelCloseProject ) );
	file.fItems.push_back( Command( "Preferences...", SimulatorCommand::kOpenPreferences ) );
	file.fItems.push_back( Separator() );
	file.fItems.push_back( Command( "Exit", kAccelQuit ) );

	Menu hardware;

	hardware.fTitle = "Hardware";
	hardware.fItems.push_back( Command( "Rotate Left", kAccelRotateLeft ) );
	hardware.fItems.push_back( Command( "Rotate Right", kAccelRotateRight ) );
	hardware.fItems.push_back( Command( "Shake", kAccelShake ) );
	hardware.fItems.push_back( Separator() );
	hardware.fItems.push_back( Command( "Back", kAccelBack ) );
	hardware.fItems.push_back( Separator() );

	// The one item whose label depends on state. The menus are rebuilt whenever
	// it changes, so this is read once, here, rather than being a callback the
	// bar has to consult while drawing.
	{
		MenuItem item = Command( !isSuspended ? "Suspend" : "Resume", kAccelSuspendResume );

		hardware.fItems.push_back( item );
	}

	Menu view;

	view.fTitle = "View";
	view.fItems.push_back( Command( "Zoom In", kAccelZoomIn ) );
	view.fItems.push_back( Command( "Zoom Out", kAccelZoomOut ) );
	view.fItems.push_back( Separator() );
	view.fItems.push_back( Command( "Welcome screen", SimulatorCommand::kCloseProject ) );
	view.fItems.push_back( Command( "Console", SimulatorCommand::kSetFocusConsole ) );
	view.fItems.push_back( Separator() );
	view.fItems.push_back( Command( "View As...", SimulatorCommand::kViewAs ) );

	menus.push_back( file );
	menus.push_back( hardware );
	menus.push_back( view );
	menus.push_back( help );
}

int
CommandForAccelerator( bool isHomeScreen, int key, U32 modifiers )
{
	if ( SimulatorKey::kNone == key )
	{
		return SimulatorCommand::kNone;
	}

	for ( int i = 0; i < kAcceleratorCount; ++i )
	{
		const Accelerator& accelerator = kAccelerators[i];

		// Exactly the modifiers the chord names, not merely at least them: a
		// stray Shift held over Ctrl+R is not a request to relaunch.
		if ( accelerator.fKey == key
			&& accelerator.fModifiers == modifiers
			&& AppliesTo( accelerator, isHomeScreen ) )
		{
			return accelerator.fCommand;
		}
	}

	return SimulatorCommand::kNone;
}

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------
