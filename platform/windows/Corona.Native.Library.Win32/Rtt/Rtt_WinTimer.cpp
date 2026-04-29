//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Corona game engine.
// For overview and more information on licensing please refer to README.md 
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "Rtt_WinTimer.h"
#include <windows.h>
#include <mmsystem.h>


namespace Rtt
{

namespace
{
	const wchar_t kHighResolutionTimerWindowClassName[] = L"RttWinTimerHighResolutionMessageWindow";
	const UINT kHighResolutionTimerMessage = WM_APP + 0x2643;
}

std::unordered_map<UINT_PTR, Rtt::WinTimer *> WinTimer::sTimerMap;
UINT_PTR WinTimer::sMostRecentTimerID;
std::mutex WinTimer::sTimerMutex;

#pragma region Constructors/Destructors
WinTimer::WinTimer(MCallback& callback, HWND windowHandle)
:	PlatformTimer(callback)
{
	fWindowHandle = windowHandle;
	fTimerPointer = 0;
	fIntervalInMilliseconds = 10;
	fHighResolutionTimerID = 0;
	fHighResolutionMessageWindowHandle = NULL;
	fHighResolutionTimerMessagePending = 0;
	fNextIntervalTimeInTicks = 0;
}

WinTimer::~WinTimer()
{
	Stop();
}

#pragma endregion


#pragma region Public Methods
void WinTimer::Start()
{
	// Do not continue if the timer is already running.
	if (IsRunning())
	{
		return;
	}

	// Start the timer, but not slower than the configured interval.
	// We do this because Windows timers can invoke later than expected.
	// To compensate, we'll schedule when to invoke the timer's callback using "fIntervalEndTimeInTicks".
	fNextIntervalTimeInTicks = (S32)::timeGetTime() + (S32)fIntervalInMilliseconds;
	fTimerID = ++sMostRecentTimerID; // ID should be non-0, so pre-increment for first time
	U32 timerInterval = Min(fIntervalInMilliseconds, 10U);
	timerInterval = Max(timerInterval, 1U);
	{
		std::lock_guard<std::mutex> scopedLock(sTimerMutex);
		sTimerMap[fTimerID] = this;
	}

	if (fIntervalInMilliseconds < 16U)
	{
		fHighResolutionMessageWindowHandle = WinTimer::CreateHighResolutionTimerWindow();
		if (fHighResolutionMessageWindowHandle)
		{
			::timeBeginPeriod(1);
			fHighResolutionTimerID = ::timeSetEvent(timerInterval, 1, WinTimer::OnHighResolutionTimerElapsed, (DWORD_PTR)fTimerID, TIME_PERIODIC | TIME_CALLBACK_FUNCTION);
			if (!fHighResolutionTimerID)
			{
				::timeEndPeriod(1);
				::DestroyWindow(fHighResolutionMessageWindowHandle);
				fHighResolutionMessageWindowHandle = NULL;
			}
		}
	}

	if (!fHighResolutionTimerID)
	{
		fTimerPointer = ::SetTimer(fWindowHandle, fTimerID, timerInterval, WinTimer::OnTimerElapsed);
		if (!IsRunning())
		{
			std::lock_guard<std::mutex> scopedLock(sTimerMutex);
			sTimerMap.erase(fTimerID);
			fTimerID = 0;
		}
	}
}

void WinTimer::Stop()
{
	// Do not continue if the timer has already been stopped.
	if (IsRunning() == false)
	{
		return;
	}

	UINT_PTR timerID = fTimerID;
	MMRESULT highResolutionTimerID = fHighResolutionTimerID;
	HWND highResolutionMessageWindowHandle = fHighResolutionMessageWindowHandle;
	UINT_PTR timerPointer = fTimerPointer;

	{
		std::lock_guard<std::mutex> scopedLock(sTimerMutex);
		sTimerMap.erase(timerID);
	}

	if (highResolutionTimerID)
	{
		::timeKillEvent(highResolutionTimerID);
		::timeEndPeriod(1);
	}
	else if (timerPointer)
	{
		::KillTimer(fWindowHandle, timerID);
	}
	if (highResolutionMessageWindowHandle)
	{
		::DestroyWindow(highResolutionMessageWindowHandle);
	}

	fHighResolutionTimerID = 0;
	fHighResolutionMessageWindowHandle = NULL;
	fHighResolutionTimerMessagePending = 0;
	fTimerPointer = 0;
	fTimerID = 0;
}

void WinTimer::SetInterval(U32 milliseconds)
{
	fIntervalInMilliseconds = Max(milliseconds, 1U);
}

bool WinTimer::IsRunning() const
{
	return (fTimerPointer != 0) || (fHighResolutionTimerID != 0);
}

void WinTimer::Evaluate()
{
	// Do not continue if the timer is not running.
	if (IsRunning() == false)
	{
		return;
	}

	// Do not continue if we haven't reached the scheduled time yet.
	if (CompareTicks((S32)::timeGetTime(), fNextIntervalTimeInTicks) < 0)
	{
		return;
	}

	// Schedule the next interval time.
	for (; CompareTicks((S32)::timeGetTime(), fNextIntervalTimeInTicks) > 0; fNextIntervalTimeInTicks += fIntervalInMilliseconds);

	// Invoke this timer's callback.
	this->operator()();
}

#pragma endregion


#pragma region Private Methods/Functions
VOID CALLBACK WinTimer::OnTimerElapsed(HWND hwnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime)
{
	WinTimer *timerPointer = NULL;
	{
		std::lock_guard<std::mutex> scopedLock(sTimerMutex);
		auto timer = sTimerMap.find(idEvent);
		if (sTimerMap.end() != timer)
		{
			timerPointer = timer->second;
			::InterlockedExchange(&timerPointer->fHighResolutionTimerMessagePending, 0);
		}
	}

	if (timerPointer)
	{
		timerPointer->Evaluate();
	}
}

void CALLBACK WinTimer::OnHighResolutionTimerElapsed(UINT timerId, UINT messageId, DWORD_PTR userData, DWORD_PTR param1, DWORD_PTR param2)
{
	HWND windowHandle = NULL;
	bool shouldPostMessage = false;
	UINT_PTR timerID = (UINT_PTR)userData;

	{
		std::lock_guard<std::mutex> scopedLock(sTimerMutex);
		auto timer = sTimerMap.find(timerID);
		if (sTimerMap.end() != timer)
		{
			windowHandle = timer->second->fHighResolutionMessageWindowHandle;
			shouldPostMessage = (0 == ::InterlockedCompareExchange(&timer->second->fHighResolutionTimerMessagePending, 1, 0));
		}
	}

	if (windowHandle && shouldPostMessage)
	{
		::PostMessage(windowHandle, kHighResolutionTimerMessage, timerID, 0);
	}
}

LRESULT CALLBACK WinTimer::OnHighResolutionTimerMessage(HWND hwnd, UINT messageId, WPARAM wParam, LPARAM lParam)
{
	if (kHighResolutionTimerMessage == messageId)
	{
		WinTimer::OnTimerElapsed(hwnd, WM_TIMER, (UINT_PTR)wParam, (DWORD)::timeGetTime());
		return 0;
	}
	return ::DefWindowProc(hwnd, messageId, wParam, lParam);
}

HWND WinTimer::CreateHighResolutionTimerWindow()
{
	HINSTANCE moduleInstance = ::GetModuleHandle(NULL);
	WNDCLASSW windowClass = {};
	windowClass.lpfnWndProc = WinTimer::OnHighResolutionTimerMessage;
	windowClass.hInstance = moduleInstance;
	windowClass.lpszClassName = kHighResolutionTimerWindowClassName;
	if (!::RegisterClassW(&windowClass) && (ERROR_CLASS_ALREADY_EXISTS != ::GetLastError()))
	{
		return NULL;
	}

	return ::CreateWindowExW(
		0, kHighResolutionTimerWindowClassName, L"", 0,
		0, 0, 0, 0,
		HWND_MESSAGE, NULL, moduleInstance, NULL);
}

S32 WinTimer::CompareTicks(S32 x, S32 y)
{
	// Overflow will occur when flipping sign bit of largest negative number.
	// Give it a one millisecond boost before flipping the sign.
	if (0x80000000 == y)
	{
		y++;
	}

	// Compare the given tick values via subtraction. Overlow for this subtraction operation is okay.
	S32 deltaTime = x - y;
	if (deltaTime < 0)
	{
		return -1;
	}
	else if (0 == deltaTime)
	{
		return 0;
	}
	return 1;
}

#pragma endregion

}	// namespace Rtt
