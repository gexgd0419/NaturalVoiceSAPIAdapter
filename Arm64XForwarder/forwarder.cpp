#include "framework.h"
#include <Shlwapi.h>
#pragma comment (lib, "shlwapi.lib")
#include <atomic>
#include <system_error>
#include <mutex>

extern HMODULE hThisDll, hTargetDll;
static std::once_flag flag;

static void LoadTargetModule()
{
	WCHAR path[MAX_PATH];
	DWORD len = GetModuleFileNameW(hThisDll, path, MAX_PATH);
	if (len == 0)
		throw std::system_error(GetLastError(), std::system_category());
	else if (len == MAX_PATH)
		throw std::system_error(ERROR_FILENAME_EXCED_RANGE, std::system_category());

	PathRemoveFileSpecW(path);
#ifdef _M_X64  // forward to X64
	PathRemoveFileSpecW(path);
	if (!PathAppendW(path, L"x64\\NaturalVoiceSAPIAdapter.dll"))
#else  // forward to ARM64
	if (!PathAppendW(path, L"NaturalVoiceSAPIAdapter.dll"))
#endif
	{
		throw std::system_error(ERROR_FILENAME_EXCED_RANGE, std::system_category());
	}

	hTargetDll = LoadLibraryW(path);
	if (!hTargetDll)
		throw std::system_error(GetLastError(), std::system_category());
}

static HMODULE GetTargetModule()
{
	if (hTargetDll)
		return hTargetDll;
	std::call_once(flag, LoadTargetModule);
	return hTargetDll;
}

STDAPI DllCanUnloadNow(void)
{
	try
	{
		static auto pfn = reinterpret_cast<decltype(DllCanUnloadNow)*>(GetProcAddress(GetTargetModule(), "DllCanUnloadNow"));
		return pfn();
	}
	catch (const std::system_error& ex)
	{
		return HRESULT_FROM_WIN32(ex.code().value());
	}
}

STDAPI DllGetClassObject(_In_ REFCLSID rclsid, _In_ REFIID riid, _Outptr_ LPVOID* ppv)
{
	try
	{
		static auto pfn = reinterpret_cast<decltype(DllGetClassObject)*>(GetProcAddress(GetTargetModule(), "DllGetClassObject"));
		return pfn(rclsid, riid, ppv);
	}
	catch (const std::system_error& ex)
	{
		return HRESULT_FROM_WIN32(ex.code().value());
	}
}

STDAPI DllRegisterServer(void)
{
	try
	{
		static auto pfn = reinterpret_cast<decltype(DllRegisterServer)*>(GetProcAddress(GetTargetModule(), "DllRegisterServer"));
		return pfn();
	}
	catch (const std::system_error& ex)
	{
		return HRESULT_FROM_WIN32(ex.code().value());
	}
}

STDAPI DllUnregisterServer(void)
{
	try
	{
		static auto pfn = reinterpret_cast<decltype(DllUnregisterServer)*>(GetProcAddress(GetTargetModule(), "DllUnregisterServer"));
		return pfn();
	}
	catch (const std::system_error& ex)
	{
		return HRESULT_FROM_WIN32(ex.code().value());
	}
}

STDAPI DllInstall(BOOL bInstall, _In_opt_  LPCWSTR pszCmdLine)
{
	try
	{
		static auto pfn = reinterpret_cast<decltype(DllInstall)*>(GetProcAddress(GetTargetModule(), "DllInstall"));
		return pfn(bInstall, pszCmdLine);
	}
	catch (const std::system_error& ex)
	{
		return HRESULT_FROM_WIN32(ex.code().value());
	}
}
