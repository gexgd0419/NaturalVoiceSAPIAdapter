// dllmain.cpp: DllMain 的实现。

#include "pch.h"
#include "framework.h"
#include "resource.h"
#include "NaturalVoiceSAPIAdapter_i.h"
#include "dllmain.h"
#include "TaskScheduler.h"
#include "WSConnectionPool.h"

CNaturalVoiceSAPIAdapterModule _AtlModule;

TaskScheduler g_taskScheduler;
extern std::unique_ptr<WSConnectionPool> g_pConnectionPool;

void InitializeLogger() noexcept;
void UninitializeLogger() noexcept;

// DLL 入口点
extern "C" BOOL WINAPI DllMain(HINSTANCE hInstance, DWORD dwReason, LPVOID lpReserved)
{
	hInstance;

	if (dwReason == DLL_PROCESS_ATTACH)
	{
		InitializeLogger();
	}
	else if (dwReason == DLL_PROCESS_DETACH)
	{
		UninitializeLogger();
		if (lpReserved == nullptr)  // being unloaded dynamically
		{
			g_taskScheduler.Uninitialize(true);
		}
		else
		{
			// skip its destruction, or ASIO would deadlock
			// https://github.com/chriskohlhoff/asio/issues/869
			g_pConnectionPool.release();

			g_taskScheduler.Uninitialize(false);
		}
	}

	return _AtlModule.DllMain(dwReason, lpReserved);
}
