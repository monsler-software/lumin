//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Corona game engine.
// For overview and more information on licensing please refer to README.md
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////

#include "Rtt_FileSystem.h"
#include "Rtt_LinuxSimulator.h"
#include "Rtt_LinuxUtils.h"
#include "Rtt_LinuxCEF.h"

#include <cstdlib>

#ifdef Rtt_USE_BGFX
	#include "Renderer/Rtt_BgfxSurfaceParams.h"
#endif

using namespace std;

smart_ptr<Rtt::SolarApp> app;

// Everything the normal end of main() does, in the same order. Lua's os.exit()
// calls exit() from inside Run(), which never comes back here, so without this
// the app would be torn down by the static destructor of `app` instead -- and by
// then the graphics backend's own statics may already be gone, which is a pure
// virtual call or a segfault rather than a shutdown. Registered from main(), so
// it runs before the destructor of anything constructed at static-init time,
// `app` included.
static void ShutDownApp()
{
	if (app.get() != NULL)
	{
		Rtt::FinalizeCEF();
		app = NULL;
	}
}

int main(int argc, char* argv[])
{
	// Before the atexit() below, and for its sake: the graphics backend has
	// statics of its own that the handler ends up freeing through, and they only
	// outlive it if they were constructed first.
#ifdef Rtt_USE_BGFX
	Rtt::BgfxExports::ClaimStatics();
#endif

	atexit(ShutDownApp);

	Rtt::InitCEF(argc, argv);

	string resourcesDir = GetStartupPath(NULL);
	resourcesDir.append("/Resources");

	string arg;
	bool isNotCommandOption = false;
	for (int i = 1; i < argc; ++i)
	{
		arg = argv[i];
		if (arg.compare(0, 1, "-") != 0)
		{
			arg = argv[i];
			isNotCommandOption = true;
			break;
		}
	}

	if (argc > 1 && isNotCommandOption && Rtt_FileExists(arg.c_str()))
	{
		if (std::filesystem::is_regular_file(arg))
		{
			string fileParent = std::filesystem::path(arg).parent_path().string();
			app = new Rtt::SolarSimulator(std::filesystem::absolute(fileParent));
		}
		else
		{
			app = new Rtt::SolarSimulator(std::filesystem::absolute(arg));
		}
	}

	// look for welcomescereen
	else if (Rtt_FileExists((resourcesDir + "/homescreen/main.lua").c_str()))
	{
		resourcesDir.append("/homescreen");
		app = new Rtt::SolarSimulator(resourcesDir);
	}
	else if (Rtt_IsDirectory(resourcesDir.c_str()))
	{
		app = new Rtt::SolarApp(resourcesDir);
	}
	else
	{
		return -1;
	}

	if (app->Init())
	{
		app->Run();
	}

	ShutDownApp();

	return 0;
}

