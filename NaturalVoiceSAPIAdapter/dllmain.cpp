// dllmain.cpp: DllMain 的实现。

#include "pch.h"
#include "framework.h"
#include "resource.h"
#include "NaturalVoiceSAPIAdapter_i.h"
#include "dllmain.h"
#include "TaskScheduler.h"
#include <spdlog/sinks/msvc_sink.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include "RegKey.h"
#include "Logger.h"

CNaturalVoiceSAPIAdapterModule _AtlModule;

TaskScheduler g_taskScheduler;

spdlog::logger logger("");

bool GetCachePath(LPWSTR path) noexcept;

static void InitializeLogger() noexcept
{
	try
	{
		std::vector<spdlog::sink_ptr> sinks;
		sinks.reserve(2);

		// Output to debug console
		logger.sinks().push_back(std::make_shared<spdlog::sinks::msvc_sink_mt>());

		WCHAR logpath[MAX_PATH];
		if (GetCachePath(logpath) && PathAppendW(logpath, L"log.txt"))
		{
			// Log file: max 3 files, 5MB each
			logger.sinks().push_back(std::make_shared<spdlog::sinks::rotating_file_sink_mt>(logpath, 5 << 20, 3));
		}

		RegKey key;
		key.Open(HKEY_CURRENT_USER, L"Software\\NaturalVoiceSAPIAdapter", KEY_QUERY_VALUE);
		DWORD loglevel = std::clamp<DWORD>(key.GetDword(L"LogLevel", spdlog::level::info), 0, 6);
		logger.set_level(static_cast<spdlog::level::level_enum>(loglevel));
		logger.flush_on(spdlog::level::err);
	}
	catch (...)
	{ }
}

static LONG WINAPI LoggingExceptionHandler(PEXCEPTION_POINTERS exinfo) noexcept
{
	auto& exrec = *exinfo->ExceptionRecord;

	std::string msg;
	try
	{
		switch (exrec.ExceptionCode)
		{
		case EXCEPTION_ILLEGAL_INSTRUCTION:
			msg = "Illegal Instruction";
			break;
		case EXCEPTION_PRIV_INSTRUCTION:
			msg = "Privileged Instruction";
			break;
		case EXCEPTION_INT_DIVIDE_BY_ZERO:
			msg = "Integer Divide by Zero";
			break;
		case EXCEPTION_INT_OVERFLOW:
			msg = "Integer Overflow";
			break;
		case EXCEPTION_STACK_OVERFLOW:
			msg = "Stack Overflow";
			break;
		case EXCEPTION_ACCESS_VIOLATION:
			msg = std::format("Access Violation {} address {:p}",
				exrec.ExceptionInformation[0] == 0 ? "reading" : "writing",
				reinterpret_cast<LPCVOID>(exrec.ExceptionInformation[1]));
			break;
		default:
			return EXCEPTION_CONTINUE_SEARCH;
		}
	}
	catch (...)
	{
		return EXCEPTION_CONTINUE_SEARCH;
	}

	// get the module name
	HMODULE hModule = nullptr;
	GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
		reinterpret_cast<LPCWSTR>(exrec.ExceptionAddress),
		&hModule);
	WCHAR modulePath[MAX_PATH] = {};
	GetModuleFileNameW(hModule, modulePath, MAX_PATH);
	LPCWSTR moduleName = wcsrchr(modulePath, L'\\');
	if (moduleName)
		moduleName++;
	else
		moduleName = modulePath;

	LogErr(
		"SEH exception: {} at address {:p} (module offset {:#x}) in module '{}'",
		msg,
		exrec.ExceptionAddress,
		reinterpret_cast<ULONG_PTR>(exrec.ExceptionAddress) - reinterpret_cast<ULONG_PTR>(hModule),
		moduleName
		);

	return EXCEPTION_CONTINUE_SEARCH;
}

// DLL 入口点
extern "C" BOOL WINAPI DllMain(HINSTANCE hInstance, DWORD dwReason, LPVOID lpReserved)
{
	hInstance;

	if (dwReason == DLL_PROCESS_ATTACH)
	{
		InitializeLogger();
		AddVectoredExceptionHandler(0, LoggingExceptionHandler);
	}
	else if (dwReason == DLL_PROCESS_DETACH)
	{
		if (lpReserved == nullptr)  // being unloaded dynamically
		{
			g_taskScheduler.Uninitialize(true);
		}
		else
		{
			g_taskScheduler.Uninitialize(false);
		}
	}

	return _AtlModule.DllMain(dwReason, lpReserved);
}
