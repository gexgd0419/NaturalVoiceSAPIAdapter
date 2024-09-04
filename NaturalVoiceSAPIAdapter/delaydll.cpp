#include "pch.h"
#include <delayimp.h>
#include <Shlwapi.h>
#include <system_error>
#include "wrappers.h"

// Call this function before using the Azure Speech SDK.
// Returns Win32 error code on failure.
// As we aren't handling structured exceptions most of the time,
// we can load all the import items at once, and centralize error handling in one place.
LSTATUS TryLoadAzureSpeechSDK()
{
	static bool loaded = false;
	if (loaded)
		return ERROR_SUCCESS;
	__try
	{
		HRESULT hr = __HrLoadAllImportsForDll("Microsoft.CognitiveServices.Speech.core.dll");
		if (FAILED(hr))
			return HRESULT_CODE(hr);
		loaded = true;
		return ERROR_SUCCESS;
	}
	__except (HRESULT_FACILITY(GetExceptionCode()) == FACILITY_WIN32
		? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH)
	{
		return HRESULT_CODE(GetExceptionCode());
	}
}

static LSTATUS DelayLoadLibrary(LPCSTR szDll, HMODULE& hDll)
{
	// search the library in the same folder as this DLL, not the EXE
	// the default implementation calls LoadLibrary directly which will search in the wrong path
	char path[MAX_PATH];
	DWORD len = GetModuleFileNameA((HMODULE)&__ImageBase, path, MAX_PATH);
	if (len == MAX_PATH)
		return ERROR_FILENAME_EXCED_RANGE;
	else if (len == 0)
		return GetLastError();
	PathRemoveFileSpecA(path);

	// load DLLs required by Speech SDK first to avoid "module not found"
	// system will use the already loaded DLL with the same name
	static constexpr const char* RequiredDLLs[] = {
		"SpeechSDKShim.dll",
		"ucrtbase.dll",
		"vcruntime140.dll",
#ifdef _WIN64
		"vcruntime140_1.dll",
#endif
		"msvcp140.dll",  // depends on vcruntime140
		"msvcp140_codecvt_ids.dll"  // depends on vcruntime140
	};
	constexpr size_t DLLCount = sizeof RequiredDLLs / sizeof RequiredDLLs[0];
	HandleWrapper<HMODULE, FreeLibrary> hRequiredDLLs[DLLCount];  // wrappers to free the DLLs

	for (size_t i = 0; i < DLLCount; i++)
	{
		if (GetModuleHandleA(RequiredDLLs[i]) == nullptr)  // if not loaded
		{
			if (!PathAppendA(path, RequiredDLLs[i]))
				return ERROR_FILENAME_EXCED_RANGE;
			hRequiredDLLs[i] = LoadLibraryA(path);
			PathRemoveFileSpecA(path);
		}
	}

	if (!PathAppendA(path, szDll))
		return ERROR_FILENAME_EXCED_RANGE;
	hDll = LoadLibraryA(path);
	if (!hDll)
		return GetLastError();

	return ERROR_SUCCESS;
}

static FARPROC WINAPI delayHook(unsigned dliNotify, PDelayLoadInfo pdli)
{
	// The delay hook function can go through some extern C functions,
	// which can make throwing C++ exceptions here undefined behavior,
	// because by default the compiler assumes
	// no C++ exceptions will be thrown from extern C functions.
	// So we just revert to throwing structured exceptions here.

	if (dliNotify == dliNotePreLoadLibrary)
	{
		HMODULE hDll = nullptr;
		// Put the loading code into a separate function,
		// so that every C++ objects can be destroyed properly
		// before an exception gets thrown.
		LSTATUS stat = DelayLoadLibrary(pdli->szDll, hDll);
		if (stat != ERROR_SUCCESS)
			RaiseException(HRESULT_FROM_WIN32(stat), 0, 0, nullptr);
		return (FARPROC)hDll;
	}
	else if (dliNotify == dliFailGetProc)
	{
		RaiseException(HRESULT_FROM_WIN32(pdli->dwLastError), 0, 0, nullptr);
	}
	return nullptr;
}

const PfnDliHook __pfnDliNotifyHook2 = delayHook;
const PfnDliHook __pfnDliFailureHook2 = delayHook;