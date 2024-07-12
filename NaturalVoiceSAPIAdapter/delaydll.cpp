#include "pch.h"
#include <delayimp.h>
#include <Shlwapi.h>
#include <system_error>

static FARPROC WINAPI delayHook(unsigned dliNotify, PDelayLoadInfo pdli)
{
	if (dliNotify == dliNotePreLoadLibrary)
	{
		// search the library in the same folder as this DLL, not the EXE
		// the default implementation calls LoadLibrary directly which will search in the wrong path
		char path[MAX_PATH];
		if (GetModuleFileNameA((HMODULE)&__ImageBase, path, MAX_PATH) == MAX_PATH
			|| !PathRemoveFileSpecA(path)
			|| !PathAppendA(path, pdli->szDll))
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