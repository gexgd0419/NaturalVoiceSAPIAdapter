// NaturalVoiceSAPIAdapter.cpp: DLL 导出的实现。


#include "pch.h"
#include "framework.h"
#include "resource.h"
#include "NaturalVoiceSAPIAdapter_i.h"
#include "dllmain.h"


using namespace ATL;

WCHAR g_regModulePath[MAX_PATH];
_ATL_REGMAP_ENTRY g_regEntries[] = { {L"ModulePath", g_regModulePath}, {nullptr, nullptr} };

HRESULT RegisterVoice(bool isOneCore, bool bInstall);

static HRESULT GetRegModulePath()
{
	DWORD len = GetModuleFileNameW((HMODULE)&__ImageBase, g_regModulePath, MAX_PATH);
	if (len == 0)
		return AtlHresultFromLastError();
	else if (len == MAX_PATH)
		return HRESULT_FROM_WIN32(ERROR_FILENAME_EXCED_RANGE);
#ifdef _M_ARM64
	// Replace the module path in the registry with Arm64XForwarder,
	// only in the ARM64 version.
	PathRemoveFileSpecW(g_regModulePath);
	if (!PathAppendW(g_regModulePath, L"Arm64XForwarder.dll"))
		return HRESULT_FROM_WIN32(ERROR_FILENAME_EXCED_RANGE);
#endif
	return S_OK;
}

// 用于确定 DLL 是否可由 OLE 卸载。
_Use_decl_annotations_
STDAPI DllCanUnloadNow(void)
{
	return _AtlModule.DllCanUnloadNow();
}

// 返回一个类工厂以创建所请求类型的对象。
_Use_decl_annotations_
STDAPI DllGetClassObject(_In_ REFCLSID rclsid, _In_ REFIID riid, _Outptr_ LPVOID* ppv)
{
	return _AtlModule.DllGetClassObject(rclsid, riid, ppv);
}

static bool IsRegistered()
{
	// Check if either SAPI or OneCore enumerator key exists
	HKEY hKey;
	auto stat1 = RegOpenKeyExW(HKEY_LOCAL_MACHINE,
		LR"(SOFTWARE\Microsoft\Speech\Voices\TokenEnums\NaturalVoiceEnumerator)",
		0, KEY_QUERY_VALUE, &hKey);
	if (stat1 == ERROR_SUCCESS)
		RegCloseKey(hKey);

	auto stat2 = RegOpenKeyExW(HKEY_LOCAL_MACHINE,
		LR"(SOFTWARE\Microsoft\Speech_OneCore\Voices\TokenEnums\NaturalVoiceEnumerator)",
		0, KEY_QUERY_VALUE, &hKey);
	if (stat2 == ERROR_SUCCESS)
		RegCloseKey(hKey);

	return stat1 == ERROR_SUCCESS || stat2 == ERROR_SUCCESS;
}

static HRESULT RegisterDll(bool isOneCore, bool bInstall)
{
	HRESULT hr = GetRegModulePath();
	if (FAILED(hr))
		return hr;
	if (bInstall)
	{
		hr = _AtlModule.DllRegisterServer();
		if (FAILED(hr))
			return hr;
		hr = RegisterVoice(isOneCore, true);
	}
	else
	{
		hr = RegisterVoice(isOneCore, false);
		if (FAILED(hr))
			return hr;
		if (!IsRegistered())
			hr = _AtlModule.DllUnregisterServer();
	}
	return hr;
}

// DllRegisterServer - 向系统注册表中添加项。
_Use_decl_annotations_
STDAPI DllRegisterServer(void)
{
	// Register SAPI only
	// To register OneCore, use DllInstall
	HRESULT hr = RegisterDll(false, true);
	if (FAILED(hr))
		RegisterDll(false, false);
	return hr;
}

// DllUnregisterServer - 移除系统注册表中的项。
_Use_decl_annotations_
STDAPI DllUnregisterServer(void)
{
	// Unregister both SAPI and OneCore when unregistering manually
	// To unregister only one of them, use DllInstall
	HRESULT hr1 = RegisterDll(false, false);
	HRESULT hr2 = RegisterDll(true, false);
	// Succeeds if one of them succeeded
	return SUCCEEDED(hr1) ? hr1 : SUCCEEDED(hr2) ? hr2 : hr1;
}

// DllInstall - 按用户和计算机在系统注册表中逐一添加/移除项。
STDAPI DllInstall(BOOL bInstall, _In_opt_  LPCWSTR pszCmdLine)
{
	HRESULT hr = E_FAIL;
	bool isOneCore = false;

	if (pszCmdLine != nullptr)
	{
		if (_wcsicmp(pszCmdLine, L"onecore") == 0)
		{
			isOneCore = true;
		}
	}

	if (bInstall)
	{
		hr = RegisterDll(isOneCore, true);
		if (FAILED(hr))
		{
			RegisterDll(isOneCore, false);
		}
	}
	else
	{
		hr = RegisterDll(isOneCore, false);
	}

	return hr;
}


