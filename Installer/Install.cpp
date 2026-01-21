#include "Installer.h"
#include "RegKey.h"
#include <system_error>
#include <algorithm>


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
    if (key.Create(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\NaturalVoiceSAPIAdapter",
        KEY_SET_VALUE | KEY_WOW64_64KEY) != ERROR_SUCCESS)
        return;

    WCHAR uninstallCmdLine[MAX_PATH + 11];
    DWORD len = GetModuleFileNameW(nullptr, uninstallCmdLine, MAX_PATH);
    if (len == 0 || len >= MAX_PATH - 3)  // 3 for quotes + null
        return;
    PathQuoteSpacesW(uninstallCmdLine);
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
    RegDeleteKeyW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\NaturalVoiceSAPIAdapter");
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
    if (!PathAppendW(path, is64Bit ? (IsArm64System() ? L"arm64" : L"x64") : L"x86"))
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
        if (Is64BitSystem())
            AddToRegistry(L"x64\\PhoneConverters.reg");
        else
            AddToRegistry(L"x86\\PhoneConverters.reg");
    }
    catch (const std::system_error& ex)
    {
        ReportError(ex.code().value());
    }
}

// Parse command line parameter value
static std::wstring GetParameterValue(int argc, LPWSTR* argv, LPCWSTR paramName)
{
    for (int i = 1; i < argc - 1; i++)
    {
        if (_wcsicmp(argv[i], paramName) == 0)
        {
            return argv[i + 1];
        }
    }
    return L"";
}

// Check if parameter exists
static bool HasParameter(int argc, LPWSTR* argv, LPCWSTR paramName)
{
    for (int i = 1; i < argc; i++)
    {
        if (_wcsicmp(argv[i], paramName) == 0)
        {
            return true;
        }
    }
    return false;
}

// Apply configuration settings from command line
static void ApplyConfigurationSettings(int argc, LPWSTR* argv)
{
    RegKey configKey, enumKey;
    
    // Create registry keys
    configKey.Create(HKEY_CURRENT_USER, L"Software\\NaturalVoiceSAPIAdapter", KEY_SET_VALUE);
    enumKey.Create(HKEY_CURRENT_USER, L"Software\\NaturalVoiceSAPIAdapter\\Enumerator", KEY_SET_VALUE);

    // Log level (0-6, default 2)
    std::wstring logLevel = GetParameterValue(argc, argv, L"-loglevel");
    if (!logLevel.empty())
    {
        DWORD level = _wtoi(logLevel.c_str());
        if (level <= 6)
        {
            configKey.SetDword(L"LogLevel", level);
        }
    }

    // Narrator voices
    if (HasParameter(argc, argv, L"-no-narrator"))
    {
        enumKey.SetDword(L"NoNarratorVoices", 1);
    }
    else if (HasParameter(argc, argv, L"-enable-narrator"))
    {
        enumKey.SetDword(L"NoNarratorVoices", 0);
    }

    // Edge voices
    if (HasParameter(argc, argv, L"-no-edge"))
    {
        enumKey.SetDword(L"NoEdgeVoices", 1);
    }
    else if (HasParameter(argc, argv, L"-enable-edge"))
    {
        enumKey.SetDword(L"NoEdgeVoices", 0);
    }

    // Azure voices
    if (HasParameter(argc, argv, L"-no-azure"))
    {
        enumKey.SetDword(L"NoAzureVoices", 1);
    }
    else if (HasParameter(argc, argv, L"-enable-azure"))
    {
        enumKey.SetDword(L"NoAzureVoices", 0);
    }

    // Narrator voice path
    std::wstring narratorPath = GetParameterValue(argc, argv, L"-narrator-path");
    if (!narratorPath.empty())
    {
        enumKey.SetString(L"NarratorVoicePath", narratorPath.c_str());
    }

    // Azure key and region
    std::wstring azureKey = GetParameterValue(argc, argv, L"-azure-key");
    std::wstring azureRegion = GetParameterValue(argc, argv, L"-azure-region");
    if (!azureKey.empty())
    {
        enumKey.SetString(L"AzureVoiceKey", azureKey.c_str());
    }
    if (!azureRegion.empty())
    {
        enumKey.SetString(L"AzureVoiceRegion", azureRegion.c_str());
    }

    // Language settings
    if (HasParameter(argc, argv, L"-all-languages"))
    {
        enumKey.SetDword(L"EdgeVoiceAllLanguages", 1);
    }
    else
    {
        std::wstring languages = GetParameterValue(argc, argv, L"-languages");
        if (!languages.empty())
        {
            enumKey.SetDword(L"EdgeVoiceAllLanguages", 0);
            // Convert comma-separated to null-separated
            std::vector<std::wstring> langList;
            std::wstring temp;
            for (wchar_t c : languages)
            {
                if (c == L',')
                {
                    if (!temp.empty())
                    {
                        langList.push_back(temp);
                        temp.clear();
                    }
                }
                else
                {
                    temp.push_back(c);
                }
            }
            if (!temp.empty())
            {
                langList.push_back(temp);
            }
            enumKey.SetMultiStringList(L"EdgeVoiceLanguages", langList);
        }
    }
}

void SilentInstall(int argc, LPWSTR* argv)
{
    // Apply configuration settings first
    ApplyConfigurationSettings(argc, argv);

    // Determine which architectures to install
    bool install32 = true;
    bool install64 = Is64BitSystem();

    // Check for architecture-specific parameters
    if (HasParameter(argc, argv, L"-32bit-only"))
    {
        install32 = true;
        install64 = false;
    }
    else if (HasParameter(argc, argv, L"-64bit-only"))
    {
        install32 = false;
        install64 = Is64BitSystem();
    }

    // Install 32-bit version
    if (install32)
    {
        try
        {
            Register(false);
        }
        catch (const std::system_error&)
        {
            // In silent mode, we still throw to let the caller handle it
            throw;
        }
    }

    // Install 64-bit version
    if (install64)
    {
        try
        {
            Register(true);
        }
        catch (const std::system_error&)
        {
            // In silent mode, we still throw to let the caller handle it
            throw;
        }
    }

    // Check and install phoneme converters if needed
    if (!HasParameter(argc, argv, L"-no-phoneme-converters"))
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

        if (!hasConverters)
        {
            try
            {
                if (Is64BitSystem())
                    AddToRegistry(L"x64\\PhoneConverters.reg");
                else
                    AddToRegistry(L"x86\\PhoneConverters.reg");
            }
            catch (const std::system_error&)
            {
                // Ignore errors in silent mode for phoneme converters
            }
        }
    }
}

void SilentUninstall()
{
    Unregister(false);
    if (Is64BitSystem())
        Unregister(true);
}