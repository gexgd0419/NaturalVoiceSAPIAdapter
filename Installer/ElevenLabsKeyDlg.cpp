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
        SetDlgItemTextW(hDlg, IDC_ELEVENLABS_MODEL, key.GetString(L"ElevenLabsModel").c_str());

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
            WCHAR model[512];
            GetDlgItemTextW(hDlg, IDC_ELEVENLABS_MODEL, model, 512);
            if (!model[0])
            {
                MessageBoxW(hDlg,
                    L"Model ID is required. Enter a model ID from the current ElevenLabs documentation.",
                    L"ElevenLabs key", MB_ICONWARNING | MB_OK);
                return TRUE;
            }

            GetDlgItemTextW(hDlg, IDC_ELEVENLABS_API_KEY, buf, 512);
            key.SetString(L"ElevenLabsApiKey", buf);
            key.SetString(L"ElevenLabsModel", model);

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
