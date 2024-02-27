// VoiceTokenEnumerator.cpp: CVoiceTokenEnumerator 的实现
#include "pch.h"
#include "VoiceTokenEnumerator.h"
#include <winrt/windows.management.deployment.h>
#include <winrt/windows.applicationmodel.h>
#include <winrt/windows.foundation.collections.h>
#include <VersionHelpers.h>
#include "SpeechServiceConstants.h"
#include "NetUtils.h"
#include "StringTokenizer.h"
#include "LangUtils.h"
#include <condition_variable>


// CVoiceTokenEnumerator

inline static void CheckHr(HRESULT hr)
{
    if (FAILED(hr))
        throw std::system_error(hr, std::system_category());
}

static CComPtr<IEnumSpObjectTokens> s_pCachedEnum = nullptr;
static std::mutex s_cacheMutex;
static std::jthread s_cacheClearThread;

HRESULT CVoiceTokenEnumerator::FinalConstruct()
{
    // Some programs assume that creating an enumerator is a low-cost operation,
    // and re-create enumerators frequently during eumeration.
    // Here we try to cache the created tokens for a short period (10 seconds) to improve performance

    std::lock_guard lock(s_cacheMutex);
    if (s_pCachedEnum)
        return s_pCachedEnum->Clone(&m_pEnum);

    CComPtr<ISpObjectTokenEnumBuilder> pEnumBuilder;
    RETONFAIL(pEnumBuilder.CoCreateInstance(CLSID_SpObjectTokenEnum));
    RETONFAIL(pEnumBuilder->SetAttribs(nullptr, nullptr));

    try
    {
        CComPtr<IEnumSpObjectTokens> pLocalEnum = EnumLocalVoices();
        RETONFAIL(pEnumBuilder->AddTokensFromTokenEnum(pLocalEnum));
    }
    catch (const std::bad_alloc&) { return E_OUTOFMEMORY; }
    catch (...) { }

    try
    {
        CComPtr<IEnumSpObjectTokens> pEdgeEnum = EnumEdgeVoices();
        RETONFAIL(pEnumBuilder->AddTokensFromTokenEnum(pEdgeEnum));
    }
    catch (const std::bad_alloc&) { return E_OUTOFMEMORY; }
    catch (...) { }

    CComPtr<IEnumSpObjectTokens> temp;
    RETONFAIL(pEnumBuilder->QueryInterface(&temp));
    s_pCachedEnum = std::move(temp);
    // discard the cache after 10 seconds
    s_cacheClearThread = std::jthread([](std::stop_token token)
        {
            std::condition_variable_any cv;
            std::mutex mtx;
            std::unique_lock cvlock(mtx);
            cv.wait_for(cvlock, token, std::chrono::seconds(10), []() { return false; });
            std::lock_guard cachelock(s_cacheMutex);
            s_pCachedEnum.Release();
        }
    );

    return pEnumBuilder->QueryInterface(&m_pEnum);
}

static CComPtr<ISpDataKey> MakeVoiceKey(StringPairCollection&& values, SubkeyCollection&& subkeys)
{
    CComPtr<ISpDataKey> pKey;
    CheckHr(CVoiceKey::_CreatorClass::CreateInstance(nullptr, IID_ISpDataKey, reinterpret_cast<LPVOID*>(&pKey)));
    CComQIPtr<IDataKeyInit>(pKey)->InitKey(std::move(values), std::move(subkeys));
    return pKey;
}

static CComPtr<ISpObjectToken> MakeVoiceToken(LPCWSTR lpszPath, StringPairCollection&& values, SubkeyCollection&& subkeys)
{
    CComPtr<ISpObjectToken> pToken;
    CheckHr(CVoiceToken::_CreatorClass::CreateInstance(nullptr, IID_ISpObjectToken, reinterpret_cast<LPVOID*>(&pToken)));
    auto pInit = CComQIPtr<IDataKeyInit>(pToken);
    pInit->InitKey(std::move(values), std::move(subkeys));
    pInit->SetPath(lpszPath);
    return pToken;
}

static std::wstring LanguageIDsFromLocaleName(std::wstring_view locale)
{
    LANGID lang = LangIDFromLocaleName(locale);
    if (lang == 0)
        return {};

    std::wstring ret = LangIDToHexLang(lang);

    LANGID neutralLang = GetLangIDFallback(lang);
    if (neutralLang != lang)
    {
        ret += L';';
        ret += LangIDToHexLang(neutralLang);
    }

    return ret;
}

// "Microsoft Aria (Natural) - English (United States)" to "Microsoft Aria"
static void TrimVoiceName(std::wstring& longName)
{
    LPCWSTR pStr = longName.c_str();
    LPCWSTR pCh = pStr;
    while (*pCh && !iswpunct(*pCh)) // Go to the first punctuation: '(', '-', etc.
        pCh++;
    if (pCh != pStr) // we advanced at least one character
    {
        pCh--; // Back to the space before punctuation
        while (pCh != pStr && iswspace(*pCh)) // Remove the spaces
            pCh--;
        if (pCh != pStr) // If not trimmed to the starting point
            longName.erase(pCh - pStr + 1); // Trim the string
    }
}

static CComPtr<ISpObjectToken> MakeLocalVoiceToken(const VoiceInfo& voiceInfo)
{
    using namespace Microsoft::CognitiveServices::Speech;

    // Path format: C:\Program Files\WindowsApps\MicrosoftWindows.Voice.en-US.Aria.1_1.0.8.0_x64__cw5n1h2txyewy/
    std::wstring path = UTF8ToWString(voiceInfo.VoicePath);
    if (path.back() == '/' || path.back() == '\\')
        path.erase(path.size() - 1); // Remove the trailing slash
    // from the last backslash '\' to the first underscore '_'
    size_t name_start = path.rfind('\\');
    if (name_start == path.npos)
        name_start = 0;
    else
        name_start++;
    size_t name_end = path.find('_', name_start);
    if (name_end == path.npos)
        name_end = path.size();
    std::wstring name = path.substr(name_start, name_end - name_start);

    std::wstring friendlyName = UTF8ToWString(voiceInfo.Name);
    std::wstring shortFriendlyName = friendlyName;
    TrimVoiceName(shortFriendlyName);

    std::wstring localeName = UTF8ToWString(voiceInfo.Locale);

    return MakeVoiceToken(
        name.c_str(),
        StringPairCollection {
            { L"", friendlyName },
            { L"CLSID", L"{013AB33B-AD1A-401C-8BEE-F6E2B046A94E}" }
        },
        SubkeyCollection {
            { L"Attributes", MakeVoiceKey(
                StringPairCollection {
                    { L"Name", shortFriendlyName },
                    { L"Gender", UTF8ToWString(voiceInfo.Properties.GetProperty("Gender")) },
                    { L"Language", LanguageIDsFromLocaleName(localeName) },
                    { L"Locale", localeName },
                    { L"Vendor", L"Microsoft" },
                    { L"NaturalVoiceType", L"Narrator;Local" }
                },
                SubkeyCollection {}
            ) },
            { L"NaturalVoiceConfig", MakeVoiceKey(
                StringPairCollection {
                    { L"ErrorMode", L"0" },
                    { L"Path", path },
                    { L"Key", MS_TTS_KEY }
                },
                SubkeyCollection {}
            ) }
        }
    );
}

CComPtr<IEnumSpObjectTokens> CVoiceTokenEnumerator::EnumLocalVoices()
{
    CComPtr<ISpObjectTokenEnumBuilder> pEnumBuilder;
    CheckHr(pEnumBuilder.CoCreateInstance(CLSID_SpObjectTokenEnum));
    CheckHr(pEnumBuilder->SetAttribs(nullptr, nullptr));

    // Possible exceptions:
    // - winrt::hresult_class_not_available
    //     when running on a Windows version with no WinRT support, such as Windows 7
    // - DllDelayLoadError
    //     when Speech SDK DLL can't be loaded
    // Let those be handled by the caller

    // Get all package paths, and then load all voices in one call
    // Because each EmbeddedSpeechConfig::FromPath() can reload some DLLs in some situations,
    // slowing down the enumeration process as more voices are installed

    auto packages = winrt::Windows::Management::Deployment::PackageManager().FindPackagesForUser(L"");
    std::vector<std::string> paths;
    for (auto package : packages)
    {
        if (package.Id().Name().starts_with(L"MicrosoftWindows.Voice."))
            paths.push_back(WStringToUTF8(package.InstalledPath()));
    }
    auto config = EmbeddedSpeechConfig::FromPaths(paths);
    auto synthesizer = SpeechSynthesizer::FromConfig(config, nullptr);
    auto result = synthesizer->GetVoicesAsync().get();
    for (auto& info : result->Voices)
    {
        auto pToken = MakeLocalVoiceToken(*info);
        pEnumBuilder->AddTokens(1, &pToken.p);
    }

    return CComQIPtr<IEnumSpObjectTokens>(pEnumBuilder);
}

static CComPtr<ISpObjectToken> MakeEdgeVoiceToken(const nlohmann::json& json)
{
    std::wstring shortName = UTF8ToWString(json.at("ShortName"));

    std::wstring friendlyName = UTF8ToWString(json.at("FriendlyName"));
    std::wstring shortFriendlyName = friendlyName;
    TrimVoiceName(shortFriendlyName);

    std::wstring localeName = UTF8ToWString(json.at("Locale"));

    return MakeVoiceToken(
        (L"Edge-" + shortName).c_str(), // registry key name format: Edge-en-US-AriaNeural
        StringPairCollection {
            { L"", friendlyName },
            { L"CLSID", L"{013AB33B-AD1A-401C-8BEE-F6E2B046A94E}" }
        },
        SubkeyCollection {
            { L"Attributes", MakeVoiceKey(
                StringPairCollection {
                    { L"Name", shortFriendlyName },
                    { L"Gender", UTF8ToWString(json.at("Gender")) },
                    { L"Language", LanguageIDsFromLocaleName(localeName) },
                    { L"Locale", localeName },
                    { L"Vendor", L"Microsoft" },
                    { L"NaturalVoiceType", L"Edge;Cloud" }
                },
                SubkeyCollection {}
            ) },
            { L"NaturalVoiceConfig", MakeVoiceKey(
                StringPairCollection {
                    { L"ErrorMode", L"0" },
                    { L"WebsocketURL", EDGE_WEBSOCKET_URL },
                    { L"Voice", shortName },
                    { L"IsEdgeVoice", L"1" }
                },
                SubkeyCollection {}
            ) }
        }
    );
}

static std::string ReadTextFromFile(HANDLE hFile)
{
    LARGE_INTEGER liSize = { 0 };
    if (!GetFileSizeEx(hFile, &liSize))
        throw std::system_error(GetLastError(), std::system_category());
    if (liSize.QuadPart > (1LL << 20) * 64)
        throw std::bad_alloc(); // throw if more than 64MB chars
    std::string str(liSize.LowPart, 0);
    str.resize_and_overwrite(liSize.LowPart, [hFile](char* buf, size_t size)
        {
            DWORD readlen;
            SetFilePointer(hFile, 0, nullptr, FILE_BEGIN);
            if (!ReadFile(hFile, buf, (DWORD)size, &readlen, nullptr)
                || size != readlen)
                throw std::system_error(GetLastError(), std::system_category());
            return readlen;
        });
    return str;
}

static bool ReplaceCacheFileContent(const std::string& data) noexcept
{
    // Save the content to a new temporary file first, then replace the cache file
    // But if the cache file does not exist, create the cache file directly
    WCHAR szPath[MAX_PATH], szTempFilePath[MAX_PATH];
    GetTempPathW(MAX_PATH, szTempFilePath);
    PathAppendW(szTempFilePath, L"EdgeVoiceListCache.json");
    BOOL cacheExists = PathFileExistsW(szTempFilePath);
    if (cacheExists)
    {
        // Recalculate a temp file name
        GetTempPathW(MAX_PATH, szPath);
        GetTempFileNameW(szPath, L"EVL", 0, szTempFilePath);
    }

    HANDLE hFile = CreateFileW(szTempFilePath, GENERIC_READ | GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (hFile == INVALID_HANDLE_VALUE)
        return false;
    DWORD writelen = 0;
    WriteFile(hFile, data.data(), (DWORD)data.size(), &writelen, nullptr);
    CloseHandle(hFile);
    if (writelen != data.size())
    {
        DeleteFileW(szTempFilePath);
        return false;
    }

    if (cacheExists)
    {
        // Replace the cache file with the temporary file
        PathAppendW(szPath, L"EdgeVoiceListCache.json");
        if (!ReplaceFileW(szPath, szTempFilePath, nullptr, 0, nullptr, nullptr))
        {
            DeleteFileW(szTempFilePath); // delete the temporary file
            return false;
        }
    }
    return true;
}

static nlohmann::json GetEdgeVoiceJson()
{
    // We use a file %TEMP%\EdgeVoiceListCache.json to cache the Edge voice list JSON locally
    // so enumerating voice tokens will not always be so slow
    // Only download JSON synchronously if the cache file does not exist (or cannot be read)
    // otherwise, always fetch from cache, then download and update the cache asynchronously

    WCHAR szPath[MAX_PATH];
    GetTempPathW(MAX_PATH, szPath);
    PathAppendW(szPath, L"EdgeVoiceListCache.json");
    HANDLE hCacheFile = CreateFileW(szPath, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (hCacheFile != INVALID_HANDLE_VALUE)
    {
        FILETIME ftNow;
        GetSystemTimeAsFileTime(&ftNow);
        FILETIME ftWrite;
        GetFileTime(hCacheFile, nullptr, &ftWrite, nullptr);
        ULARGE_INTEGER uiNow = { ftNow.dwLowDateTime, ftNow.dwHighDateTime };
        ULARGE_INTEGER uiWrite = { ftWrite.dwLowDateTime, ftWrite.dwHighDateTime };

        try
        {
            // First try to read from cache file, whether it is outdated or not
            // because it is always faster
            // Can fail because of file read error or JSON parse error
            auto json = nlohmann::json::parse(ReadTextFromFile(hCacheFile));
            CloseHandle(hCacheFile);

            // Cache voice list for an hour
            // If the cache is outdated, update it in a background thread
            if (uiNow.QuadPart < uiWrite.QuadPart ||
                uiNow.QuadPart - uiWrite.QuadPart > 10000000ULL * 60 * 60)
            {
                std::thread([]()
                {
                    try
                    {
                        std::string downloaded = DownloadToString(EDGE_VOICE_LIST_URL, nullptr);
                        (void)nlohmann::json::parse(downloaded); // check if valid json
                        ReplaceCacheFileContent(downloaded);
                    }
                    catch (...)
                    { }
                }).detach(); // detach the thread to fire-and-forget
            }

            return json; // return JSON from cache
        }
        catch (...)
        {
            // We somehow failed to read from cache
            // so we have to re-download synchronously, in the code below
            CloseHandle(hCacheFile);
        }
    }

    // Failed to read from cache, or the cache does not exist
    // so we have to download synchronously
    std::string downloaded = DownloadToString(EDGE_VOICE_LIST_URL, nullptr);
    auto json = nlohmann::json::parse(downloaded);
    ReplaceCacheFileContent(downloaded);
    return json;
}

// Enumerate all language IDs of installed phoneme converters
static std::set<LANGID> GetSupportedLanguageIDs()
{
    std::set<LANGID> langids;
    CComPtr<IEnumSpObjectTokens> pEnum;
    if (FAILED(SpEnumTokens(SPCAT_PHONECONVERTERS, nullptr, nullptr, &pEnum)))
        return {};
    for (CComPtr<ISpObjectToken> pToken; pEnum->Next(1, &pToken, nullptr) == S_OK; pToken.Release())
    {
        CComPtr<ISpDataKey> pKey;
        if (FAILED(pToken->OpenKey(SPTOKENKEY_ATTRIBUTES, &pKey)))
            continue;
        CSpDynamicString languages;
        if (FAILED(pKey->GetStringValue(L"Language", &languages)))
            continue;

        for (auto& langstr : TokenizeString(std::wstring_view(languages.m_psz), L';'))
        {
            langids.insert(HexLangToLangID(langstr));
        }
    }
    return langids;
}

static bool IsUniversalPhoneConverterSupported()
{
    CComPtr<ISpPhoneConverter> converter;
    if (FAILED(converter.CoCreateInstance(CLSID_SpPhoneConverter)))
        return false;
    CComPtr<ISpPhoneticAlphabetSelection> alphaSelector;
    return SUCCEEDED(converter.QueryInterface(&alphaSelector));
}

CComPtr<IEnumSpObjectTokens> CVoiceTokenEnumerator::EnumEdgeVoices()
{
    CComPtr<ISpObjectTokenEnumBuilder> pEnumBuilder;
    CheckHr(pEnumBuilder.CoCreateInstance(CLSID_SpObjectTokenEnum));
    CheckHr(pEnumBuilder->SetAttribs(nullptr, nullptr));

    const auto json = GetEdgeVoiceJson();

    // Universal (IPA) phoneme converter has been supported since SAPI 5.3, which supports most other languages
    // SAPI on older systems (XP) does not have this universal converter, so each language must have its corresponding phoneme converter
    // For systems not supporting the universal converter, we check for each voice if a phoneme converter for its language is present
    // If not, hide the voice from the list
    bool universalSupported = IsUniversalPhoneConverterSupported();
    std::set<LANGID> supportedLangs;
    if (!universalSupported)
        supportedLangs = GetSupportedLanguageIDs();

    for (const auto& voice : json)
    {
        if (!universalSupported
            && !supportedLangs.contains(LangIDFromLocaleName(UTF8ToWString(voice.at("Locale")))))
        {
            continue;
        }
        auto pToken = MakeEdgeVoiceToken(voice);
        pEnumBuilder->AddTokens(1, &pToken.p);
    }

    return CComQIPtr<IEnumSpObjectTokens>(pEnumBuilder);
}