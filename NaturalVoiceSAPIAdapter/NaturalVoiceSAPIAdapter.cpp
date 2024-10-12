// NaturalVoiceSAPIAdapter.cpp: DLL 导出的实现。


#include "pch.h"
#include "framework.h"
#include "resource.h"
#include "NaturalVoiceSAPIAdapter_i.h"
#include "dllmain.h"


using namespace ATL;

WCHAR g_regModulePath[MAX_PATH];
_ATL_REGMAP_ENTRY g_regEntries[] = { {L"ModulePath", g_regModulePath}, {nullptr, nullptr} };

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

// DllRegisterServer - 向系统注册表中添加项。
_Use_decl_annotations_
STDAPI DllRegisterServer(void)
{
	// 注册对象、类型库和类型库中的所有接口
	HRESULT hr = GetRegModulePath();
	if (FAILED(hr))
		return hr;
	hr = _AtlModule.DllRegisterServer();
	return hr;
}

// DllUnregisterServer - 移除系统注册表中的项。
_Use_decl_annotations_
STDAPI DllUnregisterServer(void)
{
	HRESULT hr = GetRegModulePath();
	if (FAILED(hr))
		return hr;
	hr = _AtlModule.DllUnregisterServer();
	return hr;
}

// DllInstall - 按用户和计算机在系统注册表中逐一添加/移除项。
STDAPI DllInstall(BOOL bInstall, _In_opt_  LPCWSTR pszCmdLine)
{
	HRESULT hr = E_FAIL;
	static const wchar_t szUserSwitch[] = L"user";

	if (pszCmdLine != nullptr)
	{
		if (_wcsnicmp(pszCmdLine, szUserSwitch, _countof(szUserSwitch)) == 0)
		{
			ATL::AtlSetPerUserRegistration(true);
		}
	}

	if (bInstall)
	{
		hr = DllRegisterServer();
		if (FAILED(hr))
		{
			DllUnregisterServer();
		}
	}
	else
	{
		hr = DllUnregisterServer();
	}

	return hr;
}


