//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Corona game engine.
// For overview and more information on licensing please refer to README.md 
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include <shlwapi.h>
#include <windowsx.h>	// GET_X_LPARAM
#include <gdiplus.h>
#include <stdlib.h>
#include <math.h>

#include "Core\Rtt_Build.h"
#include "Interop\UI\TaskDialog.h"
#include "Interop\MDeviceSimulatorServices.h"
#include "Interop\SimulatorRuntimeEnvironment.h"
#include "Rtt_LuaContext.h"
#include "Rtt_LuaFile.h"
#include "Rtt_MPlatform.h"
#include "Rtt_PlatformAppPackager.h"
#include "Rtt_PlatformPlayer.h"
#include "Rtt_PlatformSimulator.h"
#include "Rtt_RenderingStream.h"
#include "Rtt_Runtime.h"
#include "Rtt_SimulatorAnalytics.h"
#include "Rtt_WinPlatform.h"
#include "Rtt_WinSimulatorServices.h"
#include "Simulator.h"
#include "MainFrm.h"
#include "SimulatorDoc.h"
#include "SimulatorView.h"
#include "AboutDlg.h"
#include "BuildAndroidDlg.h"
#include "BuildWebDlg.h"
#include "BuildLinuxDlg.h"
#include "BuildNxSDlg.h"
#include "BuildWin32AppDlg.h"
#include "NewProjectDlg.h"
#include "SelectSampleProjectDlg.h"
#include "RelaunchPrefDlg.h"
#include "WinString.h"
#include "WinGlobalProperties.h"  // WMU_ message IDs
#include "PreferencesDlg.h"
#include "ProgressWnd.h"  // ActivityIndicator
#include "MessageDlg.h"   // Alert
#include "CoronaInterface.h"
#include "Rtt_SimulatorRecents.h"
#include "CustomDeviceDlg.h"
#include "UI\Rtt_SimulatorMenus.h"
#if defined( Rtt_USE_BGFX )
#	include "Renderer\Rtt_BgfxRenderer.h"
#	include <vector>
#endif

// ----------------------------------------------------------------------------

// Microsoft Visual C++ macro which allows us to easily do bitwise operations on the given enum like in C.
DEFINE_ENUM_FLAG_OPERATORS(Rtt::Runtime::LaunchOptions)

#define ENABLE_DEBUG_PRINT	0

#if ENABLE_DEBUG_PRINT
	#define DEBUG_PRINT( ... ) Rtt_LogException( __VA_ARGS__ );
#else
	#define DEBUG_PRINT( ... )
#endif

// ----------------------------------------------------------------------------

// The #includes must go above this line because this macro will override the "new" operator
// which will cause compiler errors with header files belonging to other libraries.
#ifdef _DEBUG
#	define new DEBUG_NEW
#endif

#pragma region Timer IDs
static const UINT TIMER_ID_CHECK_APP = 1;
static const UINT TIMER_ID_SHAKE_WINDOW = 2;

#pragma endregion


// Utility function
Gdiplus::RotateFlipType DegreesToRFType( int rotation );


#pragma region Message Mappings
IMPLEMENT_DYNCREATE(CSimulatorView, CView)

BEGIN_MESSAGE_MAP(CSimulatorView, CView)
	ON_WM_CREATE()
    ON_WM_CLOSE()
	ON_WM_DESTROY()
	ON_WM_ERASEBKGND()
	ON_WM_SETFOCUS()
	ON_WM_TIMER()
	ON_COMMAND(ID_APP_ABOUT, &CSimulatorView::OnAppAbout)
	ON_COMMAND(ID_HELP, &CSimulatorView::OnHelp)
	ON_COMMAND(ID_VIEW_HOME_SCREEN, &CSimulatorView::OnViewHomeScreen)
	ON_COMMAND(ID_VIEW_CONSOLE, &CSimulatorView::OnViewConsole)
	ON_COMMAND(ID_VIEW_SHAKE, &CSimulatorView::OnViewShake)
	ON_COMMAND(ID_VIEW_SUSPEND, &CSimulatorView::OnViewSuspend)
	ON_COMMAND(ID_VIEW_ROTATELEFT, &CSimulatorView::OnViewRotateLeft)
	ON_COMMAND(ID_VIEW_ROTATERIGHT, &CSimulatorView::OnViewRotateRight)
	ON_COMMAND(ID_VIEW_NAVIGATE_BACK, &CSimulatorView::OnViewNavigateBack)
	ON_COMMAND(ID_FILE_MRU_FILE1, &CSimulatorView::OnFileMRU1)
	ON_COMMAND(ID_FILE_NEW, &CSimulatorView::OnFileNew)
	ON_COMMAND(ID_FILE_OPEN, &CSimulatorView::OnFileOpen)
	ON_COMMAND(ID_FILE_OPEN_SAMPLE_PROJECT, &CSimulatorView::OnFileOpenSampleProject)
	ON_COMMAND(ID_FILE_PREFERENCES, &CSimulatorView::OnPreferences)
	ON_COMMAND(ID_BUILD_FOR_ANDROID, &CSimulatorView::OnBuildForAndroid)
	ON_COMMAND(ID_BUILD_FOR_WEB, &CSimulatorView::OnBuildForWeb)
	ON_COMMAND(ID_BUILD_FOR_LINUX, &CSimulatorView::OnBuildForLinux)
	ON_COMMAND(ID_BUILD_FOR_NXS, &CSimulatorView::OnBuildForNxS)
	ON_COMMAND(ID_BUILD_FOR_WIN32, &CSimulatorView::OnBuildForWin32)
	ON_COMMAND(ID_FILE_OPENINEDITOR, &CSimulatorView::OnFileOpenInEditor)
	ON_COMMAND(ID_FILE_RELAUNCH, &CSimulatorView::OnFileRelaunch)
	ON_COMMAND(ID_FILE_CLOSE, &CSimulatorView::OnFileClose)
	ON_COMMAND(ID_FILE_SHOW_PROJECT_FILES, &CSimulatorView::OnShowProjectFiles)
	ON_COMMAND(ID_FILE_SHOWPROJECTSANDBOX, &CSimulatorView::OnShowProjectSandbox)
	ON_COMMAND(ID_FILE_CLEARPROJECTSANDBOX, &CSimulatorView::OnClearProjectSandbox)
	ON_UPDATE_COMMAND_UI(ID_VIEW_SUSPEND, &CSimulatorView::OnUpdateViewSuspend)
	ON_COMMAND_RANGE(ID_VIEWAS_BEGIN, ID_VIEWAS_END, &CSimulatorView::OnWindowViewAs)
	ON_UPDATE_COMMAND_UI_RANGE(ID_VIEWAS_BEGIN, ID_VIEWAS_END, &CSimulatorView::OnUpdateWindowViewAs )
	ON_UPDATE_COMMAND_UI(ID_VIEW_VIEWAS, &CSimulatorView::OnUpdateWindowViewAs )
	ON_UPDATE_COMMAND_UI(ID_VIEW_NAVIGATE_BACK, &CSimulatorView::OnUpdateViewNavigateBack)
	ON_UPDATE_COMMAND_UI(ID_FILE_RELAUNCH, &CSimulatorView::OnUpdateFileRelaunch)
	ON_UPDATE_COMMAND_UI(ID_FILE_CLOSE, &CSimulatorView::OnUpdateFileClose)
	ON_UPDATE_COMMAND_UI(ID_BUILD_FOR_ANDROID, &CSimulatorView::OnUpdateBuildMenuItem)
	ON_UPDATE_COMMAND_UI(ID_BUILD_FOR_WEB, &CSimulatorView::OnUpdateBuildMenuItem)
	ON_UPDATE_COMMAND_UI(ID_BUILD_FOR_LINUX, &CSimulatorView::OnUpdateBuildMenuItem)
	ON_UPDATE_COMMAND_UI(ID_BUILD_FOR_NXS, &CSimulatorView::OnUpdateBuildMenuItem)
	ON_UPDATE_COMMAND_UI(ID_BUILD_FOR_WIN32, &CSimulatorView::OnUpdateBuildMenuItem)
	ON_UPDATE_COMMAND_UI(ID_FILE_OPENINEDITOR, &CSimulatorView::OnUpdateFileOpenInEditor)
	ON_UPDATE_COMMAND_UI(ID_FILE_SHOW_PROJECT_FILES, &CSimulatorView::OnUpdateShowProjectFiles)
	ON_UPDATE_COMMAND_UI(ID_FILE_SHOWPROJECTSANDBOX, &CSimulatorView::OnUpdateShowProjectSandbox)
	ON_MESSAGE(WMU_ACTIVITYINDICATOR, &CSimulatorView::OnActivityIndicator)
	ON_MESSAGE(WMU_NATIVEALERT, &CSimulatorView::OnNativeAlert)
END_MESSAGE_MAP()

BEGIN_MESSAGE_MAP(CSimulatorView::CCoronaControlContainer, CStatic)
	ON_WM_CREATE()
	ON_WM_CTLCOLOR_REFLECT()
	ON_WM_SIZE()
END_MESSAGE_MAP()

#pragma endregion


#pragma region Constructor/Destructor
/// Creates a new Corona Simulator CView.
CSimulatorView::CSimulatorView()
:	mScopedComInitializer(Interop::ScopedComInitializer::ApartmentType::kSingleThreaded),
	mSimulatorServices(*this),
	mMessageDlgPointer(nullptr),
	mProgressDlgPointer(nullptr),
	mDeviceConfig(*Rtt_AllocatorCreate()),
	mRuntimeLoadedEventHandler(this, &CSimulatorView::OnRuntimeLoaded)
{
	CSimulatorApp *applicationPointer = (CSimulatorApp*)AfxGetApp();

    mRotation = 0;  // current rotation
    mpSkinBitmap = nullptr;
	mRuntimeEnvironmentPointer = nullptr;
	mIsShowingInternalScreen = false;
	mDisplayName = applicationPointer->GetDisplayName();
	mAppChangeHandle = nullptr;
	mShakeAmount = SIM_SHAKE_AMOUNT;
	mShakeReps = SIM_SHAKE_REPS;
	mShakeNum = 0;
	m_nSkinId = Rtt::TargetDevice::kUnknownSkin;
	mRelaunchCount = 0;
#if defined( Rtt_USE_BGFX )
	mMenuBarFailed = false;
#endif
}

/// Destructor. Destroys owned objects.
CSimulatorView::~CSimulatorView()
{
	CStringA relaunchCountStr;
	relaunchCountStr.Format("%d", mRelaunchCount);
    GetWinProperties()->GetAnalytics()->Log("relaunch", "count", relaunchCountStr);

	if (mRuntimeEnvironmentPointer)
	{
		Interop::SimulatorRuntimeEnvironment::Destroy(mRuntimeEnvironmentPointer);
		mRuntimeEnvironmentPointer = nullptr;
	}
	if (mAppChangeHandle)
	{
		::FindCloseChangeNotification(mAppChangeHandle);
		mAppChangeHandle = nullptr;
	}
	if (mMessageDlgPointer)
	{
		if (mMessageDlgPointer->GetSafeHwnd())
		{
			mMessageDlgPointer->SendMessage(WM_COMMAND, IDCANCEL, 0);
		}
		delete mMessageDlgPointer;
		mMessageDlgPointer = nullptr;
	}
	if (mProgressDlgPointer)
	{
		delete mProgressDlgPointer;
		mProgressDlgPointer = nullptr;
	}
}

#pragma endregion


#pragma region Window Event Handlers
/// Called after a new file was selected by the CSimulatorDoc.
void CSimulatorView::OnInitialUpdate()
{
}

/// Called after the CSimulatorDoc has been updated with a different file or if the file was closed.
/// Starts simulating the selected file or displays the home/welcome screen.
/// @param pSender Pointer to the view that requested an update. NULL if update came from the document.
/// @param lHint Contains information about the modification made to the document.
/// @param pHint Pointer to an object storing information about the modification.
///              NULL if not information has been provided.
void CSimulatorView::OnUpdate(CView* pSender, LPARAM lHint, CObject *pHint)
{
	// Start simulation with the new or updated file.
	// If the last file was closed, then this will display the home screen.

	InitializeSimulation(m_nSkinId);
}

// OnDestroy - clean up
void CSimulatorView::OnDestroy()
{
	StopSimulation();

	if (mpSkinBitmap)
	{
		delete mpSkinBitmap;
		mpSkinBitmap = nullptr;
	}

	CView::OnDestroy();
}

// PreCreateWindow - remove border, set default size
BOOL CSimulatorView::PreCreateWindow(CREATESTRUCT& cs)
{
    cs.style &= ~WS_BORDER;  // view window doesn't need a border
	cs.cx = 320; // Note that this doesn't do much, see Simulator.cpp for where the main frame size gets set.
	cs.cy = 480;
	return CView::PreCreateWindow(cs);
}

// OnCreate - init OpenGL
int CSimulatorView::OnCreate(LPCREATESTRUCT lpCreateStruct)
{
	if (CView::OnCreate(lpCreateStruct) == -1)
	{
		return -1;
	}

	// Add the Corona container control to the view.
	// This container control will always be sized to fit the screen within the device skin.
	RECT bounds;
	bounds.top = 0;
	bounds.left = 0;
	bounds.bottom = lpCreateStruct->cy;
	bounds.right = lpCreateStruct->cx;
	mCoronaContainerControl.Create(nullptr, WS_CHILD | WS_VISIBLE, bounds, this);
	mCoronaContainerControl.GetCoronaControl().ShowWindow(SW_HIDE);

	// Start the timer that implements the file modification detection feature
	SetTimer(TIMER_ID_CHECK_APP, 500, NULL);
	
	// Get system skin directory (we have to do this manually because the just-in-time
	// way that the platform is loaded in tandem with the first application means that the information 
	// is not available in the platform when we need it)
	Rtt_ASSERT(AfxGetApp() != NULL);
	mSystemSkinsDir = ((CSimulatorApp *)AfxGetApp())->GetResourceDir();
	mSystemSkinsDir += _T("\\Skins\\");

	// Get user's skins directory if there is one (we have to do this manually because the just-in-time
	// way that the platform is loaded in tandem with the first application means that the information 
	// is not available in the platform when we need it)
	TCHAR myDocs[MAX_PATH];
	myDocs[0] = 0;

	HRESULT hr = ::SHGetFolderPath(NULL, CSIDL_APPDATA, NULL, 0, myDocs);
	_tcscat_s(myDocs, MAX_PATH, _T("\\Corona Labs\\Corona Simulator\\Skins\\"));
	mUserSkinsDir = myDocs;

	mCustomSkinFileName = mUserSkinsDir + _T("CustomDevice.lua.tmp");

	LoadSkinResources();

	// Now that we've loaded the skins, get a valid skin id
	if (mDisplayName.IsEmpty())
	{
		m_nSkinId = Rtt::TargetDevice::fDefaultSkinID;
	}
	else if (mDisplayName == _T("CustomDevice"))
	{
		m_nSkinId = Rtt::TargetDevice::kCustomSkin;
	}
	else
	{
		m_nSkinId = Rtt::TargetDevice::SkinForLabel(CStringA(mDisplayName));
	}

	return 0;
}

void CSimulatorView::OnSetFocus(CWnd* pOldWnd)
{
	CWnd& coronaControl = mCoronaContainerControl.GetCoronaControl();
	if (coronaControl.IsWindowVisible())
	{
		coronaControl.SetFocus();
	}
}

bool CSimulatorView::HasApplicationChanged()
{
	static bool dialogRunning = false;
	bool result = false;

	// If we've put up a runtime error or are doing a build, the app will be suspended and
	// we don't want to restart here so behave as if we're already displaying the dialog.
	bool isSuspended = IsSimulationSuspended();
	if (!dialogRunning && !isSuspended && (GetDocument()->GetPath().GetLength() > 0))
	{
		if (mAppChangeHandle == NULL)
		{
			CString projectPath = GetDocument()->GetProject()->GetDir();

			if (projectPath.GetLength() < 3 ||	// path is too short
				(projectPath[0] == '\\' && projectPath[1] == '\\') ||	// path starts with "\\"
				(projectPath[1] == ':' && GetDriveType(projectPath.Left(3)) == DRIVE_REMOTE))	// path's drive is remote
			{
				// This a UNC or remote path and filesystem notifications don't work so bail

				return false;
			}

			WinString tstr;
			tstr.SetTCHAR(projectPath);

			// fprintf(stderr, "Project: %s\n", tstr.GetUTF8());
			mAppChangeHandle = FindFirstChangeNotification(tstr.GetTCHAR(), true, FILE_NOTIFY_CHANGE_LAST_WRITE);
		}

		dialogRunning = true;

		// See if there's been a change
		DWORD waitResult = 0;
		
		waitResult = WaitForSingleObject(mAppChangeHandle, 1);

		if (waitResult == WAIT_OBJECT_0)
		{
			result = true;

			// Drain the queue (there may be other notifications related to same file system change)
			while (WaitForSingleObject(mAppChangeHandle, 1) == WAIT_OBJECT_0)
				FindNextChangeNotification(mAppChangeHandle);

			// Sign up for future change notifications
			FindNextChangeNotification(mAppChangeHandle);
		}

		CSimulatorApp *applicationPointer = (CSimulatorApp*)AfxGetApp();
		if (result && (applicationPointer!= NULL && applicationPointer->GetRelaunchSimStyle() == RELAUNCH_SIM_ASK))
		{
			CRelaunchPrefDlg relaunchPrefDlg;

			int dlgResult = relaunchPrefDlg.DoModal();
			if (dlgResult != IDOK)
			{
				result = false;
			}
		}

		dialogRunning = false;
	}

	return result;
}

// OnTimer - redraw window
void CSimulatorView::OnTimer(UINT nIDEvent)
{
	CSimulatorApp *applicationPointer = (CSimulatorApp*)AfxGetApp();

	// Perform the scheduled operation.
	switch (nIDEvent)
	{
	case TIMER_ID_CHECK_APP:
#if defined( Rtt_USE_BGFX )
		// A suspended runtime advances nothing and so ends no frames, which under bgfx means the window stops
		// being presented at all -- and the bar has to keep working while suspended, since Resume is on it.
		// So it gets a frame of its own for as long as that lasts.
		if (mMenuBar.IsInitialized() && IsSimulationSuspended())
		{
			auto& renderer = mRuntimeEnvironmentPointer->GetRuntime()->GetDisplay().GetRenderer();
			auto bgfxRendererPointer = dynamic_cast<Rtt::BgfxRenderer*>(&renderer);
			if (bgfxRendererPointer)
			{
				bgfxRendererPointer->RenderOverlayFrame(true);
			}
		}
#endif
		if (applicationPointer!= NULL && applicationPointer->GetRelaunchSimStyle() != RELAUNCH_SIM_NEVER)
		{
			if (HasApplicationChanged())
			{
				// fprintf(stderr, "Relaunching the app due to project modification ...\n");
				// Restart the currently running project.
				RestartSimulation();
			}
		}

		break;

	case TIMER_ID_SHAKE_WINDOW:

		if (mShakeNum < mShakeReps)
		{
			int offset = ((mShakeNum % 2) == 0) ? mShakeAmount : -(mShakeAmount * 2);
			AfxGetMainWnd()->MoveWindow(mShakeOriginRect.left + offset, mShakeOriginRect.top, mShakeOriginRect.Width(), mShakeOriginRect.Height(), true);
			++mShakeNum;
		}
		else
		{
			// We're done, make sure the window is back where it should be
			AfxGetMainWnd()->MoveWindow(mShakeOriginRect, true);
			KillTimer(TIMER_ID_SHAKE_WINDOW);   
		}
		break;
	}
}

// OnClose- WinPlatformServices::Terminate() sends msg here, forward to Main window
// Also used if skin .png files are missing
void CSimulatorView::OnClose()
{
	StopSimulation();
    AfxGetMainWnd()->SendMessage( WM_CLOSE );
}

void CSimulatorView::OnDraw(CDC* pDC)
{
	// Fetch the region to draw in.
	CRect rect;
	GetClientRect(&rect);

	// Fetch the device bitmap to draw if a device was selected.
	// Ignore the device bitmap if the view is currently showing the home screen.
	Gdiplus::Bitmap *pBitmap = NULL;
	COLORREF backgroundColor = RGB(255, 255, 255);

	if (false == mIsShowingInternalScreen)
	{
		pBitmap = GetSkinBitmap();
		// TODO: implement background color in skin Lua
		if (!pBitmap || (GetDisplayName() == "iPad Retina"))
		{
			// Use a black background for skinless devices or white devices.
			backgroundColor = RGB(0, 0, 0);
		}
	}

	// Draw the background.
	if (pBitmap)
	{
		// Draw the device image in the background.
		CDC dcMemory;
		CBitmap mfcbitmap;
		HBITMAP hBmp;
		dcMemory.CreateCompatibleDC(pDC);
		pBitmap->GetHBITMAP(backgroundColor, &hBmp);
		mfcbitmap.Attach(hBmp);
		dcMemory.SelectObject(&mfcbitmap);
		pDC->SetStretchBltMode(COLORONCOLOR);  // improves skin rendering dramatically
		// The window is taller than the skin by the menu bar, which is drawn by bgfx into the render surface
		// rather than painted here; the skin goes below it, unstretched. Zero without bgfx.
		const int menuBarHeight = GetMenuBarHeight();

		pDC->StretchBlt(
				0, menuBarHeight, rect.Width(), rect.Height() - menuBarHeight,
				&dcMemory, 0, 0, pBitmap->GetWidth(), pBitmap->GetHeight(), SRCCOPY);
	}
	else
	{
		// Draw a solid background.
		CBrush brush(backgroundColor);
		CBrush* pOldBrush = pDC->SelectObject(&brush);
		CPen pen;
		pen.CreatePen(PS_SOLID, 3, backgroundColor);
		CPen* pOldPen = pDC->SelectObject(&pen);
		pDC->Rectangle(rect);
		pDC->SelectObject(pOldBrush);
		pDC->SelectObject(pOldPen);
	}
}

// OnEraseBkgnd - do nothing, to minimize flickering
BOOL CSimulatorView::OnEraseBkgnd(CDC* pDC)
{
	return TRUE;
}

#pragma endregion


#pragma region Menu Event Handlers
/// Displays the About Box window.
void CSimulatorView::OnAppAbout()
{
	CAboutDlg aboutDlg;
	aboutDlg.DoModal();
}

/// Displays help documentation on Corona Labs' website via the system's default web browser.
void CSimulatorView::OnHelp()
{
	try
	{
		const wchar_t kUrl[] = L"https://docs.coronalabs.com/guide";
		::ShellExecuteW(nullptr, L"open", kUrl, nullptr, nullptr, SW_SHOWNORMAL);
	}
	catch (...) { }
}

/// Closes the current project and displays the welcome/home screen.
void CSimulatorView::OnViewHomeScreen()
{
	// Do not continue if the machine does not meet the simulator's minimum OpenGL requirements.
	if (ValidateOpenGL() == false)
	{
		return;
	}

	// Display the home screen.
	StopSimulation();
	GetParentFrame()->OnUpdateFrameTitle(FALSE);
	SetRotation(0);
	RunCoronaProject(((CSimulatorApp*)AfxGetApp())->GetResourceDir() + _T("\\homescreen"));
	GetWinProperties()->GetAnalytics()->Log("show-welcome", NULL);

}

/// Closes the current project and displays the welcome/home screen.
void CSimulatorView::OnViewConsole()
{
	((CSimulatorApp*)AfxGetApp())->GetOutputViewerProcessPointer()->RequestShowMainWindow();

	GetWinProperties()->GetAnalytics()->Log("show-console", NULL);
}

// OnViewShake - handle Shake menu item
void CSimulatorView::OnViewShake()
{
	if (mRuntimeEnvironmentPointer && mRuntimeEnvironmentPointer->GetDeviceSimulatorServices())
	{
		mRuntimeEnvironmentPointer->GetDeviceSimulatorServices()->Shake();
		mShakeNum = 0;
		AfxGetMainWnd()->GetWindowRect(&mShakeOriginRect);
		SetTimer(TIMER_ID_SHAKE_WINDOW, SIM_SHAKE_PERIOD, NULL);
		GetWinProperties()->GetAnalytics()->Log("shake", NULL);
	}
}

// OnViewSuspend - handle Suspend/Resume menu item
void CSimulatorView::OnViewSuspend()
{
	// Suspend/resume the Corona runtime.
	// Always show the "Suspended" overlay, unless the shift key was held down.
	bool showSuspendedScreen = !((::GetKeyState(VK_LSHIFT) | ::GetKeyState(VK_RSHIFT)) & 0x80);
	SuspendResumeSimulationWithOverlay(showSuspendedScreen, true);
}

// OnUpdateViewSuspend - set to Suspend or Resume as appropriate
void CSimulatorView::OnUpdateViewSuspend(CCmdUI *pCmdUI)
{
	bool isSuspended = IsSimulationSuspended();
	int id = isSuspended ? IDS_HARDWARE_RESUME : IDS_HARDWARE_SUSPEND;
	CString sCaption;
	sCaption.LoadString(id);
	pCmdUI->SetText( sCaption );

	GetWinProperties()->GetAnalytics()->Log("suspend-resume", "type", CStringA(sCaption));
}

// OnViewRotateLeft - change rotation value, tell corona core, update skin
void CSimulatorView::OnViewRotateLeft()
{
	// Do not rotate the welcome screen.
	if (mIsShowingInternalScreen)
	{
		return;
	}

	// Do not continue if the simulated device's screen does not support rotation.
	if (false == mDeviceConfig.supportsScreenRotation)
	{
		return;
	}

	// Rotate the simulated device.
    SetRotation( GetRotation() + 270 );
	if (GetSkinBitmap() != NULL)
	{
		GetSkinBitmap()->RotateFlip( Gdiplus::Rotate270FlipNone );
	}
	if (mRuntimeEnvironmentPointer && mRuntimeEnvironmentPointer->GetDeviceSimulatorServices())
	{
		mRuntimeEnvironmentPointer->GetDeviceSimulatorServices()->RotateCounterClockwise();
	}
    UpdateSimulatorSkin();
	GetWinProperties()->GetAnalytics()->Log("rotate", "direction", "left");
}

// OnViewRotateRight - change rotation value, tell corona core, update skin
void CSimulatorView::OnViewRotateRight()
{
	// Do not rotate the welcome screen.
	if (mIsShowingInternalScreen)
	{
		return;
	}

	// Do not continue if the simulated device's screen does not support rotation.
	if (false == mDeviceConfig.supportsScreenRotation)
	{
		return;
	}

	// Rotate the simulated device.
    SetRotation( GetRotation() + 90 );
	if (GetSkinBitmap() != NULL)
	{
		GetSkinBitmap()->RotateFlip( Gdiplus::Rotate90FlipNone );
	}
	if (mRuntimeEnvironmentPointer && mRuntimeEnvironmentPointer->GetDeviceSimulatorServices())
	{
		mRuntimeEnvironmentPointer->GetDeviceSimulatorServices()->RotateClockwise();
	}
	UpdateSimulatorSkin();
	GetWinProperties()->GetAnalytics()->Log("rotate", "direction", "right");
}

void CSimulatorView::OnViewNavigateBack()
{
	// Do not continue if we're not simulating a device.
	if (mIsShowingInternalScreen)
	{
		return;
	}
	if (!mRuntimeEnvironmentPointer || !mRuntimeEnvironmentPointer->GetDeviceSimulatorServices())
	{
		return;
	}

	// Do not continue if we're currently suspended.
	if (IsSimulationSuspended())
	{
		return;
	}

	// Do not continue if the simulated device does not support back key navigation.
	if (!mDeviceConfig.supportsKeyEvents || !mDeviceConfig.supportsBackKey)
	{
		return;
	}

	// Send "back" key down/up messages to the Corona control.
	// Corona will automatically terminate the runtime if the Lua key listener does not handle them.
	auto windowPointer = &mCoronaContainerControl.GetCoronaControl();
	windowPointer->PostMessage(WM_KEYDOWN, VK_BROWSER_BACK, 0L);
	windowPointer->PostMessage(WM_KEYUP, VK_BROWSER_BACK, 0x40000000L);
	GetWinProperties()->GetAnalytics()->Log("back", nullptr);
}

/// Called when the top most file in the "Most Recent Files" list is clicked on in the menu.
/// The application won't update the document when the top most file is selected so we
/// we have to update it manually here.
void CSimulatorView::OnFileMRU1()
{
	// Fetch the application's MRU list.
	// Warning: The returned pointer can be NULL if no files were ever loaded.
	CRecentFileList *recentFileListPointer = ((CSimulatorApp*)AfxGetApp())->GetRecentFileList();
	if (NULL == recentFileListPointer)
	{
		return;
	}
	
	// Compare the selected file name with what is currently opened.
	const CString& fileName = (*recentFileListPointer)[0];
	if (!mIsShowingInternalScreen && (GetDocument()->GetPath() == fileName))
	{
		// The selected file is already open. Relaunch it in the simulator.
		++mRelaunchCount;

		RestartSimulation();
	}
	else
	{
		// Open the selected file for simulation.
		GetWinProperties()->GetAnalytics()->Log("open-recent", NULL);

		GetDocument()->GetDocTemplate()->OpenDocumentFile(fileName);
	}
}

//Called from the simulator services. This matches the selected recent project with the windows recent
//project ID and posts a message to load it, this must be invoked through a message post
void CSimulatorView::PostOpenWithPath(CString fileName)
{
	CRecentFileList *recentFileListPointer = ((CSimulatorApp*)AfxGetApp())->GetRecentFileList();
	if (recentFileListPointer)
	{
		for (int i = 0; i < recentFileListPointer->GetSize(); i++)
		{
			const CString& fileName2 = (*recentFileListPointer)[i];
			if (fileName == fileName2)
			{
				this->GetParent()->PostMessage(WM_COMMAND,ID_FILE_MRU_FIRST+i);
				return;
			}
		}
		recentFileListPointer->Add(fileName);
		this->GetParent()->PostMessage(WM_COMMAND, ID_FILE_MRU_FIRST);
	}
}

// OnPreferences - show Preferences dialog
void CSimulatorView::OnPreferences()
{
	// Display the preferences dialog.
	CPreferencesDlg dlgPreferences;
	auto result = dlgPreferences.DoModal();
	if (result != IDOK)
	{
		// No changes were made.
		GetWinProperties()->GetAnalytics()->Log("preferences", "result", "not-changed");

		return;
	}

	GetWinProperties()->GetAnalytics()->Log("preferences", "result", "changed");

	// If we're currently running a project, then update its "Show runtime error" setting.
	CSimulatorApp *applicationPointer = (CSimulatorApp*)AfxGetApp();
	if (applicationPointer && mRuntimeEnvironmentPointer)
	{
		Rtt::Runtime *runtimePointer = mRuntimeEnvironmentPointer->GetRuntime();
		bool bIsShowingRuntimeErrors = applicationPointer->IsShowingRuntimeErrors();
		bool showRuntimeErrors = mIsShowingInternalScreen ? false : bIsShowingRuntimeErrors;
		runtimePointer->SetProperty(Rtt::Runtime::kShowRuntimeErrors, showRuntimeErrors);
	}
}

/// Displays a window for creating a new Corona project.
void CSimulatorView::OnFileNew()
{
	// Display the "New Project" dialog.
	CNewProjectDlg dialog;
	int result = dialog.DoModal();
	if ((result != IDOK) || dialog.GetNewProjectPath().IsEmpty())
	{
		return;
	}

	// Start simulating the new project.
	CString projectFilePath = dialog.GetNewProjectPath() + _T("\\main.lua");
	GetDocument()->GetDocTemplate()->OpenDocumentFile(projectFilePath);

	// Show the new project's folder in Windows Explorer.
	OnShowProjectFiles();

	// Show the new project's folder in their editor
	OnFileOpenInEditor();
}

// OnFileOpen - overloaded to check OpenGL and reopen file if already open
void CSimulatorView::OnFileOpen()
{
	CString filename;

	// Do not continue if the machine does not meet the simulator's minimum OpenGL requirements.
	if (ValidateOpenGL() == false)
	{
		return;
	}

	// Open the file if the user didn't "Cancel" out of the dialog.
	if (AfxGetApp()->DoPromptFileName(filename, AFX_IDS_OPENFILE,
                               OFN_HIDEREADONLY | OFN_FILEMUSTEXIST,
                               TRUE, NULL))
	{
		  // if the file is already open, relaunch
		  if (!mIsShowingInternalScreen && (GetDocument()->GetPath() == filename))
		  {
			  ++mRelaunchCount;

			  RestartSimulation();
		  }
		  else 
		  {
			  GetWinProperties()->GetAnalytics()->Log("open-project", NULL);

			  GetDocument()->GetDocTemplate()->OpenDocumentFile(filename);
		  }
	}
}

/// Displays an "Open File" dialog which default to the sample projects directory.
void CSimulatorView::OnFileOpenSampleProject()
{
	// Do not continue if unable to find the Samples directory.
	CSimulatorApp *applicationPointer = (CSimulatorApp*)AfxGetApp();
	if (applicationPointer->CheckDirExists(applicationPointer->GetSampleDir()) == false)
	{
		CString title;
		CString message;
		title.LoadString(IDS_WARNING);
		message.LoadString(IDS_UNABLE_TO_FIND_SAMPLE_PROJECTS_DIRECTORY);
		MessageBox(message, title, MB_OK | MB_ICONWARNING);
		return;
	}

	// Display the "Select Sample Project" dialog.
	CSelectSampleProjectDlg dialog;
	int result = dialog.DoModal();
	if (result != IDOK)
	{
		return;
	}

	// Start simulating the selected project.
	CString filePath = dialog.GetSelectedProjectPath() + _T("\\main.lua");
	if (!mIsShowingInternalScreen && (GetDocument()->GetPath() == filePath))
	{
		RestartSimulation();
	}
	else 
	{
		GetDocument()->GetDocTemplate()->OpenDocumentFile(filePath);
	}

	WinString projectName;
	projectName.SetTCHAR(GetDocument()->GetTitle());
	GetWinProperties()->GetAnalytics()->Log("open-sample", "sample", projectName.GetUTF8());
}

/// <summary>Opens a dialog to build the currently selected project for Android.</summary>
void CSimulatorView::OnBuildForAndroid()
{
    // Check whether JDK and jarsigner.exe are available first
	BOOL retval = CSimulatorApp::InitJavaPaths();
    if ( ! retval)
	{
        return;
	}

	// If app is running, suspend it during the build
	bool buildSuspendedSimulator = false;
	bool isSuspended = IsSimulationSuspended();
	if (false == isSuspended)
	{
		buildSuspendedSimulator = true;
		SuspendResumeSimulationWithOverlay(true, false);
	}

	// Display the build window.
	CBuildAndroidDlg dlg;
	dlg.SetProject( GetDocument()->GetProject() );
	dlg.DoModal();
	if (buildSuspendedSimulator)
	{
		// Toggle suspend
		SuspendResumeSimulationWithOverlay(true, false);
	}
}

/// <summary>Opens a dialog to build the currently selected project as an HTML5 app.</summary>
void CSimulatorView::OnBuildForWeb()
{
	// If app is running, suspend it during the build
	bool buildSuspendedSimulator = false;
	bool isSuspended = IsSimulationSuspended();
	if (false == isSuspended)
	{
		buildSuspendedSimulator = true;
		SuspendResumeSimulationWithOverlay(true, false);
	}

	// Display the build window.
	CBuildWebDlg dlg;
	dlg.SetProject( GetDocument()->GetProject() );
	dlg.DoModal();
	if (buildSuspendedSimulator)
	{
		// Toggle suspend
		SuspendResumeSimulationWithOverlay(true, false);
	}
}

/// <summary>Opens a dialog to build the currently selected project as an HTML5 app.</summary>
void CSimulatorView::OnBuildForLinux()
{
	// If app is running, suspend it during the build
	bool buildSuspendedSimulator = false;
	bool isSuspended = IsSimulationSuspended();
	if (false == isSuspended)
	{
		buildSuspendedSimulator = true;
		SuspendResumeSimulationWithOverlay(true, false);
	}

	// Display the build window.
	CBuildLinuxDlg dlg;
	dlg.SetProject( GetDocument()->GetProject() );
	dlg.DoModal();
	if (buildSuspendedSimulator)
	{
		// Toggle suspend
		SuspendResumeSimulationWithOverlay(true, false);
	}
}

/// <summary>Opens a dialog to build the currently selected project as an NxS Switch app.</summary>
void CSimulatorView::OnBuildForNxS()
{
	CSimulatorApp* applicationPointer = (CSimulatorApp*)AfxGetApp();
	if (!applicationPointer->ShouldShowNXBuildDlg())
	{
		return;
	}
	// If app is running, suspend it during the build
	bool buildSuspendedSimulator = false;
	bool isSuspended = IsSimulationSuspended();
	if (false == isSuspended)
	{
		buildSuspendedSimulator = true;
		SuspendResumeSimulationWithOverlay(true, false);
	}

	// Display the build window.
	CBuildNxSDlg dlg;
	dlg.SetProject(GetDocument()->GetProject());
	dlg.DoModal();
	if (buildSuspendedSimulator)
	{
		// Toggle suspend
		SuspendResumeSimulationWithOverlay(true, false);
	}
}

/// <summary>Opens a dialog to build the currently selected project as a Win32 desktop app.</summary>
void CSimulatorView::OnBuildForWin32()
{
	// Ask the user to select a Corona project if we're not currently running one.
	// Note: This should never happen, but check just in case.
	if (!mRuntimeEnvironmentPointer || !mRuntimeEnvironmentPointer->GetRuntime())
	{
		OnFileOpen();
		if (!mRuntimeEnvironmentPointer || !mRuntimeEnvironmentPointer->GetRuntime())
		{
			return;
		}
	}

	// Do not contnue if not all plugins have been acquired for the selected project.
	// We do this because local Win32 app builds require the plugins zips to be downloaded first.
	if (VerifyAllPluginsAcquired() == false)
	{
		return;
	}

	// If app is running, suspend it during the build.
	bool wasAppRunning = false;
	bool isSuspended = IsSimulationSuspended();
	if (false == isSuspended)
	{
		wasAppRunning = true;
		SuspendResumeSimulationWithOverlay(true, false);
	}

	// Display the build window.
	CBuildWin32AppDlg dialog;
	dialog.SetProject(GetDocument()->GetProject());
	dialog.DoModal();

	// Resume the project if it was previously running and the user hasn't started running the built app.
	if (wasAppRunning && !dialog.HasRanBuiltApp())
	{
		SuspendResumeSimulationWithOverlay(true, false);
	}
}

/// <summary>Enables/disables the "Build for Android/HTML5/Windows" item in the menu.</summary>
void CSimulatorView::OnUpdateBuildMenuItem(CCmdUI *pCmdUI)
{
   pCmdUI->Enable( mRuntimeEnvironmentPointer && ! GetDocument()->GetPath().IsEmpty() );
}

// OnFileOpenInEditor - give project name to shell, if associated with an editor
// Note TODO item below
void CSimulatorView::OnFileOpenInEditor()
{
	const int MAX_PATH_LENGTH = 512;
	CString applicationFileName;
	CString fileAssociation;
	DWORD fileAssociationLength = MAX_PATH_LENGTH;
	HRESULT result;
	int length;
	int index;
	bool hasValidFileAssociation = false;

	try
	{
		// Fetch this application's file name without the path.
		length = ::GetModuleFileName(NULL, applicationFileName.GetBuffer(MAX_PATH_LENGTH), MAX_PATH_LENGTH);
		applicationFileName.ReleaseBuffer(length);
		if (length > 0)
		{
			index = applicationFileName.ReverseFind(_T('\\'));
			if (index > 0)
			{
				applicationFileName.Delete(0, index + 1);
			}
		}

		// Fetch the file association in Windows for all Lua files, if assigned.
		// ie: This is the default executable used to open a Lua file when double clicked on.
		result = ::AssocQueryString(
						ASSOCF_INIT_DEFAULTTOSTAR, ASSOCSTR_EXECUTABLE, _T(".lua"),
						NULL, fileAssociation.GetBuffer(MAX_PATH_LENGTH), &fileAssociationLength);
		fileAssociation.ReleaseBuffer();
		CString fullAssociationPath(fileAssociation);
		if (S_OK == result)
		{
			index = fileAssociation.ReverseFind(_T('\\'));
			if (index > 0)
			{
				fileAssociation.Delete(0, index + 1);
			}
		}

		// Check if we have a valid file association in Windows to edit the "main.lua" file.
		// Note: The default association on Windows XP or lower is an empty string.
		if (fileAssociation.GetLength() > 0)
		{
			// A file association has been assigned.
			// Make sure it is associated with an application that can open the file.
			// Note: The default association on Windows Vista or higher is "shell32.dll".
			CString extension = fileAssociation.Right(4);
			if ((extension.CompareNoCase(_T(".exe")) == 0) ||
			    (extension.CompareNoCase(_T(".com")) == 0) ||
			    (extension.CompareNoCase(_T(".bat")) == 0))
			{
				// Also, don't allow the applications below since they won't edit the file.
				if ((fileAssociation.CompareNoCase(applicationFileName) != 0) &&
				    (fileAssociation.CompareNoCase(_T("Lightroom.exe")) != 0))     // Adobe Lightroom
				{
					// The application associated with the file is valid.
					hasValidFileAssociation = true;
				}
			}
		}

		// Open the Lua file for editing using the default application assigned in Windows.
		// If Windows doesn't have a valid file association, then open it with Notepad.
		if (hasValidFileAssociation)
		{
			if (fileAssociation == _T("sublime_text.exe") || fileAssociation == _T("Code.exe")) {
				CString fullPath(GetDocument()->GetPath());
				index = fullPath.ReverseFind(_T('\\'));
				if (index > 0)
				{
					CString dirPath(fullPath);
					dirPath.Delete(index, dirPath.GetLength() - index);
					fullPath.Insert(0, _T('"'));
					fullPath.Append(_T("\" --add \""));
					fullPath.Append(dirPath);
					fullPath.Append(_T("\""));
				}
				::ShellExecute(nullptr, nullptr, fullAssociationPath, fullPath, nullptr, SW_SHOWNORMAL);
			}
			else {
				::ShellExecute(nullptr, _T("open"), GetDocument()->GetPath(), nullptr, nullptr, SW_SHOWNORMAL);
			}
			WinString appName;
			appName.SetTCHAR(fileAssociation);
			GetWinProperties()->GetAnalytics()->Log( "open-in-editor", "editor", appName.GetUTF8() );
		}
		else
		{
			::ShellExecute(nullptr, nullptr, _T("notepad.exe"), GetDocument()->GetPath(), nullptr, SW_SHOWNORMAL);
			GetWinProperties()->GetAnalytics()->Log( "open-in-editor", "editor", "notepad" );
		}
	}
	catch (...)
	{ }
}

// OnUpdateFileOpenInEditor - enable menu item when project is open
void CSimulatorView::OnUpdateFileOpenInEditor(CCmdUI *pCmdUI)
{
   pCmdUI->Enable( mRuntimeEnvironmentPointer && ! GetDocument()->GetPath().IsEmpty() );
}

/// Restart simulation of the last selected project.
void CSimulatorView::OnFileRelaunch()
{
	if (GetDocument()->GetPath().GetLength() > 0)
	{
		// Restart the currently running project.
		RestartSimulation();
	}
	else
	{
		// A project is not currently open.
		// Attempt to open and run the last ran project in the "recent file list".
		CRecentFileList *recentFileListPointer = ((CSimulatorApp*)AfxGetApp())->GetRecentFileList();
		CString lastFilePathName = (*recentFileListPointer)[0];
		if (lastFilePathName.GetString() > 0)
		{
			GetDocument()->GetDocTemplate()->OpenDocumentFile(lastFilePathName);
		}
	}
}

/// Enables or disables the "Relaunch" menu item.
void CSimulatorView::OnUpdateFileRelaunch(CCmdUI *pCmdUI)
{
	CRecentFileList *recentFileListPointer = ((CSimulatorApp*)AfxGetApp())->GetRecentFileList();
	bool hasRecentFile = (recentFileListPointer && ((*recentFileListPointer)[0].GetLength() > 0));
	bool isProjectOpen = (GetDocument()->GetPath().GetLength() > 0);
	pCmdUI->Enable((isProjectOpen || hasRecentFile) ? TRUE : FALSE);
}

/// Display the currently running Corona project's root folder in Windows Explorer.
void CSimulatorView::OnShowProjectFiles()
{
	// Fetch current project. Will be NULL no project is selected.
	auto projectPointer = GetDocument()->GetProject();
	if (!projectPointer)
	{
		return;
	}

	// Fetch the project's absolute path without the file name. Make sure directory exists.
	CString projectPath = projectPointer->GetDir();
	if (CSimulatorApp::CheckDirExists(projectPath) == false)
	{
		return;
	}
	
	// Display the project folder in the default file viewer. Typically "Windows Explorer".
	try { ::ShellExecute(nullptr, _T("open"), projectPath, nullptr, nullptr, SW_SHOWNORMAL); }
	catch (...) { }
	            
	GetWinProperties()->GetAnalytics()->Log("show-project-files", nullptr);
}

/// Enables or disables the "Show Project Files" menu item.
void CSimulatorView::OnUpdateShowProjectFiles(CCmdUI *pCmdUI)
{
	BOOL enabled = (GetDocument()->GetPath().GetLength() > 0) ? TRUE : FALSE;
	pCmdUI->Enable(enabled);
}

// OnShowProjectSandbox - Open this app's sandbox in Windows Explorer
void CSimulatorView::OnShowProjectSandbox()
{
	// Do not continue if not simulating a project.
	if (!mRuntimeEnvironmentPointer || !mRuntimeEnvironmentPointer->GetDeviceSimulatorServices())
	{
		return;
	}

	CStringW utf16DirectoryPath = mRuntimeEnvironmentPointer->GetUtf16PathFor(Rtt::MPlatform::kDocumentsDir);
	utf16DirectoryPath.Append(L"\\..\\.");
	if (CSimulatorApp::CheckDirExists(utf16DirectoryPath))
	{
		::ShellExecuteW(nullptr, L"open", utf16DirectoryPath, nullptr, nullptr, SW_SHOWNORMAL);
		GetWinProperties()->GetAnalytics()->Log("show-sandbox", nullptr);
	}
}

// OnClearProjectSandbox - Open this app's sandbox in Windows Explorer
void CSimulatorView::OnClearProjectSandbox()
{
	// Do not continue if not simulating a project.
	if (!mRuntimeEnvironmentPointer || !mRuntimeEnvironmentPointer->GetDeviceSimulatorServices())
	{
		return;
	}

	const TCHAR *confPrefKey = _T("SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer\\DontShowMeThisDialogAgain");
	TCHAR sandboxPath[MAX_PATH];
	CStringW utf16DirectoryPath = mRuntimeEnvironmentPointer->GetUtf16PathFor(Rtt::MPlatform::kDocumentsDir);

	// TODO: handle paths longer than MAX_PATH gracefully

	// We have the Documents directory but we want its parent
	utf16DirectoryPath.Append(L"\\..\\.");

	// Resolve the relative part of the sandbox path
	PathCanonicalize(sandboxPath, utf16DirectoryPath.GetString());

	// The trailing part of the sandbox path works as a key for SHMessageBoxCheck()
	CString confPrefValue = CString("CPS-");

	confPrefValue.Append(PathFindFileName(sandboxPath));

	// If shift is held down, forget any remembered value for the "don't show dialog again" checkbox
	bool forgetRememberedSetting = (((::GetKeyState(VK_LSHIFT) | ::GetKeyState(VK_RSHIFT)) & 0x80) != 0);

	if (forgetRememberedSetting)
	{
		HKEY hKey = NULL;
		DWORD regResult = RegOpenKeyEx(HKEY_CURRENT_USER,
			confPrefKey,
			0L,
			KEY_SET_VALUE,
			&hKey);

		if (regResult == ERROR_SUCCESS)
		{
			RegDeleteValue(hKey, confPrefValue);
			RegCloseKey(hKey);
		}
	}

	WinString clearPrompt;

	clearPrompt.Format(_T("Are you sure you want to delete the contents of the sandbox for '%s'?\n\nThis will also clear any app preferences and restart the project"), GetDocument()->GetTitle());

	// This confirmation dialog includes a "don't show dialog again" checkbox
	int yesNo = SHMessageBoxCheck(this->GetSafeHwnd(),
		clearPrompt.GetUTF16(),
		TEXT("Clear Project Sandbox"),
		MB_YESNO,
		IDYES,
		confPrefValue);

	if (yesNo == IDYES)
	{
		StopSimulation();

		// The fine API has a crunchy way of handling multiple paths and it must be
		// terminated with a double NUL
		sandboxPath[_tcslen(sandboxPath)+1] = 0;

		// Send the app's sandbox to the recycle bin.  As luck would have it, the prefs database 
		// for the app is in the same location so we have nothing more to do
		SHFILEOPSTRUCT shFileOp = {
			NULL,
			FO_DELETE,
			sandboxPath,
			NULL,
			FOF_SILENT | FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_ALLOWUNDO,
			FALSE,
			NULL,
			NULL };

		SHFileOperation(&shFileOp);

		Rtt_Log("Project sandbox and preferences cleared");

		GetWinProperties()->GetAnalytics()->Log("clear-sandbox", nullptr);

		StartSimulation();
	}
	else
	{
		// they choose not to proceed, forget that answer if they checked the box
		HKEY hKey = NULL;
		DWORD regResult = RegOpenKeyEx(HKEY_CURRENT_USER,
			confPrefKey,
			0L,
			KEY_SET_VALUE,
			&hKey);

		if (regResult == ERROR_SUCCESS)
		{
			RegDeleteValue(hKey, confPrefValue);
			RegCloseKey(hKey);
		}
	}
}

// OnUpdateShowProjectSandbox - enable menu item when project is open
void CSimulatorView::OnUpdateShowProjectSandbox(CCmdUI *pCmdUI)
{
	bool isSimulatingProject = (mRuntimeEnvironmentPointer && mRuntimeEnvironmentPointer->GetDeviceSimulatorServices());
	pCmdUI->Enable(isSimulatingProject ? TRUE : FALSE);
}

// OnFileClose - close project, show Welcome window again if enabled
void CSimulatorView::OnFileClose()
{
	// Stop the currently simulated project, if running.
	StopSimulation();

	// Next part depends if we're running in debug mode or not.
	CSimulatorApp* pApp = (CSimulatorApp*)::AfxGetApp();
	if (pApp)
	{
		if (pApp->IsDebugModeEnabled())
		{
			// This application is running in debug mode. Exit out of the application.
			// This is because the debugger exits when simulation has stopped.
//			AfxGetMainWnd()->SendMessage(WM_CLOSE);

		}
		else
		{
			// Display the welcome window, if enabled.
			GetDocument()->GetDocTemplate()->OpenDocumentFile(NULL);
		}
	}
}

// OnUpdateFileClose - enable menu item if project is open
void CSimulatorView::OnUpdateFileClose(CCmdUI *pCmdUI)
{
	bool enable = mRuntimeEnvironmentPointer && !GetDocument()->GetPath().IsEmpty();
	pCmdUI->Enable(enable ? TRUE : FALSE);
}

/// Called when a device skin has been selected from the "View As" menu.
/// Displays the selected device skin within the simulator window.
/// @param nID The unique ID of the menu item that was clicked on.
void CSimulatorView::OnWindowViewAs( UINT nID )
{
	Rtt::TargetDevice::Skin skinId = Rtt::TargetDevice::kUnknownSkin;

	if (nID == ID_VIEWAS_CUSTOMDEVICE)
	{
		// Display the custom device skin dialog.
		CustomDeviceDlg cdDlg;
		int dlgResult = cdDlg.DoModal();

		// Do not continue if the user canceled out of the dialog.
		if (dlgResult != IDOK)
		{
			return;
		}

		// Fetch the device settings entered into the above dialog.
		WinString customDeviceTemplateName;
		WinString customDeviceTemplate;
		WinString customDeviceWidthStr, customDeviceHeightStr;
		customDeviceTemplateName.SetTCHAR( mSystemSkinsDir );
		customDeviceTemplateName.Append( "CustomDevice.lua.template" );
		customDeviceWidthStr.Format( "%d", cdDlg.GetCustomDeviceWidth() );
		customDeviceHeightStr.Format( "%d", cdDlg.GetCustomDeviceHeight() );

		// Read the custom device skin template.
		if ( ! customDeviceTemplate.GetFileContents( customDeviceTemplateName ) )
		{
			Rtt_TRACE(("Error reading template from '%s'\n", customDeviceTemplateName));

			return;
		}

		// Replace the template's parameters with the values entered into the dialog.
		customDeviceTemplate.Replace( "{customDeviceWidth}", customDeviceWidthStr );
        customDeviceTemplate.Replace( "{customDeviceHeight}", customDeviceHeightStr );
		customDeviceTemplate.Replace( "{customDeviceIsRotatable}", "true" );
		{
			WinString stringTranscoder(cdDlg.GetCustomDeviceName());
			customDeviceTemplate.Replace("{customDeviceName}", stringTranscoder.GetUTF8());
		}
		{
			WinString stringTranscoder(cdDlg.GetCustomDevicePlatform());
			customDeviceTemplate.Replace("{customDevicePlatform}", stringTranscoder.GetUTF8());
		}

		// Save the custom skin's settings to file.
		WinString customSkinFileNamePath( mCustomSkinFileName );
		customDeviceTemplateName.SetTCHAR( mCustomSkinFileName.GetString() );
		if ( ! customDeviceTemplate.WriteFileContents( customSkinFileNamePath.GetUTF8() ) )
		{
			Rtt_TRACE(("Error writing custom device template to '%s'\n", customSkinFileNamePath.GetUTF8()));
			return;
		}

		// Select the custom skin to be used by the simulator.
		skinId = Rtt::TargetDevice::kCustomSkin;
	}
	else
	{
		// Fetch the selected device skin's unique ID.
		skinId = SkinIDFromMenuID(nID);
	}

	if (skinId == Rtt::TargetDevice::kCustomSkin)
	{
		GetWinProperties()->GetAnalytics()->Log("change-skin", "skin", "custom-device" );
	}
	else
	{
		CString skinName(Rtt::TargetDevice::LabelForSkin(skinId));

		GetWinProperties()->GetAnalytics()->Log("change-skin", "skin", CStringA(skinName) );
	}

	// Display the selected device skin
	InitializeSimulation(skinId);
}

// OnUpdateWindowViewAs - check mark for the currently displayed skin
//
// This used to build the "View As" submenu on first show, walking the skin
// list and hanging a "Borderless" submenu off it. The menu is gone -- the
// simulator's bar is Rtt::MenuBar, drawn by bgfx -- so all that is left is the
// check mark, for whatever else routes these command ids.
void CSimulatorView::OnUpdateWindowViewAs( CCmdUI *pCmdUI )
{
	if (pCmdUI->m_nID == ID_VIEWAS_CUSTOMDEVICE)
		pCmdUI->SetCheck( Rtt::TargetDevice::kCustomSkin == m_nSkinId );
	else
		pCmdUI->SetCheck( SkinIDFromMenuID( pCmdUI->m_nID ) == m_nSkinId );
}

void CSimulatorView::OnUpdateViewNavigateBack(CCmdUI *pCmdUI)
{
	bool isEnabled = false;
	if (mRuntimeEnvironmentPointer && mRuntimeEnvironmentPointer->GetDeviceSimulatorServices() && !IsSimulationSuspended())
	{
		if (mDeviceConfig.supportsKeyEvents && mDeviceConfig.supportsBackKey)
		{
			isEnabled = true;
		}
	}
	pCmdUI->Enable(isEnabled ? TRUE : FALSE);
}

#pragma endregion

void CSimulatorView::GetFilePaths(LPCTSTR pattern, CStringArray& filepaths)
{
	// Object to enumerate files
	CFileFind finder;

	// Init the file finding job
	BOOL working = finder.FindFile( pattern );

	// For each file that is found:
	while ( working )
	{
		// Update finder status with new file
		working = finder.FindNextFile();

		// Skip '.' and '..'
		if ( finder.IsDots() )
		{
			continue;
		}

		// Skip sub-directories
		if ( finder.IsDirectory() )
		{
			continue;
		}
		// Add file path to container
		filepaths.Add( finder.GetFilePath() );
	}
	// Cleanup file finder
	finder.Close();
}


bool CSimulatorView::LoadSkinResources()
{
	CString msg;
	CStringArray filePaths;
	CString systemSkinFilesGlob;

	systemSkinFilesGlob = mSystemSkinsDir + _T("*.lua");
	
	GetFilePaths(systemSkinFilesGlob, filePaths);

	// Only "Pro" users get to have custom skins.  For business purposes, the ability to
	// use Daily Builds equates to "Pro and above"
#ifdef WE_FIXED_THE_APP_INITIALIZATION_ORDER
	// TODO: Unfortunately the app initialization process is sufficiently convoluted that we have
	// a chicken and egg issue here.  One day we'll figure it out, until then everyone on
	// Windows gets user defined skins, not just Pro
	const Rtt::AuthorizationTicket *ticket = GetWinProperties()->GetTicket();
	if ( ticket && ticket->IsDailyBuildAllowed() )
#endif
	{
		CString userSkinsGlob;

		userSkinsGlob = mUserSkinsDir + _T("*.lua");

		GetFilePaths(CString(userSkinsGlob), filePaths);
	}

	// Put the skins into a data structure we can share with core code
	char **skinPaths;

	skinPaths = (char **) calloc(sizeof(char *), filePaths.GetSize());

	if (skinPaths == NULL && filePaths.GetSize() > 0)
	{
		printf("CSimulatorView::OnUpdateWindowViewAs: Problem processing skin files.  Please restart\n");
	}
	else
	{
		for ( int i = 0; i < filePaths.GetSize(); i++ )
		{
			skinPaths[i] = (char *) _strdup(CStringA(filePaths[i]));
		}

		// Tell the core about the skins
		Rtt::TargetDevice::Initialize(skinPaths, filePaths.GetSize());

		for (int i = 0; i < filePaths.GetSize(); i++)
		{
			free(skinPaths[i]);
		}
		free(skinPaths);
	}

	return TRUE;
}

#pragma region Corona Event Handlers
void CSimulatorView::OnRuntimeLoaded(Interop::RuntimeEnvironment& sender, const Interop::EventArgs& arguments)
{
	// Store a pointer to the newly created/loaded Corona runtime.
	mRuntimeEnvironmentPointer = (Interop::SimulatorRuntimeEnvironment*)&sender;

	// Finish configuring the runtime now that it is loaded.
	Rtt::Runtime *runtimePointer = mRuntimeEnvironmentPointer->GetRuntime();
	bool bIsShowingRuntimeErrors = ((CSimulatorApp*)AfxGetApp())->IsShowingRuntimeErrors();
	bool showRuntimeErrors = mIsShowingInternalScreen ? false : bIsShowingRuntimeErrors;
	runtimePointer->SetProperty(Rtt::Runtime::kShowRuntimeErrors, showRuntimeErrors);

	// If simulating a project, then log its paths.
	if (mRuntimeEnvironmentPointer->GetDeviceSimulatorServices())
	{
		Rtt_TRACE_SIM((
				"Loading project from:   %s\r\n",
				mRuntimeEnvironmentPointer->GetUtf8PathFor(Rtt::MPlatform::kResourceDir)));
		Rtt_TRACE_SIM((
				"Project sandbox folder: %s\r\n",
				mRuntimeEnvironmentPointer->GetUtf8PathFor(Rtt::MPlatform::kDocumentsDir)));
	}

	// Fetch the loaded app's default rotation/orientation.
	int launchRotation = 0;
	if (mRuntimeEnvironmentPointer->GetDeviceSimulatorServices())
	{
		auto orientation = mRuntimeEnvironmentPointer->GetDeviceSimulatorServices()->GetOrientation();
		switch (orientation)
		{
			case Rtt::DeviceOrientation::kUpright:
				launchRotation = 0;
				break;
			case Rtt::DeviceOrientation::kSidewaysRight:	// bottom of device is to the right
				launchRotation = 270;
				break;
			case Rtt::DeviceOrientation::kUpsideDown:		// bottom of device is at the top
				launchRotation = 180;
				break;
			case Rtt::DeviceOrientation::kSidewaysLeft:		// bottom of device is to the left
				launchRotation = 90;
				break;
		}
	}

	// Rotate the device skin's bitmap to match the app's rotation.
	int diffRotation = launchRotation - GetRotation();
	if( diffRotation < 0 )
	{
        diffRotation += 360;
	}
	if (NULL != GetSkinBitmap())
	{
		GetSkinBitmap()->RotateFlip( DegreesToRFType( diffRotation ) );
	}
	
	// Store the rotation value.
	SetRotation( launchRotation );

#if defined( Rtt_USE_BGFX )
	// The bar can only be brought up once there is a renderer to draw it with, which is now. It also decides
	// how much of the surface is not Corona's, so it has to happen before the window is laid out below.
	EnsureMenuBar();
	UpdateMenuBar();
#endif

	// Update the window for the current skin and rotation.
	UpdateSimulatorSkin();
}

// OnActivityIndicator - ActivityIndicator message from lua code
// Use CProgressWnd for now, but doesn't look great.  TODO: improve appearance
// wParam = 0 for hide, 1 for show.  Only one window can be active at a time.
// lParam = NULL or pointer to string of text shown on window
LRESULT CSimulatorView::OnActivityIndicator(WPARAM wParam, LPARAM lParam)
{
	if (wParam)
	{
		// Show the progress dialog.
		if (!mProgressDlgPointer)
		{
			mProgressDlgPointer = new CProgressWnd(this);
			mProgressDlgPointer->Create(CProgressWnd::IDD);
		}
		mProgressDlgPointer->ShowWindow(SW_SHOW);
	}
	else
	{
		// Hide the progress dialog.
		if (mProgressDlgPointer)
		{
			mProgressDlgPointer->ShowWindow(SW_HIDE);
		}
	}
	return 0;
}

// OnNativeAlert - message from lua code
// Use CMessageDlg, configured as indicated by WMU_ALERT_PARAMS in lParam
// wParam = 0 for cancel, 1 for show.  Only one window can be active at a time.
// lParam = NULL (only OK button) or ptr to array of strings for buttons
LRESULT CSimulatorView::OnNativeAlert(WPARAM wParam, LPARAM lParam)
{
    int result = 0;

	WMU_ALERT_PARAMS *pWAP = (WMU_ALERT_PARAMS *)lParam;
	if (nullptr == pWAP)
	{
		return -1;
	}

    if (0 == wParam)  // Cancel alert -- usually canceled directly
	{
		if (0 != pWAP->hwnd)
		{
			::SendMessage((HWND)pWAP->hwnd, WM_COMMAND, IDCANCEL, NULL);
		}
	}
	else // 0 != wParam, show alert
	{
        // Clean up previous memory
        if (mMessageDlgPointer)
		{
			if (mMessageDlgPointer->GetSafeHwnd())
			{
				mMessageDlgPointer->SendMessage(WM_COMMAND, IDCANCEL, 0);
			}
			delete mMessageDlgPointer;
			mMessageDlgPointer = nullptr;
		}

        // Create a new message box.
		mMessageDlgPointer = new CMessageDlg(this);

		// Set up parameters of message window
		WinString string;
		string.SetUTF8( pWAP->sTitle );
		mMessageDlgPointer->SetTitle(string.GetTCHAR());
		string.SetUTF8( pWAP->sMsg );
		mMessageDlgPointer->SetText(string.GetTCHAR());

		// Custom labels for up to 3 buttons
		if ( pWAP->nButtonLabels > 0 )
		{
			string.SetUTF8( pWAP->psButtonLabels[0] );
			mMessageDlgPointer->SetDefaultText(string.GetTCHAR());
		}
		if ( pWAP->nButtonLabels > 1 )
		{
			string.SetUTF8( pWAP->psButtonLabels[1] );
			mMessageDlgPointer->SetAltText(string.GetTCHAR());
		}
		if ( pWAP->nButtonLabels > 2 )
		{
			string.SetUTF8( pWAP->psButtonLabels[2] );
			mMessageDlgPointer->SetButton3Text(string.GetTCHAR());
		}

		// When a button is pressed pLuaResource is used to call back into lua code
		mMessageDlgPointer->SetNativeAlertInfo(pWAP->pLuaResource);

		// Create the message window (non-zero means success)
		if (mMessageDlgPointer->Create(CMessageDlg::IDD) == 0)
		{
			result = -1;
		}
		mMessageDlgPointer->ShowWindow(SW_SHOW);

		// Return hwnd for future cancel message
		pWAP->hwnd = mMessageDlgPointer->GetSafeHwnd();
	}

	return result;
}

#pragma endregion


#pragma region Public Functions
/// Starts simulating the project file currently held by the CSimulatorDoc.
void CSimulatorView::StartSimulation()
{
	// Stop the last running simulation.
	if (mRuntimeEnvironmentPointer)
	{
		StopSimulation();
	}

	Rtt::SimulatorAnalytics *pAnalytics = GetWinProperties()->GetAnalytics();
	if (pAnalytics && (GetDocument()->GetPath().GetLength() > 0))
	{
		// Log this project open
		pAnalytics->Log(
			"open-project",
			"skin",
			Rtt::TargetDevice::LabelForSkin( (Rtt::TargetDevice::Skin)GetAnalyticsSkinId() ) );
	}

	// Run the currently selected Corona project set in this view's document.
	RunCoronaProject();
}

/// Restarts simulation of the project file currently held by the CSimulatorDoc.
void CSimulatorView::RestartSimulation()
{
	// If the simulator is not currently running, then do a "Start" instead.
	if (nullptr == mRuntimeEnvironmentPointer)
	{
		StartSimulation();
		return;
	}

	// Record the "Restart" to the usage feedback to be posted to Corona Labs' server later.
	Rtt::SimulatorAnalytics *pAnalytics = GetWinProperties()->GetAnalytics();
	if (pAnalytics && (GetDocument()->GetPath().GetLength() > 0))
	{
	    ++mRelaunchCount;
	}

	// Run the currently selected Corona project set in this view's document.
	RunCoronaProject();
}

void CSimulatorView::SuspendResumeSimulationWithOverlay(bool showOverlay, bool sendSystemEvents)
{
	// Do not continue if we're showing an internal screen, such as the Welcome window.
	if (mIsShowingInternalScreen)
	{
		return;
	}

	// Do not continue if we're not currently running a project.
	if (!mRuntimeEnvironmentPointer)
	{
		return;
	}
	auto runtimePointer = mRuntimeEnvironmentPointer->GetRuntime();
	if (!runtimePointer)
	{
		return;
	}

	// Suspend/resume the Corona runtime.
	if (runtimePointer->IsSuspended())
	{
		runtimePointer->Resume(sendSystemEvents);
	}
	else
	{
		runtimePointer->Suspend(sendSystemEvents);
	}
	if (runtimePointer->IsSuspended())
	{
#if defined( Rtt_USE_BGFX )
		// The window stays enabled, and the Corona control stays visible: the bar is drawn into that control,
		// and Resume is on the bar. It says "Suspended" itself, which is also all a suspended runtime draws.
		if (showOverlay)
		{
			mMenuBar.SetStatusText("Suspended");
		}
#else
		if (showOverlay)
		{
			mCoronaContainerControl.SetWindowTextW(L"Suspended");
			mCoronaContainerControl.GetCoronaControl().ShowWindow(SW_HIDE);
		}
		EnableWindow(FALSE);
#endif
	}
	else
	{
#if defined( Rtt_USE_BGFX )
		mMenuBar.SetStatusText(nullptr);
#else
		EnableWindow(TRUE);
#endif
		if (mCoronaContainerControl.GetCoronaControl().IsWindowVisible() == FALSE)
		{
			mCoronaContainerControl.SetWindowTextW(L"");
			mCoronaContainerControl.GetCoronaControl().ShowWindow(SW_SHOW);
			mCoronaContainerControl.GetCoronaControl().SetFocus();
		}
	}

#if defined( Rtt_USE_BGFX )
	// The item's label is the state, so the menus are rebuilt rather than re-read.
	UpdateMenuBar();
#endif
}

/// Stops the current simulation and blanks out the screen.
void CSimulatorView::StopSimulation()
{
	// Do not continue if already stopped.
	if (!mRuntimeEnvironmentPointer)
	{
		return;
	}

	// Terminate the Corona runtime.
	Interop::SimulatorRuntimeEnvironment::Destroy(mRuntimeEnvironmentPointer);
	mRuntimeEnvironmentPointer = nullptr;

	RECT bounds;

	mCoronaContainerControl.GetWindowRect(&bounds);

	mCoronaContainerControl.DestroyWindow();
	mCoronaContainerControl.Create(nullptr, WS_CHILD | WS_VISIBLE, bounds, this);

	// Hide the Corona control and show its black container without any text.
	mCoronaContainerControl.SetWindowTextW(L"");
	mCoronaContainerControl.GetCoronaControl().ShowWindow(SW_HIDE);

	// Clear the simulator screen.
	UpdateSimulatorSkin();
}

bool CSimulatorView::IsSimulationSuspended() const
{
	if (mRuntimeEnvironmentPointer)
	{
		auto runtimePointer = mRuntimeEnvironmentPointer->GetRuntime();
		if (runtimePointer)
		{
			return runtimePointer->IsSuspended();
		}
	}
	return false;
}

// InitializeSimulation - select new skin and update
bool CSimulatorView::InitializeSimulation(Rtt::TargetDevice::Skin skinId)
{
	int zoom = (int) AfxGetApp()->GetProfileInt(REGISTRY_SECTION, REGISTRY_ZOOM, REGISTRY_ZOOM_DEFAULT);
	int rotation = AfxGetApp()->GetProfileInt(REGISTRY_SECTION, REGISTRY_ROTATION, REGISTRY_ROTATION_DEFAULT);

	// Restore the remembered zoom level and rotation
	CMainFrame *pMainWnd = (CMainFrame *)GetParentFrame();
	ASSERT(pMainWnd);

	pMainWnd->SetZoom(zoom);
	SetRotation(rotation);

	if (skinId == Rtt::TargetDevice::kCustomSkin)
	{
		mDisplayName = _T("CustomDevice");
	}
	else
	{
		mDisplayName = Rtt::TargetDevice::LabelForSkin(skinId);
	}
	((CSimulatorApp*)AfxGetApp())->PutDisplayName(mDisplayName);

	bool skinLoaded = InitSkin(skinId);
	UpdateSimulatorSkin();

	// Draw the device skin onscreen.
	if (skinLoaded)
	{
		RestartSimulation();
	}

	return skinLoaded;
}

// UpdateSimulatorSkin - Update main window size & skin based on display type, 
// zoom state, rotation state.
// CMainFrame handles resizing the window for zoom & rotate
void CSimulatorView::UpdateSimulatorSkin()
{
	CSimulatorApp *applicationPointer = (CSimulatorApp*)AfxGetApp();
	
	// Determine what the size of the client area of the window needs to be.
    UINT clientWidth = 0;
    UINT clientHeight = 0;
	Gdiplus::Bitmap *pBitmap = mIsShowingInternalScreen ? nullptr : GetSkinBitmap();
    if (pBitmap)
	{
		// A simulator skin was loaded by InitSkin(). Use the already rotated bitmap's dimensions.
		clientWidth = pBitmap->GetWidth();
		clientHeight = pBitmap->GetHeight();
	}
	else if (mIsShowingInternalScreen)
	{
		// We are not simulating a device. Use the runtime's width and height.
		if (mRuntimeEnvironmentPointer && mRuntimeEnvironmentPointer->GetRuntime())
		{
			Rtt::Display& display = mRuntimeEnvironmentPointer->GetRuntime()->GetDisplay();
			clientWidth = display.ContentWidth();
			clientHeight = display.ContentHeight();
		}

		// Apply the system's DPI scale factor to the width and height.
		// Note: In the future, we should resize the window when the DPI scale factor changes dynamically.
		auto deviceContextPointer = this->GetDC();
		if (deviceContextPointer)
		{
			const int kBaseDpi = 96;
			int dpiValue = deviceContextPointer->GetDeviceCaps(LOGPIXELSX);
			if ((dpiValue > 0) && (dpiValue != kBaseDpi))
			{
				double dpiScale = (double)dpiValue / (double)kBaseDpi;
				clientWidth = (int)std::round((double)clientWidth * dpiScale);
			}
			dpiValue = deviceContextPointer->GetDeviceCaps(LOGPIXELSY);
			if ((dpiValue > 0) && (dpiValue != kBaseDpi))
			{
				double dpiScale = (double)dpiValue / (double)kBaseDpi;
				clientHeight = (int)std::round((double)clientHeight * dpiScale);
			}
		}
	}
	else
	{
		// We're simulating a skinless device such as an Ouya or GameStick.
		if (mDeviceConfig.supportsScreenRotation)
		{
			bool isUpright = ((GetRotation() == 0) || (GetRotation() == 180));
			clientWidth = (UINT)(isUpright ? mDeviceConfig.screenWidth : mDeviceConfig.screenHeight);
			clientHeight = (UINT)(isUpright ? mDeviceConfig.screenHeight : mDeviceConfig.screenWidth);
		}
		else
		{
			bool isPortrait = mDeviceConfig.isUprightOrientationPortrait;
			clientWidth = (UINT)(isPortrait ? mDeviceConfig.screenWidth : mDeviceConfig.screenHeight);
			clientHeight = (UINT)(isPortrait ? mDeviceConfig.screenHeight : mDeviceConfig.screenWidth);
		}
	}
	
	// Validate width and height.
	if ((clientWidth <= 0) || (clientHeight <= 0))
	{
        return;
	}
	
	// Fetch the size of the window's client area, which the surface we render to.
	CMainFrame *pMainWnd = (CMainFrame*)GetParentFrame();
	if (!pMainWnd)
	{
		return;
	}
	CRect clientBounds;
	pMainWnd->GetClientRect(clientBounds);
	
	// Do not update window size if it is currently minimized.
	if (pMainWnd->IsIconic() || (clientBounds.Width() <= 0) || (clientBounds.Height() <= 0))
	{
		return;
	}
	
	// Calculate a good zoom factor for the window to size itself to.
	if (mIsShowingInternalScreen)
	{
		// We are displaying the Home or Demo screens.
		// Set the zoom factor to 0, which means do not zoom.
		pMainWnd->SetZoom(0);
	}
	else
	{
		// Zoom in/out to fit the device skin.
		pMainWnd->AdjustZoom(clientWidth, clientHeight);
	}
	float zoomFactor = pMainWnd->CalcZoomFactor();
	
	// Set the client window size that will render the device skin and the Corona contents.
	//
	// The menu bar is drawn into the same surface Corona renders into, above the content, so the window is
	// that much taller than what the zoom factor works out to. Zero without bgfx, where there is no bar.
	const int menuBarHeight = GetMenuBarHeight();

	clientBounds.top = 0;
	clientBounds.left = 0;
	clientBounds.right = (int)floor(zoomFactor * clientWidth);
	clientBounds.bottom = (int)floor(zoomFactor * clientHeight) + menuBarHeight;
	pMainWnd->SizeToClient(clientBounds);
	
	// Calculate the bounds of the Corona control.
	CRect coronaBounds;
	if (pBitmap)
	{
		// We're displaying a skin. So, set the bounds of the Corona control in the skin's screen region.
		// Note: The skin's origin is in the bottom-left corner. We need to flip it to the top-left corner for Windows.
		bool isUpright = ((GetRotation() == 0) || (GetRotation() == 180));
		LONG unrotatedBitmapWidth = isUpright ? pBitmap->GetWidth() : pBitmap->GetHeight();
		LONG unrotatedBitmapHeight = isUpright ? pBitmap->GetHeight() : pBitmap->GetWidth();
		coronaBounds.top = unrotatedBitmapHeight - (LONG)(mDeviceConfig.screenOriginY + mDeviceConfig.screenHeight + 0.5f);
		coronaBounds.left = (LONG)(mDeviceConfig.screenOriginX + 0.5f);
		coronaBounds.bottom = unrotatedBitmapHeight - (LONG)(mDeviceConfig.screenOriginY + 0.5f);
		coronaBounds.right = (LONG)(mDeviceConfig.screenOriginX + mDeviceConfig.screenWidth + 0.5f);

		// Rotate the Corona control bounds if the skin is rotated.
		switch (GetRotation())
		{
			case 90:
			{
				CRect rotatedBounds;
				rotatedBounds.left = unrotatedBitmapHeight - coronaBounds.bottom;
				rotatedBounds.top = coronaBounds.left;
				rotatedBounds.right = unrotatedBitmapHeight - coronaBounds.top;
				rotatedBounds.bottom = coronaBounds.right;
				coronaBounds = rotatedBounds;
				break;
			}
			case 180:
			{
				CRect rotatedBounds;
				rotatedBounds.left = unrotatedBitmapWidth - coronaBounds.right;
				rotatedBounds.top = unrotatedBitmapHeight - coronaBounds.bottom;
				rotatedBounds.right = unrotatedBitmapWidth - coronaBounds.left;
				rotatedBounds.bottom = unrotatedBitmapHeight - coronaBounds.top;
				coronaBounds = rotatedBounds;
				break;
			}
			case 270:
			{
				CRect rotatedBounds;
				rotatedBounds.left = coronaBounds.top;
				rotatedBounds.top = unrotatedBitmapWidth - coronaBounds.right;
				rotatedBounds.right = coronaBounds.bottom;
				rotatedBounds.bottom = unrotatedBitmapWidth - coronaBounds.left;
				coronaBounds = rotatedBounds;
				break;
			}
		}

		// Apple the zoom level scale.
		ScaleRect(coronaBounds, zoomFactor);
	}
	else
	{
		// Not showing a skin. Use the same bounds as the window's client area.
		coronaBounds.CopyRect(&clientBounds);
	}

	// Everything above placed the device's screen. The control the screen is rendered into is the bar taller
	// than that, because the bar is drawn into it -- so it starts that much higher up, and the screen ends up
	// exactly where the skin says it should be. With a skin showing, the strip the bar occupies is over the
	// device's bezel; without one it is the top of the window.
	if (menuBarHeight > 0)
	{
		if (pBitmap)
		{
			// Shift the screen down instead of growing upward into the skin's own top edge, which the bar
			// would otherwise be drawn over rather than above.
			coronaBounds.OffsetRect(0, menuBarHeight);
		}
		coronaBounds.top -= menuBarHeight;
	}

	mCoronaContainerControl.MoveWindow(coronaBounds, FALSE);

    // Set size, position, and visibility of view window
	this->MoveWindow(clientBounds, TRUE);
	this->ShowWindow(SW_SHOW);
}

bool CSimulatorView::VerifyAllPluginsAcquired()
{
	// Do not continue if we're not currently running a Corona project.
	if (!mRuntimeEnvironmentPointer || !mRuntimeEnvironmentPointer->GetRuntime())
	{
		return true;
	}

	// Verify that all of the Corona project's plugins have been downloaded/acquired.
	if (mRuntimeEnvironmentPointer->GetRuntime()->RequiresDownloadablePlugins())
	{
		Rtt::String utf8MissingPluginsString;
		auto runtimePointer = mRuntimeEnvironmentPointer->GetRuntime();
		if (Rtt::PlatformAppPackager::AreAllPluginsAvailable(runtimePointer, &utf8MissingPluginsString) == false)
		{
			// Display a message box detailing which plugins were not found and how to resolve it.
			CStringW title;
			CStringW message;
			title.LoadStringW(IDS_WARNING);
			WinString missingPluginsString(L"");
			if (!utf8MissingPluginsString.IsEmpty())
			{
				missingPluginsString.SetUTF16(L"Corona failed to acquire the following plugins:\n- ");
				WinString stringBuffer(utf8MissingPluginsString.GetString());
				stringBuffer.Replace("\n", "\n- ");
				missingPluginsString.Append(stringBuffer.GetUTF16());
				missingPluginsString.Append(L"\n\n");
			}
			message.Format(IDS_CANNOT_BUILD_WITHOUT_PLUGINS, missingPluginsString.GetUTF16());
			Interop::UI::TaskDialog dialog;
			dialog.GetSettings().SetParentWindowHandle(this->GetSafeHwnd());
			dialog.GetSettings().SetTitleText(title);
			dialog.GetSettings().SetMessageText(message);
			dialog.GetSettings().GetButtonLabels().push_back(std::wstring(L"&Learn More"));
			dialog.GetSettings().GetButtonLabels().push_back(std::wstring(L"&Cancel"));
			dialog.Show();

			// Display Corona's documentation about plugin "build.settings" via the default web browser.
			if (dialog.GetLastPressedButtonIndex() == 0)
			{
				try
				{
					::ShellExecuteW(
							nullptr, L"open",
							L"https://docs.coronalabs.com/daily/guide/distribution/buildSettings/index.html#plugins",
							nullptr, nullptr, SW_SHOWNORMAL);
				}
				catch (...) {}
			}

			// Returning false indicates that we've failed to acquire all plugins.
			return false;
		}
	}

	// All plugins have been acquired or the project does not require plugins.
	return true;
}

#ifdef _DEBUG
void CSimulatorView::AssertValid() const
{
	CView::AssertValid();
}

void CSimulatorView::Dump(CDumpContext& dc) const
{
	CView::Dump(dc);
}

CSimulatorDoc* CSimulatorView::GetDocument() const // non-debug version is inline
{
	ASSERT(m_pDocument->IsKindOf(RUNTIME_CLASS(CSimulatorDoc)));
	return (CSimulatorDoc*)m_pDocument;
}
#endif //_DEBUG

#pragma endregion


#pragma region Private Functions
/// Open the document's currently selectd Corona project and starts simulation.
/// Shows the home screen if no project was selected and only if enabled in application preferences.
void CSimulatorView::RunCoronaProject()
{
	if (ValidateOpenGL() == false)
	{
		return;
 	}

    // Fetch the document's currently selected "main.lua" file.
    CString filePath = GetDocument()->GetPath();
    if (filePath.IsEmpty())
    {
        // A file was not selected. Show the home screen if enabled.
        // Do not show the home screen if we're running the Corona Debugger.
        CSimulatorApp *applicationPointer = (CSimulatorApp*)AfxGetApp();
        if (applicationPointer->IsHomeScreenEnabled() &&
            (applicationPointer->IsDebugModeEnabled() == false))
        {
            filePath = applicationPointer->GetHomeScreenFilePath();
        }
        else
        {
            applicationPointer->LoadZoomFromRegistry();
        }
    }

    // Run the Corona project.
    RunCoronaProject(CCoronaProject::RemoveMainLua(filePath));
}

/// Opens the given Corona project and starts simulation.
/// @param filePath The path to the directory containing the Corona project's "main.lua" file.
///                 Set this to an empty string to stop simulation and show a blank screen.
void CSimulatorView::RunCoronaProject(CString& projectPath)
{
	// Fetch the application object.
	CSimulatorApp *applicationPointer = ((CSimulatorApp*)AfxGetApp());
	if (!applicationPointer)
	{
		return;
	}
	
	// Identify if we are loading the home screen Corona project.
	{
		CString lowercaseProjectPath = projectPath;
		lowercaseProjectPath.MakeLower();
		CString lowercaseResourceDir = ((CSimulatorApp*)AfxGetApp())->GetResourceDir();
		lowercaseResourceDir.MakeLower();
		mIsShowingInternalScreen = (lowercaseProjectPath.Find(lowercaseResourceDir) >= 0);
	}

	// Set the working directory that the Open File dialog will use to the
	// parent directory of the currently active project.
	if (!mIsShowingInternalScreen)
	{
		CString sParentDir = projectPath;
		sParentDir = sParentDir.Left( sParentDir.ReverseFind(_T('\\')) );
		CSimulatorApp *applicationPointer = (CSimulatorApp*)AfxGetApp();
		applicationPointer->SetWorkingDir( sParentDir );
		applicationPointer->LoadZoomFromRegistry();
	}
	
	// The simulator's menu bar used to be swapped here, between a welcome
	// screen menu and a fuller one for a loaded project. Both are gone: the
	// bar is Rtt::MenuBar now, drawn by bgfx over the window, and it rebuilds
	// itself from the same distinction without the frame window's help.

	// Do not load the project if the machine does not meet the minimum OpenGL requirements.
	if ((projectPath.GetLength() > 0) && (ValidateOpenGL() == false))
	{
		projectPath.Empty();
	}

	// Disable logging if showing the Welcome screen, unless our undocumented debug flag is set in the registry.
	// We do this because the simulator user will only be interested in debugging their own simulated apps.
	// Note: Debug logging cannot be disabled when running the debug version of the Corona Simulator.
	if (mIsShowingInternalScreen && !applicationPointer->GetProfileInt(REGISTRY_SECTION, _T("debugWelcomeProcess"), 0))
	{
		Rtt_LogDisable();
	}
	else
	{
		Rtt_LogEnable();
	}
	
	// Terminate the last Corona runtime.
	Interop::SimulatorRuntimeEnvironment::Destroy(mRuntimeEnvironmentPointer);
	mRuntimeEnvironmentPointer = nullptr;

	// Load and run the application.
	if (projectPath.GetLength() > 0)
	{
		RECT bounds;

		mCoronaContainerControl.GetWindowRect(&bounds);

		mCoronaContainerControl.DestroyWindow();
		mCoronaContainerControl.Create(nullptr, WS_CHILD | WS_VISIBLE, bounds, this);

		// Show the Corona control before creating the runtime. The Corona runtime will render to this control.
		mCoronaContainerControl.GetCoronaControl().ShowWindow(SW_SHOW);
		mCoronaContainerControl.GetCoronaControl().SetFocus();

		// Set up the Corona runtime launch settings.
		Interop::SimulatorRuntimeEnvironment::CreationSettings settings;
		settings.ResourceDirectoryPath = projectPath;
		settings.MainWindowHandle = nullptr;			// <- Do not let the runtime take control of the main window.
		settings.RenderSurfaceHandle = mCoronaContainerControl.GetCoronaControl().GetSafeHwnd();
		settings.LoadedEventHandlerPointer = &mRuntimeLoadedEventHandler;
		if (mIsShowingInternalScreen)
		{
			// Give the Welcome Window access to our "WinSimulatorServices" object's features.
			settings.CoronaSimulatorServicesPointer = &mSimulatorServices;
		}
		else
		{
			// Provide the configuration of the device we will be simulating.
			settings.DeviceConfigPointer = &mDeviceConfig;

			// Set up the runtime for debug mode, if enabled.
			if (((CSimulatorApp*)::AfxGetApp())->IsDebugModeEnabled())
			{
				settings.LaunchOptions |= Rtt::Runtime::kConnectToDebugger;
			}
		}

		// Create and startup the Corona runtime.
		// Note: This class' OnRuntimeLoaded() method will be called before this function returns if successfully loaded.
		auto result = Interop::SimulatorRuntimeEnvironment::CreateUsing(settings);

		// Display an error message if we've failed to load the Corona project.
		if (result.HasFailed() || !mRuntimeEnvironmentPointer)
		{
			CStringW title;
			title.LoadString(IDS_WARNING);
			auto errorMessage = result.GetMessageW();
			if (!errorMessage || (L'\0' == errorMessage[0]))
			{
				errorMessage = L"Failed to load Corona project.";
			}
			::MessageBoxW(GetSafeHwnd(), errorMessage, (LPCWSTR)title, MB_OK | MB_ICONWARNING);
		}
	}
	
	// Update the size of the window to match the project's default orientation and configured skin, if specified.
	EnableWindow(TRUE);	// in case we were suspended
	if (!mRuntimeEnvironmentPointer)
	{
		UpdateSimulatorSkin();
		mCoronaContainerControl.SetWindowTextW(L"");
		mCoronaContainerControl.GetCoronaControl().ShowWindow(SW_HIDE);
	}

	// If we're monitoring a project directory, close that handle (we'll open a new one when we need to)
	if (mAppChangeHandle)
	{
		FindCloseChangeNotification(mAppChangeHandle);
		mAppChangeHandle = nullptr;
	}
}

// SkinDisplayNameFromID - translate skin type from resource id
Rtt::TargetDevice::Skin CSimulatorView::SkinIDFromMenuID( UINT nMenuID )
{
	Rtt::TargetDevice::Skin skinID = (Rtt::TargetDevice::Skin) ( nMenuID - ID_VIEWAS_BEGIN );

	return skinID;
}

/// Gets a unique integer ID for the currently selected device skin.
/// @return Returns a unique skin ID of type Rtt::TargetDevice for the current skin selection.
///         Returns Rtt::TargetDevice::kUnknownSkin if the current skin selction is unknown.
int CSimulatorView::GetAnalyticsSkinId()
{
	return m_nSkinId;
}

// SetRotation - validate rotation value and set member variable
void CSimulatorView::SetRotation( int rotation )
{
    // only 0, 90, 180, 270 are valid
	rotation = rotation % 360;
	if (rotation < 90)  // neg. numbers are set to 0
        rotation = 0;
	else if( rotation < 180)
        rotation = 90;
	else if( rotation < 270)
        rotation = 180;
	else if( rotation < 360 )
        rotation = 270;

	// Store the given rotation value.
	CSimulatorApp *applicationPointer = ((CSimulatorApp*)AfxGetApp());
	if (applicationPointer)
	{
		applicationPointer->PutRotation(rotation);
	}
	mRotation = rotation;
}

// ScaleRect - Multiply all members of a rect by a scale factor.  Modifies rect.
void CSimulatorView::ScaleRect( CRect& rect, float scale )
{
    rect.left = (LONG) floor( rect.left * scale );
    rect.top = (LONG) floor( rect.top * scale );
    rect.right = (LONG) floor( rect.right * scale );
    rect.bottom = (LONG) floor( rect.bottom * scale );
}

// InitSkin - Load device bitmap and rotate as needed
// Only called from InitializeSimulation()
bool CSimulatorView::InitSkin( Rtt::TargetDevice::Skin skinId )
{
	WinString skinFile;

	if (skinId == Rtt::TargetDevice::kCustomSkin)
	{
		skinFile.SetTCHAR(mCustomSkinFileName);
	}
	else
	{
		skinFile.SetUTF8( Rtt::TargetDevice::LuaObjectFileFromSkin( skinId ) );
	}

	m_nSkinId = skinId;

	// Get the skins directory which is wherever the Lua file for the skin is
	// (things like the skin bitmap and statusbar images will be specified 
	// relative to this)
	wchar_t skinPathBuf[MAX_PATH];

	_tcsncpy_s(skinPathBuf, skinFile.GetTCHAR(), MAX_PATH);

	PathRemoveFileSpec(skinPathBuf);

	// Delete the last loaded bitmap.
    if (mpSkinBitmap != NULL)
	{
        delete mpSkinBitmap;
		mpSkinBitmap = NULL;
	}
	
	// Load the skin's configuration in Lua.
	Rtt::PlatformSimulator::LoadConfig(skinFile.GetUTF8(), mDeviceConfig);
	CString sPath( mDeviceConfig.deviceImageFile.GetString() );

	// Load skin image file as a bitmap.
	if (sPath.GetLength() > 0)
	{
		PathAppend(skinPathBuf, sPath);

		mpSkinBitmap = Gdiplus::Bitmap::FromFile(CStringW(skinPathBuf));
		if (mpSkinBitmap && (mpSkinBitmap->GetLastStatus() == Gdiplus::Ok))
		{
			// Match bitmap orientation to current window rotation.
			mpSkinBitmap->RotateFlip( DegreesToRFType( GetRotation() ) );
		}
		else
		{
			// Image file not found or failed to load it.
			// Assume the simulated device is skinless and continue.
			if (mpSkinBitmap)
			{
				delete mpSkinBitmap;
				mpSkinBitmap = NULL;
			}
		}
	}
	
	// Device skin was loaded successfully. It will be drawn within UpdateSimulatorSkin().
    return true;
}

// ValidateOpenGL - 
// Checks if the current OpenGL context meets Corona's minimum requirements.
// This function should be called after calling EnableOpenGL() and before simulating an app.
// Displays an error message if the minimum requirements have not been met.
// Returns true if the minimum OpenGL requirements for simulation have been met and
// it is okay to proceed to simulate an app.
bool CSimulatorView::ValidateOpenGL()
{
	// Verify that the control we want to render to meets Corona's minimum requirements.
	HWND windowHandle = mCoronaContainerControl.GetCoronaControl().GetSafeHwnd();
	auto result = Interop::RuntimeEnvironment::ValidateRenderSurface(windowHandle);

	// Fetch the renderer's version string.
	WinString rendererVersionString;
	rendererVersionString.SetUTF8(result.RendererVersion.GetString());
	if (rendererVersionString.IsEmpty())
	{
		rendererVersionString.SetUTF16(L"OpenGL Driver Version: Unknown");
	}

	// Display a warning message if we can render, but not all graphics features will work.
	if (result.CanRender && (false == result.SupportsAllShaders))
	{
		CMessageDlg messageDlg;
		CString sMessage;
		CString sRequiredVersion;

		sRequiredVersion.Format(
				_T("%d.%d"), result.MinVersionSupported.GetMajorNumber(), result.MinVersionSupported.GetMinorNumber());
		sMessage.Format(IDS_OPENGL_VERSION_WARNING, sRequiredVersion, rendererVersionString.GetTCHAR());
		messageDlg.SetTitle(IDS_WARNING);
		messageDlg.SetText(sMessage);
		messageDlg.SetDefaultText(IDS_OK);
		messageDlg.SetIconStyle(MB_ICONEXCLAMATION);
		if (messageDlg.DoModal() != ID_MSG_BUTTON2)
		{
			// User has chosen not to continue. Fail the validation test.
			mCoronaContainerControl.ShowWindow(SW_HIDE);
			return false;
		}
	}

	// Display a major warning only once if we cannot render reliable at all.
	// This will most likely happen if the OpenGL driver is too old.
	static bool sDisableCheckForGL21 = false;
	if (!sDisableCheckForGL21 && !result.CanRender)
	{
		CMessageDlg messageDlg;
		CString sMessage;
		CString sRequiredVersion;

		sRequiredVersion.Format(
				_T("%d.%d"), result.MinVersionSupported.GetMajorNumber(), result.MinVersionSupported.GetMinorNumber());
		sMessage.Format(IDS_OPENGL21_VERSION_WARNING, sRequiredVersion, rendererVersionString.GetTCHAR());
		messageDlg.SetTitle(IDS_WARNING);
		messageDlg.SetText(sMessage);
		messageDlg.SetDefaultText(IDS_CANCEL);
		messageDlg.SetAltText(IDS_IGNORE);
		messageDlg.SetIconStyle(MB_ICONEXCLAMATION);
		if (messageDlg.DoModal() != ID_MSG_BUTTON2)
		{
			// User has chosen not to continue. Fail the validation test.
			mCoronaContainerControl.ShowWindow(SW_HIDE);
			return false;
		}
		else
		{
			// User chose to ignore the warning.
			// In this case, never warn the user about this issue again so as not to annoy him/her.
			sDisableCheckForGL21 = true;
		}
	}

	// The system successfully meets Corona's minimum graphics requirements
	// ...or there is a graphics issue and the user has chosen to continue at his/her own risk.
	if (!mCoronaContainerControl.IsWindowVisible())
	{
		mCoronaContainerControl.ShowWindow(SW_SHOW);
	}
	return true;
}

/// Gets a list of recent projects.
void CSimulatorView::GetRecentDocs(Rtt::LightPtrArray<Rtt::RecentProjectInfo> *listPointer)
{
	// Validate.
	if (!listPointer)
	{
		return;
	}

	// Delete all entries in the given list.
	listPointer->Clear();

	// Fetch this application's recent file list.
	CRecentFileList *recentListPointer = ((CSimulatorApp*)AfxGetApp())->GetRecentFileList();
	if (!recentListPointer)
	{
		return;
	}

	// Copy information in this app's recents list to the given list.
	WinString stringTranscoder;
	int listCount = recentListPointer->GetSize();
	if (listCount > 10)
	{
		listCount = 10;
	}
	for (int index = 0; index < listCount; index++)
	{
		// Fetch the next file path in the recents list.
		// Note: Will be an empty string if there are no more recent files in the collection.
		CString lastFilePathName = (*recentListPointer)[index];
		if (lastFilePathName.IsEmpty())
		{
			continue;
		}

		// Create a recent project info object.
		Rtt::RecentProjectInfo *infoPointer = new Rtt::RecentProjectInfo();
		if (!infoPointer)
		{
			continue;
		}

		// Copy the project's folder name to the info object.
		CString sTitle = lastFilePathName;
		sTitle = CCoronaProject::RemoveMainLua(sTitle);
		sTitle = sTitle.Right(sTitle.GetLength() - sTitle.ReverseFind(_T('\\')) - 1);
		stringTranscoder.SetTCHAR(sTitle);
		infoPointer->formattedString = stringTranscoder.GetUTF8();

		// Copy the project's "main.lua" file path to the info object.
		stringTranscoder.SetTCHAR(lastFilePathName);
		infoPointer->fullURLString = stringTranscoder.GetUTF8();

		// Add the info object to the given collection.
		listPointer->Append(infoPointer);
	}
}


// DegressToRFType - Convert rotation in degress to RotateFlipType
Gdiplus::RotateFlipType DegreesToRFType( int rotation )
{
    switch (rotation)
	{
	    case 0:
			return Gdiplus::RotateNoneFlipNone;
	    case 90:
			return Gdiplus::Rotate90FlipNone;
	    case 180:
			return Gdiplus::Rotate180FlipNone;
	    case 270:
			return Gdiplus::Rotate270FlipNone;
	}
	return Gdiplus::RotateNoneFlipNone;
}

#pragma endregion


#pragma region CCoronaControlContainer Class
int CSimulatorView::CCoronaControlContainer::OnCreate(LPCREATESTRUCT lpCreateStruct)
{
	// Call the base class' method first.
	int result = CStatic::OnCreate(lpCreateStruct);
	if (-1 == result)
	{
		return -1;
	}

	// This center aligns this control's "Suspended" text horizontally and vertically.
	ModifyStyle(WS_TABSTOP, SS_CENTER | SS_CENTERIMAGE);

	// Create the Corona control.
	RECT bounds;
	bounds.top = 0;
	bounds.left = 0;
	bounds.bottom = lpCreateStruct->cy;
	bounds.right = lpCreateStruct->cx;
	mCoronaControl.Create(
			nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, bounds, this, 1);
	mCoronaControl.SetFocus();
	return 0;
}

HBRUSH CSimulatorView::CCoronaControlContainer::CtlColor(CDC* pDC, UINT nCtlColor)
{
	pDC->SetTextColor(RGB(255, 255, 255));
	pDC->SetBkMode(OPAQUE);
	pDC->SetBkColor(RGB(0, 0, 0));
	return (HBRUSH)GetStockObject(BLACK_BRUSH);
}

void CSimulatorView::CCoronaControlContainer::OnSize(UINT nType, int cx, int cy)
{
	// Resize the Corona control to match the new size of this container control
	mCoronaControl.SetWindowPos(nullptr, 0, 0, cx, cy, SWP_NOZORDER | SWP_NOMOVE);
}

CWnd& CSimulatorView::CCoronaControlContainer::GetCoronaControl()
{
	return mCoronaControl;
}

#pragma endregion

#pragma region Menu Bar
// The simulator's menu bar.
//
// Every platform used to grow its own -- an MFC menu resource here, an AppKit main menu on macOS, a Dear
// ImGui bar on Linux -- so the same File menu existed three times and behaved differently in each. Under
// bgfx none of them can reach the window any more: bgfx owns it, and a Win32 menu on the frame would be
// painted over. So the bar is Rtt::MenuBar, drawn through the renderer's overlay hook, and what it puts on
// itself is UI/Rtt_SimulatorMenus.h -- the same description every host uses.

int CSimulatorView::GetMenuBarHeight()
{
#if defined( Rtt_USE_BGFX )
	return Rtt::MenuBar::GetHeight();
#else
	return 0;
#endif
}

#if defined( Rtt_USE_BGFX )

void CSimulatorView::EnsureMenuBar()
{
	if (mMenuBar.IsInitialized() || mMenuBarFailed)
	{
		return;
	}
	if (!mRuntimeEnvironmentPointer || !mRuntimeEnvironmentPointer->GetRuntime())
	{
		return;
	}

	// bgfx has to exist before the bar can bake a font or compile a shader, and it does not until the
	// runtime has built its renderer -- which is why this is here rather than in OnCreate().
	auto& renderer = mRuntimeEnvironmentPointer->GetRuntime()->GetDisplay().GetRenderer();
	auto bgfxRendererPointer = dynamic_cast<Rtt::BgfxRenderer*>(&renderer);
	if (!bgfxRendererPointer)
	{
		// A project asked for another backend by name. Without bgfx there is nowhere to draw a bar; every
		// command on it still has a keyboard shortcut, which this build keeps working below.
		mMenuBarFailed = true;
		return;
	}

	// The bar bakes its own atlas out of a TrueType file, so it needs one. Segoe UI is what the rest of the
	// simulator's chrome is drawn in, and it is present on every Windows the simulator supports.
	std::vector<char> fontBytes;
	{
		wchar_t fontPath[MAX_PATH];
		fontPath[0] = L'\0';
		::GetWindowsDirectoryW(fontPath, MAX_PATH);
		::PathAppendW(fontPath, L"Fonts\\segoeui.ttf");

		HANDLE fileHandle = ::CreateFileW(
				fontPath, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (INVALID_HANDLE_VALUE != fileHandle)
		{
			LARGE_INTEGER fileSize{};
			if (::GetFileSizeEx(fileHandle, &fileSize) && (fileSize.QuadPart > 0))
			{
				fontBytes.resize((size_t)fileSize.QuadPart);

				DWORD bytesRead = 0;
				if (!::ReadFile(fileHandle, &fontBytes[0], (DWORD)fontBytes.size(), &bytesRead, nullptr) ||
				    (bytesRead != fontBytes.size()))
				{
					fontBytes.clear();
				}
			}
			::CloseHandle(fileHandle);
		}
	}

	// Without a bar the simulator is harder to use but still runs. Remembered rather than retried, because
	// bringing the bar up compiles two shaders.
	if (fontBytes.empty() || !mMenuBar.Initialize(&fontBytes[0], fontBytes.size()))
	{
		mMenuBarFailed = true;
		Rtt_LogException("WARNING: the simulator's menu bar could not be created\r\n");
		return;
	}

	mMenuBar.SetCommandHandler(&CSimulatorView::OnMenuBarCommand, this);
	bgfxRendererPointer->SetOverlay(
			&CSimulatorView::RenderMenuBarOverlay, &CSimulatorView::ReleaseMenuBarOverlay, this);

	// The surface is taller than the device screen by exactly the bar, and this is what tells the rest of
	// Corona so; see RenderSurfaceControl::SetOverlayHeight().
	auto renderSurfacePointer = mRuntimeEnvironmentPointer->GetRenderSurface();
	if (renderSurfacePointer)
	{
		renderSurfacePointer->SetOverlayHeight(GetMenuBarHeight());
		renderSurfacePointer->SetMessageFilter(&CSimulatorView::OnRenderSurfaceMessage, this);
	}

	UpdateMenuBar();
}

void CSimulatorView::UpdateMenuBar()
{
	// Nothing to put on the bar until there is one; EnsureMenuBar() calls back here once there is.
	if (!mMenuBar.IsInitialized())
	{
		return;
	}

	std::vector<Rtt::Menu> menus;

	Rtt::BuildSimulatorMenus(mIsShowingInternalScreen, IsSimulationSuspended(), menus);

	mMenuBar.SetMenus(menus);
}

void CSimulatorView::RenderMenuBarOverlay(void* userdata, Rtt::U16 view, Rtt::U32 width, Rtt::U32 height)
{
	auto viewPointer = (CSimulatorView*)userdata;

	viewPointer->mMenuBar.Render(view, width, height);
}

void CSimulatorView::ReleaseMenuBarOverlay(void* userdata)
{
	// The menus themselves are kept: they are plain data, and the next runtime's bar wants the same ones.
	// Only the bgfx handles go.
	((CSimulatorView*)userdata)->mMenuBar.Finalize();
}

UINT CSimulatorView::CommandIdFor(int command)
{
	switch (command)
	{
		case Rtt::SimulatorCommand::kNewProject:			return ID_FILE_NEWPROJECT;
		case Rtt::SimulatorCommand::kOpenProject:			return ID_FILE_OPEN;
		case Rtt::SimulatorCommand::kOpenInEditor:			return ID_FILE_OPENINEDITOR;
		case Rtt::SimulatorCommand::kShowProjectFiles:		return ID_FILE_SHOW_PROJECT_FILES;
		case Rtt::SimulatorCommand::kShowProjectSandbox:	return ID_FILE_SHOWPROJECTSANDBOX;
		case Rtt::SimulatorCommand::kClearProjectSandbox:	return ID_FILE_CLEARPROJECTSANDBOX;
		case Rtt::SimulatorCommand::kRelaunch:				return ID_FILE_RELAUNCH;

		// With nothing loaded, relaunching "the last project" is what the one relaunch command does.
		case Rtt::SimulatorCommand::kRelaunchLastProject:	return ID_FILE_RELAUNCH;

		case Rtt::SimulatorCommand::kCloseProject:			return ID_FILE_CLOSE;
		case Rtt::SimulatorCommand::kOpenPreferences:		return ID_FILE_PREFERENCES;
		case Rtt::SimulatorCommand::kQuit:					return ID_APP_EXIT;

		case Rtt::SimulatorCommand::kBuildAndroid:			return ID_BUILD_FOR_ANDROID;
		case Rtt::SimulatorCommand::kBuildHTML5:			return ID_BUILD_FOR_WEB;
		case Rtt::SimulatorCommand::kBuildLinux:			return ID_BUILD_FOR_LINUX;

		case Rtt::SimulatorCommand::kRotateLeft:			return ID_VIEW_ROTATELEFT;
		case Rtt::SimulatorCommand::kRotateRight:			return ID_VIEW_ROTATERIGHT;
		case Rtt::SimulatorCommand::kShake:					return ID_VIEW_SHAKE;
		case Rtt::SimulatorCommand::kSuspendResume:			return ID_VIEW_SUSPEND;

		case Rtt::SimulatorCommand::kSetFocusConsole:		return ID_VIEW_CONSOLE;
		case Rtt::SimulatorCommand::kViewAs:				return ID_VIEW_VIEWAS;

		case Rtt::SimulatorCommand::kOpenDocumentation:		return ID_HELP;
		case Rtt::SimulatorCommand::kOpenSampleProjects:	return ID_FILE_OPEN_SAMPLE_PROJECT;
		case Rtt::SimulatorCommand::kAbout:					return ID_APP_ABOUT;

		// Zoom belongs to the frame window, which owns the zoom level; the ids are its, not this view's.
		case Rtt::SimulatorCommand::kZoomIn:				return ID_WINDOW_ZOOMIN;
		case Rtt::SimulatorCommand::kZoomOut:				return ID_WINDOW_ZOOMOUT;

		default:											return 0;
	}
}

void CSimulatorView::OnMenuBarCommand(void* userdata, int command)
{
	auto viewPointer = (CSimulatorView*)userdata;

	UINT commandId = CommandIdFor(command);
	if (0 == commandId)
	{
		return;
	}

	// Posted rather than sent, and to the frame rather than to this view: the commands were already routed
	// that way when they came from a Win32 menu, so a menu item and the accelerator that does the same thing
	// end up in the same place. Posting also lets the click that chose the item finish first -- some of these
	// tear down the runtime, and with it the bar that is still handling the click.
	auto frameWindowPointer = viewPointer->GetParentFrame();
	if (frameWindowPointer)
	{
		frameWindowPointer->PostMessage(WM_COMMAND, MAKEWPARAM(commandId, 0), 0);
	}
}

// Which of the keys an accelerator can be built on this virtual-key code is, if any.
static int SimulatorKeyForVirtualKey(WPARAM virtualKeyCode)
{
	switch (virtualKeyCode)
	{
		case 'B':				return Rtt::SimulatorKey::kB;
		case 'N':				return Rtt::SimulatorKey::kN;
		case 'O':				return Rtt::SimulatorKey::kO;
		case 'Q':				return Rtt::SimulatorKey::kQ;
		case 'R':				return Rtt::SimulatorKey::kR;
		case 'W':				return Rtt::SimulatorKey::kW;

		case VK_LEFT:			return Rtt::SimulatorKey::kLeft;
		case VK_RIGHT:			return Rtt::SimulatorKey::kRight;
		case VK_UP:				return Rtt::SimulatorKey::kUp;
		case VK_DOWN:			return Rtt::SimulatorKey::kDown;

		// Zoom is spelled "Ctrl+Plus" and "Ctrl+Minus" on the menu. The plus is whatever key carries it
		// unshifted, which is the OEM '=' on a US layout, and the keypad pair for anyone using it.
		case VK_OEM_PLUS:
		case VK_ADD:			return Rtt::SimulatorKey::kPlus;

		case VK_OEM_MINUS:
		case VK_SUBTRACT:		return Rtt::SimulatorKey::kMinus;

		default:				return Rtt::SimulatorKey::kNone;
	}
}

bool CSimulatorView::OnRenderSurfaceMessage(void* userdata, UINT messageId, WPARAM wParam, LPARAM lParam)
{
	auto viewPointer = (CSimulatorView*)userdata;
	auto& menuBar = viewPointer->mMenuBar;

	if (!menuBar.IsInitialized())
	{
		return false;
	}

	switch (messageId)
	{
		case WM_MOUSEMOVE:
			return menuBar.OnMouseMove(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));

		case WM_LBUTTONDOWN:
		case WM_LBUTTONDBLCLK:
			return menuBar.OnMouseDown(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));

		case WM_LBUTTONUP:
			return menuBar.OnMouseUp(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));

		case WM_MOUSEWHEEL:
			// Nothing on the bar scrolls, but a wheel event while a menu is open should not reach the content
			// behind it either.
			return menuBar.IsOpen();

		case WM_KILLFOCUS:
			// A menu left open when the window loses focus would still be showing when it comes back, over
			// content that has moved on. Closed, but not consumed: the surface has its own use for this.
			menuBar.Close();
			return false;

		case WM_KEYDOWN:
		{
			// Autorepeat would fire a command per repeat, which for something like Relaunch is not what
			// holding the key down means.
			if (lParam & 0x40000000)
			{
				return false;
			}

			if (VK_ESCAPE == wParam)
			{
				if (menuBar.IsOpen())
				{
					menuBar.Close();
					return true;
				}
				return false;
			}

			Rtt::U32 modifiers = Rtt::SimulatorModifier::kNone;

			// Control is the primary modifier everywhere but macOS, and this host is not macOS.
			if (::GetKeyState(VK_CONTROL) & 0x8000)	modifiers |= Rtt::SimulatorModifier::kPrimary;
			if (::GetKeyState(VK_SHIFT) & 0x8000)	modifiers |= Rtt::SimulatorModifier::kShift;
			if (::GetKeyState(VK_MENU) & 0x8000)	modifiers |= Rtt::SimulatorModifier::kAlt;

			int command = Rtt::CommandForAccelerator(
					viewPointer->mIsShowingInternalScreen, SimulatorKeyForVirtualKey(wParam), modifiers);
			if (Rtt::SimulatorCommand::kNone == command)
			{
				return false;
			}

			menuBar.Close();
			OnMenuBarCommand(viewPointer, command);

			return true;
		}
	}

	return false;
}

#endif // Rtt_USE_BGFX

#pragma endregion
