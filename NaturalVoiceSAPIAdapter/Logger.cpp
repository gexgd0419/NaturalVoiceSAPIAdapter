#include "pch.h"
#include <minidumpapiset.h>
#pragma comment (lib, "dbghelp.lib")
#include <spdlog/sinks/msvc_sink.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include "RegKey.h"
#include "Logger.h"
#include "TaskScheduler.h"
#include "StrUtils.h"

spdlog::logger logger("");
static bool s_consoleCreated = false, s_crashDumpEnabled = false;
MINIDUMP_TYPE s_crashDumpType = MiniDumpNormal;
LPTOP_LEVEL_EXCEPTION_FILTER s_oldUnhandledExceptionFilter = nullptr;
bool g_websocketppLogsEnabled = false;

class console_sink : public spdlog::sinks::base_sink<std::mutex>
{
	HANDLE hConsoleOut;
public:
	explicit console_sink(HANDLE hConsoleOut) noexcept : hConsoleOut(hConsoleOut) {}
protected:
	void sink_it_(const spdlog::details::log_msg& msg) override
	{
		spdlog::memory_buf_t buf;
		formatter_->format(msg, buf);
		auto wstr = UTF8ToWString(buf);
		WriteConsoleW(hConsoleOut, wstr.data(), (DWORD)wstr.size(), nullptr, nullptr);
	}
	void flush_() override {}
};

extern TaskScheduler g_taskScheduler;
bool GetCachePath(LPWSTR path) noexcept;

static BOOL WINAPI MiniDumpCallback(
	_Inout_ PVOID CallbackParam,
	_In_    PMINIDUMP_CALLBACK_INPUT CallbackInput,
	_Inout_ PMINIDUMP_CALLBACK_OUTPUT CallbackOutput)
{
	CallbackParam;
	switch (CallbackInput->CallbackType)
	{
	case MemoryCallback:
		return FALSE;  // No additional memory included

	case CancelCallback:
		CallbackOutput->CheckCancel = FALSE;
		CallbackOutput->Cancel = FALSE;      // Does not allow cancelling
		return TRUE;

	case IncludeModuleCallback:
	case IncludeThreadCallback:
	case ThreadCallback:
		return TRUE;  // Include everything

	case ModuleCallback:
		// If the module is this DLL, keep everything
		if ((ULONG_PTR)CallbackInput->Module.BaseOfImage == (ULONG_PTR)&__ImageBase)
			return TRUE;

		// For modules other than this DLL, exclude the data segment
		CallbackOutput->ModuleWriteFlags &= ~ModuleWriteDataSeg;

		return TRUE;

	default:
		return FALSE;
	}
}

static LONG WINAPI MyUnhandledExceptionFilter(PEXCEPTION_POINTERS exinfo) noexcept
{
	try
	{
		auto& exrec = *exinfo->ExceptionRecord;

		std::string msg;
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
			msg = std::format("Code: {:#x}", exrec.ExceptionCode);
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

		LogCritical(
			"Unhandled SEH exception: {} at address {:p} (module offset {:#x}) in module '{}'",
			msg,
			exrec.ExceptionAddress,
			reinterpret_cast<ULONG_PTR>(exrec.ExceptionAddress) - reinterpret_cast<ULONG_PTR>(hModule),
			moduleName
		);

		// Write dump
		WCHAR dumppath[MAX_PATH];
		if (GetCachePath(dumppath) && PathAppendW(dumppath, L"minidump.dmp"))
		{
			MINIDUMP_EXCEPTION_INFORMATION dumpinfo = {
				.ThreadId = GetCurrentThreadId(),
				.ExceptionPointers = exinfo,
				.ClientPointers = FALSE
			};
			MINIDUMP_CALLBACK_INFORMATION callbackinfo = {
				.CallbackRoutine = MiniDumpCallback,
				.CallbackParam = nullptr
			};
			HANDLE hFile = CreateFileW(dumppath, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, nullptr,
				CREATE_ALWAYS, 0, nullptr);
			if (hFile != INVALID_HANDLE_VALUE)
			{
				if (MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), hFile,
					s_crashDumpType,
					&dumpinfo, nullptr, &callbackinfo))
				{
					LogCritical("Dump file saved");
				}
				else
				{
					LogCritical("Cannot write mini dump: {}",
						AnsiToUTF8(std::system_category().message(GetLastError())));
				}
				CloseHandle(hFile);
			}
			else
			{
				LogCritical("Cannot create dump file: {}",
					AnsiToUTF8(std::system_category().message(GetLastError())));
			}
		}
	}
	catch (...)
	{
	}

	return s_oldUnhandledExceptionFilter ? s_oldUnhandledExceptionFilter(exinfo) : EXCEPTION_CONTINUE_SEARCH;
}

void InitializeLogger() noexcept
{
	try
	{
		// Output to debug console
		logger.sinks().push_back(std::make_shared<spdlog::sinks::msvc_sink_mt>());

		RegKey key = RegOpenConfigKey();

		auto userLogPath = key.GetString(L"LogFile");

		if (userLogPath.empty())
		{
			// Default log path: %LOCALAPPDATA\NaturalVoiceSAPIAdapter\log.#.txt
			WCHAR logpath[MAX_PATH];
			if (GetCachePath(logpath) && PathAppendW(logpath, L"log.txt"))
			{
				// Log file: max 3 files, 5MB each
				logger.sinks().push_back(std::make_shared<spdlog::sinks::rotating_file_sink_mt>(logpath, 5 << 20, 3));
			}
		}
		else
		{
			logger.sinks().push_back(std::make_shared<spdlog::sinks::basic_file_sink_mt>(userLogPath));
		}

		DWORD loglevel = std::clamp<DWORD>(key.GetDword(L"LogLevel", spdlog::level::info), 0, 6);
		logger.set_level(static_cast<spdlog::level::level_enum>(loglevel));
		loglevel = std::clamp<DWORD>(key.GetDword(L"LogFlushLevel", spdlog::level::warn), 0, 6);
		logger.flush_on(static_cast<spdlog::level::level_enum>(loglevel));

		DWORD flushInterval = key.GetDword(L"LogFlushInterval");
		if (flushInterval != 0)
			g_taskScheduler.StartNewTask(0, flushInterval, []() { logger.flush(); });

		// websocketpp logs are disabled by default
		// because most errors can be reported by exceptions and to our main log
		g_websocketppLogsEnabled = key.GetDword(L"EnableWebsocketppLogs") != 0;

		if (key.GetDword(L"LogToConsole"))
		{
			if (AllocConsole())
			{
				s_consoleCreated = true;
				SetConsoleTitleW(L"NaturalVoiceSAPIAdapter debug console");
				logger.sinks().push_back(std::make_shared<console_sink>(GetStdHandle(STD_OUTPUT_HANDLE)));
			}
		}

		if (key.GetDword(L"EnableCrashDump"))
		{
			s_crashDumpEnabled = true;
			s_oldUnhandledExceptionFilter = SetUnhandledExceptionFilter(MyUnhandledExceptionFilter);
			s_crashDumpType = (MINIDUMP_TYPE)(key.GetDword(L"CrashDumpType",
				MiniDumpWithIndirectlyReferencedMemory
				| MiniDumpWithDataSegs
				| MiniDumpFilterModulePaths) & MiniDumpValidTypeFlags);
		}
	}
	catch (...)
	{
	}
}

void UninitializeLogger() noexcept
{
	if (s_crashDumpEnabled)
	{
		SetUnhandledExceptionFilter(s_oldUnhandledExceptionFilter);
	}
	if (s_consoleCreated)
	{
		FreeConsole();
	}
}