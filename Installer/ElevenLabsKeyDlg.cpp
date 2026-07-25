#include "framework.h"
#include "Installer.h"
#include "RegKey.h"

INT_PTR CALLBACK ElevenLabsKeyDlg(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);
    switch (message)
    {
    case WM_INITDIALOG:
    {
        RegKey key;
        key.Open(HKEY_CURRENT_USER, L"Software\\NaturalVoiceSAPIAdapter\\Enumerator", KEY_QUERY_VALUE);

        SetDlgItemTextW(hDlg, IDC_ELEVENLABS_API_KEY, key.GetString(L"ElevenLabsApiKey").c_str());

        // Populate model combobox
        HWND hModel = GetDlgItem(hDlg, IDC_ELEVENLABS_MODEL);
        const LPCWSTR models[] = {
            L"eleven_multilingual_v2",
            L"eleven_turbo_v2_5",
            L"eleven_turbo_v2",
            L"eleven_monolingual_v1",
        };
        for (auto* m : models)
            SendMessageW(hModel, CB_ADDSTRING, 0, (LPARAM)m);

        std::wstring curModel = key.GetString(L"ElevenLabsModel");
        if (curModel.empty()) curModel = L"eleven_multilingual_v2";
        int sel = (int)SendMessageW(hModel, CB_FINDSTRINGEXACT, (WPARAM)-1, (LPARAM)curModel.c_str());
        if (sel < 0)
        {
            // Model not in list — add it and select it
            sel = (int)SendMessageW(hModel, CB_ADDSTRING, 0, (LPARAM)curModel.c_str());
        }
        SendMessageW(hModel, CB_SETCURSEL, sel >= 0 ? sel : 0, 0);

        return TRUE;
    }

    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case IDOK:
        {
            RegKey key;
            key.Create(HKEY_CURRENT_USER, L"Software\\NaturalVoiceSAPIAdapter\\Enumerator", KEY_SET_VALUE);

            WCHAR buf[512];
            GetDlgItemTextW(hDlg, IDC_ELEVENLABS_API_KEY, buf, 512);
            key.SetString(L"ElevenLabsApiKey", buf);

            HWND hModel = GetDlgItem(hDlg, IDC_ELEVENLABS_MODEL);
            int sel = (int)SendMessageW(hModel, CB_GETCURSEL, 0, 0);
            if (sel >= 0)
            {
                SendMessageW(hModel, CB_GETLBTEXT, sel, (LPARAM)buf);
                key.SetString(L"ElevenLabsModel", buf);
            }

            EndDialog(hDlg, IDOK);
            return TRUE;
        }
        case IDCANCEL:
            EndDialog(hDlg, IDCANCEL);
            return TRUE;
        }
        break;

    case WM_NOTIFY:
        switch (((LPNMHDR)lParam)->code)
        {
        case NM_CLICK:
        case NM_RETURN:
            ShellExecuteW(nullptr, nullptr,
                L"https://elevenlabs.io/app/settings/api-keys",
                nullptr, nullptr, SW_SHOWNORMAL);
            break;
        }
        break;
    }
    return (INT_PTR)FALSE;
}
