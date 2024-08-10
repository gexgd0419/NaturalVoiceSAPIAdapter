// VoiceTokenEnumerator.cpp: CVoiceTokenEnumerator 的实现
#include "pch.h"
#include "VoiceTokenEnumerator.h"
#include <VersionHelpers.h>
#include "SpeechServiceConstants.h"
#include "NetUtils.h"
#include "StringTokenizer.h"
#include "LangUtils.h"
#include <condition_variable>
#include "wrappers.h"
#include "TaskScheduler.h"
#include "RegKey.h"
#include "SapiException.h"
#include "Logger.h"


// CVoiceTokenEnumerator

inline static void CheckHr(HRESULT hr)
{
    if (FAILED(hr))
        throw std::system_error(hr, std::system_category());
}

static std::vector<std::shared_ptr<DataKeyData>> s_cachedTokens;

static std::mutex s_cacheMutex;
static bool s_isCacheTaskScheduled = false;
extern TaskScheduler g_taskScheduler;


HRESULT CVoiceTokenEnumerator::FinalConstruct() noexcept
{
    // Exception handling in enumerator:
    //   Returning an error code will make the whole SAPI voice enumeration process fail,
    //   instead of just ignoring this faulty enumerator.
    //   As a result, no SAPI clients can enumerate voices.
    //   To prevent this, if an enumeration function fails, it should silently return without throwing.
    //   Only critical situations such as no memory or failing to create an enumerator object at all can be reported,
    //   others should be silently ignored and return S_OK.

    ScopeTracer tracer("Voice enum: Constructor begin", "Voice enum: Constructor end");
    try
    {
        // Some programs assume that creating an enumerator is a low-cost operation,
        // and re-create enumerators frequently during eumeration.
        // Here we try to cache the created tokens for a short period (10 seconds) to improve performance

        std::lock_guard lock(s_cacheMutex);

        CComPtr<ISpObjectTokenEnumBuilder> pEnumBuilder;
        CheckSapiHr(pEnumBuilder.CoCreateInstance(CLSID_SpObjectTokenEnum));
        CheckSapiHr(pEnumBuilder->SetAttribs(nullptr, nullptr));

        if (!s_cachedTokens.empty())
        {
            for (auto& token : s_cachedTokens)
            {
                CComPtr<ISpObjectToken> pToken;
                CheckSapiHr(CVoiceToken::Create(token, &pToken));
                CheckSapiHr(pEnumBuilder->AddTokens(1, &pToken.p));
            }
            CheckSapiHr(pEnumBuilder->QueryInterface(&m_pEnum));
            return S_OK;
        }

        RegKey key;
        // Failing to open the key will make all query methods return default values
        key.Open(HKEY_CURRENT_USER, L"Software\\NaturalVoiceSAPIAdapter\\Enumerator", KEY_QUERY_VALUE);

        DWORD fAllLanguages = key.GetDword(L"EdgeVoiceAllLanguages");
        std::vector<std::wstring> languages = key.GetMultiStringList(L"EdgeVoiceLanguages");
        std::wstring narratorVoicePath = key.GetString(L"NarratorVoicePath");
        if (narratorVoicePath.empty())
        {
            WCHAR szDefaultPath[MAX_PATH];
            DWORD len = GetModuleFileNameW((HMODULE)&__ImageBase, szDefaultPath, MAX_PATH);
            if (len != 0 && len != MAX_PATH)
            {
                PathRemoveFileSpecW(szDefaultPath);
                // try DLLPath\NarratorVoices
                if (PathAppendW(szDefaultPath, L"NarratorVoices"))
                {
                    if (PathFileExistsW(szDefaultPath))
                        narratorVoicePath = szDefaultPath;
                    PathRemoveFileSpecW(szDefaultPath);
                }
                if (narratorVoicePath.empty())
                {
                    // try DLLPath\..\NarratorVoices
                    PathRemoveFileSpecW(szDefaultPath);
                    if (PathAppendW(szDefaultPath, L"NarratorVoices"))
                    {
                        if (PathFileExistsW(szDefaultPath))
                            narratorVoicePath = szDefaultPath;
                    }
                }
            }
        }
        std::wstring azureKey = key.GetString(L"AzureVoiceKey"), azureRegion = key.GetString(L"AzureVoiceRegion");
        ErrorMode errorMode = static_cast<ErrorMode>(std::clamp(key.GetDword(L"DefaultErrorMode", 0UL), 0UL, 2UL));

        if (!key.GetDword(L"Disable"))
        {
            if (!key.GetDword(L"NoNarratorVoices")
                && IsWindows7OrGreater())  // this requires Win 7
            {
                // Use the same map, so that local voices with the same ID won't appear twice
                TokenMap tokens;

                if (!narratorVoicePath.empty())
                    EnumLocalVoicesInFolder(tokens, (narratorVoicePath + L"\\*").c_str(), errorMode);

                EnumLocalVoices(tokens, errorMode);

                for (auto& token : tokens)
                {
                    s_cachedTokens.push_back(std::move(token.second));
                }
            }

            TokenMap onlineTokens;
            if (!key.GetDword(L"NoEdgeVoices"))
            {
                EnumEdgeVoices(onlineTokens, fAllLanguages, languages, errorMode);

                // If Edge voices should override Azure voices, put them in the same map, first Edge, then Azure.
                // If not, add the Edge voices and clear the map immediately before Azure voices, as follows.
                if (!key.GetDword(L"EdgeVoicesOverrideAzureVoices"))
                {
                    for (auto& token : onlineTokens)
                        s_cachedTokens.push_back(std::move(token.second));
                    onlineTokens.clear();
                }
            }

            if (!key.GetDword(L"NoAzureVoices") && !azureKey.empty() && !azureRegion.empty())
            {
                // Put Azure voices in the map.
                // Edge voices may or may not previously be put into the same map, depending on configuration.
                // If Edge voices are in the map, Azure voices with the same IDs will not be added.
                EnumAzureVoices(onlineTokens, fAllLanguages, languages, azureKey, azureRegion, errorMode);
            }

            for (auto& token : onlineTokens)
                s_cachedTokens.push_back(std::move(token.second));
        }

        if (!s_isCacheTaskScheduled)
        {
            s_isCacheTaskScheduled = true;
            g_taskScheduler.StartNewTask(10000, []()
                {
                    std::lock_guard lock(s_cacheMutex);
                    s_cachedTokens.clear();
                    s_isCacheTaskScheduled = false;
                });
        }

        for (auto& token : s_cachedTokens)
        {
            CComPtr<ISpObjectToken> pToken;
            CheckSapiHr(CVoiceToken::Create(token, &pToken));
            CheckSapiHr(pEnumBuilder->AddTokens(1, &pToken.p));
        }
        CheckSapiHr(pEnumBuilder->QueryInterface(&m_pEnum));

        if (logger.should_log(spdlog::level::info))
        {
            LogInfo("Voice enum: Enumerated {} voice(s)", s_cachedTokens.size());
        }

        return S_OK;
    }
    // All exceptions caught here are critical. They will prevent other voices from being enumerated.
    catch (const std::bad_alloc&)
    {
        LogCritical("Out of memory");
        return E_OUTOFMEMORY;
    }
    catch (const std::system_error& ex)
    {
        LogCritical("Voice enum: Cannot create enumerator: {}", ex);
        return HRESULT_FROM_WIN32(ex.code().value());
    }
    catch (const std::exception& ex)
    {
        LogCritical("Voice enum: Cannot create enumerator: {}", ex);
        return E_FAIL;
    }
    catch (...) // C++ exceptions should not cross COM boundary
    {
        LogCritical("Voice enum: Cannot create enumerator: Unknown error");
        return E_FAIL;
    }
}

static std::wstring LanguageIDsFromLocaleName(const std::wstring& locale)
{
    LANGID lang = LangIDFromLocaleName(locale.c_str());
    if (lang == 0)
        return {};

    std::wstring ret = LangIDToHexLang(lang);

    for (LANGID fallback : GetLangIDFallbacks(lang))
    {
        ret += L';';
        ret += LangIDToHexLang(fallback);
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

static std::shared_ptr<DataKeyData> MakeLocalVoiceToken(
    const VoiceInfo& voiceInfo,
    ErrorMode errorMode = ErrorMode::ProbeForError,
    const std::wstring& namePrefix = {}
)
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
    std::wstring name = namePrefix + path.substr(name_start, name_end - name_start);

    std::wstring friendlyName = UTF8ToWString(voiceInfo.Name);
    std::wstring shortFriendlyName = friendlyName;
    TrimVoiceName(shortFriendlyName);

    std::wstring localeName = UTF8ToWString(voiceInfo.Locale);

    return std::shared_ptr<DataKeyData>(new DataKeyData {
        .path = name,
        .values = {
            { L"", std::move(friendlyName) },
            { L"CLSID", L"{013AB33B-AD1A-401C-8BEE-F6E2B046A94E}" }
        },
        .subkeys = {
            { L"Attributes", {
                .path = name + L"\\Attributes",
                .values = {
                    { L"Name", std::move(shortFriendlyName) },
                    { L"Gender", UTF8ToWString(voiceInfo.Properties.GetProperty("Gender")) },
                    { L"Language", LanguageIDsFromLocaleName(localeName) },
                    { L"Locale", std::move(localeName) },
                    { L"Vendor", L"Microsoft" },
                    { L"NaturalVoiceType", L"Narrator;Local" }
                }
            } },
            { L"NaturalVoiceConfig", {
                .path = name + L"\\NaturalVoiceConfig",
                .values = {
                    { L"ErrorMode", std::to_wstring(static_cast<UINT>(errorMode)) },
                    { L"Path", std::move(path) },
                    { L"Key", MS_TTS_KEY }
                }
            } }
        }
    });
}

// Exception handling in token enumeration functions:
//   Fail immediately on std::bad_alloc, which is often critical;
//   Log and ignore on other exceptions, because we don't want to break SAPI and prevent enumerating other SAPI voices.

void CVoiceTokenEnumerator::EnumLocalVoices(TokenMap& tokens, ErrorMode errorMode)
{
    try
    {
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
        if (paths.empty())
            return;
        auto config = EmbeddedSpeechConfig::FromPaths(paths);
        auto synthesizer = SpeechSynthesizer::FromConfig(config, nullptr);
        auto result = synthesizer->GetVoicesAsync().get();
        if (result->Reason == ResultReason::VoicesListRetrieved)
        {
            for (auto& info : result->Voices)
            {
                tokens.try_emplace(info->Name, MakeLocalVoiceToken(*info, errorMode));
            }
        }
        else
        {
            LogWarn("Voice enum: Cannot get installed voice list: {}", result->ErrorDetails);
        }
    }
    catch (const std::bad_alloc&)
    {
        throw;
    }
    catch (const winrt::hresult_error& ex)
    {
        // CLASS_E_CLASSNOTAVAILABLE will be thrown when running on a Windows version with no WinRT support,
        // such as Windows 7.
        // Ignore this case and log the others.
        if (ex.code() != CLASS_E_CLASSNOTAVAILABLE)
        {
            LogWarn("Voice enum: Cannot get installed voice list: {}", ex.message());
        }
    }
    catch (const std::exception& ex)
    {
        LogWarn("Voice enum: Cannot get installed voice list: {}", ex);
    }
}

void CVoiceTokenEnumerator::EnumLocalVoicesInFolder(TokenMap& tokens, LPCWSTR basepath, ErrorMode errorMode)
{
    if (wcslen(basepath) >= MAX_PATH)
        return;
    WCHAR path[MAX_PATH];
    wcscpy_s(path, basepath);
    PathRemoveFileSpecW(path);

    try
    {
        // Get all package paths, and then load all voices in one call
        // Because each EmbeddedSpeechConfig::FromPath() can reload some DLLs in some situations,
        // slowing down the enumeration process as more voices are installed

        // Because of a bug in the Azure Speech SDK:
        // https://github.com/Azure-Samples/cognitive-services-speech-sdk/issues/2288
        // Model paths containing non-ASCII characters cannot be loaded.
        // Changing the current directory and using relative paths may get around this,
        // but the current directory is a process-wide setting and changing it is not thread-safe

        WIN32_FIND_DATAW fd;

        HFindFile hFind = FindFirstFileW(basepath, &fd);
        if (hFind == INVALID_HANDLE_VALUE)
            return;

        std::vector<std::string> paths;
        do
        {
            // Ignore . and ..
            if (fd.cFileName[0] == '.'
                && (fd.cFileName[1] == '\0'
                    || (fd.cFileName[1] == '.' && fd.cFileName[2] == '\0')))
                continue;

            // Only add non-hidden subfolders
            if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                && !(fd.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN))
            {
                if (PathAppendW(path, fd.cFileName))
                {
                    paths.push_back(WStringToUTF8(path));
                    PathRemoveFileSpecW(path);
                }
            }
        } while (FindNextFileW(hFind, &fd));

        if (paths.empty())
            return;

        auto config = EmbeddedSpeechConfig::FromPaths(paths);
        auto synthesizer = SpeechSynthesizer::FromConfig(config, nullptr);
        auto result = synthesizer->GetVoicesAsync().get();
        const std::wstring prefix = L"Local-";
        if (result->Reason == ResultReason::VoicesListRetrieved)
        {
            for (auto& info : result->Voices)
            {
                tokens.try_emplace(info->Name, MakeLocalVoiceToken(*info, errorMode, prefix));
            }
        }
        else
        {
            LogWarn("Voice enum: Cannot get voice list from folder: {}", result->ErrorDetails);
        }
    }
    catch (const std::bad_alloc&)
    {
        throw;
    }
    catch (const std::exception& ex)
    {
        LogWarn("Voice enum: Cannot get voice list from folder: {}", ex);
    }
}

static std::shared_ptr<DataKeyData> MakeEdgeVoiceToken(
    const nlohmann::json& json,
    ErrorMode errorMode = ErrorMode::ProbeForError
)
{
    std::wstring shortName = UTF8ToWString(json.at("ShortName"));

    std::wstring friendlyName = UTF8ToWString(json.at("FriendlyName"));
    std::wstring shortFriendlyName = friendlyName;
    TrimVoiceName(shortFriendlyName);

    std::wstring localeName = UTF8ToWString(json.at("Locale"));

    std::wstring regName = L"Edge-" + shortName; // registry key name format: Edge-en-US-AriaNeural

    return std::shared_ptr<DataKeyData>(new DataKeyData {
        .path = regName,
        .values = {
            { L"", std::move(friendlyName) },
            { L"CLSID", L"{013AB33B-AD1A-401C-8BEE-F6E2B046A94E}" }
        },
        .subkeys = {
            { L"Attributes", {
                .path = regName + L"\\Attributes",
                .values = {
                    { L"Name", std::move(shortFriendlyName) },
                    { L"Gender", UTF8ToWString(json.at("Gender")) },
                    { L"Language", LanguageIDsFromLocaleName(localeName) },
                    { L"Locale", std::move(localeName) },
                    { L"Vendor", L"Microsoft" },
                    { L"NaturalVoiceType", L"Edge;Cloud" }
                }
            } },
            { L"NaturalVoiceConfig", {
                .path = regName + L"\\NaturalVoiceConfig",
                .values = {
                    { L"ErrorMode", std::to_wstring(static_cast<UINT>(errorMode)) },
                    { L"WebsocketURL", EDGE_WEBSOCKET_URL },
                    { L"Voice", shortName },
                    { L"IsEdgeVoice", L"1" }
                }
            } }
        }
    });
}

static std::shared_ptr<DataKeyData> MakeAzureVoiceToken(
    const nlohmann::json& json,
    const std::wstring& key,
    const std::wstring& region,
    ErrorMode errorMode = ErrorMode::ProbeForError
)
{
    std::wstring shortName = UTF8ToWString(json.at("ShortName"));

    std::wstring shortFriendlyName = UTF8ToWString(json.at("DisplayName"));
    std::wstring localeName = UTF8ToWString(json.at("Locale"));
    std::wstring localeDisplayName = UTF8ToWString(json.at("LocaleName"));
    std::wstring friendlyName = std::format(L"Azure {} - {}", shortFriendlyName, localeDisplayName);

    std::wstring regName = L"Azure-" + shortName; // registry key name format: Azure-en-US-AriaNeural

    return std::shared_ptr<DataKeyData>(new DataKeyData {
        .path = regName,
        .values = {
            { L"", std::move(friendlyName) },
            { L"CLSID", L"{013AB33B-AD1A-401C-8BEE-F6E2B046A94E}" }
        },
        .subkeys = {
            { L"Attributes", {
                .path = regName + L"\\Attributes",
                .values = {
                    { L"Name", std::move(shortFriendlyName) },
                    { L"Gender", UTF8ToWString(json.at("Gender")) },
                    { L"Language", LanguageIDsFromLocaleName(localeName) },
                    { L"Locale", std::move(localeName) },
                    { L"Vendor", L"Microsoft" },
                    { L"NaturalVoiceType", L"Azure;Cloud" }
                }
            } },
            { L"NaturalVoiceConfig", {
                .path = regName + L"\\NaturalVoiceConfig",
                .values = {
                    { L"ErrorMode", std::to_wstring(static_cast<UINT>(errorMode)) },
                    { L"Voice", shortName },
                    { L"Key", key },
                    { L"Region", region }
                }
            } }
        }
    });
}

// Enumerate all language IDs of installed phoneme converters
static std::set<LANGID> GetSupportedLanguageIDs()
{
    std::set<LANGID> langids;
    CComPtr<IEnumSpObjectTokens> pEnum;
    CheckSapiHr(SpEnumTokens(SPCAT_PHONECONVERTERS, nullptr, nullptr, &pEnum));
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
    CheckSapiHr(converter.CoCreateInstance(CLSID_SpPhoneConverter));
    CComPtr<ISpPhoneticAlphabetSelection> alphaSelector;
    return SUCCEEDED(converter.QueryInterface(&alphaSelector));
}

static std::set<LANGID> GetUserPreferredLanguageIDs(bool includeFallbacks)
{
    std::set<LANGID> langids;
    ULONG numLangs = 0, cchBuffer = 0;
    
    static const auto pfnGetUserPreferredUILanguages
        = reinterpret_cast<decltype(GetUserPreferredUILanguages)*>
        (GetProcAddress(GetModuleHandleW(L"kernel32"), "GetUserPreferredUILanguages"));

    if (!pfnGetUserPreferredUILanguages)
    {
        LANGID langid = GetUserDefaultLangID();
        langids.insert(langid);
        if (includeFallbacks)
            langids.insert_range(GetLangIDFallbacks(langid));
        langids.insert(MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US)); // always included
        return langids;
    }

    if (!pfnGetUserPreferredUILanguages(MUI_LANGUAGE_ID, &numLangs, nullptr, &cchBuffer))
        throw std::system_error(GetLastError(), std::system_category());
    auto pBuffer = std::make_unique_for_overwrite<WCHAR[]>(cchBuffer);
    if (!pfnGetUserPreferredUILanguages(MUI_LANGUAGE_ID, &numLangs, pBuffer.get(), &cchBuffer))
        throw std::system_error(GetLastError(), std::system_category());

    for (const auto& langidstr : TokenizeString(std::wstring_view(pBuffer.get(), cchBuffer - 2), L'\0'))
    {
        LANGID langid = HexLangToLangID(langidstr);
        langids.insert(langid);
        if (includeFallbacks)
            langids.insert_range(GetLangIDFallbacks(langid));
    }

    static const auto pfnResolveLocaleName
        = reinterpret_cast<decltype(ResolveLocaleName)*>
        (GetProcAddress(GetModuleHandleW(L"kernel32"), "ResolveLocaleName"));

    if (pfnResolveLocaleName)
    {
        try
        {
            for (const auto& langstr :
                winrt::Windows::System::UserProfile::GlobalizationPreferences::Languages())
            {
                WCHAR resolvedLocale[LOCALE_NAME_MAX_LENGTH] = {};
                if (pfnResolveLocaleName(langstr.c_str(), resolvedLocale, LOCALE_NAME_MAX_LENGTH) == 0)
                    continue;
                LANGID langid = LangIDFromLocaleName(resolvedLocale);
                if (langid == LOCALE_CUSTOM_UNSPECIFIED)
                    continue;
                langids.insert(langid);
                if (includeFallbacks)
                    langids.insert_range(GetLangIDFallbacks(langid));
            }
        }
        catch (const winrt::hresult_error&)
        {
        }
    }

    langids.insert(MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US)); // always included
    return langids;
}

static bool IsLanguageInList(const std::wstring& language, const std::vector<std::wstring>& languages)
{
    // A voice's language should be able to match a broader list item
    // e.g. "en-US" can match list item "en"
    for (auto& langInList : languages)
    {
        if (langInList.size() > language.size())
            continue;
        if (language.size() == langInList.size() && EqualsIgnoreCase(language, langInList))
            return true;
        wchar_t prefixEndChar = *(language.data() + langInList.size());
        if (prefixEndChar != '-' && prefixEndChar != '\0')
            continue;
        std::wstring_view langPrefix(language.data(), langInList.size());
        if (EqualsIgnoreCase(langPrefix, langInList))
            return true;
    }
    return false;
}

nlohmann::json GetCachedJson(LPCWSTR cacheName, LPCSTR downloadUrl, LPCSTR downloadHeaders);

template <class TokenMaker>
    requires std::is_invocable_r_v<std::shared_ptr<DataKeyData>, TokenMaker, const nlohmann::json&>
void EnumOnlineVoices(std::map<std::string, std::shared_ptr<DataKeyData>>& tokens,
    LPCWSTR cacheName, LPCSTR downloadUrl, LPCSTR downloadHeaders,
    BOOL allLanguages, const std::vector<std::wstring>& languages,
    TokenMaker&& tokenMaker)
{
    try
    {
        const auto json = GetCachedJson(cacheName, downloadUrl, downloadHeaders);

        // Universal (IPA) phoneme converter has been supported since SAPI 5.3, which supports most other languages
        // SAPI on older systems (XP) does not have this universal converter, so each language must have its corresponding phoneme converter
        // For systems not supporting the universal converter, we check for each voice if a phoneme converter for its language is present
        // If not, hide the voice from the list
        bool universalSupported = IsUniversalPhoneConverterSupported();
        std::set<LANGID> supportedLangs;
        if (!universalSupported)
            supportedLangs = GetSupportedLanguageIDs();

        std::set<LANGID> userLangs;
        if (!allLanguages && languages.empty())
            userLangs = GetUserPreferredLanguageIDs(false);

        for (const auto& voice : json)
        {
            auto locale = UTF8ToWString(voice.at("Locale"));
            LANGID langid = LangIDFromLocaleName(locale.c_str());
            if (!universalSupported && !supportedLangs.contains(langid))
                continue;
            if (!allLanguages)
            {
                if (languages.empty())
                {
                    // the language list is empty, use the display languages
                    if (!userLangs.contains(langid))
                        continue;
                }
                else
                {
                    if (!IsLanguageInList(locale, languages))
                        continue;
                }
            }
            tokens.try_emplace(voice.at("ShortName"), tokenMaker(voice));
        }
    }
    catch (const std::bad_alloc&)
    {
        throw;
    }
    catch (const std::exception& ex)
    {
        LogWarn("Voice enum: Cannot get online voice list: {}", ex);
    }
}

void CVoiceTokenEnumerator::EnumEdgeVoices(TokenMap& tokens, BOOL allLanguages, const std::vector<std::wstring>& languages,
    ErrorMode errorMode)
{
    EnumOnlineVoices(tokens, L"EdgeVoiceListCache.json", EDGE_VOICE_LIST_URL, "",
        allLanguages, languages,
        [errorMode](const nlohmann::json& json)
        {
            return MakeEdgeVoiceToken(json, errorMode);
        }
    );
}

void CVoiceTokenEnumerator::EnumAzureVoices(TokenMap& tokens, BOOL allLanguages, const std::vector<std::wstring>& languages,
    const std::wstring& key, const std::wstring& region, ErrorMode errorMode)
{
    EnumOnlineVoices(tokens, L"AzureVoiceListCache.json",
        (std::string("https://") + WStringToUTF8(region) + AZURE_TTS_HOST_AFTER_REGION + AZURE_VOICE_LIST_PATH).c_str(),
        (std::string("Ocp-Apim-Subscription-Key: ") + WStringToUTF8(key) + "\r\n").c_str(),
        allLanguages, languages,
        [key, region, errorMode](const nlohmann::json& json)
        {
            return MakeAzureVoiceToken(json, key, region, errorMode);
        });
}