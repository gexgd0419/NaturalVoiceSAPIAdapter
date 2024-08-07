#include "framework.h"
#include "resource.h"
#include <vector>
#include <string>
#include <memory>
#include "RegKey.h"

struct LocaleInfo
{
    LCID lcid;
    std::wstring name;
    std::wstring desc;
    std::wstring engdesc;
    std::wstring nativedesc;
    LocaleInfo(LCID lcid, LPCWSTR lang, LPCWSTR ctry, LPCWSTR desc, LPCWSTR engdesc, LPCWSTR nativedesc)
        : lcid(lcid),
        name(std::wstring(lang) + L'-' + ctry),
        desc(desc),
        engdesc(engdesc),
        nativedesc(nativedesc)
    {}
};

static std::vector<LocaleInfo> s_locales;
static std::vector<UINT> s_selectedLocaleIndices;

static BOOL CALLBACK EnumLocalesProc(LPWSTR lcidHexString)
{
    LCID lcid = wcstoul(lcidHexString, nullptr, 16);

    WCHAR lang[9], ctry[9];
    GetLocaleInfoW(lcid, LOCALE_SISO639LANGNAME, lang, 9);   // language e.g. en
    GetLocaleInfoW(lcid, LOCALE_SISO3166CTRYNAME, ctry, 9);  // country  e.g. US

    int cch = GetLocaleInfoW(lcid, LOCALE_SLOCALIZEDDISPLAYNAME, nullptr, 0);
    std::unique_ptr<WCHAR[]> desc(new WCHAR[cch]);
    GetLocaleInfoW(lcid, LOCALE_SLOCALIZEDDISPLAYNAME, desc.get(), cch);

    cch = GetLocaleInfoW(lcid, LOCALE_SENGLISHLANGUAGENAME, nullptr, 0);
    std::unique_ptr<WCHAR[]> engdesc(new WCHAR[cch]);
    GetLocaleInfoW(lcid, LOCALE_SENGLISHLANGUAGENAME, engdesc.get(), cch);

    cch = GetLocaleInfoW(lcid, LOCALE_SNATIVELANGUAGENAME, nullptr, 0);
    std::unique_ptr<WCHAR[]> nativedesc(new WCHAR[cch]);
    GetLocaleInfoW(lcid, LOCALE_SNATIVELANGUAGENAME, nativedesc.get(), cch);

    s_locales.emplace_back(lcid, lang, ctry, desc.get(), engdesc.get(), nativedesc.get());
    return TRUE;
}

static bool LCStrInStr(LCID locale, LPCWSTR str1, LPCWSTR str2)
{
    static constexpr DWORD LCMAP_FLAGS = LCMAP_LOWERCASE | LCMAP_LINGUISTIC_CASING | LCMAP_HIRAGANA | LCMAP_HALFWIDTH;
    int cch = LCMapStringW(locale, LCMAP_FLAGS, str1, -1, nullptr, 0);
    std::unique_ptr<WCHAR[]> str1_mapped(new WCHAR[cch]);
    LCMapStringW(locale, LCMAP_FLAGS, str1, -1, str1_mapped.get(), cch);
    cch = LCMapStringW(locale, LCMAP_FLAGS, str2, -1, nullptr, 0);
    std::unique_ptr<WCHAR[]> str2_mapped(new WCHAR[cch]);
    LCMapStringW(locale, LCMAP_FLAGS, str2, -1, str2_mapped.get(), cch);
    return wcsstr(str1_mapped.get(), str2_mapped.get()) != nullptr;
}

static bool MatchLocale(const std::vector<LPCWSTR>& searchTokens, const LocaleInfo& locale)
{
    LPWSTR pContext = nullptr;
    for (LPCWSTR token : searchTokens)
    {
        if (!LCStrInStr(LOCALE_INVARIANT, locale.name.c_str(), token)
            && !LCStrInStr(LOCALE_USER_DEFAULT, locale.desc.c_str(), token)
            && !LCStrInStr(LOCALE_INVARIANT, locale.engdesc.c_str(), token)
            && !LCStrInStr(locale.lcid, locale.nativedesc.c_str(), token))
        {
            return false;
        }
    }
    return true;
}

static void AddLangDlgRefreshList(HWND hDlg)
{
    HWND hList = GetDlgItem(hDlg, IDC_LANG_LIST);

    SendMessageW(hList, WM_SETREDRAW, FALSE, 0);
    SendMessageW(hList, LB_RESETCONTENT, 0, 0);

    HWND hEdit = GetDlgItem(hDlg, IDC_LANG_SEARCH);
    int len = GetWindowTextLengthW(hEdit);
    std::unique_ptr<WCHAR[]> pSearchText(new WCHAR[len + 1]());
    GetWindowTextW(hEdit, pSearchText.get(), len + 1);

    // Split search text into tokens by spaces
    LPWSTR pContext = nullptr;
    std::vector<LPCWSTR> tokens;
    for (LPWSTR pToken = wcstok_s(pSearchText.get(), L" ", &pContext);
        pToken != nullptr;
        pToken = wcstok_s(nullptr, L" ", &pContext))
    {
        tokens.push_back(pToken);
    }

    for (size_t i = 0; i < s_locales.size(); i++)
    {
        auto& locale = s_locales[i];
        if (MatchLocale(tokens, locale))
        {
            UINT index = SendMessageW(hList, LB_ADDSTRING, 0, (LPARAM)locale.desc.c_str());
            SendMessageW(hList, LB_SETITEMDATA, index, i);  // item data = locale index, because sorting is enabled
        }
    }

    SendMessageW(hList, WM_SETREDRAW, TRUE, 0);
    SendMessageW(hList, LB_SETSEL, 1, 0);  // Select the first item
}

static INT_PTR CALLBACK AddLangDlg(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);
    switch (message)
    {
    case WM_INITDIALOG:
        AddLangDlgRefreshList(hDlg);
        return TRUE;

    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case IDOK:
        {
            HWND hList = GetDlgItem(hDlg, IDC_LANG_LIST);
            s_selectedLocaleIndices.clear();
            UINT count = SendMessageW(hList, LB_GETCOUNT, 0, 0);
            for (UINT i = 0; i < count; i++)
            {
                if (SendMessageW(hList, LB_GETSEL, i, 0))
                {
                    // get selected locale index in item data
                    UINT index = SendMessageW(hList, LB_GETITEMDATA, i, 0);
                    s_selectedLocaleIndices.push_back(index);
                }
            }
            EndDialog(hDlg, IDOK);
            return TRUE;
        }
        case IDCANCEL:
            EndDialog(hDlg, IDCANCEL);
            return TRUE;

        case IDC_LANG_SEARCH:
            if (HIWORD(wParam) == EN_CHANGE)
            {
                AddLangDlgRefreshList(hDlg);
                return TRUE;
            }
            break;

        case IDC_LANG_LIST:
            if (HIWORD(wParam) == LBN_DBLCLK)
            {
                PostMessageW(hDlg, WM_COMMAND, MAKEWPARAM(IDOK, BN_CLICKED), 0);
            }
            break;
        }
        break;
    }
    return (INT_PTR)FALSE;
}

static size_t FindLocaleIndex(LPCWSTR locale)
{
    for (size_t i = 0; i < s_locales.size(); i++)
    {
        if (_wcsicmp(s_locales[i].name.c_str(), locale) == 0)
            return i;
    }
    return (size_t)-1;
}

static LRESULT CALLBACK LangListWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == WM_KEYDOWN && wParam == VK_DELETE)
    {
        PostMessageW(GetParent(hWnd), WM_COMMAND, MAKEWPARAM(IDC_LANG_REMOVE, BN_CLICKED), 0);
    }
    return CallWindowProcW((WNDPROC)GetWindowLongPtrW(hWnd, GWLP_USERDATA), hWnd, message, wParam, lParam);
}

static BOOL LangDlgInit(HWND hDlg)
{
    if (s_locales.empty())
        EnumSystemLocalesW(EnumLocalesProc, LCID_SUPPORTED);

    BOOL customEnabled = FALSE;
    HWND hList = GetDlgItem(hDlg, IDC_LANG_LIST);
    // Subclass the list box to handle Delete key press, and store the old wndproc in user data
    SetWindowLongPtrW(hList, GWLP_USERDATA,
        SetWindowLongPtrW(hList, GWLP_WNDPROC, (LONG_PTR)LangListWndProc));

    RegKey key;
    key.Open(HKEY_CURRENT_USER, L"Software\\NaturalVoiceSAPIAdapter\\Enumerator", KEY_QUERY_VALUE);
    if (key.GetDword(L"EdgeVoiceAllLanguages"))
    {
        CheckDlgButton(hDlg, IDC_LANG_ALL, BST_CHECKED);
    }
    else
    {
        auto languages = key.GetMultiStringList(L"EdgeVoiceLanguages");
        if (languages.empty())
        {
            CheckDlgButton(hDlg, IDC_LANG_SYS, BST_CHECKED);
        }
        else
        {
            customEnabled = TRUE;
            CheckDlgButton(hDlg, IDC_LANG_CUSTOM, BST_CHECKED);
            for (auto& language : languages)
            {
                size_t localeIndex = FindLocaleIndex(language.c_str());
                // If locale found, use its index and description
                // If not (-1), use the original text from registry, and set item data to -1
                UINT itemIndex = SendMessageW(hList, LB_ADDSTRING, 0,
                    (LPARAM)(localeIndex != (size_t)-1 ? s_locales[localeIndex].desc.c_str() : language.c_str()));
                SendMessageW(hList, LB_SETITEMDATA, itemIndex, localeIndex);
            }
        }
    }

    EnableWindow(GetDlgItem(hDlg, IDC_LANG_LIST), customEnabled);
    EnableWindow(GetDlgItem(hDlg, IDC_LANG_ADD), customEnabled);
    EnableWindow(GetDlgItem(hDlg, IDC_LANG_REMOVE), customEnabled);

    return TRUE;
}

static void LangDlgOnOK(HWND hDlg)
{
    RegKey key;
    key.Create(HKEY_CURRENT_USER, L"Software\\NaturalVoiceSAPIAdapter\\Enumerator", KEY_SET_VALUE);
    if (IsDlgButtonChecked(hDlg, IDC_LANG_ALL) == BST_CHECKED)
    {
        key.SetDword(L"EdgeVoiceAllLanguages", 1);
    }
    else if (IsDlgButtonChecked(hDlg, IDC_LANG_ALL) == BST_CHECKED)
    {
        key.SetDword(L"EdgeVoiceAllLanguages", 0);
        key.SetString(L"EdgeVoiceLanguages", L"\0", REG_MULTI_SZ);
    }
    else
    {
        key.SetDword(L"EdgeVoiceAllLanguages", 0);
        HWND hList = GetDlgItem(hDlg, IDC_LANG_LIST);
        UINT count = SendMessageW(hList, LB_GETCOUNT, 0, 0);
        std::vector<std::wstring> languages;
        for (UINT i = 0; i < count; i++)
        {
            size_t localeIndex = SendMessageW(hList, LB_GETITEMDATA, i, 0);
            if (localeIndex == (size_t)-1)  // -1: use the list item text
            {
                size_t cch = SendMessageW(hList, LB_GETTEXTLEN, i, 0);
                std::unique_ptr<WCHAR[]> text(new WCHAR[cch + 1]);
                SendMessageW(hList, LB_GETTEXT, i, (LPARAM)text.get());
                languages.emplace_back(text.get());
            }
            else
            {
                languages.push_back(s_locales[localeIndex].name);
            }
        }
        key.SetMultiStringList(L"EdgeVoiceLanguages", languages);
    }
}

INT_PTR CALLBACK LangDlg(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);
    switch (message)
    {
    case WM_INITDIALOG:
        return LangDlgInit(hDlg);

    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case IDOK:
            LangDlgOnOK(hDlg);
            EndDialog(hDlg, IDOK);
            return TRUE;
        case IDCANCEL:
            EndDialog(hDlg, IDCANCEL);
            return TRUE;

        case IDC_LANG_ADD:
            if (DialogBoxParamW(nullptr, MAKEINTRESOURCEW(IDD_ADDLANG), hDlg, AddLangDlg, 0) == IDOK)
            {
                HWND hList = GetDlgItem(hDlg, IDC_LANG_LIST);
                for (UINT localeIndex : s_selectedLocaleIndices)
                {
                    LPCWSTR text = s_locales[localeIndex].desc.c_str();
                    // If same item exists, skip it
                    if (SendMessageW(hList, LB_FINDSTRINGEXACT, -1, (LPARAM)text) != LB_ERR)
                        continue;
                    UINT itemIndex = SendMessageW(hList, LB_ADDSTRING, 0, (LPARAM)text);
                    SendMessageW(hList, LB_SETITEMDATA, itemIndex, localeIndex);
                }
            }
            break;

        case IDC_LANG_REMOVE:
        {
            HWND hList = GetDlgItem(hDlg, IDC_LANG_LIST);
            UINT count = SendMessageW(hList, LB_GETCOUNT, 0, 0);
            for (int i = count - 1; i >= 0; i--)
            {
                if (SendMessageW(hList, LB_GETSEL, i, 0))
                {
                    SendMessageW(hList, LB_DELETESTRING, i, 0);
                }
            }
            break;
        }

        case IDC_LANG_ALL:
        case IDC_LANG_SYS:
        case IDC_LANG_CUSTOM:
        {
            BOOL enabled = LOWORD(wParam) == IDC_LANG_CUSTOM;
            EnableWindow(GetDlgItem(hDlg, IDC_LANG_LIST), enabled);
            EnableWindow(GetDlgItem(hDlg, IDC_LANG_ADD), enabled);
            EnableWindow(GetDlgItem(hDlg, IDC_LANG_REMOVE), enabled);
            break;
        }
        }
        break;
    }
    return (INT_PTR)FALSE;
}