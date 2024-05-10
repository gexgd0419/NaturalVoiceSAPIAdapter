// dllmain.cpp: DllMain 的实现。

#include "pch.h"
#include "framework.h"
#include "resource.h"
#include "NaturalVoiceSAPIAdapter_i.h"
#include "dllmain.h"

CNaturalVoiceSAPIAdapterModule _AtlModule;

// Use a timer queue to start background threads
// so that all threads can be cancelled before this DLL is unloaded
HANDLE g_hTimerQueue = CreateTimerQueue();

// DLL 入口点
extern "C" BOOL WINAPI DllMain(HINSTANCE hInstance, DWORD dwReason, LPVOID lpReserved)
{
	hInstance;

	if (dwReason == DLL_PROCESS_DETACH)
	{
		// Use INVALID_HANDLE_VALUE to wait for callback functions to complete
		(void)DeleteTimerQueueEx(g_hTimerQueue, INVALID_HANDLE_VALUE);
	}

	return _AtlModule.DllMain(dwReason, lpReserved);
}
