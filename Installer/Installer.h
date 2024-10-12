#pragma once

#include "framework.h"
#include "resource.h"
#include <string>
#include "RegKey.h"

inline BOOL Is64BitSystem() noexcept
{
#ifdef _WIN64
    return TRUE;
#else
    static auto pfn = (decltype(IsWow64Process)*)GetProcAddress(GetModuleHandleW(L"kernel32"), "IsWow64Process");
    BOOL iswow = FALSE;
    return pfn && pfn(GetCurrentProcess(), &iswow) && iswow;
#endif
}

inline BOOL IsArm64System() noexcept
{
#ifdef _M_ARM64
    return TRUE;
#else
    static auto pfn = (decltype(IsWow64Process2)*)GetProcAddress(GetModuleHandleW(L"kernel32"), "IsWow64Process2");
    USHORT proc_arch, host_arch;
    return pfn && pfn(GetCurrentProcess(), &proc_arch, &host_arch) && host_arch == IMAGE_FILE_MACHINE_ARM64;
#endif
}

inline BOOL IsWindowsVersionOrGreater(DWORD dwMajor, DWORD dwMinor) noexcept
{
    OSVERSIONINFOEXW osvi = { sizeof(osvi), 0, 0, 0, 0, {0}, 0, 0 };
    DWORDLONG        const dwlConditionMask = VerSetConditionMask(
        VerSetConditionMask(0, VER_MAJORVERSION, VER_GREATER_EQUAL)
        , VER_MINORVERSION, VER_GREATER_EQUAL);

    osvi.dwMajorVersion = dwMajor;
    osvi.dwMinorVersion = dwMinor;

    return VerifyVersionInfoW(&osvi, VER_MAJORVERSION | VER_MINORVERSION, dwlConditionMask);
}

inline BOOL IsWindowsVersionOrGreater(WORD Win32_WinNT) noexcept
{
    return IsWindowsVersionOrGreater(HIBYTE(Win32_WinNT), LOBYTE(Win32_WinNT));
}

inline BOOL IsWindows10BuildOrGreater(DWORD dwBuild) noexcept
{
    OSVERSIONINFOEXW osvi = { sizeof(osvi), 0, 0, 0, 0, {0}, 0, 0 };
    DWORDLONG        const dwlConditionMask = VerSetConditionMask(
        VerSetConditionMask(0, VER_MAJORVERSION, VER_GREATER_EQUAL)
        , VER_BUILDNUMBER, VER_GREATER_EQUAL);

    osvi.dwMajorVersion = 10;
    osvi.dwBuildNumber = dwBuild;

    return VerifyVersionInfoW(&osvi, VER_MAJORVERSION | VER_BUILDNUMBER, dwlConditionMask);
}

inline BOOL IsWindows7OrGreater() noexcept
{
    return IsWindowsVersionOrGreater(_WIN32_WINNT_WIN7);
}

inline BOOL SupportsUAC() noexcept
{
    return IsWindowsVersionOrGreater(_WIN32_WINNT_VISTA);
}

inline BOOL SupportsInstallingNarratorVoices() noexcept
{
    return IsWindows10BuildOrGreater(17763);
}

inline BOOL IsAdmin() noexcept
{
    BOOL isAdmin = FALSE;
    BYTE adminSid[sizeof(SID) + sizeof(DWORD)];
    DWORD cb = sizeof adminSid;
    CreateWellKnownSid(WinBuiltinAdministratorsSid, nullptr, adminSid, &cb);
    CheckTokenMembership(nullptr, adminSid, &isAdmin);
    return isAdmin;
}

inline std::wstring LoadResString(UINT id)
{
    LPWSTR p;
    int len = LoadStringW(nullptr, id, (LPWSTR)&p, 0);
    if (len == 0)
        return {};
    std::wstring s(p, len);
    if (s.back() == L'\0')
        s.pop_back();
    return s;
}

inline int ShowMessageBox(LPCWSTR msg, UINT flags) noexcept
{
    HWND hWnd = GetActiveWindow();
    WCHAR title[512] = {};
    GetWindowTextW(hWnd, title, 512);
    return MessageBoxW(hWnd, msg, title, flags);
}

inline int ShowMessageBox(LPCSTR msg, UINT flags) noexcept
{
    HWND hWnd = GetActiveWindow();
    CHAR title[512] = {};
    GetWindowTextA(hWnd, title, 512);
    return MessageBoxA(hWnd, msg, title, flags);
}

inline int ShowMessageBox(UINT msgid, UINT flags) noexcept
{
    return ShowMessageBox(LoadResString(msgid).c_str(), flags);
}

inline void ReportError(DWORD err) noexcept
{
    WCHAR buffer[512] = {};
    switch (err)
    {
    case ERROR_CANCELLED:
        return;
    case ERROR_ACCESS_DENIED:
        LoadStringW(nullptr, IDS_PERMISSION, buffer, 512);
        break;
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND:
        LoadStringW(nullptr, IDS_FILE_NOT_FOUND, buffer, 512);
        break;
    default:
        FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM, nullptr, err, LANG_USER_DEFAULT, buffer, 512, nullptr);
        break;
    }
    ShowMessageBox(buffer, err == ERROR_SUCCESS ? MB_ICONINFORMATION : MB_ICONEXCLAMATION);
}

inline std::wstring GetInstalledPath(bool is64Bit)
{
    RegKey key;
    key.Open(HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Classes\\CLSID\\{013ab33b-ad1a-401c-8bee-f6e2b046a94e}\\InprocServer32",
        (is64Bit ? KEY_WOW64_64KEY : KEY_WOW64_32KEY) | KEY_QUERY_VALUE);
    return key.GetString(nullptr);
}

inline uint64_t GetFileVersion(LPCWSTR path)
{
    DWORD size = GetFileVersionInfoSizeW(path, nullptr);
    if (size == 0)
        return (uint64_t)-1;
    std::unique_ptr<BYTE> pVerData(new BYTE[size]);
    if (GetFileVersionInfoW(path, 0, size, pVerData.get()))
    {
        VS_FIXEDFILEINFO* pInfo;
        UINT uLen;
        if (VerQueryValueW(pVerData.get(), L"\\", (LPVOID*)&pInfo, &uLen))
        {
            return pInfo->dwFileVersionLS | ((uint64_t)pInfo->dwFileVersionMS << 32);
        }
    }
    return (uint64_t)-1;
}

