#include "pch.h"
#include "framework.h"
#include <AclAPI.h>
#include "wrappers.h"
#include "StrUtils.h"

using namespace ATL;

static DWORD GrantAllAppPackagesPermissions(LPCWSTR objectName, SE_OBJECT_TYPE objectType, DWORD accessMask) noexcept
{
	EXPLICIT_ACCESSW ea = {
		.grfAccessPermissions = accessMask,
		.grfAccessMode = GRANT_ACCESS,
		.grfInheritance = SUB_CONTAINERS_AND_OBJECTS_INHERIT,
		.Trustee = {
			.TrusteeForm = TRUSTEE_IS_NAME,
			.ptstrName = const_cast<PWSTR>(L"ALL APPLICATION PACKAGES")
		}
	};

	PACL pOldDacl, pNewDacl;
	PSECURITY_DESCRIPTOR psd;

	DWORD err;
	err = GetNamedSecurityInfoW(objectName, objectType, DACL_SECURITY_INFORMATION,
		nullptr, nullptr, &pOldDacl, nullptr, &psd);
	if (err == ERROR_SUCCESS)
	{
		err = SetEntriesInAclW(1, &ea, pOldDacl, &pNewDacl);
		if (err == ERROR_SUCCESS)
		{
			err = SetNamedSecurityInfoW(const_cast<LPWSTR>(objectName), objectType, DACL_SECURITY_INFORMATION,
				nullptr, nullptr, pNewDacl, nullptr);
			LocalFree(pNewDacl);
		}
		LocalFree(psd);
	}

	return err;
}

// Allow "ALL APPLICATION PACKAGES" to read/execute all related files & registry keys
// to prevent permission issues in UWPs
static DWORD ChangeSecurityForOneCore()
{
	WCHAR path[MAX_PATH];
	DWORD len = GetModuleFileNameW((HMODULE)&__ImageBase, path, MAX_PATH);
	if (len == 0)
		return GetLastError();
	else if (len == MAX_PATH)
		return ERROR_FILENAME_EXCED_RANGE;
	PathRemoveFileSpecW(path);
	PathRemoveFileSpecW(path);

	if (!PathAppendW(path, L"x86"))
		return GetLastError();
	DWORD err = GrantAllAppPackagesPermissions(path, SE_FILE_OBJECT, FILE_GENERIC_READ | FILE_GENERIC_EXECUTE);
	if (err != ERROR_SUCCESS && err != ERROR_FILE_NOT_FOUND)
		return err;
	PathRemoveFileSpecW(path);

	if (!PathAppendW(path, L"x64"))
		return GetLastError();
	if (err != ERROR_SUCCESS && err != ERROR_FILE_NOT_FOUND)
		return err;
	err = GrantAllAppPackagesPermissions(path, SE_FILE_OBJECT, FILE_GENERIC_READ | FILE_GENERIC_EXECUTE);
	PathRemoveFileSpecW(path);

	if (!PathAppendW(path, L"ARM64"))
		return GetLastError();
	err = GrantAllAppPackagesPermissions(path, SE_FILE_OBJECT, FILE_GENERIC_READ | FILE_GENERIC_EXECUTE);
	if (err != ERROR_SUCCESS && err != ERROR_FILE_NOT_FOUND)
		return err;
	PathRemoveFileSpecW(path);

	return GrantAllAppPackagesPermissions(L"CURRENT_USER\\Software\\NaturalVoiceSAPIAdapter", SE_REGISTRY_KEY, KEY_READ);
}

static bool WriteOneCoreXMLFile(LPCWSTR dirpath, LPCWSTR filepath)
{
	// Sets the owner of every file/folder created to "TrustedInstaller"

	BYTE sdData[SECURITY_DESCRIPTOR_MIN_LENGTH];
	if (!InitializeSecurityDescriptor(sdData, SECURITY_DESCRIPTOR_REVISION))
		return false;
	BYTE sidData[SECURITY_MAX_SID_SIZE];
	DWORD cbSid = sizeof sidData;
	WCHAR domain[32];
	DWORD cchDomain = 32;
	SID_NAME_USE nameUse;
	if (!LookupAccountNameW(nullptr, L"NT SERVICE\\TrustedInstaller", sidData, &cbSid, domain, &cchDomain, &nameUse))
		return false;
	if (!SetSecurityDescriptorOwner(sdData, sidData, FALSE))
		return false;
	SECURITY_ATTRIBUTES sa = { sizeof sa, sdData, FALSE };  // owner set, other parts defaulted

	// Create folder & file

	if (!CreateDirectoryW(dirpath, &sa) && GetLastError() != ERROR_ALREADY_EXISTS)
		return false;

	HANDLE hFile = CreateFileW(filepath, GENERIC_WRITE, FILE_SHARE_READ, &sa, CREATE_ALWAYS,
		FILE_FLAG_BACKUP_SEMANTICS, nullptr);
	if (hFile == INVALID_HANDLE_VALUE)
		return false;

	auto filedata = GetResData(L"tokens.xml", L"ONECORE");
	DWORD cbWritten = 0;
	WriteFile(hFile, filedata.data(), (DWORD)filedata.size(), &cbWritten, nullptr);
	CloseHandle(hFile);

	return filedata.size() == cbWritten;
}

static HRESULT InstallOneCore(bool bInstall)
{
	// Duplicate token and enable "Restore files" privilege
	// so that we can write to C:\Windows\System32\Speech_OneCore\common

	if (!ImpersonateSelf(SecurityImpersonation))
		return HRESULT_FROM_WIN32(GetLastError());
	ScopeGuard autoRevert{ RevertToSelf };

	HANDLE hToken;
	if (!OpenThreadToken(GetCurrentThread(), TOKEN_ADJUST_PRIVILEGES, TRUE, &hToken))
		return HRESULT_FROM_WIN32(GetLastError());
	TOKEN_PRIVILEGES priv = { 1, { 0, 0, SE_PRIVILEGE_ENABLED } };
	BOOL ret = LookupPrivilegeValueW(nullptr, SE_RESTORE_NAME, &priv.Privileges[0].Luid);
	ret = ret && AdjustTokenPrivileges(hToken, FALSE, &priv, sizeof priv, nullptr, nullptr);
	CloseHandle(hToken);
	if (!ret)
		return HRESULT_FROM_WIN32(GetLastError());

	// get the path: C:\Windows\System32\Speech_OneCore\common\NaturalVoiceSAPIAdapter\tokens.xml

	WCHAR dirpath[MAX_PATH], filepath[MAX_PATH];
	UINT pathlen = GetSystemDirectoryW(dirpath, MAX_PATH);
	if (pathlen == 0)
		return HRESULT_FROM_WIN32(GetLastError());
	else if (pathlen >= MAX_PATH)
		return HRESULT_FROM_WIN32(ERROR_FILENAME_EXCED_RANGE);

	if (!PathAppendW(dirpath, L"Speech_OneCore\\common\\NaturalVoiceSAPIAdapter")
		|| PathCombineW(filepath, dirpath, L"tokens.xml") == nullptr)
		return HRESULT_FROM_WIN32(ERROR_FILENAME_EXCED_RANGE);

	if (bInstall)
	{
		if (!WriteOneCoreXMLFile(dirpath, filepath))
			return HRESULT_FROM_WIN32(GetLastError());
		DWORD err = ChangeSecurityForOneCore();
		if (err != ERROR_SUCCESS)
			return HRESULT_FROM_WIN32(err);
	}
	else
	{
		if (!DeleteFileW(filepath) || !RemoveDirectoryW(dirpath))
			return HRESULT_FROM_WIN32(GetLastError());
	}

	return S_OK;
}

HRESULT RegisterVoice(bool isOneCore, bool bInstall)
{
	PCWSTR regpath = isOneCore
		? LR"(SOFTWARE\Microsoft\Speech_OneCore\Voices\TokenEnums\NaturalVoiceEnumerator)"
		: LR"(SOFTWARE\Microsoft\Speech\Voices\TokenEnums\NaturalVoiceEnumerator)";
	if (bInstall)
	{
		if (isOneCore)
			RETONFAIL(InstallOneCore(true));
		HKEY hKey;
		LSTATUS err = RegCreateKeyExW(HKEY_LOCAL_MACHINE, regpath,
			0, nullptr, 0, KEY_SET_VALUE, nullptr, &hKey, nullptr);
		if (err != ERROR_SUCCESS)
			return HRESULT_FROM_WIN32(err);
		static constexpr wchar_t CLSID[] = L"{b8b9e38f-e5a2-4661-9fde-4ac7377aa6f6}";
		err = RegSetValueExW(hKey, L"CLSID", 0, REG_SZ, reinterpret_cast<const BYTE*>(CLSID), sizeof CLSID);
		RegCloseKey(hKey);
		if (err != ERROR_SUCCESS)
			return HRESULT_FROM_WIN32(err);
		return S_OK;
	}
	else
	{
		DWORD err1 = ERROR_SUCCESS, err2 = ERROR_SUCCESS;
		err1 = RegDeleteKeyW(HKEY_LOCAL_MACHINE, regpath);
		if (isOneCore)
			err2 = InstallOneCore(false);
		DWORD err = err1 != ERROR_SUCCESS ? err1 : err2;
		return HRESULT_FROM_WIN32(err);
	}
}
