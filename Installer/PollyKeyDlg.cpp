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

        // Populate engine combobox
        HWND hEngine = GetDlgItem(hDlg, IDC_POLLY_ENGINE);
        const LPCWSTR engines[] = { L"generative", L"neural", L"long-form", L"standard" };
        for (auto* e : engines)
            SendMessageW(hEngine, CB_ADDSTRING, 0, (LPARAM)e);

        std::wstring curEngine = key.GetString(L"PollyEngine");
        if (curEngine.empty()) curEngine = L"neural";
        int sel = (int)SendMessageW(hEngine, CB_FINDSTRINGEXACT, (WPARAM)-1, (LPARAM)curEngine.c_str());
        SendMessageW(hEngine, CB_SETCURSEL, sel >= 0 ? sel : 1 /*neural*/, 0);

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
            GetDlgItemTextW(hDlg, IDC_POLLY_ACCESS_KEY, buf, 512);
            key.SetString(L"PollyAccessKey", buf);
            GetDlgItemTextW(hDlg, IDC_POLLY_SECRET_KEY, buf, 512);
            key.SetString(L"PollySecretKey", buf);
            GetDlgItemTextW(hDlg, IDC_POLLY_REGION, buf, 512);
            key.SetString(L"PollyRegion", buf);

            HWND hEngine = GetDlgItem(hDlg, IDC_POLLY_ENGINE);
            int sel = (int)SendMessageW(hEngine, CB_GETCURSEL, 0, 0);
            if (sel >= 0)
            {
                SendMessageW(hEngine, CB_GETLBTEXT, sel, (LPARAM)buf);
                key.SetString(L"PollyEngine", buf);
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
                L"https://console.aws.amazon.com/iam/home#/users",
                nullptr, nullptr, SW_SHOWNORMAL);
            break;
        }
        break;
    }
    return (INT_PTR)FALSE;
}
