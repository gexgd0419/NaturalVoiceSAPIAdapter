// dllmain.cpp: DllMain 的实现。

#include "pch.h"
#include "framework.h"
#include "resource.h"
#include "NaturalVoiceSAPIAdapter_i.h"
#include "dllmain.h"

CNaturalVoiceSAPIAdapterModule _AtlModule;

// Use a timer queue to start background threads
// so that all threads can be cancelled before this DLL is unloaded
// NOTE: Timer queues CANNOT be created in DllMain, otherwise deadlocks would happen on Windows XP
HANDLE g_hTimerQueue = nullptr;

// DLL 入口点
extern "C" BOOL WINAPI DllMain(HINSTANCE hInstance, DWORD dwReason, LPVOID lpReserved)
{
	hInstance;

	if (dwReason == DLL_PROCESS_DETACH)
	{
		if (lpReserved == nullptr)  // being unloaded dynamically
		{
			// Use INVALID_HANDLE_VALUE to wait for callback functions to complete
			(void)DeleteTimerQueueEx(g_hTimerQueue, INVALID_HANDLE_VALUE);
		}
		else
		{
			// Use nullptr to delete without waiting for callbacks
			(void)DeleteTimerQueueEx(g_hTimerQueue, nullptr);
		}
	}

	return _AtlModule.DllMain(dwReason, lpReserved);
}
