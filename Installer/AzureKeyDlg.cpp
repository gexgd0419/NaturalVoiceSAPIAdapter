#include "framework.h"
#include "Installer.h"
#include "RegKey.h"

INT_PTR CALLBACK AzureKeyDlg(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);
    switch (message)
    {
    case WM_INITDIALOG:
    {
        RegKey key;
        key.Open(HKEY_CURRENT_USER, L"Software\\NaturalVoiceSAPIAdapter\\Enumerator", KEY_QUERY_VALUE);
        
        SetDlgItemTextW(hDlg, IDC_AZURE_KEY, key.GetString(L"AzureVoiceKey").c_str());
        SetDlgItemTextW(hDlg, IDC_AZURE_REGION, key.GetString(L"AzureVoiceRegion").c_str());

        return TRUE;
    }

    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case IDOK:
        {
            RegKey key;
            key.Create(HKEY_CURRENT_USER, L"Software\\NaturalVoiceSAPIAdapter\\Enumerator", KEY_SET_VALUE);

            WCHAR buf[256];
            GetDlgItemTextW(hDlg, IDC_AZURE_KEY, buf, 256);
            key.SetString(L"AzureVoiceKey", buf);
            GetDlgItemTextW(hDlg, IDC_AZURE_REGION, buf, 256);
            key.SetString(L"AzureVoiceRegion", buf);

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
            ShellExecuteW(nullptr, nullptr, L"https://portal.azure.com/", nullptr, nullptr, SW_SHOWNORMAL);
            break;
        }
        break;
    }
    return (INT_PTR)FALSE;
}