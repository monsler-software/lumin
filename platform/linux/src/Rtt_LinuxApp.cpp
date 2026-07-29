//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Corona game engine.
// For overview and more information on licensing please refer to README.md
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////

#include <string.h>
#include <fstream>
#include "Core/Rtt_Build.h"
#include "Core/Rtt_Time.h"
#include "Rtt_Runtime.h"
#include "Rtt_LuaContext.h"
#include "Core/Rtt_Types.h"
#include "Rtt_LinuxApp.h"
#include "Rtt_LinuxPlatform.h"
#include "Rtt_LinuxRuntimeDelegate.h"
#include "Rtt_LuaFile.h"
#include "Core/Rtt_FileSystem.h"
#include "Rtt_Archive.h"
#include "Display/Rtt_Display.h"
#include "Renderer/Rtt_Renderer.h"
#include "Display/Rtt_DisplayDefaults.h"
#include "Rtt_Freetype.h"
#include "Rtt_LuaLibSimulator.h"
#include "Rtt_LinuxSimulatorView.h"
#include "Rtt_LinuxUtils.h"
#include "Rtt_MPlatformServices.h"
#include "Rtt_HTTPClient.h"
#include "Rtt_LinuxKeyListener.h"
#include "Rtt_BitmapUtils.h"
#include "Rtt_LinuxMenuBar.h"
#include "default.ttf.h"
#include <curl/curl.h>

#if defined( Rtt_USE_BGFX )
	#include "Renderer/Rtt_BgfxRenderer.h"
#endif
#include <utility>		// for pairs
#include "lua.h"
#include "lauxlib.h"

using namespace Rtt;
using namespace std;

//#define Rtt_DEBUG_TOUCH 1

// for redirecting output to Solar2DConsole
extern "C"
{
	static int print2console(lua_State* L)
	{
		return SolarAppContext::Print(L);
	}
}

namespace Rtt
{

	SolarApp::SolarApp(const string& resourceDir)
		: fResourceDir(resourceDir)
		, fWindow(NULL)
		, fImCtx(NULL)
		, fMenuBarFailed(false)
		, fActivityIndicator(false)
	{
		fMouse = new LinuxMouseListener();
	}

	SolarApp::~SolarApp()
	{
		fContext = NULL;
		curl_global_cleanup();

		// Cleanup
#if !defined( Rtt_USE_BGFX )
		ImGui_ImplOpenGL3_Shutdown();
#endif
		ImGui_ImplSDL2_Shutdown();
		ImGui::DestroyContext();

		if (fGLcontext)
		{
			SDL_GL_DeleteContext(fGLcontext);
		}
		SDL_DestroyWindow(fWindow);
		SDL_Quit();
	}


#if defined( Rtt_USE_BGFX )
	// A placeholder size for the window bgfx initializes against, replaced by
	// the project's own once config.lua has been read.
	static const int kDefaultWindowWidth = 320;
	static const int kDefaultWindowHeight = 480;
#endif

	bool SolarApp::InitSDL()
	{
		curl_global_init(CURL_GLOBAL_ALL);

		// Initialize SDL (Note: video is required to start event loop) 
		if (SDL_Init(SDL_INIT_VIDEO) < 0)
		{
			Rtt_LogException("Couldn't initialize SDL: %s\n", SDL_GetError());
			return false;
		}

		// GL 3.0 + GLSL 130
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
		SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
		SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
		SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
		SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
		SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
		SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);
		SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE | SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);

#if defined( Rtt_USE_BGFX )
		// bgfx creates the graphics context itself, from the bare window, so
		// SDL must not make one here: a GL context on this window would fight
		// the one bgfx sets up, and its Vulkan backend cannot use a window
		// configured for GL at all.
		//
		// The size matters as much as the flags. SDL_CreateWindow is asked for
		// 0x0 and the real size only arrives later, once config.lua has been
		// read, but bgfx captures the resolution when it initializes -- and a
		// 1x1 swap chain is what it makes of a window that has no size yet.
		// Resizable, since content is rendered at the window's own resolution:
		// a window of any size is a valid one, and the runtime follows it.
		uint32_t windowStyle = SDL_WINDOW_ALLOW_HIGHDPI;

		if (!IsRunningOnSimulator())
		{
			windowStyle |= SDL_WINDOW_RESIZABLE;
		}

		fWindow = SDL_CreateWindow("", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
			kDefaultWindowWidth, kDefaultWindowHeight, windowStyle);
		SetIcon();

		fGLcontext = NULL;

		// Dear ImGui still needs its context: the event loop calls into it
		// whether or not anything is drawn. Only its OpenGL renderer backend is
		// left out, since there is no GL context for it to draw through -- the
		// simulator UI stays invisible until ImGui has a bgfx backend here.
		IMGUI_CHECKVERSION();
		fImCtx = ImGui::CreateContext();
		ImGuiIO& io = ImGui::GetIO(); (void)io;
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

		ImGui_ImplSDL2_InitForVulkan(fWindow);

		return true;
#else
		uint32_t windowStyle = SDL_WINDOW_OPENGL | SDL_WINDOW_ALLOW_HIGHDPI;
		fWindow = SDL_CreateWindow("", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 0, 0, windowStyle);
		SetIcon();

		fGLcontext = SDL_GL_CreateContext(fWindow);
		SDL_GL_MakeCurrent(fWindow, fGLcontext);
		SDL_GL_SetSwapInterval(1); // Enable vsync

		// Setup Dear ImGui context

		IMGUI_CHECKVERSION();
		fImCtx = ImGui::CreateContext();
		ImGuiIO& io = ImGui::GetIO(); (void)io;
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;       // Enable Keyboard Controls

		// Setup Platform/Renderer backends
		ImGui_ImplSDL2_InitForOpenGL(fWindow, fGLcontext);
		const char* glsl_version = "#version 130";
		ImGui_ImplOpenGL3_Init(glsl_version);

		return true;
#endif
	}

	void SolarApp::SetIcon()
	{
		int image_width = 0;
		int image_height = 0;
		string icon_path = GetStartupPath(NULL);
		icon_path.append("/Resources/solar2d.png");

		FILE* f = fopen(icon_path.c_str(), "rb");
		if (f)
		{
			unsigned char* img = bitmapUtil::loadPNG(f, image_width, image_height);
			fclose(f);

			if (img)
			{
				// Set up the pixel format color masks for RGB(A) byte arrays.
				// Only STBI_rgb (3) and STBI_rgb_alpha (4) are supported here!
				Uint32 rmask, gmask, bmask, amask;
#if SDL_BYTEORDER == SDL_BIG_ENDIAN
				int shift = (req_format == STBI_rgb) ? 8 : 0;
				rmask = 0xff000000 >> shift;
				gmask = 0x00ff0000 >> shift;
				bmask = 0x0000ff00 >> shift;
				amask = 0x000000ff >> shift;
#else // little endian, like x86
				rmask = 0x000000ff;
				gmask = 0x0000ff00;
				bmask = 0x00ff0000;
				amask = 0xff000000;
#endif

				int depth = 32;
				int pitch = 4 * image_width;
				SDL_Surface* icon = SDL_CreateRGBSurfaceFrom(img, image_width, image_height, depth, pitch, rmask, gmask, bmask, amask);
				SDL_SetWindowIcon(fWindow, icon);
				SDL_FreeSurface(icon);

				free(img);
			}
		}
	}

	bool SolarApp::Init()
	{
		if (InitSDL())
		{
			return LoadApp(fResourceDir);
		}
		return false;
	}

	void SolarApp::GetWindowPosition(int* x, int* y)
	{
		SDL_GetWindowPosition(fWindow, x, y);
	}

	void SolarApp::GetWindowSize(int* w, int* h)
	{
		SDL_GetWindowSize(fWindow, w, h);
		*h -= GetMenuHeight();
	}

	bool SolarApp::LoadApp(const string& path)
	{
		fContext = new SolarAppContext(fWindow);
		CreateMenu();
		return fContext->LoadApp(fResourceDir);
	}

	bool SolarApp::PollEvents()
	{
		// TEMPORARY debug hook: synthesize clicks at LUMIN_TESTCLICK="x,y;x,y;..."
		if (const char* spec = getenv("LUMIN_TESTCLICK"))
		{
			static int sFrames = 0;
			++sFrames;

			int index = 0;
			const char* p = spec;
			while (p && *p)
			{
				int cx = 0, cy = 0;
				if (sscanf(p, "%d,%d", &cx, &cy) == 2)
				{
					const int downAt = 120 + index * 90;
					if (sFrames == downAt || sFrames == downAt + 6)
					{
						SDL_Event e; SDL_zero(e);
						e.type = (sFrames == downAt) ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
						e.button.windowID = SDL_GetWindowID(fWindow);
						e.button.button = SDL_BUTTON_LEFT;
						e.button.state = (sFrames == downAt) ? SDL_PRESSED : SDL_RELEASED;
						e.button.clicks = 1;
						e.button.x = cx; e.button.y = cy;
						fprintf(stderr, "TESTCLICK #%d %s at %d,%d\n", index, (sFrames == downAt) ? "down" : "up", cx, cy);
						SDL_PushEvent(&e);
					}
				}
				++index;
				p = strchr(p, ';');
				if (p) ++p;
			}
		}

		vector<SDL_Event> events;
		SDL_Event evt;
		while (SDL_PollEvent(&evt))
		{
			// GUI
			ImGui_ImplSDL2_ProcessEvent(&evt);

			if (fConsole)
				fConsole->ProcessEvent(evt);

			if (fDlg)
				fDlg->ProcessEvent(evt);

			// Before anything else looks at it: a click on the bar, or
			// anywhere while a menu is open, belongs to the bar and to nothing
			// underneath it.
			if (ProcessMenuBarEvent(evt))
				continue;

			if (evt.type == SDL_QUIT)
				return false;
			if (evt.type == SDL_WINDOWEVENT && evt.window.event == SDL_WINDOWEVENT_CLOSE && evt.window.windowID == SDL_GetWindowID(fWindow))
				return false;

			events.push_back(evt);
		}

		ImGuiIO& io = ImGui::GetIO();
		for (int i = 0; i < events.size(); i++)
		{
			//SDL_Log("SDL_EVENT %d\n", event.type);
			//U64 start_time = Rtt_AbsoluteToMilliseconds(Rtt_GetAbsoluteTime());
			const SDL_Event& evt = events[i];
			switch (evt.type)
			{
			case SDL_APP_WILLENTERFOREGROUND:
			{
				fContext->Resume();
				break;
			}
			case SDL_APP_WILLENTERBACKGROUND:
			{
				fContext->Pause();
				break;
			}

			case sdl::OnSetCursor:
			{
				string cursorName = (const char*)evt.user.data1;
				free(evt.user.data1);
				// todo
				break;
			}
			case sdl::OnMouseCursorVisible:
				SDL_ShowCursor(evt.user.code ? SDL_ENABLE : SDL_DISABLE);
				break;

			case sdl::OnWindowNormal:
				SDL_RestoreWindow(fWindow);
				fContext->Resume();
				break;

			case sdl::OnWindowMaximized:
				SDL_MaximizeWindow(fWindow);
				fContext->Resume();
				break;

			case sdl::OnWindowFullscreen:
				SDL_SetWindowFullscreen(fWindow, SDL_WINDOW_FULLSCREEN);	// SDL_WINDOW_FULLSCREEN_DESKTOP
				fContext->Resume();
				break;

			case sdl::OnWindowMinimized:
				SDL_MinimizeWindow(fWindow);
				fContext->Pause();
				break;

			case SDL_WINDOWEVENT:
			{
				//SDL_Log("SDL_WINDOWEVENT %d: %d %d,%d", e.window.windowID, e.window.event, e.window.data1, e.window.data2);
				switch (evt.window.event)
				{
				case SDL_WINDOWEVENT_MAXIMIZED:
					break;

				case SDL_WINDOWEVENT_SHOWN:
				case SDL_WINDOWEVENT_RESTORED:
					if (evt.window.windowID == SDL_GetWindowID(fWindow))
					{
						fContext->Resume();
					}
					break;
				case SDL_WINDOWEVENT_HIDDEN:
				case SDL_WINDOWEVENT_MINIMIZED:
					if (evt.window.windowID == SDL_GetWindowID(fWindow))
					{
						fContext->Pause();
					}
					break;
				case SDL_WINDOWEVENT_SIZE_CHANGED:
				{
					if (evt.window.windowID == SDL_GetWindowID(fWindow))
					{
						// Both the backend and Corona itself need the new size:
						// the backend so it stops presenting at the old one,
						// the runtime so the content scale, the projection and
						// the "resize" event follow the window.
						//
						// Computed here rather than through GetWindowSize,
						// which subtracts the menu height with "=" where it
						// means "-=" and so cannot report a usable height.
						if (fContext != NULL && fContext->GetRuntime() != NULL)
						{
							int w, h;
							SDL_GetWindowSize(fWindow, &w, &h);
							h -= GetMenuHeight();

							if (w > 0 && h > 0)
							{
								fContext->OnSurfaceResized(w, h);
							}
						}
					}
					break;
				}
				case SDL_WINDOWEVENT_MOVED:
				{
					if (evt.window.windowID == SDL_GetWindowID(fWindow) && IsHomeScreen(GetAppName()))
					{
						fConfig["x"] = evt.window.data1;
						fConfig["y"] = evt.window.data2;
					}
					break;
				}
				case SDL_WINDOWEVENT_CLOSE:
				{
					break;
				}
				default:
					break;
				}
				break;
			}

			case SDL_FINGERDOWN:
			case SDL_FINGERUP:
			case SDL_FINGERMOTION:
			case SDL_MOUSEBUTTONDOWN:
			case SDL_MOUSEBUTTONUP:
			case SDL_MOUSEMOTION:
			case SDL_MOUSEWHEEL:
				// When io.WantCaptureMouse is true, do not dispatch mouse input data to your main application.
				if (fDlg == NULL && (evt.window.windowID == SDL_GetWindowID(fWindow)) && !io.WantCaptureMouse)
				{
					// focus is in native objects ?
					if (!DispathNativeObjectsEvent(evt))
					{
						fMouse->OnEvent(evt, fWindow);
					}
				}
				break;

			case SDL_KEYDOWN:
			{
				// ignore key repeat
				if (evt.key.repeat == 0)
				{
					SDL_Keycode	keycode = evt.key.keysym.sym;
					uint16_t mod = evt.key.keysym.mod;
					bool isNumLockDown = mod & KMOD_NUM ? true : false;
					bool isCapsLockDown = mod & KMOD_CAPS ? true : false;
					bool isShiftDown = mod & KMOD_SHIFT ? true : false;
					bool isCtrlDown = mod & KMOD_CTRL ? true : false;
					bool isAltDown = mod & KMOD_ALT ? true : false;
					bool isCommandDown = mod & KMOD_GUI ? true : false;

					PlatformInputDevice* dev = NULL;
					const char* keyName = GetKeyName(keycode);
					KeyEvent ke(dev, KeyEvent::kDown, keyName, keycode, isShiftDown, isAltDown, isCtrlDown, isCommandDown);
					GetRuntime()->DispatchEvent(ke);
				}
				break;
			}

			case SDL_KEYUP:
			{
				SDL_Keycode	keycode = evt.key.keysym.sym;
				uint16_t mod = evt.key.keysym.mod;
				bool isNumLockDown = mod & KMOD_NUM ? true : false;
				bool isCapsLockDown = mod & KMOD_CAPS ? true : false;
				bool isShiftDown = mod & KMOD_SHIFT ? true : false;
				bool isCtrlDown = mod & KMOD_CTRL ? true : false;
				bool isAltDown = mod & KMOD_ALT ? true : false;
				bool isCommandDown = mod & KMOD_GUI ? true : false;

				PlatformInputDevice* dev = NULL;
				const char* keyName = GetKeyName(keycode);
				KeyEvent ke(dev, KeyEvent::kUp, keyName, keycode, isShiftDown, isAltDown, isCtrlDown, isCommandDown);
				GetRuntime()->DispatchEvent(ke);
				break;
			}

			default:
				SolarEvent(evt);
				break;
			}

			//int advance_time = (int)(Rtt_AbsoluteToMilliseconds(Rtt_GetAbsoluteTime()) - start_time);
			//	Rtt_Log("event %x, advance time %d\n", event.type, advance_time);
		}

#if defined( Rtt_USE_BGFX )
		// Not from the drawing, for the reason given below: a still scene
		// never reaches it, and the bar would never come up at all.
		EnsureMenuBar();

		// The bar rides along in the runtime's frame, and the runtime renders
		// only when its own content changed -- a still scene presents nothing
		// for as long as it stays still. A menu opening, or the highlight
		// following the cursor, is not something the runtime knows about, so
		// it has to be told, or the bar catches up with the mouse only
		// whenever the content next happens to redraw.
		//
		// This belongs in the event loop rather than in the drawing, which is
		// itself only reached from a frame the runtime decided to render: a
		// still scene would never get as far as asking.
		if (GetRuntime() != NULL && fMenuBar.NeedsRedraw())
		{
			GetRuntime()->GetDisplay().Invalidate();
		}
#endif

		return true;
	}

	bool SolarApp::DispathNativeObjectsEvent(const SDL_Event& evt)
	{
		for (int i = 0; i < fNativeObjects.size(); i++)
		{
			if (fNativeObjects[i]->ProcessEvent(evt))
			{
				// focus is in native object
				return true;
			}
		}
		return false;
	}

	void SolarApp::Run()
	{
		// When the next frame is due. Kept as a running deadline rather than
		// being derived from "now" at the end of each pass, because the loop no
		// longer runs once per frame: input wakes it up in between, and a frame
		// rate measured from whenever that last happened is not a frame rate.
		U64 nextFrameTime = Rtt_AbsoluteToMilliseconds(Rtt_GetAbsoluteTime());

		// main app loop
		while (1)
		{
			// Read per pass rather than once: the fps a project asks for is only
			// known after it has loaded, and the welcome screen's is not the one
			// whatever gets opened from it will run at.
			const int fps = fContext->GetFPS();
			const U64 frameDuration = U64(1000.0f / Max(fps, 1));

			// Events are taken as soon as they arrive, whether or not a frame is
			// due. Everything the menu bar reacts to -- the highlight following
			// the cursor, a menu opening -- is settled here, so waiting for the
			// frame boundary to so much as look at a click was pure latency: at
			// the welcome screen's frame rate, most of what the bar's lag was.
			if (!PollEvents())
				break;

			U64 now = Rtt_AbsoluteToMilliseconds(Rtt_GetAbsoluteTime());

			// Advancing is what the frame rate governs, and only it: the runtime
			// has no clock of its own here, and every call is a frame -- one more
			// enterFrame, one more step of every transition. Running it whenever
			// the mouse moved would simply make the project play faster.
			if (now >= nextFrameTime)
			{
				if (fConsole)
					fConsole->Draw();

				fContext->advance();

				if (fDlg)
					fDlg->Draw();

				now = Rtt_AbsoluteToMilliseconds(Rtt_GetAbsoluteTime());

				// A frame that overran is a frame that overran; the ones it ate
				// into are not run back to back afterwards to make up for it.
				nextFrameTime = Max(nextFrameTime + frameDuration, now);
			}

			// Don't hog the CPU -- but wake the moment there is something to
			// react to. SDL_WaitEventTimeout with no event to fill in leaves what
			// woke it on the queue, for the PollEvents at the top of the next
			// pass to take.
			const U64 wait = nextFrameTime > now ? nextFrameTime - now : 1;

			SDL_WaitEventTimeout(NULL, int(wait));
		}
	}

	void SolarApp::Log(const char* buf, int len)
	{
		// truncate
		const int maxsize = 20000;
		if (fLogData.size() > maxsize)
		{
			fLogData.erase(0, (fLogData.size() - maxsize) * 0.9);	// 10%
		}
		fLogData.append(buf, len);
	}

	void SolarApp::EnsureMenuBar()
	{
#if defined( Rtt_USE_BGFX )
		if (fMenuBar.IsInitialized() || fMenuBarFailed || GetRuntime() == NULL)
		{
			return;
		}

		// bgfx has to exist before the bar can bake a font or compile a
		// shader, and it does not until the runtime has built its renderer --
		// which is why this is here rather than in Init.
		BgfxRenderer& renderer = static_cast<BgfxRenderer&>(GetRuntime()->GetDisplay().GetRenderer());

		if (!fMenuBar.Initialize(default_ttf, sizeof(default_ttf)))
		{
			// Without a bar the simulator is harder to use but still runs, and
			// every command on it has a keyboard shortcut.
			//
			// Remembered, because this is reached once a frame: bringing the
			// bar up compiles two shaders, and retrying that every frame would
			// cost far more than the bar it is failing to produce.
			fMenuBarFailed = true;

			Rtt_LogException("WARNING: the simulator's menu bar could not be created\n");
			return;
		}

		fMenuBar.SetCommandHandler(&SolarApp::OnMenuCommand, this);

		renderer.SetOverlay(&SolarApp::RenderOverlay, &SolarApp::ReleaseOverlay, this);

		CreateMenu();
#endif
	}

	void SolarApp::ReleaseOverlay(void* userdata)
	{
		// The menus themselves are kept: they are plain data, and the next
		// runtime's bar wants the same ones. Only the bgfx handles go.
		static_cast<SolarApp*>(userdata)->fMenuBar.Finalize();
	}

	void SolarApp::RenderOverlay(void* userdata, U16 view, U32 width, U32 height)
	{
		SolarApp* self = static_cast<SolarApp*>(userdata);

		// A modal dialog is still Dear ImGui's, and it takes over: the bar
		// greys out and stops responding until the dialog goes away, which is
		// what the ImGui::BeginDisabled around the old bar did.
		self->fMenuBar.SetEnabled(self->fDlg == NULL);

		self->fMenuBar.Render(view, width, height);
	}

	void SolarApp::OnMenuCommand(void* userdata, int command)
	{
		SolarApp* self = static_cast<SolarApp*>(userdata);

		if (SimulatorCommand::kSuspendResume == command)
		{
			if (self->IsSuspended())
			{
				self->Resume();
			}
			else
			{
				self->Pause();
			}

			// The item's label is the state, so the menus are rebuilt rather
			// than re-read.
			self->CreateMenu();

			return;
		}

		// Everything else goes back through the event queue, which is where
		// the commands were already handled from: a menu item and the dialog
		// button that does the same thing end up in the same place.
		const int evt = SdlEventForCommand(command);

		if (evt != 0)
		{
			PushEvent(evt);
		}
	}

	bool SolarApp::ProcessMenuBarEvent(const SDL_Event& e)
	{
		if (!fMenuBar.IsInitialized() || fDlg != NULL)
		{
			return false;
		}

		// The console is a window of its own, and its events arrive in the
		// same queue. Every case below carries a window id in the same place,
		// but SDL_QUIT and the user events do not, so this cannot be hoisted
		// out of the switch.
		const Uint32 windowID = SDL_GetWindowID(fWindow);

		switch (e.type)
		{
		case SDL_MOUSEMOTION:
			return e.motion.windowID == windowID
				&& fMenuBar.OnMouseMove(e.motion.x, e.motion.y);

		case SDL_MOUSEBUTTONDOWN:
			return e.button.windowID == windowID && SDL_BUTTON_LEFT == e.button.button
				&& fMenuBar.OnMouseDown(e.button.x, e.button.y);

		case SDL_MOUSEBUTTONUP:
			return e.button.windowID == windowID && SDL_BUTTON_LEFT == e.button.button
				&& fMenuBar.OnMouseUp(e.button.x, e.button.y);

		case SDL_MOUSEWHEEL:
			// Nothing on the bar scrolls, but a wheel event while a menu is
			// open should not reach the content behind it either.
			return e.wheel.windowID == windowID && fMenuBar.IsOpen();

		case SDL_KEYDOWN:
		{
			if (e.key.windowID != windowID)
			{
				return false;
			}

			if (SDLK_ESCAPE == e.key.keysym.sym && fMenuBar.IsOpen())
			{
				fMenuBar.Close();
				return true;
			}

			const int command = CommandForKeyEvent(e.key, IsHomeScreen(GetAppName()));

			if (command != SimulatorCommand::kNone)
			{
				fMenuBar.Close();

				OnMenuCommand(this, command);

				return true;
			}

			return false;
		}

		case SDL_WINDOWEVENT:
			// A menu left open when the window loses focus would still be
			// showing when it comes back, over content that has moved on.
			if (e.window.windowID == windowID && SDL_WINDOWEVENT_FOCUS_LOST == e.window.event)
			{
				fMenuBar.Close();
			}
			return false;

		default:
			return false;
		}
	}

	void SolarApp::RenderGUI()
	{
#if defined( Rtt_USE_BGFX )
		if (GetRuntime() == NULL)
		{
			return;
		}

		// While the runtime is running, its own frame carries the bar: the
		// renderer's overlay hook draws it as the last thing before the frame
		// is submitted. A suspended runtime submits no frames at all, though,
		// so the bar -- which is how it gets resumed -- has to be given one.
		if (IsSuspended())
		{
			fMenuBar.SetStatusText("Suspended");

			BgfxRenderer& renderer = static_cast<BgfxRenderer&>(GetRuntime()->GetDisplay().GetRenderer());

			renderer.RenderOverlayFrame(true);
		}
		else
		{
			fMenuBar.SetStatusText(NULL);
		}

		return;
#else
		if (fImCtx == NULL)
			return;

		SDL_GL_MakeCurrent(fWindow, fGLcontext);
		ImGui::SetCurrentContext(fImCtx);

		// draw GUI
		ImGui_ImplOpenGL3_NewFrame();
		ImGui_ImplSDL2_NewFrame();
		ImGui::NewFrame();

		if (IsSuspended())
		{
			glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
			glClear(GL_COLOR_BUFFER_BIT);

			// Always center this window when appearing
			ImVec2 center = ImGui::GetMainViewport()->GetCenter();
			ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));

			if (ImGui::Begin("##Suspended", NULL, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs))
			{
				ImGui::Text("Suspended");
				ImGui::End();
			}
		}

		for (int i = 0; i < fNativeObjects.size(); i++)
		{
			fNativeObjects[i]->Draw();
		}

		// Activity Indicator
		if (fActivityIndicator)
		{
			DrawActivity();
		}

		ImGui::EndFrame();

		ImGui::Render();
		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
#endif
	}

	void SolarApp::OnIconized()
	{
		fContext->RestartRenderer();
	}

	void SolarApp::SetWindowSize(int w, int h)
	{
		SDL_SetWindowSize(fWindow, w, h + GetMenuHeight());
	}

	void SolarApp::AddDisplayObject(LinuxDisplayObject* obj)
	{
		fNativeObjects.push_back(obj);

		// sanity check
		Rtt_ASSERT(fNativeObjects < 1000);
	}

	void SolarApp::RemoveDisplayObject(LinuxDisplayObject* obj)
	{
		const auto& it = find(fNativeObjects.begin(), fNativeObjects.end(), obj);
		if (it != fNativeObjects.end())
		{
			fNativeObjects.erase(it);
		}
	}

	NativeAlertRef SolarApp::ShowNativeAlert(const char* title, const char* msg, const char** buttonLabels, U32 numButtons, LuaResource* resource)
	{
		fDlg = new DlgAlert(title, msg, buttonLabels, numButtons, resource);
		return NULL;
	}

	//
	// FileWatcher
	//

	FileWatcher::FileWatcher()
		: m_inotify_fd(-1)
		, m_watch_descriptor(-1)
	{
	}

	bool FileWatcher::Start(const string& folder)
	{
		if (m_inotify_fd >= 0)
		{
			// re-entry
			Stop();
		}

		m_inotify_fd = inotify_init();
		if (m_inotify_fd < 0)
		{
			Rtt_LogException("inotify_init failed for %s\n", folder.c_str());
			return false;
		}

		//adding the directory into watch list.
		m_watch_descriptor = inotify_add_watch(m_inotify_fd, folder.c_str(), IN_CREATE | IN_DELETE | IN_MODIFY);
		if (m_watch_descriptor < 0)
		{
			Rtt_LogException("inotify_add_watch failed for %s\n", folder.c_str());
			close(m_inotify_fd);
			m_inotify_fd = -1;
			return false;
		}

		fcntl(m_inotify_fd, F_SETFL, fcntl(m_inotify_fd, F_GETFL) | O_NONBLOCK);

		fThread = new mythread();
		fThread->start([this]() { Watch(); });

		return true;
	}

	void FileWatcher::Stop()
	{
		fThread = NULL;
		if (m_inotify_fd >= 0)
		{
			if (m_watch_descriptor >= 0)
			{
				inotify_rm_watch(m_inotify_fd, m_watch_descriptor);
			}
			close(m_inotify_fd);

			m_inotify_fd = -1;
			m_watch_descriptor = -1;
		}
	}

	FileWatcher::~FileWatcher()
	{
		Stop();
	}

	// thread func
	void FileWatcher::Watch()
	{
		while (fThread && fThread->is_running())
		{
			if (m_inotify_fd >= 0 && m_watch_descriptor >= 0)
			{
				const int EVENT_SIZE = sizeof(struct inotify_event);
				const int EVENT_BUF_LEN = 1024 * (EVENT_SIZE + 16);

				char buffer[EVENT_BUF_LEN];
				int length = read(m_inotify_fd, buffer, EVENT_BUF_LEN);
				if (length < 0)
				{
					int rc = errno;
					switch (rc)
					{
					case EAGAIN:
						length = 0;
						break;
					default:
						Rtt_LogException("failed to read onFileChanged event\n");
						Stop();
						return;
					}
				}

				// actually the read returns the list of change events happens. Here, read the change event one by one and process it accordingly.
				// actually the read returns the list of change events happens. Here, read the change event one by one and process it accordingly.
				int i = 0;
				while (i < length)
				{
					struct inotify_event* event = (struct inotify_event*)&buffer[i];
					if (event->len > 0 && strlen(event->name) > 0)
					{
						SDL_Event e = {};
						e.type = sdl::OnFileSystemEvent;
						e.user.code = event->mask;
						e.user.data1 = strdup(event->name);
						SDL_PushEvent(&e);
					}
					i += EVENT_SIZE + event->len;
				}
			}

			this_thread::sleep_for(chrono::milliseconds(100));
		}
	}

	//
	// Config
	//

	Config::Config()
	{
	}

	Config::Config(const string& path, bool crypted)
	{
		fIsCrypted = crypted;
		Load(path);
	}

	Config::~Config()
	{
		Save();
	}

	void Config::Load(const string& path, bool crypted)
	{
		fIsCrypted = crypted;
		fPath = path;		// save
		FILE* f = fopen(path.c_str(), "r");
		if (f)
		{
			char line[1024];
			while (fgets(line, sizeof(line), f))
			{
				string s = line;
				s = trim(s);

				if (fIsCrypted)
				{
					s = Decrypt(s);
					if (s.empty())
					{
						continue;
					}
				}

				vector<string> a;
				splitString(a, s, "=");
				if (a.size() > 0)
				{
					fConfig[a[0]] = a.size() > 1 ? a[1] : "";

					// load to env
					if (a[0] == "debugBuildProcess" && a.size() > 1)
					{
						setenv(a[0].c_str(), a[1].c_str(), true);
					}
				}
			}
			fclose(f);
		}
	}

	// load & join & save
	void Config::Save()
	{
		FILE* f = fopen(fPath.c_str(), "w");
		if (f)
		{
			for (const auto& it : fConfig)
			{
				string s = it.first;
				s.append("=");
				s.append(it.second.to_string());

				if (fIsCrypted)
				{
					s = Encrypt(s);
					if (s.empty())
					{
						continue;
					}
				}

				fprintf(f, "%s\n", s.c_str());

				// save to env
				if (it.first == "debugBuildProcess")
				{
					setenv(it.first.c_str(), it.second.c_str(), true);
				}
			}
			fclose(f);
		}
	}

	as_value& Config::operator[](const string& name)
	{
		auto it = fConfig.find(name);
		if (it == fConfig.end())
		{
			fConfig[name] = as_value();
			it = fConfig.find(name);
		}
		return it->second;
	}

	const as_value& Config::operator[](const string& name) const
	{
		static as_value undefined;
		undefined.set_undefined();

		const auto& it = fConfig.find(name);
		if (it == fConfig.end())
			return undefined;
		else
			return it->second;
	}

}	// Rtt
