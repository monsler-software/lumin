//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Corona game engine.
// For overview and more information on licensing please refer to README.md
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////

#pragma once

#include "UI/Rtt_MenuBar.h"

#include <SDL2/SDL.h>

namespace Rtt
{
	// What goes on the simulator's menu bar, and the keys that reach the same
	// commands without it.
	//
	// The bar itself is Rtt::MenuBar, which knows nothing about SDL or about
	// Corona; this is the layer between them. It exists as its own file
	// because the menus and their accelerators are one description of one
	// thing, and keeping them side by side is what stops the two from drifting
	// apart -- which is exactly what happened to the Dear ImGui bar this
	// replaces, where a shortcut could be listed on an item and wired to a
	// different command a few hundred lines away.

	// isHomeScreen picks between the welcome screen's short bar and the fuller
	// one a loaded project gets.
	void BuildSimulatorMenus(bool isHomeScreen, std::vector<Menu>& menus);

	// Turns a key press into the command an accelerator names, or -1 if the
	// chord is not one of them. Commands are the `sdl` enum, plus SDL_QUIT.
	int CommandForKeyEvent(const SDL_KeyboardEvent& key, bool isHomeScreen);
}
