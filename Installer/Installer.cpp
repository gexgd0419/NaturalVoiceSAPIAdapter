#include "framework.h"
#include "Installer.h"

static BOOL Is64BitSystem()
{
#ifdef _WIN64
    return TRUE;
#else
    static auto pfn = (decltype(IsWow64Process)*)GetProcAddress(GetModuleHandleW(L"kernel32"), "IsWow64Process");
    BOOL iswow = FALSE;
    return pfn && pfn(GetCurrentProcess(), &iswow) && iswow;
#endif
}

static BOOL SupportsUAC()
{
    OSVERSIONINFOEXW osvi = { sizeof(osvi), 0, 0, 0, 0, {0}, 0, 0 };
    DWORDLONG        const dwlConditionMask = VerSetConditionMask(0, VER_MAJORVERSION, VER_GREATER_EQUAL);

    osvi.dwMajorVersion = 6;

    return VerifyVersionInfoW(&osvi, VER_MAJORVERSION, dwlConditionMask);
}

static BOOL SupportsNarratorVoices()
{
    OSVERSIONINFOEXW osvi = { sizeof(osvi), 0, 0, 0, 0, {0}, 0, 0 };
    DWORDLONG        const dwlConditionMask = VerSetConditionMask(
        VerSetConditionMask(0, VER_MAJORVERSION, VER_GREATER_EQUAL)
        , VER_BUILDNUMBER, VER_GREATER_EQUAL);

    osvi.dwMajorVersion = 10;
    osvi.dwBuildNumber  = 17763;

    return VerifyVersionInfoW(&osvi, VER_MAJORVERSION | VER_BUILDNUMBER, dwlConditionMask);
}

static BOOL IsAdmin()
{
    BOOL isAdmin = FALSE;
    BYTE adminSid[sizeof(SID) + sizeof(DWORD)];
    DWORD cb = sizeof adminSid;
    CreateWellKnownSid(WinBuiltinAdministratorsSid, nullptr, adminSid, &cb);
    CheckTokenMembership(nullptr, adminSid, &isAdmin);
    return isAdmin;
}

static bool GetInstalledPath(bool is64Bit, LPWSTR path, DWORD cchMax)
{
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Classes\\CLSID\\{013ab33b-ad1a-401c-8bee-f6e2b046a94e}\\InprocServer32", 0,
        (is64Bit ? KEY_WOW64_64KEY : KEY_WOW64_32KEY) | KEY_QUERY_VALUE, &hKey) != ERROR_SUCCESS)
        return false;

    DWORD cb = cchMax * sizeof(DWORD);
    if (RegQueryValueExW(hKey, nullptr, nullptr, nullptr, (LPBYTE)path, &cb) != ERROR_SUCCESS)
    {
        RegCloseKey(hKey);
        return false;
    }

    DWORD cch = cb / 2;
    if (cch >= cchMax)
        cch = MAX_PATH - 1;
    path[cch] = L'\0';

    RegCloseKey(hKey);
    return true;
}

static void CheckInstallation(bool is64Bit, HWND hDlg, UINT idStatic, UINT idUninstallBtn)
{
    WCHAR path[MAX_PATH], szText[256], szFormat[256];
    LPVOID pVerData;
    DWORD hVer, size;
    VS_FIXEDFILEINFO* pInfo;
    UINT uLen;

    if (is64Bit && !Is64BitSystem())
        return;

    if (!GetInstalledPath(is64Bit, path, MAX_PATH))
        goto NotInstalled;

    size = GetFileVersionInfoSizeW(path, &hVer);
    if (size == 0)
        goto NotInstalled;

    pVerData = malloc(size);
    if (!pVerData)
        goto NotInstalled;

    if (!GetFileVersionInfoW(path, 0, size, pVerData)
        || !VerQueryValueW(pVerData, L"\\", (LPVOID*)&pInfo, &uLen))
    {
        free(pVerData);
        goto NotInstalled;
    }

    LoadStringW(nullptr, IDS_INSTALLED, szFormat, 256);
    swprintf_s(szText, szFormat,
        HIWORD(pInfo->dwFileVersionMS), LOWORD(pInfo->dwFileVersionMS),
        HIWORD(pInfo->dwFileVersionLS), LOWORD(pInfo->dwFileVersionLS));
    SetDlgItemTextW(hDlg, idStatic, szText);
    EnableWindow(GetDlgItem(hDlg, idUninstallBtn), TRUE);
    free(pVerData);
    return;

NotInstalled:
    LoadStringW(nullptr, IDS_NOT_INSTALLED, szText, 256);
    SetDlgItemTextW(hDlg, idStatic, szText);
    HWND hBtn = GetDlgItem(hDlg, idUninstallBtn);
    if (GetFocus() == hBtn)
        SetFocus(GetNextDlgTabItem(hDlg, hBtn, FALSE));
    EnableWindow(hBtn, FALSE);
}

static void EnableRange(HWND hDlg, UINT idFrom, UINT idTo, BOOL enable)
{
    for (UINT id = idFrom; id <= idTo; id++)
        EnableWindow(GetDlgItem(hDlg, id), enable);
}

static BOOL MainDlgInit(HWND hDlg)
{
    if (!IsAdmin())
    {
        if (SupportsUAC())
        {
            for (UINT id = IDC_INSTALL_32BIT; id <= IDC_UNINSTALL_64BIT; id++)
                SendDlgItemMessageW(hDlg, id, BCM_SETSHIELD, 0, TRUE);
        }
    }
    if (!Is64BitSystem())
    {
        EnableRange(hDlg, IDC_INSTALL_64BIT, IDC_UNINSTALL_64BIT, FALSE);
        ShowWindow(GetDlgItem(hDlg, IDC_STATIC_64BIT_HEADER), SW_HIDE);
        ShowWindow(GetDlgItem(hDlg, IDC_STATIC_64BIT_STATUS), SW_HIDE);
        ShowWindow(GetDlgItem(hDlg, IDC_INSTALL_64BIT), SW_HIDE);
        ShowWindow(GetDlgItem(hDlg, IDC_UNINSTALL_64BIT), SW_HIDE);
    }

    CheckInstallation(false, hDlg, IDC_STATIC_32BIT_STATUS, IDC_UNINSTALL_32BIT);
    CheckInstallation(true, hDlg, IDC_STATIC_64BIT_STATUS, IDC_UNINSTALL_64BIT);

    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\NaturalVoiceSAPIAdapter\\Enumerator", 0,
        KEY_QUERY_VALUE, &hKey) == ERROR_SUCCESS)
    {
        DWORD data, cbData;
        data = 0;
        cbData = sizeof(DWORD);
        RegQueryValueExW(hKey, L"NoNarratorVoices", nullptr, nullptr, (LPBYTE)&data, &cbData);
        CheckDlgButton(hDlg, IDC_CHK_NARRATOR_VOICES, data ? BST_UNCHECKED : BST_CHECKED);
        data = 0;
        cbData = sizeof(DWORD);
        RegQueryValueExW(hKey, L"NoEdgeVoices", nullptr, nullptr, (LPBYTE)&data, &cbData);
        CheckDlgButton(hDlg, IDC_CHK_EDGE_VOICES, data ? BST_UNCHECKED : BST_CHECKED);
        data = 0;
        cbData = sizeof(DWORD);
        RegQueryValueExW(hKey, L"EdgeVoiceAllLanguages", nullptr, nullptr, (LPBYTE)&data, &cbData);
        CheckDlgButton(hDlg, data ? IDC_ALL_LANGS : IDC_CUR_LANG, BST_CHECKED);
        RegCloseKey(hKey);
    }
    else
    {
        CheckDlgButton(hDlg, IDC_CHK_NARRATOR_VOICES, BST_CHECKED);
        CheckDlgButton(hDlg, IDC_CHK_EDGE_VOICES, BST_CHECKED);
        CheckDlgButton(hDlg, IDC_CUR_LANG, BST_CHECKED);
    }

    EnableRange(hDlg, IDC_ALL_LANGS, IDC_CUR_LANG,
        SendDlgItemMessageW(hDlg, IDC_CHK_EDGE_VOICES, BM_GETCHECK, 0, 0) == BST_CHECKED);

    if  (SupportsNarratorVoices())
    {
        SetFocus(GetDlgItem(hDlg, IDC_CHK_NARRATOR_VOICES));
    }
    else
    {
        CheckDlgButton(hDlg, IDC_CHK_NARRATOR_VOICES, BST_UNCHECKED);
        EnableWindow(GetDlgItem(hDlg, IDC_CHK_NARRATOR_VOICES), FALSE);
        SetFocus(GetDlgItem(hDlg, IDC_CHK_EDGE_VOICES));
        HWND hTooltip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_ALWAYSTIP,
            CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, hDlg, nullptr, nullptr, nullptr);
        TOOLINFOW info = { sizeof info };
        info.uFlags = TTF_SUBCLASS;
        info.hwnd = hDlg;
        info.uId = 1;
        GetWindowRect(GetDlgItem(hDlg, IDC_CHK_NARRATOR_VOICES), &info.rect);
        MapWindowPoints(nullptr, hDlg, (LPPOINT)&info.rect, 2);
        info.lpszText = MAKEINTRESOURCEW(IDS_NARRATOR_VOICE_NOT_SUPPORTED);
        SendMessageW(hTooltip, TTM_ADDTOOLW, 0, (LPARAM)&info);
    }
    return FALSE;
}

static INT_PTR CALLBACK AboutDlg(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);
    switch (message)
    {
    case WM_INITDIALOG:
        return TRUE;

    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
        {
            EndDialog(hDlg, LOWORD(wParam));
            return TRUE;
        }
        break;

    case WM_NOTIFY:
        switch (((LPNMHDR)lParam)->code)
        {
        case NM_CLICK:
        case NM_RETURN:
            ShellExecuteW(nullptr, nullptr, L"https://github.com/gexgd0419/NaturalVoiceSAPIAdapter",
                nullptr, nullptr, SW_SHOW);
            break;
        }
    }
    return (INT_PTR)FALSE;
}

static void SetEnumeratorRegDWord(LPCWSTR name, DWORD value)
{
    HKEY hKey;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\NaturalVoiceSAPIAdapter\\Enumerator", 0, nullptr, 0,
        KEY_SET_VALUE, nullptr, &hKey, nullptr) == ERROR_SUCCESS)
    {
        RegSetValueExW(hKey, name, 0, REG_DWORD, (LPBYTE)&value, sizeof(DWORD));
        RegCloseKey(hKey);
    }
}

static void ReportError(DWORD err)
{
    WCHAR buffer[512] = {};
    switch (err)
    {
    case ERROR_CANCELLED:
        return;
    case ERROR_ACCESS_DENIED:
        LoadStringW(nullptr, IDS_PERMISSION, buffer, 512);
        break;
    default:
        FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM, nullptr, err, LANG_USER_DEFAULT, buffer, 512, nullptr);
        break;
    }
    MessageBoxW(GetActiveWindow(), buffer, L"", err == ERROR_SUCCESS ? MB_ICONINFORMATION : MB_ICONEXCLAMATION);
}

static void Register(bool is64Bit)
{
    WCHAR dllpath[MAX_PATH];
    GetModuleFileNameW(nullptr, dllpath, MAX_PATH);
    LPWSTR pSlash = wcsrchr(dllpath, L'\\');
    if (pSlash)
        *pSlash = L'\0';
    if (is64Bit)
        wcscat_s(dllpath, L"\\x64\\NaturalVoiceSAPIAdapter.dll");
    else
        wcscat_s(dllpath, L"\\x86\\NaturalVoiceSAPIAdapter.dll");

    WCHAR cmdline[512];
    swprintf_s(cmdline, L"/s \"%s\"", dllpath);

    SHELLEXECUTEINFOW info = { sizeof info };
    info.fMask = SEE_MASK_NOCLOSEPROCESS;
    info.lpFile = L"regsvr32";
    info.lpParameters = cmdline;
    if (!IsAdmin() && SupportsUAC())
        info.lpVerb = L"runas";

    if (!ShellExecuteExW(&info) || !info.hProcess)
    {
        ReportError(GetLastError());
        return;
    }

    WaitForSingleObject(info.hProcess, INFINITE);
    DWORD exitcode;
    GetExitCodeProcess(info.hProcess, &exitcode);
    ReportError(exitcode);

    CloseHandle(info.hProcess);
}

static void Unregister(bool is64Bit)
{
    WCHAR dllpath[MAX_PATH];
    if (!GetInstalledPath(is64Bit, dllpath, MAX_PATH))
        return;

    WCHAR cmdline[512];
    swprintf_s(cmdline, L"/u /s \"%s\"", dllpath);

    SHELLEXECUTEINFOW info = { sizeof info };
    info.fMask = SEE_MASK_NOCLOSEPROCESS;
    info.lpFile = L"regsvr32";
    info.lpParameters = cmdline;
    if (!IsAdmin() && SupportsUAC())
        info.lpVerb = L"runas";

    if (!ShellExecuteExW(&info) || !info.hProcess)
    {
        ReportError(GetLastError());
        return;
    }

    WaitForSingleObject(info.hProcess, INFINITE);
    DWORD exitcode;
    GetExitCodeProcess(info.hProcess, &exitcode);
    ReportError(exitcode);

    CloseHandle(info.hProcess);
}

static INT_PTR CALLBACK MainDlg(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);
    switch (message)
    {
    case WM_INITDIALOG:
        return MainDlgInit(hDlg);

    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case IDOK:
        case IDCANCEL:
            EndDialog(hDlg, LOWORD(wParam));
            return TRUE;
        case IDC_ABOUT:
            DialogBoxParamW(nullptr, MAKEINTRESOURCEW(IDD_ABOUTBOX), hDlg, AboutDlg, 0);
            break;

        case IDC_INSTALL_32BIT:
            Register(false);
            CheckInstallation(false, hDlg, IDC_STATIC_32BIT_STATUS, IDC_UNINSTALL_32BIT);
            break;
        case IDC_INSTALL_64BIT:
            Register(true);
            CheckInstallation(true, hDlg, IDC_STATIC_64BIT_STATUS, IDC_UNINSTALL_64BIT);
            break;
        case IDC_UNINSTALL_32BIT:
            Unregister(false);
            CheckInstallation(false, hDlg, IDC_STATIC_32BIT_STATUS, IDC_UNINSTALL_32BIT);
            break;
        case IDC_UNINSTALL_64BIT:
            Unregister(true);
            CheckInstallation(true, hDlg, IDC_STATIC_64BIT_STATUS, IDC_UNINSTALL_64BIT);
            break;

        case IDC_CHK_NARRATOR_VOICES:
            SetEnumeratorRegDWord(L"NoNarratorVoices",
                SendDlgItemMessageW(hDlg, IDC_CHK_NARRATOR_VOICES, BM_GETCHECK, 0, 0) == BST_UNCHECKED);
            break;
        case IDC_CHK_EDGE_VOICES:
            SetEnumeratorRegDWord(L"NoEdgeVoices",
                SendDlgItemMessageW(hDlg, IDC_CHK_EDGE_VOICES, BM_GETCHECK, 0, 0) == BST_UNCHECKED);
            EnableRange(hDlg, IDC_ALL_LANGS, IDC_CUR_LANG,
                SendDlgItemMessageW(hDlg, IDC_CHK_EDGE_VOICES, BM_GETCHECK, 0, 0) == BST_CHECKED);
            break;
        case IDC_ALL_LANGS:
            SetEnumeratorRegDWord(L"EdgeVoiceAllLanguages", 1);
            break;
        case IDC_CUR_LANG:
            SetEnumeratorRegDWord(L"EdgeVoiceAllLanguages", 0);
            break;
        }
        break;
    }
    return (INT_PTR)FALSE;
}

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_ LPWSTR    lpCmdLine,
    _In_ int       nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);
    UNREFERENCED_PARAMETER(nCmdShow);

    DialogBoxParamW(hInstance, MAKEINTRESOURCEW(IDD_MAIN), nullptr, MainDlg, 0);

    return 0;
}