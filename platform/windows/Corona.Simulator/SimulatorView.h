//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Corona game engine.
// For overview and more information on licensing please refer to README.md 
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////

#pragma once

#include "Core\Rtt_Build.h"
#include "Core\Rtt_Array.h"
#include "Interop\ScopedComInitializer.h"
#include "Interop\SimulatorRuntimeEnvironment.h"
#include "Rtt_PlatformSimulator.h"
#include "Rtt_TargetDevice.h"
#include "Rtt_WinSimulatorServices.h"
#include "UI\Rtt_SimulatorMenus.h"
#if defined( Rtt_USE_BGFX )
#	include "UI\Rtt_MenuBar.h"
#endif


#pragma region Forward Declarations
namespace Rtt
{
	struct RecentProjectInfo;
}
namespace Gdiplus
{
	class Bitmap;
}
class CMessageDlg;
class CProgressWnd;
class CSimulatorDoc;

#pragma endregion


class CSimulatorView : public CView
{
	protected:
		CSimulatorView();
		DECLARE_DYNCREATE(CSimulatorView)

	public:
		virtual ~CSimulatorView();

		virtual void OnInitialUpdate();
		virtual BOOL PreCreateWindow(CREATESTRUCT& cs);
#ifdef _DEBUG
		virtual void AssertValid() const;
		virtual void Dump(CDumpContext& dc) const;
#endif
		void StartSimulation();
		void RestartSimulation();
		void SuspendResumeSimulationWithOverlay(bool showOverlay, bool sendSystemEvents);
		void StopSimulation();
		bool IsShowingInternalScreen() { return mIsShowingInternalScreen; }
		bool IsSimulationSuspended() const;
		CSimulatorDoc* GetDocument() const;
		CString GetDisplayName()   { return mDisplayName; }
		const Rtt::PlatformSimulator::Config& GetDeviceConfig() { return mDeviceConfig; }
		int GetRotation() { return mRotation; }
		Gdiplus::Bitmap *GetSkinBitmap() { return mpSkinBitmap; }
		void UpdateSimulatorSkin();  // zoom functions in CMainFrame call this
		Interop::SimulatorRuntimeEnvironment* GetRuntimeEnvironment()  { return mRuntimeEnvironmentPointer; }
		void GetRecentDocs(Rtt::LightPtrArray<Rtt::RecentProjectInfo> *list);
		void PostOpenWithPath(CString fileName);

		/// <summary>
		///  <para>
		///   Determines if all plugins for the currently running project have been successfully downloaded and acquired.
		///  </para>
		///  <para>Will display a message box if at least 1 plugin the project depends on was not found</para>
		///  <para>Intended to be called by the Win32 build dialog if at least 1 plugin was missing.</para>
		/// </summary>
		/// <returns>
		///  <para>
		///   Returns true if all plugins for the currently running project have been acquired
		///   or if the project the does not depend on any plugins.
		///  </para>
		///  <para>Returns false if at least 1 plugin the projct depends on was not found.</para>
		/// </returns>
		bool VerifyAllPluginsAcquired();

		/// <summary>
		///  <para>Custom control used by CSimulatorView to render Corona's content.</para>
		///  <para>This container is a CStatic (ie: label control) that can be used to display a suspended message.</para>
		///  <para>
		///   This container's child control, accessed via GetCoronaControl(), is expected to be used by Corona
		///   to render content to.
		///  </para>
		///  <para>
		///   This child Corona control is expected to be hidden in order to reveal this container's suspended message.
		///  </para>
		/// </summary>
		class CCoronaControlContainer : public CStatic
		{
			public:
				/// <summary>Fetches the container's child Corona control used to render Corona's content.</summary>
				/// <returns>Returns a reference to this contain's child Corona control.</returns>
				CWnd& GetCoronaControl();

			protected:
				DECLARE_MESSAGE_MAP()
				afx_msg int OnCreate(LPCREATESTRUCT lpCreateStruct);
				afx_msg HBRUSH CtlColor(CDC* pDC, UINT nCtlColor);
				afx_msg void OnSize(UINT nType, int cx, int cy);

			private:
				CButton mCoronaControl;
		};

	protected:
		virtual void OnDraw(CDC* pDC);
		virtual void OnUpdate(CView* pSender, LPARAM lHint, CObject *pHint);
		virtual void OnTimer(UINT nIDEvent);

		DECLARE_MESSAGE_MAP()
		afx_msg int OnCreate(LPCREATESTRUCT lpCreateStruct);
		afx_msg void OnDestroy();
		afx_msg void OnSetFocus(CWnd* pOldWnd);
		afx_msg BOOL OnEraseBkgnd(CDC* pDC);
		afx_msg void OnAppAbout();
		afx_msg void OnHelp();
		afx_msg void OnPreferences();
		afx_msg void OnViewHomeScreen();
		afx_msg void OnViewConsole();
		afx_msg void OnViewShake();
		afx_msg void OnViewSuspend();
		afx_msg void OnViewRotateLeft();
		afx_msg void OnViewRotateRight();
		afx_msg void OnViewNavigateBack();
		afx_msg void OnFileMRU1();
		afx_msg void OnFileNew();
		afx_msg void OnFileOpen();
		afx_msg void OnFileOpenSampleProject();
		afx_msg void OnBuildForAndroid();
		afx_msg void OnBuildForWeb();
		afx_msg void OnBuildForLinux();
		afx_msg void OnBuildForNxS();
		afx_msg void OnBuildForWin32();
		afx_msg void OnFileOpenInEditor();
		afx_msg void OnFileRelaunch();
		afx_msg void OnShowProjectFiles();
		afx_msg void OnShowProjectSandbox();
		afx_msg void OnClearProjectSandbox();
		afx_msg void OnFileClose();
		afx_msg void OnClose();
		afx_msg void OnUpdateViewSuspend(CCmdUI *pCmdUI);
		afx_msg void OnWindowViewAs( UINT nID );
		afx_msg void OnUpdateWindowViewAs( CCmdUI *pCmdUI );
		afx_msg void OnUpdateViewNavigateBack(CCmdUI *pCmdUI);
		afx_msg void OnUpdateFileRelaunch(CCmdUI *pCmdUI);
		afx_msg void OnUpdateFileClose(CCmdUI *pCmdUI);
		afx_msg void OnUpdateShowProjectFiles(CCmdUI *pCmdUI);
		afx_msg void OnUpdateShowProjectSandbox(CCmdUI *pCmdUI);
		afx_msg void OnUpdateBuildMenuItem(CCmdUI *pCmdUI);
		afx_msg void OnUpdateFileOpenInEditor(CCmdUI *pCmdUI);
		afx_msg LRESULT OnActivityIndicator(WPARAM wParam, LPARAM lParam);
		afx_msg LRESULT OnNativeAlert(WPARAM wParam, LPARAM lParam);

	private:
		void RunCoronaProject();
		void RunCoronaProject(CString &projectPath);
		void ScaleRect(CRect& rect, float scale);
		Rtt::TargetDevice::Skin SkinIDFromMenuID(UINT nID);
		int GetAnalyticsSkinId();
		bool InitSkin(Rtt::TargetDevice::Skin skinId);
		bool InitializeSimulation(Rtt::TargetDevice::Skin skinId);
		void SetRotation(int rotation);
		bool ValidateOpenGL();
		bool HasApplicationChanged();
		bool LoadSkinResources();
		void GetFilePaths(LPCTSTR pattern, CStringArray& filepaths);
		void OnRuntimeLoaded(Interop::RuntimeEnvironment& sender, const Interop::EventArgs& arguments);

			/// <summary>How much of the top of the render surface the menu bar takes.</summary>
			/// <remarks>
			///  <para>
			///   The bar is drawn by bgfx into the same surface Corona renders into, because bgfx renders into
			///   one window. So that surface is taller than the device screen, and everything measured against
			///   the screen -- the window size, the skin's placement, where a click landed -- has to take this
			///   off first.
			///  </para>
			///  <para>Zero in a build without bgfx, which has no bar to make room for.</para>
			/// </remarks>
			static int GetMenuBarHeight();

#if defined( Rtt_USE_BGFX )
			/// <summary>Brings the menu bar up, once there is a bgfx renderer to draw it with.</summary>
			/// <remarks>
			///  Called after the runtime has loaded, since the renderer does not exist before that. Does nothing
			///  if the bar is already up, or has already failed to come up.
			/// </remarks>
			void EnsureMenuBar();

			/// <summary>Fills the bar in for whatever is loaded now.</summary>
			/// <remarks>
			///  Called again whenever that changes -- between the welcome screen and a project, and when
			///  suspending flips the label on one of the items.
			/// </remarks>
			void UpdateMenuBar();

			/// <summary>Draws the bar over the finished frame. Registered with the bgfx renderer.</summary>
			static void RenderMenuBarOverlay(void* userdata, Rtt::U16 view, Rtt::U32 width, Rtt::U32 height);

			/// <summary>Lets the bar go before bgfx does, which is every time the runtime is torn down.</summary>
			static void ReleaseMenuBarOverlay(void* userdata);

			/// <summary>Runs the command a menu item or an accelerator names.</summary>
			static void OnMenuBarCommand(void* userdata, int command);

			/// <summary>Gives the bar first refusal on the render surface's Windows messages.</summary>
			/// <returns>Returns true if the bar took the message, in which case Corona must not also see it.</returns>
			static bool OnRenderSurfaceMessage(void* userdata, UINT messageId, WPARAM wParam, LPARAM lParam);

			/// <summary>The Windows command id that carries out a SimulatorCommand, or zero for none.</summary>
			static UINT CommandIdFor(int command);

			Rtt::MenuBar mMenuBar;

			/// <summary>Set once bringing the bar up has failed, so it is not attempted again.</summary>
			bool mMenuBarFailed;
#endif


		CCoronaControlContainer mCoronaContainerControl;
		Interop::SimulatorRuntimeEnvironment* mRuntimeEnvironmentPointer;
		Interop::SimulatorRuntimeEnvironment::LoadedEvent::MethodHandler<CSimulatorView> mRuntimeLoadedEventHandler;
		Interop::ScopedComInitializer mScopedComInitializer;
		Rtt::WinSimulatorServices mSimulatorServices;
		CMessageDlg* mMessageDlgPointer;
		CProgressWnd* mProgressDlgPointer;
		CString mDisplayName;
		int mRotation;  // 0, 90, 180, 270
		Gdiplus::Bitmap *mpSkinBitmap;
		Rtt::PlatformSimulator::Config mDeviceConfig;
		bool mIsShowingInternalScreen;	// Set true when showing the Home Screen or Demo Screen.
		HANDLE mAppChangeHandle;		// Filesystem notifications handle
		int mShakeAmount;
		int mShakeReps;
		int mShakeNum;
		CRect mShakeOriginRect;
		Rtt::TargetDevice::Skin m_nSkinId;
		CString mCustomSkinFileName;
		CString mSystemSkinsDir;
		CString mUserSkinsDir;
		int mRelaunchCount;
};

#ifndef _DEBUG  // debug version in SimulatorView.cpp
inline CSimulatorDoc* CSimulatorView::GetDocument() const
   { return reinterpret_cast<CSimulatorDoc*>(m_pDocument); }
#endif
