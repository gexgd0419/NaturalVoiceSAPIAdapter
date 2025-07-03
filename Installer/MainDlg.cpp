#include "framework.h"
#include "Installer.h"
#include "RegKey.h"
#include <algorithm>
#include <shlobj.h>
#pragma comment (lib, "shell32.lib")

INT_PTR CALLBACK AboutDlg(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
INT_PTR CALLBACK LangDlg(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
INT_PTR CALLBACK AzureKeyDlg(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
void Register(bool is64Bit);
void Unregister(bool is64Bit);

static void CheckInstallation(bool is64Bit, HWND hDlg, UINT idStatic, UINT idUninstallBtn)
{
    WCHAR szText[256];

    if (is64Bit && !Is64BitSystem())
        return;

    uint64_t ver = GetFileVersion(GetInstalledPath(is64Bit).c_str());
    
    if (ver != (uint64_t)-1)
    {
        swprintf_s(szText, LoadResString(IDS_INSTALLED).c_str(),
            HIWORD(ver >> 32), LOWORD(ver >> 32),
            HIWORD(ver), LOWORD(ver));
        SetDlgItemTextW(hDlg, idStatic, szText);
        EnableWindow(GetDlgItem(hDlg, idUninstallBtn), TRUE);
    }
    else
    {
        SetDlgItemTextW(hDlg, idStatic, LoadResString(IDS_NOT_INSTALLED).c_str());
        HWND hBtn = GetDlgItem(hDlg, idUninstallBtn);
        if (GetFocus() == hBtn)
            SetFocus(GetNextDlgTabItem(hDlg, hBtn, FALSE));
        EnableWindow(hBtn, FALSE);
    }
}

static void UpdateEnableStates(HWND hDlg)
{
    BOOL localEnabled = IsWindowEnabled(GetDlgItem(hDlg, IDC_CHK_NARRATOR_VOICES))
        && IsDlgButtonChecked(hDlg, IDC_CHK_NARRATOR_VOICES) == BST_CHECKED;
    EnableWindow(GetDlgItem(hDlg, IDC_STATIC_LOCAL_VOICE_PATH), localEnabled);
    EnableWindow(GetDlgItem(hDlg, IDC_LOCAL_VOICE_PATH), localEnabled);
    EnableWindow(GetDlgItem(hDlg, IDC_BROWSE_LOCAL_VOICE), localEnabled);
    BOOL edgeEnabled = IsDlgButtonChecked(hDlg, IDC_CHK_EDGE_VOICES) == BST_CHECKED;
    BOOL azureEnabled = IsDlgButtonChecked(hDlg, IDC_CHK_AZURE_VOICES) == BST_CHECKED;
    EnableWindow(GetDlgItem(hDlg, IDC_SET_AZURE_KEY), azureEnabled);
    BOOL onlineEnabled = edgeEnabled || azureEnabled;
    EnableWindow(GetDlgItem(hDlg, IDC_STATIC_INCLUDED_LANGUAGES), onlineEnabled);
    EnableWindow(GetDlgItem(hDlg, IDC_INCLUDED_LANGUAGES), onlineEnabled);
    EnableWindow(GetDlgItem(hDlg, IDC_CHANGE_LANGUAGES), onlineEnabled);
}

static void UpdateDisplay(HWND hDlg)
{
    CheckInstallation(false, hDlg, IDC_STATIC_32BIT_STATUS, IDC_UNINSTALL_32BIT);
    CheckInstallation(true, hDlg, IDC_STATIC_64BIT_STATUS, IDC_UNINSTALL_64BIT);

    RegKey key = RegOpenEnumeratorConfigKey();
    CheckDlgButton(hDlg, IDC_CHK_NARRATOR_VOICES,
        key.GetDword(L"NoNarratorVoices") ? BST_UNCHECKED : BST_CHECKED);
    CheckDlgButton(hDlg, IDC_CHK_EDGE_VOICES,
        key.GetDword(L"NoEdgeVoices") ? BST_UNCHECKED : BST_CHECKED);
    CheckDlgButton(hDlg, IDC_CHK_AZURE_VOICES,
        key.GetDword(L"NoAzureVoices")
        || (key.GetString(L"AzureVoiceKey").empty() && key.GetString(L"AzureVoiceRegion").empty())
        ? BST_UNCHECKED : BST_CHECKED);
    SetDlgItemTextW(hDlg, IDC_LOCAL_VOICE_PATH, key.GetString(L"NarratorVoicePath").c_str());

    if (key.GetDword(L"EdgeVoiceAllLanguages"))
    {
        SetDlgItemTextW(hDlg, IDC_INCLUDED_LANGUAGES, LoadResString(IDS_LANGUAGES_ALL).c_str());
    }
    else
    {
        std::wstring languages = key.GetString(L"EdgeVoiceLanguages");
        if (languages.empty() || (languages.size() == 1 && languages[0] == L'\0'))
        {
            SetDlgItemTextW(hDlg, IDC_INCLUDED_LANGUAGES, LoadResString(IDS_LANGUAGES_FOLLOW_SYSTEM).c_str());
        }
        else
        {
            if (languages.back() == L'\0')
                languages.pop_back();
            std::replace(languages.begin(), languages.end(), L'\0', L',');
            SetDlgItemTextW(hDlg, IDC_INCLUDED_LANGUAGES, languages.c_str());
        }
    }

    key = RegOpenConfigKey();

    HWND hLogLevelCombo = GetDlgItem(hDlg, IDC_LOG_LEVEL);
    WCHAR logLevels[64];
    LoadStringW(nullptr, IDS_LOG_LEVELS, logLevels, 64);
    LPWSTR pContext = nullptr;
    for (LPCWSTR level = wcstok_s(logLevels, L"\t", &pContext);
        level != nullptr;
        level = wcstok_s(nullptr, L"\t", &pContext))
    {
        SendMessageW(hLogLevelCombo, CB_ADDSTRING, 0, (LPARAM)level);
    }
    SendMessageW(hLogLevelCombo, CB_SETCURSEL,
        std::clamp<DWORD>(key.GetDword(L"LogLevel", 2), 0, 6), 0);

    UpdateEnableStates(hDlg);
}

static BOOL MainDlgInit(HWND hDlg)
{
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    if (!IsAdmin())
    {
        if (SupportsUAC())
        {
            SendDlgItemMessageW(hDlg, IDC_INSTALL_32BIT, BCM_SETSHIELD, 0, TRUE);
            SendDlgItemMessageW(hDlg, IDC_INSTALL_64BIT, BCM_SETSHIELD, 0, TRUE);
            SendDlgItemMessageW(hDlg, IDC_UNINSTALL_32BIT, BCM_SETSHIELD, 0, TRUE);
            SendDlgItemMessageW(hDlg, IDC_UNINSTALL_64BIT, BCM_SETSHIELD, 0, TRUE);
        }
    }
    if (!Is64BitSystem())
    {
        ShowWindow(GetDlgItem(hDlg, IDC_STATIC_64BIT_HEADER), SW_HIDE);
        ShowWindow(GetDlgItem(hDlg, IDC_STATIC_64BIT_STATUS), SW_HIDE);
        ShowWindow(GetDlgItem(hDlg, IDC_INSTALL_64BIT), SW_HIDE);
        ShowWindow(GetDlgItem(hDlg, IDC_UNINSTALL_64BIT), SW_HIDE);
    }

    if (SupportsInstallingNarratorVoices() || IsWindows7OrGreater() || RegOpenConfigKey().GetDword(L"ForceEnableAzureSpeechSDK"))
    {
        SetDlgItemTextW(hDlg, IDC_NARRATOR_VOICE_TIP_LINKS, LoadResString(IDS_LOCAL_VOICE_PATH_TIP_WIN7).c_str());
    }
    else
    {
        SetDlgItemTextW(hDlg, IDC_NARRATOR_VOICE_TIP_LINKS, LoadResString(IDS_LOCAL_VOICE_PATH_TIP_NOSUPPORT).c_str());
        EnableWindow(GetDlgItem(hDlg, IDC_CHK_NARRATOR_VOICES), FALSE);
    }

    UpdateDisplay(hDlg);

    return TRUE;
}

static void SaveChanges(HWND hDlg)
{
    RegKey key;

    key.Create(HKEY_CURRENT_USER, L"Software\\NaturalVoiceSAPIAdapter", KEY_SET_VALUE);
    key.SetDword(L"LogLevel", SendDlgItemMessageW(hDlg, IDC_LOG_LEVEL, CB_GETCURSEL, 0, 0));

    key.Create(HKEY_CURRENT_USER, L"Software\\NaturalVoiceSAPIAdapter\\Enumerator", KEY_SET_VALUE);
    key.SetDword(L"NoNarratorVoices", IsDlgButtonChecked(hDlg, IDC_CHK_NARRATOR_VOICES) == BST_UNCHECKED);
    key.SetDword(L"NoEdgeVoices", IsDlgButtonChecked(hDlg, IDC_CHK_EDGE_VOICES) == BST_UNCHECKED);
    key.SetDword(L"NoAzureVoices", IsDlgButtonChecked(hDlg, IDC_CHK_AZURE_VOICES) == BST_UNCHECKED);
    WCHAR path[MAX_PATH];
    GetDlgItemTextW(hDlg, IDC_LOCAL_VOICE_PATH, path, MAX_PATH);
    key.SetString(L"NarratorVoicePath", path);
}

INT_PTR CALLBACK MainDlg(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    try
    {
        switch (message)
        {
        case WM_INITDIALOG:
            return MainDlgInit(hDlg);

        case WM_COMMAND:
            switch (LOWORD(wParam))
            {
            case IDOK:
            case IDCANCEL:
                SaveChanges(hDlg);
                EndDialog(hDlg, LOWORD(wParam));
                return TRUE;

            case IDC_ABOUT:
                DialogBoxParamW(nullptr, MAKEINTRESOURCEW(IDD_ABOUTBOX), hDlg, AboutDlg, 0);
                break;

            case IDC_INSTALL_32BIT:
                SaveChanges(hDlg);
                Register(false);
                ShowMessageBox(IDS_INSTALL_COMPLETE, MB_ICONINFORMATION);
                UpdateDisplay(hDlg);
                break;
            case IDC_INSTALL_64BIT:
                SaveChanges(hDlg);
                Register(true);
                ShowMessageBox(IDS_INSTALL_COMPLETE, MB_ICONINFORMATION);
                UpdateDisplay(hDlg);
                break;
            case IDC_UNINSTALL_32BIT:
                SaveChanges(hDlg);
                Unregister(false);
                ReportError(ERROR_SUCCESS);
                UpdateDisplay(hDlg);
                break;
            case IDC_UNINSTALL_64BIT:
                SaveChanges(hDlg);
                Unregister(true);
                ReportError(ERROR_SUCCESS);
                UpdateDisplay(hDlg);
                break;

            case IDC_CHK_NARRATOR_VOICES:
            case IDC_CHK_EDGE_VOICES:
            case IDC_CHK_AZURE_VOICES:
                UpdateEnableStates(hDlg);
                SaveChanges(hDlg);
                break;

            case IDC_LOG_LEVEL:
                if (HIWORD(wParam) == CBN_SELCHANGE)
                    SaveChanges(hDlg);
                break;

            case IDC_SET_AZURE_KEY:
                DialogBoxParamW(nullptr, MAKEINTRESOURCEW(IDD_AZUREKEY), hDlg, AzureKeyDlg, 0);
                break;
            case IDC_CHANGE_LANGUAGES:
                DialogBoxParamW(nullptr, MAKEINTRESOURCEW(IDD_LANG), hDlg, LangDlg, 0);
                UpdateDisplay(hDlg);
                break;

            case IDC_BROWSE_LOCAL_VOICE:
            {
                BROWSEINFOW bi = {};
                bi.hwndOwner = hDlg;
                bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
                constexpr auto CoDeleter = [](LPVOID p) { CoTaskMemFree(p); };
                std::unique_ptr<ITEMIDLIST, decltype(CoDeleter)> pidl(SHBrowseForFolderW(&bi), CoDeleter);
                if (pidl)
                {
                    WCHAR path[MAX_PATH];
                    if (SHGetPathFromIDListW(pidl.get(), path))
                    {
                        SetDlgItemTextW(hDlg, IDC_LOCAL_VOICE_PATH, path);
                        std::wstring_view sv = path;
                        if (!std::all_of(sv.begin(), sv.end(), [](wchar_t ch) { return ch < 128; }))
                        {
                            ShowMessageBox(IDS_LOCAL_VOICE_PATH_NONASCII, MB_ICONEXCLAMATION);
                        }
                    }
                    SaveChanges(hDlg);
                }
                break;
            }
            case IDC_OPEN_CACHE_FOLDER:
            {
                WCHAR path[MAX_PATH];
                if (SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, path) == S_OK
                    && PathAppendW(path, L"NaturalVoiceSAPIAdapter"))
                {
                    ShellExecuteW(hDlg, nullptr, path, nullptr, nullptr, SW_SHOWNORMAL);
                }
                break;
            }
            }
            break;

        case WM_NOTIFY:
            switch (((LPNMHDR)lParam)->code)
            {
            case NM_CLICK:
            case NM_RETURN:
            {
                LITEM& item = ((PNMLINK)lParam)->item;
                if (wcscmp(item.szID, L"narrator") == 0)
                    ShellExecuteW(hDlg, nullptr, L"ms-settings:easeofaccess-narrator", nullptr, nullptr, SW_SHOWNORMAL);
                else if (wcscmp(item.szID, L"download") == 0)
                    ShellExecuteW(hDlg, nullptr, L"https://github.com/gexgd0419/NaturalVoiceSAPIAdapter/wiki/Narrator-natural-voice-download-links",
                        nullptr, nullptr, SW_SHOWNORMAL);
                else if (wcscmp(item.szID, L"download-zh") == 0)
                    ShellExecuteW(hDlg, nullptr,
                        L"https://github.com/gexgd0419/NaturalVoiceSAPIAdapter/wiki/%E8%AE%B2%E8%BF%B0%E4%BA%BA%E8%87%AA%E7%84%B6%E8%AF%AD%E9%9F%B3%E4%B8%8B%E8%BD%BD%E9%93%BE%E6%8E%A5",
                        nullptr, nullptr, SW_SHOWNORMAL);
                break;
            }
            }
        }
    }
    catch (const std::bad_alloc&)
    {
        MessageBoxW(nullptr, L"Out of memory", L"Error", MB_ICONERROR | MB_SYSTEMMODAL);
        throw;
    }
    catch (const std::system_error& ex)
    {
        ReportError(ex.code().value());
    }
    catch (const std::exception& ex)
    {
        ShowMessageBox(ex.what(), MB_ICONEXCLAMATION);
    }
    return (INT_PTR)FALSE;
}
