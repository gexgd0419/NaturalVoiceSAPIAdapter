#include "framework.h"
#include "Installer.h"
#include "RegKey.h"

INT_PTR CALLBACK PollyKeyDlg(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);
    switch (message)
    {
    case WM_INITDIALOG:
    {
        RegKey key;
        key.Open(HKEY_CURRENT_USER, L"Software\\NaturalVoiceSAPIAdapter\\Enumerator", KEY_QUERY_VALUE);

        SetDlgItemTextW(hDlg, IDC_POLLY_ACCESS_KEY, key.GetString(L"PollyAccessKey").c_str());
        SetDlgItemTextW(hDlg, IDC_POLLY_SECRET_KEY, key.GetString(L"PollySecretKey").c_str());
        SetDlgItemTextW(hDlg, IDC_POLLY_REGION,     key.GetString(L"PollyRegion").c_str());
        SetDlgItemTextW(hDlg, IDC_POLLY_ENGINE, key.GetString(L"PollyEngine").c_str());

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
            WCHAR engine[512];
            GetDlgItemTextW(hDlg, IDC_POLLY_ENGINE, engine, 512);
            if (!engine[0])
            {
                MessageBoxW(hDlg,
                    L"Engine is required. Enter an engine name from the current Amazon Polly documentation.",
                    L"Amazon Polly keys", MB_ICONWARNING | MB_OK);
                return TRUE;
            }

            GetDlgItemTextW(hDlg, IDC_POLLY_ACCESS_KEY, buf, 512);
            key.SetString(L"PollyAccessKey", buf);
            GetDlgItemTextW(hDlg, IDC_POLLY_SECRET_KEY, buf, 512);
            key.SetString(L"PollySecretKey", buf);
            GetDlgItemTextW(hDlg, IDC_POLLY_REGION, buf, 512);
            key.SetString(L"PollyRegion", buf);
            key.SetString(L"PollyEngine", engine);

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
                L"https://console.aws.amazon.com/iam/home#/users",
                nullptr, nullptr, SW_SHOWNORMAL);
            break;
        }
        break;
    }
    return (INT_PTR)FALSE;
}
