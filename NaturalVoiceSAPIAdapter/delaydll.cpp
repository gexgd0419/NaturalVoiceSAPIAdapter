#include "pch.h"
#include "delaydll.h"
#include <delayimp.h>
#include <Shlwapi.h>

static FARPROC WINAPI delayHook(unsigned dliNotify, PDelayLoadInfo pdli)
{
	if (dliNotify == dliNotePreLoadLibrary)
	{
		char path[MAX_PATH];
		GetModuleFileNameA((HMODULE)&__ImageBase, path, MAX_PATH);
		PathRemoveFileSpecA(path);
		PathAppendA(path, pdli->szDll);
		HMODULE hDll = LoadLibraryExA(path, nullptr, 0);
		if (!hDll) throw DllDelayLoadError();
	}
	else if (dliNotify == dliFailGetProc)
	{
		// throw a C++ exception to prevent the helper function from throwing a structured exception
		// by default structured exception will not be handled by C++ try/catch
		throw DllDelayLoadError(pdli->dwLastError);
	}
	return nullptr;
}

const PfnDliHook __pfnDliNotifyHook2 = delayHook;
const PfnDliHook __pfnDliFailureHook2 = delayHook;