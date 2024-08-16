#include "pch.h"
#include <delayimp.h>
#include <Shlwapi.h>
#include <system_error>
#include "wrappers.h"

static FARPROC WINAPI delayHook(unsigned dliNotify, PDelayLoadInfo pdli)
{
	if (dliNotify == dliNotePreLoadLibrary)
	{
		// search the library in the same folder as this DLL, not the EXE
		// the default implementation calls LoadLibrary directly which will search in the wrong path
		char path[MAX_PATH];
		DWORD len = GetModuleFileNameA((HMODULE)&__ImageBase, path, MAX_PATH);
		if (len == 0 || len == MAX_PATH)
			throw std::system_error(len == MAX_PATH ? ERROR_FILENAME_EXCED_RANGE : GetLastError(), std::system_category());
		PathRemoveFileSpecA(path);

		// load DLLs required by Speech SDK first to avoid "module not found"
		// system will use the already loaded DLL with the same name
		static constexpr const char* RequiredDLLs[] = { "SpeechSDKShim.dll", "ucrtbase.dll",
			"msvcp140.dll", "msvcp140_codecvt_ids.dll", "vcruntime140.dll"
#ifdef _WIN64
			, "vcruntime140_1.dll"
#endif
		};
		constexpr size_t DLLCount = sizeof RequiredDLLs / sizeof RequiredDLLs[0];
		HandleWrapper<HMODULE, FreeLibrary> hRequiredDLLs[DLLCount];  // wrappers to free the DLLs

		for (size_t i = 0; i < DLLCount; i++)
		{
			if (GetModuleHandleA(RequiredDLLs[i]) == nullptr)  // if not loaded
			{
				if (!PathAppendA(path, RequiredDLLs[i]))
					throw std::system_error(ERROR_FILENAME_EXCED_RANGE, std::system_category());
				hRequiredDLLs[i] = LoadLibraryA(path);
				PathRemoveFileSpecA(path);
			}
		}

		if (!PathAppendA(path, pdli->szDll))
			throw std::system_error(ERROR_FILENAME_EXCED_RANGE, std::system_category());
		HMODULE hDll = LoadLibraryA(path);
		if (!hDll) throw std::system_error(GetLastError(), std::system_category());
	}
	else if (dliNotify == dliFailGetProc)
	{
		// throw a C++ exception to prevent the helper function from throwing a structured exception
		// by default structured exception will not be handled by C++ try/catch
		throw std::system_error(pdli->dwLastError, std::system_category());
	}
	return nullptr;
}

const PfnDliHook __pfnDliNotifyHook2 = delayHook;
const PfnDliHook __pfnDliFailureHook2 = delayHook;