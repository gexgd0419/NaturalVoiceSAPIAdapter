#include "Installer.h"
#include "RegKey.h"
#include <system_error>


// Returns the exit code. Throws if failed to launch.
static DWORD LaunchProcess(LPCWSTR pszApp, LPCWSTR pszCmdLine, bool asAdmin)
{
    HWND hWnd = GetActiveWindow();

    SHELLEXECUTEINFOW info = { sizeof info };
    info.fMask = SEE_MASK_NOCLOSEPROCESS;
    info.lpFile = pszApp;
    info.lpParameters = pszCmdLine;
    info.nShow = SW_HIDE;
    info.hwnd = hWnd;
    if (asAdmin && !IsAdmin() && SupportsUAC())
        info.lpVerb = L"runas";

    if (!ShellExecuteExW(&info))
    {
        throw std::system_error(GetLastError(), std::system_category());
    }

    DWORD exitcode = 0;
    if (info.hProcess)
    {
        HCURSOR hCur = SetCursor(LoadCursorW(nullptr, IDC_WAIT));
        WaitForSingleObject(info.hProcess, INFINITE);
        GetExitCodeProcess(info.hProcess, &exitcode);
        CloseHandle(info.hProcess);
        SetCursor(hCur);
    }

    return exitcode;
}

static void AddUninstallRegistryKey()
{
    RegKey key;
    if (key.Create(HKEY_LOCAL_MACHINE,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\NaturalVoiceSAPIAdapter",
        KEY_SET_VALUE | KEY_WOW64_64KEY) != ERROR_SUCCESS)
        return;

    WCHAR uninstallCmdLine[MAX_PATH + 11];
    DWORD len = GetModuleFileNameW(nullptr, uninstallCmdLine, MAX_PATH);
    if (len == 0 || len == MAX_PATH)
        return;
    if (!PathQuoteSpacesW(uninstallCmdLine))
        return;
    wcscat_s(uninstallCmdLine, L" -uninstall");

    key.SetString(L"DisplayName", L"NaturalVoiceSAPIAdapter");
    key.SetString(L"DisplayVersion", L"0.2");
    key.SetString(L"Publisher", L"gexgd0419 on GitHub");
    key.SetString(L"UninstallString", uninstallCmdLine);
    key.SetString(L"HelpLink", L"https://github.com/gexgd0419/NaturalVoiceSAPIAdapter");
    key.SetString(L"URLInfoAbout", L"https://github.com/gexgd0419/NaturalVoiceSAPIAdapter");
    key.SetString(L"URLUpdateInfo", L"https://github.com/gexgd0419/NaturalVoiceSAPIAdapter/releases");
}

static void RemoveUninstallRegistryKey()
{
    // Windows XP does not support RegDeleteKeyEx,
    // so we open its parent then delete using RegDeleteKey
    // to make sure to operate on the 64-bit key
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall",
        0, KEY_ENUMERATE_SUB_KEYS | KEY_WOW64_64KEY, &hKey))
    {
        RegDeleteKeyW(hKey, L"NaturalVoiceSAPIAdapter");
        RegCloseKey(hKey);
    }
}

void Register(bool is64Bit)
{
    WCHAR path[MAX_PATH];
    DWORD len = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (len == 0 || len == MAX_PATH)
        throw std::system_error(
            len == MAX_PATH ? ERROR_FILENAME_EXCED_RANGE : GetLastError(),
            std::system_category());

    PathRemoveFileSpecW(path);
    if (!PathAppendW(path, is64Bit ? L"x64" : L"x86"))
        throw std::system_error(ERROR_FILENAME_EXCED_RANGE, std::system_category());

    if (!SupportsInstallingNarratorVoices())
    {
        // On systems that do not support Narrator voices natively,
        // we should patch the Azure Speech SDK DLLs
        if (!PathAppendW(path, L"SpeechSDKPatcher.exe"))
            throw std::system_error(ERROR_FILENAME_EXCED_RANGE, std::system_category());

        DWORD exitcode = LaunchProcess(path, L"-quiet", false);

        // if no permission, try again as admin
        if (exitcode == ERROR_ACCESS_DENIED && !IsAdmin() && SupportsUAC())
            exitcode = LaunchProcess(path, L"-quiet", true);

        if (exitcode != ERROR_SUCCESS)
            throw std::system_error(exitcode, std::system_category());

        PathRemoveFileSpecW(path);
    }

    if (!PathAppendW(path, L"NaturalVoiceSAPIAdapter.dll"))
        throw std::system_error(ERROR_FILENAME_EXCED_RANGE, std::system_category());

    std::wstring cmdline = std::wstring(L"/s \"") + path + L'"';

    DWORD exitcode = LaunchProcess(L"regsvr32", cmdline.c_str(), true);
    if (exitcode != 0)
        throw std::system_error(exitcode, std::system_category());

    AddUninstallRegistryKey();
}

void Unregister(bool is64Bit)
{
    std::wstring dllpath = GetInstalledPath(is64Bit);

    if (!dllpath.empty())
    {
        std::wstring cmdline = L"/u /s \"" + dllpath + L'"';

        DWORD exitcode = LaunchProcess(L"regsvr32", cmdline.c_str(), true);
        if (exitcode != 0)
            throw std::system_error(exitcode, std::system_category());
    }

    if (is64Bit
        ? GetInstalledPath(false).empty()
        : (!Is64BitSystem() || GetInstalledPath(true).empty())
        )
    {
        RemoveUninstallRegistryKey();
    }
}

static void AddToRegistry(LPCWSTR regfile)
{
    WCHAR regfilepath[MAX_PATH];
    DWORD len = GetModuleFileNameW(nullptr, regfilepath, MAX_PATH);
    if (len == 0 || len == MAX_PATH)
        return;
    PathRemoveFileSpecW(regfilepath);
    if (!PathAppendW(regfilepath, regfile))
        return;

    // check if the .reg file exists first
    if (!PathFileExistsW(regfilepath) && GetLastError() == ERROR_FILE_NOT_FOUND)
    {
        ReportError(ERROR_FILE_NOT_FOUND);
        return;
    }

    std::wstring cmdline = std::wstring(L"import \"") + regfilepath + L'"';

    DWORD exitcode = LaunchProcess(L"reg", cmdline.c_str(), true);
    // We can know if it failed or not, but not why failed
    ReportError(exitcode == 0 ? ERROR_SUCCESS : E_FAIL);
}

void CheckPhonemeConverters()
{
    HKEY hKey;
    bool hasConverters = true;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Speech\\PhoneConverters\\Tokens\\Universal",
        0, KEY_QUERY_VALUE | KEY_WOW64_32KEY, &hKey) == ERROR_SUCCESS)
    {
        RegCloseKey(hKey);
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Speech\\PhoneConverters\\Tokens\\Universal",
            0, KEY_QUERY_VALUE | KEY_WOW64_64KEY, &hKey) == ERROR_SUCCESS)
        {
            RegCloseKey(hKey);
        }
        else
            hasConverters = false;
    }
    else
        hasConverters = false;

    if (hasConverters)
        return;

    if (ShowMessageBox(IDS_INSTALL_PHONEME_CONVERTERS, MB_ICONASTERISK | MB_YESNO) != IDYES)
        return;

    try
    {
        AddToRegistry(L"x86\\PhoneConverters.reg");
        if (Is64BitSystem())
            AddToRegistry(L"x64\\PhoneConverters.reg");
    }
    catch (const std::system_error& ex)
    {
        ReportError(ex.code().value());
    }
}