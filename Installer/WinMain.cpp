#include "Installer.h"

void Register(bool is64Bit);
void Unregister(bool is64Bit);
INT_PTR CALLBACK MainDlg(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_ LPWSTR    lpCmdLine,
    _In_ int       nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(nCmdShow);

    if (_wcsicmp(lpCmdLine, L"-uninstall") == 0)
    {
        try
        {
            Unregister(false);
            if (Is64BitSystem())
                Unregister(true);

            ReportError(ERROR_SUCCESS);
        }
        catch (const std::system_error& ex)
        {
            DWORD err = ex.code().value();
            ReportError(err);
            return err;
        }
    }
    else
    {
        DialogBoxParamW(hInstance, MAKEINTRESOURCEW(IDD_MAIN), nullptr, MainDlg, 0);
    }

    return 0;
}