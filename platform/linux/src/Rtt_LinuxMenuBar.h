//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Corona game engine.
// For overview and more information on licensing please refer to README.md
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////

#pragma once

#include "UI/Rtt_SimulatorMenus.h"

#include <SDL2/SDL.h>

namespace Rtt
{
	// What is between SDL and the simulator's menus.
	//
	// The menus themselves, and the chords their items advertise, are
	// Rtt_SimulatorMenus.h: one description, shared by every host, so that a
	// shortcut printed on an item and the key that triggers it cannot drift
	// apart -- which is exactly what happened to the Dear ImGui bar this
	// replaces, where a shortcut could be listed on an item and wired to a
	// different command a few hundred lines away.
	//
	// What is left here is the two things only SDL can answer: which neutral
	// key and modifiers an SDL_KeyboardEvent carries, and which of this host's
	// `sdl` events a command turns into.

	// The command an accelerator names, or SimulatorCommand::kNone if the chord
	// is not one of them.
	int CommandForKeyEvent(const SDL_KeyboardEvent& key, bool isHomeScreen);

	// The event this host pushes to carry out a SimulatorCommand -- one of the
	// `sdl` enum, or SDL_QUIT. Zero for a command that is not dispatched that
	// way, which is Suspend/Resume: the app acts on it directly.
	int SdlEventForCommand(int command);
}
