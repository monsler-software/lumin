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
	// A command item, spelled out rather than assembled through a builder:
	// what a menu bar is is a list, and the list reads better as one.
	static MenuItem Command(const char* label, int command, const char* shortcut = "")
	{
		MenuItem item;

		item.fLabel = label;
		item.fShortcut = shortcut;
		item.fCommand = command;

		return item;
	}

	static MenuItem Separator()
	{
		MenuItem item;

		item.fSeparator = true;

		return item;
	}

	void BuildSimulatorMenus(bool isHomeScreen, std::vector<Menu>& menus)
	{
		menus.clear();

		Menu file;
		Menu help;

		file.fTitle = "File";
		help.fTitle = "Help";

		help.fItems.push_back(Command("Online Documentation...", OnOpenDocumentation));
		help.fItems.push_back(Command("Sample projects...", OnOpenSampleProjects));
		help.fItems.push_back(Separator());
		help.fItems.push_back(Command("About Simulator...", OnAbout));

		if (isHomeScreen)
		{
			// Nothing is loaded, so everything about a project is missing:
			// there is no sandbox to show, nothing to relaunch but the last
			// one, and nothing to build.
			file.fItems.push_back(Command("New Project...", OnNewProject, "Ctrl+N"));
			file.fItems.push_back(Command("Open Project...", OnOpenProject, "Ctrl+O"));
			file.fItems.push_back(Separator());
			file.fItems.push_back(Command("Relaunch Last Project", OnRelaunchLastProject, "Ctrl+R"));
			file.fItems.push_back(Separator());
			file.fItems.push_back(Command("Preferences...", OnOpenPreferences));
			file.fItems.push_back(Separator());
			file.fItems.push_back(Command("Exit", SDL_QUIT, "Ctrl+Q"));

			menus.push_back(file);
			menus.push_back(help);

			return;
		}

		MenuItem build;

		build.fLabel = "Build";
		build.fItems.push_back(Command("Android...", OnBuildAndroid, "Ctrl+B"));
		build.fItems.push_back(Command("HTML5...", OnBuildHTML5, "Ctrl+Alt+B"));
		build.fItems.push_back(Command("Linux...", OnBuildLinux, "Ctrl+Shift+Alt+B"));

		file.fItems.push_back(Command("New Project...", OnNewProject, "Ctrl+N"));
		file.fItems.push_back(Command("Open Project...", OnOpenProject, "Ctrl+O"));
		file.fItems.push_back(Separator());
		file.fItems.push_back(build);
		file.fItems.push_back(Command("Open in Editor", OnOpenInEditor, "Ctrl+Shift+O"));
		file.fItems.push_back(Command("Show Project Files", OnShowProjectFiles));
		file.fItems.push_back(Command("Show Project Sandbox", OnShowProjectSandbox));
		file.fItems.push_back(Separator());
		file.fItems.push_back(Command("Clear Project Sandbox", OnClearProjectSandbox));
		file.fItems.push_back(Separator());
		file.fItems.push_back(Command("Relaunch", OnRelaunch, "Ctrl+R"));
		file.fItems.push_back(Command("Close Project", OnCloseProject, "Ctrl+W"));
		file.fItems.push_back(Command("Preferences...", OnOpenPreferences));
		file.fItems.push_back(Separator());
		file.fItems.push_back(Command("Exit", SDL_QUIT, "Ctrl+Q"));

		Menu hardware;

		hardware.fTitle = "Hardware";
		hardware.fItems.push_back(Command("Rotate Left", OnRotateLeft, "Ctrl+Left"));
		hardware.fItems.push_back(Command("Rotate Right", OnRotateRight, "Ctrl+Right"));
		hardware.fItems.push_back(Command("Shake", OnShake, "Ctrl+Up"));
		hardware.fItems.push_back(Separator());
		hardware.fItems.push_back(Command("Back", OnCloseProject, "Alt+Left"));
		hardware.fItems.push_back(Separator());

		// The one item whose label depends on state. The menus are rebuilt
		// whenever it changes, so this is read once, here, rather than being a
		// callback the bar has to consult while drawing.
		hardware.fItems.push_back(Command(
			!app->IsSuspended() ? "Suspend" : "Resume",
			OnSuspendResume, "Ctrl+Down"));

		Menu view;

		view.fTitle = "View";
		view.fItems.push_back(Command("Zoom In", OnZoomIn, "Ctrl+Plus"));
		view.fItems.push_back(Command("Zoom Out", OnZoomOut, "Ctrl+Minus"));
		view.fItems.push_back(Separator());
		view.fItems.push_back(Command("Welcome screen", OnCloseProject));
		view.fItems.push_back(Command("Console", OnSetFocusConsole));
		view.fItems.push_back(Separator());
		view.fItems.push_back(Command("View As...", OnViewAs));

		menus.push_back(file);
		menus.push_back(hardware);
		menus.push_back(view);
		menus.push_back(help);
	}

	int CommandForKeyEvent(const SDL_KeyboardEvent& key, bool isHomeScreen)
	{
		// Autorepeat would fire a command per repeat, which for something like
		// Relaunch is not what holding the key down means.
		if (key.repeat != 0)
		{
			return -1;
		}

		const SDL_Keymod mod = SDL_Keymod(key.keysym.mod);

		const bool ctrl = 0 != (mod & KMOD_CTRL);
		const bool shift = 0 != (mod & KMOD_SHIFT);
		const bool alt = 0 != (mod & KMOD_ALT);

		if (isHomeScreen)
		{
			if (!ctrl || shift || alt)
			{
				return -1;
			}

			switch (key.keysym.sym)
			{
			case SDLK_n: return OnNewProject;
			case SDLK_o: return OnOpenProject;
			case SDLK_r: return OnRelaunchLastProject;
			case SDLK_q: return SDL_QUIT;
			default: return -1;
			}
		}

		// Back, the one chord that is not Ctrl-based.
		if (alt && !ctrl && !shift && SDLK_LEFT == key.keysym.sym)
		{
			return OnCloseProject;
		}

		if (!ctrl)
		{
			return -1;
		}

		// The three build targets differ only in their modifiers, so they are
		// tested together rather than falling through the table below.
		if (SDLK_b == key.keysym.sym)
		{
			if (shift && alt) return OnBuildLinux;
			if (!shift && alt) return OnBuildHTML5;
			if (!shift && !alt) return OnBuildAndroid;

			return -1;
		}

		if (shift && !alt && SDLK_o == key.keysym.sym)
		{
			return OnOpenInEditor;
		}

		if (shift || alt)
		{
			return -1;
		}

		switch (key.keysym.sym)
		{
		case SDLK_n: return OnNewProject;
		case SDLK_o: return OnOpenProject;
		case SDLK_r: return OnRelaunch;
		case SDLK_w: return OnCloseProject;
		case SDLK_q: return SDL_QUIT;

		case SDLK_DOWN: return OnSuspendResume;

		// Promised by the Hardware menu's labels, though the Dear ImGui bar
		// this replaces never actually wired them up.
		case SDLK_LEFT: return OnRotateLeft;
		case SDLK_RIGHT: return OnRotateRight;
		case SDLK_UP: return OnShake;

		// Zoom, spelled "Ctrl+Plus" and "Ctrl+Minus" on the menu. The plus is
		// whatever key carries it unshifted, which is '=' on most layouts, and
		// the keypad pair for anyone using it.
		case SDLK_EQUALS:
		case SDLK_PLUS:
		case SDLK_KP_PLUS:
			return OnZoomIn;

		case SDLK_MINUS:
		case SDLK_KP_MINUS:
			return OnZoomOut;

		default:
			return -1;
		}
	}
}
