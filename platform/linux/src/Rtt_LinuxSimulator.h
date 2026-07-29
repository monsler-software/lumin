//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Corona game engine.
// For overview and more information on licensing please refer to README.md
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////

#pragma once

#include "Rtt_LinuxApp.h"
#include "Rtt_LinuxSimulatorView.h"

namespace Rtt
{
	struct SolarSimulator : public SolarApp
	{
		SolarSimulator(const std::string& resourceDir);
		virtual ~SolarSimulator();

		void OnOpen(const std::string& path);
		void OnRelaunch();
		void OnZoomIn();
		void OnZoomOut();
		void OnViewAsChanged(const SkinProperties* skin);

		void OnRotateLeft();
		void OnRotateRight();
		void WatchFolder(const std::string& path);
		bool LoadApp(const std::string& path) override;
		void SolarEvent(const SDL_Event& e) override;
		void StartConsole() override;
		void CreateMenu() override;
		bool IsRunningOnSimulator() override { return true; }
		bool Init() override;

		// How much of the window the bar takes off the top, which the content
		// below it is laid out against. A constant rather than something
		// measured: the window is sized -- and this is subtracted -- long
		// before bgfx exists to measure a bar with.
		virtual int GetMenuHeight() const { return MenuBar::GetHeight(); }

	private:

		smart_ptr<FileWatcher> fWatcher;
		Skins fSkins;
	};
}

extern Rtt::SolarSimulator* solarSimulator;


