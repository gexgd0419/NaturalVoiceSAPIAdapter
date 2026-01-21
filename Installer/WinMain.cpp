#include "Installer.h"
#include <shellapi.h>
#include <iostream>

void Register(bool is64Bit);
void Unregister(bool is64Bit);
INT_PTR CALLBACK MainDlg(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
void SilentInstall(int argc, LPWSTR* argv);
void SilentUninstall();

// Show help message
void ShowHelp()
{
    MessageBoxW(nullptr,
        L"NaturalVoiceSAPIAdapter Installer - Command Line Options\n\n"
        L"Basic Usage:\n"
        L"  Installer.exe                    - Open GUI installer\n"
        L"  Installer.exe -silent            - Silent installation\n"
        L"  Installer.exe -silent -uninstall - Silent uninstallation\n\n"
        L"Parameters:\n"
        L"  -?, -h, -help, /?, /h, /help     - Show this help\n"
        L"  -silent, -s, /silent, /s         - Enable silent mode\n"
        L"  -uninstall, /uninstall           - Uninstall mode\n\n"
        L"Architecture:\n"
        L"  -32bit-only                      - Install 32-bit version only\n"
        L"  -64bit-only                      - Install 64-bit version only\n\n"
        L"Voice Engines:\n"
        L"  -enable-narrator, -no-narrator   - Enable/disable Narrator voices\n"
        L"  -enable-edge, -no-edge           - Enable/disable Edge voices\n"
        L"  -enable-azure, -no-azure         - Enable/disable Azure voices\n\n"
        L"Configuration:\n"
        L"  -narrator-path <path>            - Set Narrator voice path\n"
        L"  -azure-key <key>                 - Set Azure Speech Service key\n"
        L"  -azure-region <region>           - Set Azure Speech Service region\n"
        L"  -languages <list>                - Language list (comma-separated)\n"
        L"  -all-languages                   - Include all languages\n"
        L"  -loglevel <0-6>                  - Set log level (0=Off, 6=Verbose)\n"
        L"  -no-phoneme-converters           - Skip phoneme converters\n\n"
        L"Examples:\n"
        L"  Installer.exe -silent -enable-edge -languages \"en-US,zh-CN\"\n"
        L"  Installer.exe -silent -64bit-only -no-narrator -enable-edge\n\n"
        L"For detailed documentation, see SILENT_INSTALL.md",
        L"NaturalVoiceSAPIAdapter Installer Help",
        MB_ICONINFORMATION | MB_OK);
}

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_ LPWSTR    lpCmdLine,
    _In_ int       nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(nCmdShow);
    UNREFERENCED_PARAMETER(lpCmdLine);

    // Parse command line arguments
    int argc;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv)
    {
        return GetLastError();
    }

    // Check for help request first
    for (int i = 1; i < argc; i++)
    {
        if (_wcsicmp(argv[i], L"-?") == 0 || _wcsicmp(argv[i], L"/?") == 0 ||
            _wcsicmp(argv[i], L"-h") == 0 || _wcsicmp(argv[i], L"/h") == 0 ||
            _wcsicmp(argv[i], L"-help") == 0 || _wcsicmp(argv[i], L"/help") == 0 ||
            _wcsicmp(argv[i], L"--help") == 0)
        {
            ShowHelp();
            LocalFree(argv);
            return 0;
        }
    }

    // Check for silent install/uninstall
    bool silentMode = false;
    bool uninstallMode = false;
    
    for (int i = 1; i < argc; i++)
    {
        if (_wcsicmp(argv[i], L"-silent") == 0 || _wcsicmp(argv[i], L"/silent") == 0 ||
            _wcsicmp(argv[i], L"-s") == 0 || _wcsicmp(argv[i], L"/s") == 0)
        {
            silentMode = true;
        }
        else if (_wcsicmp(argv[i], L"-uninstall") == 0 || _wcsicmp(argv[i], L"/uninstall") == 0)
        {
            uninstallMode = true;
        }
    }

    DWORD exitCode = 0;

    try
    {
        if (uninstallMode)
        {
            if (silentMode)
            {
                SilentUninstall();
            }
            else
            {
                Unregister(false);
                if (Is64BitSystem())
                    Unregister(true);
                ReportError(ERROR_SUCCESS);
            }
        }
        else if (silentMode)
        {
            SilentInstall(argc, argv);
        }
        else
        {
            DialogBoxParamW(hInstance, MAKEINTRESOURCEW(IDD_MAIN), nullptr, MainDlg, 0);
        }
    }
    catch (const std::system_error& ex)
    {
        exitCode = ex.code().value();
        if (!silentMode)
            ReportError(exitCode);
    }
    catch (const std::exception& ex)
    {
        if (!silentMode)
        {
            MessageBoxA(nullptr, ex.what(), "Error", MB_ICONERROR);
        }
        exitCode = E_FAIL;
    }

    LocalFree(argv);
    return exitCode;
}