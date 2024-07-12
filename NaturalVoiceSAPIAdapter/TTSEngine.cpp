// TTSEngine.cpp: CTTSEngine 的实现

#include "pch.h"
#include "TTSEngine.h"
#include "SpeechRestAPI.h"
#include "NetUtils.h"
#include "SpeechServiceConstants.h"

// CTTSEngine


// ISpObjectWithToken Implementation

// Initializes this instance of CTTSEngine to use the voice specified in registry
STDMETHODIMP CTTSEngine::SetObjectToken(ISpObjectToken* pToken) noexcept
{
    try
    {
        RETONFAIL(SpGenericSetObjectToken(pToken, m_cpToken));

        RETONFAIL(InitVoice());

        RETONFAIL(InitPhoneConverter());

        return S_OK;
    }
    catch (const std::bad_alloc&)
    {
        return E_OUTOFMEMORY;
    }
    catch (const std::system_error& ex)
    {
        Error(ex.what());
        auto& cat = ex.code().category();
        if (cat == std::system_category() || cat == asio::system_category())
            return HRESULT_FROM_WIN32(ex.code().value());
        return E_FAIL;
    }
    catch (const std::exception& ex)
    {
        Error(ex.what());
        MessageBoxA(NULL, ex.what(), NULL, 0);
        return E_FAIL;
    }
    catch (AZACHR hr)
    {
        return hr == AZAC_ERR_INVALID_ARG ? E_INVALIDARG : E_FAIL;
    }
    catch (...) // C++ exceptions should not cross COM boundary
    {
        return E_FAIL;
    }
}


// ISpTTSEngine Implementation 

STDMETHODIMP CTTSEngine::Speak(DWORD /*dwSpeakFlags*/,
    REFGUID /*rguidFormatId*/,
    const WAVEFORMATEX* /*pWaveFormatEx*/,
    const SPVTEXTFRAG* pTextFragList,
    ISpTTSEngineSite* pOutputSite) noexcept
{
    try
    {
        // Check args
        if (SP_IS_BAD_INTERFACE_PTR(pOutputSite) ||
            SP_IS_BAD_READ_PTR(pTextFragList))
        {
            return E_INVALIDARG;
        }
        if (!m_synthesizer && !m_restApi)
        {
            return SPERR_UNINITIALIZED;
        }

        ULONGLONG eventInterests = 0;
        pOutputSite->GetEventInterest(&eventInterests);
        if (m_synthesizer)
            SetupSynthesizerEvents(eventInterests);
        else
            SetupRestAPIEvents(eventInterests);

        m_pOutputSite = pOutputSite;
        m_waveBytesWritten = 0;
        BuildSSML(pTextFragList);

        std::future<void> future;

        if (m_synthesizer)
        {
            future = std::async(std::launch::async, [this]() { CheckSynthesisResult(m_synthesizer->SpeakSsml(m_ssml)); });
        }
        else
        {
            future = m_restApi->SpeakAsync(m_ssml);
        }

        while (!(pOutputSite->GetActions() & SPVES_ABORT)
            && future.wait_for(std::chrono::milliseconds(50)) == std::future_status::timeout)
        {
            if (pOutputSite->GetActions() & SPVES_SKIP)
            {
                // Skipping is not supported
                pOutputSite->CompleteSkip(0);
            }
        }

        if (pOutputSite->GetActions() & SPVES_ABORT) // requested stop
        {
            if (m_synthesizer)
                m_synthesizer->StopSpeakingAsync().wait();
            else
                m_restApi->Stop();
            future.get(); // wait for the future and get its stored exception thrown
        }
        else
        {
            future.get(); // wait for the future and get its stored exception thrown
            if (m_isEdgeVoice)
            {
                // finish all remaining bookmark events at the end
                const auto size = m_bookmarks.size();
                SPEVENT ev = { 0 };
                ev.ullAudioStreamOffset = m_waveBytesWritten;
                ev.eEventId = SPEI_TTS_BOOKMARK;
                ev.elParamType = SPET_LPARAM_IS_STRING;
                for (auto i = m_bookmarkIndex; i < size; i++)
                {
                    auto& bookmark = m_bookmarks[i];
                    ev.lParam = reinterpret_cast<LPARAM>(bookmark.name.c_str());
                    ev.wParam = _wtol(bookmark.name.c_str());
                    pOutputSite->AddEvents(&ev, 1);
                }
            }
        }

        return S_OK;
    }
    catch (const std::bad_alloc&)
    {
        return E_OUTOFMEMORY;
    }
    catch (const std::system_error& ex)
    {
        Error(ex.what());
        auto& cat = ex.code().category();
        if (cat == std::system_category() || cat == asio::system_category())
            return HRESULT_FROM_WIN32(ex.code().value());
        return E_FAIL;
    }
    catch (const std::exception& ex)
    {
        Error(ex.what());
        return E_FAIL;
    }
    catch (...) // C++ exceptions should not cross COM boundary
    {
        return E_FAIL;
    }
} /* CTTSEngine::Speak */

STDMETHODIMP CTTSEngine::GetOutputFormat(const GUID* /*pTargetFormatId*/, const WAVEFORMATEX* /*pTargetWaveFormatEx*/,
    GUID* pDesiredFormatId, WAVEFORMATEX** ppCoMemDesiredWaveFormatEx) noexcept
{
    // Embedded voice only supports 24kHz 16Bit mono
    return SpConvertStreamFormatEnum(SPSF_24kHz16BitMono, pDesiredFormatId, ppCoMemDesiredWaveFormatEx);
}


// Other Member Functions

HRESULT CTTSEngine::InitPhoneConverter()
{
    LANGID lang = 0;
    RETONFAIL(SpGetLanguageFromToken(m_cpToken, &lang));

    CComPtr<ISpDataKey> pAttrKey;
    CSpDynamicString locale;
    if (SUCCEEDED(m_cpToken->OpenKey(SPTOKENKEY_ATTRIBUTES, &pAttrKey))
        && SUCCEEDED(pAttrKey->GetStringValue(L"Locale", &locale)))
    {
        m_localeName = locale;
    }
    else
    {
        m_localeName = L"en-US";
    }

    return SpCreatePhoneConverter(lang, nullptr, nullptr, &m_phoneConverter);
}

HRESULT CTTSEngine::InitVoice()
{
    CComPtr<ISpDataKey> pConfigKey;
    CSpDynamicString pszRegion, pszKey, pszPath, pszVoice;
    
    RETONFAIL(m_cpToken->OpenKey(L"NaturalVoiceConfig", &pConfigKey)); // this key must exist

    DWORD dwErrorMode;
    HRESULT hr = pConfigKey->GetDWORD(L"ErrorMode", &dwErrorMode);
    if (FAILED(hr)) dwErrorMode = 0;
    m_errorMode = (ErrorMode)std::clamp(dwErrorMode, 0UL, 2UL);

    hr = InitLocalVoice(pConfigKey);
    if (SUCCEEDED(hr))
        return hr;
    hr = InitCloudVoiceSynthesizer(pConfigKey);
    if (SUCCEEDED(hr))
        return hr;
    hr = InitCloudVoiceRestAPI(pConfigKey);
    return hr;
}

HRESULT CTTSEngine::InitLocalVoice(ISpDataKey* pConfigKey)
{
    try
    {
        CSpDynamicString pszPath, pszKey;
        RETONFAIL(pConfigKey->GetStringValue(L"Path", &pszPath));
        RETONFAIL(pConfigKey->GetStringValue(L"Key", &pszKey));

        auto config = EmbeddedSpeechConfig::FromPath(WStringToUTF8(pszPath.m_psz));

        config->SetSpeechSynthesisOutputFormat(SpeechSynthesisOutputFormat::Riff24Khz16BitMonoPcm);
        config->SetProperty(PropertyId::SpeechServiceResponse_RequestSentenceBoundary, "true");
        config->SetProperty(PropertyId::SpeechServiceResponse_RequestPunctuationBoundary, "false");

        // get the voice name by loading it first
        auto synthesizer = SpeechSynthesizer::FromConfig(config);
        auto result = synthesizer->GetVoicesAsync().get();
        if (result->Voices.empty())
            return E_INVALIDARG;
        config->SetSpeechSynthesisVoice(result->Voices[0]->Name, WStringToUTF8(pszKey.m_psz));

        if (m_errorMode == ErrorMode::ProbeForError)
        {
            auto synthesizer = SpeechSynthesizer::FromConfig(config, nullptr);
            RETONFAIL(CheckSynthesisResult(synthesizer->SpeakText(""))); // test for possible error
        }

        m_synthesizer = SpeechSynthesizer::FromConfig(config, AudioConfig::FromStreamOutput(
            AudioOutputStream::CreatePushStream(std::bind_front(&CTTSEngine::OnAudioData, this))));

        return S_OK;
    }
    catch (const std::system_error&) // Possibly DLL loading error
    {
        return E_FAIL; // If so, just return an error code so we can fallback
    }
}

HRESULT CTTSEngine::InitCloudVoiceSynthesizer(ISpDataKey* pConfigKey)
{
    try
    {
        CSpDynamicString pszKey, pszRegion, pszVoice;
        RETONFAIL(pConfigKey->GetStringValue(L"Key", &pszKey));
        RETONFAIL(pConfigKey->GetStringValue(L"Region", &pszRegion));
        RETONFAIL(pConfigKey->GetStringValue(L"Voice", &pszVoice));

        auto config = SpeechConfig::FromSubscription(WStringToUTF8(pszKey.m_psz), WStringToUTF8(pszRegion.m_psz));

        config->SetSpeechSynthesisOutputFormat(SpeechSynthesisOutputFormat::Riff24Khz16BitMonoPcm);
        config->SetProperty(PropertyId::SpeechServiceResponse_RequestSentenceBoundary, "true");
        config->SetProperty(PropertyId::SpeechServiceResponse_RequestPunctuationBoundary, "false");

        auto proxy = GetProxyForUrl("https://" + WStringToUTF8(pszRegion.m_psz) + AZURE_TTS_HOST_AFTER_REGION);
        if (!proxy.empty())
        {
            auto url = ParseUrl(proxy);
            uint32_t port = 80;
            std::from_chars(url.port.data(), url.port.data() + url.port.size(), port);
            config->SetProxy(std::string(url.host), port);
        }

        config->SetSpeechSynthesisVoiceName(WStringToUTF8(pszVoice.m_psz));
        m_onlineVoiceName = pszVoice;

        if (m_errorMode == ErrorMode::ProbeForError)
        {
            auto synthesizer = SpeechSynthesizer::FromConfig(config, nullptr);
            RETONFAIL(CheckSynthesisResult(synthesizer->SpeakText(""))); // test for possible error
        }

        m_synthesizer = SpeechSynthesizer::FromConfig(config, AudioConfig::FromStreamOutput(
            AudioOutputStream::CreatePushStream(std::bind_front(&CTTSEngine::OnAudioData, this))));

        return S_OK;
    }
    catch (const std::system_error&) // Possibly DLL loading error
    {   
        return E_FAIL; // If so, just return an error code so we can fallback
    }
}

HRESULT CTTSEngine::InitCloudVoiceRestAPI(ISpDataKey* pConfigKey)
{
    m_restApi = std::make_unique<SpeechRestAPI>();

    CSpDynamicString pszVoice, pszKey;
    RETONFAIL(pConfigKey->GetStringValue(L"Voice", &pszVoice));
    m_onlineVoiceName = pszVoice;

    DWORD dwValue = 0;
    if (SUCCEEDED(pConfigKey->GetDWORD(L"IsEdgeVoice", &dwValue)))
        m_isEdgeVoice = dwValue;
    
    if (CSpDynamicString pszWebsocketUrl; SUCCEEDED(pConfigKey->GetStringValue(L"WebsocketURL", &pszWebsocketUrl)))
    {
        pConfigKey->GetStringValue(L"Key", &pszKey);
        m_restApi->SetWebsocketUrl(
            pszKey ? WStringToUTF8(pszKey.m_psz) : "",
            WStringToUTF8(pszWebsocketUrl.m_psz)
        );
    }
    else if (CSpDynamicString pszRegion; SUCCEEDED(pConfigKey->GetStringValue(L"Region", &pszRegion)))
    {
        RETONFAIL(pConfigKey->GetStringValue(L"Key", &pszKey));
        m_restApi->SetSubscription(
            WStringToUTF8(pszKey.m_psz),
            WStringToUTF8(pszRegion.m_psz)
        );
    }
    else
        return E_INVALIDARG;

    return S_OK;
}

int CTTSEngine::OnAudioData(uint8_t* data, uint32_t len)
{
    ULONG written = 0;
    m_pOutputSite->Write(data, len, &written);
    m_waveBytesWritten += written;
    return written;
}
void CTTSEngine::OnBookmark(uint64_t offsetTicks, const std::wstring& bookmark)
{
    SPEVENT ev = { 0 };
    ev.ullAudioStreamOffset = WaveTicksToBytes(offsetTicks);
    ev.eEventId = SPEI_TTS_BOOKMARK;
    ev.elParamType = SPET_LPARAM_IS_STRING;
    ev.lParam = reinterpret_cast<LPARAM>(bookmark.c_str());
    ev.wParam = _wtol(bookmark.c_str());
    m_pOutputSite->AddEvents(&ev, 1);
}
void CTTSEngine::OnBoundary(uint64_t audioOffsetTicks, uint32_t textOffset, uint32_t textLength, SPEVENTENUM boundaryType)
{
    SPEVENT ev = { 0 };
    ev.ullAudioStreamOffset = WaveTicksToBytes(audioOffsetTicks);
    ev.eEventId = boundaryType;
    ev.elParamType = SPET_LPARAM_IS_UNDEFINED;
    ULONG offset = textOffset, length = textLength;
    MapTextOffset(offset, length);
    ev.lParam = offset;
    ev.wParam = length;
    m_pOutputSite->AddEvents(&ev, 1);

    if (m_isEdgeVoice && boundaryType == SPEI_WORD_BOUNDARY)
    {
        // trigger every untriggered bookmark until current word's end position
        auto size = m_bookmarks.size();
        while (m_bookmarkIndex < size)
        {
            auto& bookmark = m_bookmarks[m_bookmarkIndex];
            if (offset + length <= bookmark.ulSAPITextOffset)
                break;
            OnBookmark(audioOffsetTicks, bookmark.name);
            m_bookmarkIndex++;
        }
    }
}
void CTTSEngine::OnViseme(uint64_t offsetTicks, uint32_t visemeId)
{
    SPEVENT ev = { 0 };
    ev.ullAudioStreamOffset = WaveTicksToBytes(offsetTicks);
    ev.eEventId = SPEI_VISEME;
    ev.elParamType = SPET_LPARAM_IS_UNDEFINED;
    ev.wParam = 0;
    // Cognitive Speech uses the same viseme ID values as SAPI 
    ev.lParam = MAKELONG(visemeId, 0);
    m_pOutputSite->AddEvents(&ev, 1);
}

void CTTSEngine::SetupSynthesizerEvents(ULONGLONG interests)
{
    ClearSynthesizerEvents();

    if (interests & SPEI_TTS_BOOKMARK)
        m_synthesizer->BookmarkReached += [this](const SpeechSynthesisBookmarkEventArgs& arg)
        {
            OnBookmark(arg.AudioOffset, UTF8ToWString(arg.Text));
        };

    if (interests & (SPEI_WORD_BOUNDARY | SPEI_SENTENCE_BOUNDARY))
        m_synthesizer->WordBoundary += [this](const SpeechSynthesisWordBoundaryEventArgs& arg)
        {
            if (arg.BoundaryType == SpeechSynthesisBoundaryType::Punctuation)
                return;
            OnBoundary(arg.AudioOffset, arg.TextOffset, arg.WordLength,
                arg.BoundaryType == SpeechSynthesisBoundaryType::Sentence ? SPEI_SENTENCE_BOUNDARY : SPEI_WORD_BOUNDARY);
        };

    if (interests & SPEI_VISEME)
        m_synthesizer->VisemeReceived += [this](const SpeechSynthesisVisemeEventArgs& arg)
        {
            OnViseme(arg.AudioOffset, arg.VisemeId);
        };
}

void CTTSEngine::ClearSynthesizerEvents()
{
    m_synthesizer->BookmarkReached.DisconnectAll();
    m_synthesizer->WordBoundary.DisconnectAll();
    m_synthesizer->VisemeReceived.DisconnectAll();
    m_synthesizer->SynthesisCompleted.DisconnectAll();
    m_synthesizer->SynthesisCanceled.DisconnectAll();
}

void CTTSEngine::SetupRestAPIEvents(ULONGLONG interests)
{
    m_restApi->AudioReceivedCallback = std::bind_front(&CTTSEngine::OnAudioData, this);
    if (interests & SPEI_TTS_BOOKMARK)
        m_restApi->BookmarkCallback = [this](auto a, auto b) { OnBookmark(a, UTF8ToWString(b)); };
    if (interests & SPEI_WORD_BOUNDARY)
        m_restApi->WordBoundaryCallback = [this](auto a, auto b, auto c) { OnBoundary(a, b, c, SPEI_WORD_BOUNDARY); };
    if (interests & SPEI_SENTENCE_BOUNDARY)
        m_restApi->SentenceBoundaryCallback = [this](auto a, auto b, auto c) { OnBoundary(a, b, c, SPEI_SENTENCE_BOUNDARY); };
    if (interests & SPEI_VISEME)
        m_restApi->VisemeCallback = std::bind_front(&CTTSEngine::OnViseme, this);
}

void CTTSEngine::AppendTextFragToSsml(const SPVTEXTFRAG* pTextFrag)
{
    // entities are converted to characters before passing in, so we have to convert it back to XML
    // these entities are processed: &lt;&gt;&amp;&quot;&apos;

    LPCWSTR pEnd = pTextFrag->pTextStart + pTextFrag->ulTextLen;
    m_ssml.reserve(m_ssml.size() + pTextFrag->ulTextLen);

    for (LPCWSTR pCh = pTextFrag->pTextStart; pCh != pEnd && *pCh; pCh++)
    {
        switch (*pCh)
        {
        case '<': m_ssml.append(L"&lt;"); break;
        case '>': m_ssml.append(L"&gt;"); break;
        case '&': m_ssml.append(L"&amp;"); break;
        case '"': m_ssml.append(L"&quot;"); break;
        case '\'': m_ssml.append(L"&apos;"); break;
        default: m_ssml.push_back(*pCh); continue;
        }

        // match the next character in SAPI text with the character after the inserted entity in SSML text
        m_offsetMappings.emplace_back(pTextFrag->ulTextSrcOffset + (ULONG)(pCh - pTextFrag->pTextStart) + 1, (ULONG)m_ssml.size());
    }
}

void CTTSEngine::AppendPhonemesToSsml(const SPPHONEID* pPhoneIds)
{
    WCHAR phoneme[SP_MAX_PRON_LENGTH * 8];
    HRESULT hr = m_phoneConverter->IdToPhone(pPhoneIds, phoneme);
    if (FAILED(hr))
        return;

    for (LPCWSTR pCh = phoneme; *pCh; pCh++)
    {
        switch (*pCh)
        {
        case '<': m_ssml.append(L"&lt;"); break;
        case '>': m_ssml.append(L"&gt;"); break;
        case '&': m_ssml.append(L"&amp;"); break;
        case '"': m_ssml.append(L"&quot;"); break;
        case '\'': m_ssml.append(L"&apos;"); break;
        default: m_ssml.push_back(*pCh); break;
        }
    }
}

void CTTSEngine::AppendSAPIContextToSsml(const SPVCONTEXT& context)
{
    // map <context id='xxx'>...</context>
    // to <say-as interpret-as='xxx' format='xxx'>...</say-as>

    m_ssml.append(L"<say-as interpret-as='");

    std::wstring_view cat = context.pCategory;

    // standard: 'date_xxx'
    // when parsed from SSML, 'date:xxx' is used when format isn't standard
    if (EqualsIgnoreCase(cat.substr(0, 4), L"date")
        && (cat[4] == '_' || cat[4] == ':')) 
    {
        // <context id='date_dmy'> to <say-as interpret-as='date' format='dmy'>
        m_ssml.append(L"date' format='");
        auto fmt = cat.substr(5);
        if (EqualsIgnoreCase(fmt, L"year"))
            m_ssml.push_back('y');
        else
            m_ssml.append(fmt);
    }
    else if (EqualsIgnoreCase(cat, L"number_cardinal"))
        m_ssml.append(L"cardinal");
    else if (EqualsIgnoreCase(cat, L"number_fraction"))
        m_ssml.append(L"fraction");
    else if (EqualsIgnoreCase(cat, L"phone_number"))
        m_ssml.append(L"telephone");
    else
        m_ssml.append(cat); // other category IDs are passed as-is

    m_ssml.append(L"'>");
}

void CTTSEngine::BuildSSML(const SPVTEXTFRAG* pTextFragList)
{
    m_ssml.assign(L"<speak version='1.0' xmlns='http://www.w3.org/2001/10/synthesis' xmlns:mstts='http://www.w3.org/2001/mstts' xml:lang='");
    m_ssml.append(m_localeName);
    m_ssml.append(L"'>");

    USHORT mainVolume;
    if (FAILED(m_pOutputSite->GetVolume(&mainVolume)))
        mainVolume = 100;
    long mainRate;
    if (FAILED(m_pOutputSite->GetRate(&mainRate)))
        mainRate = 0;

    bool isInProsodyTag = false, isInEmphasisTag = false, isInSayAsTag = false;

    // Edge online voices only support a limited subset of SSML
    bool isEdgeVoice = m_isEdgeVoice;

    m_offsetMappings.clear();
    m_mappingIndex = 0;

    m_bookmarks.clear();
    m_bookmarkIndex = 0;

    ULONG lastSAPIOffset = 0;

    // online voices requires a <voice> tag, even after calling SetSpeechSynthesisVoiceName
    if (!m_onlineVoiceName.empty())
    {
        m_ssml.append(L"<voice name='");
        m_ssml.append(m_onlineVoiceName);
        m_ssml.append(L"'>");
    }

    for (auto pTextFrag = pTextFragList; pTextFrag; pTextFrag = pTextFrag->pNext)
    {
        // tag structure: <prosody><emphasis><say-as></say-as></emphasis></prosody>
        // <say-as> cannot contain tags

        if (!isInProsodyTag)
        {
            USHORT volume = (USHORT)std::clamp(mainVolume * pTextFrag->State.Volume / 100, 0UL, 100UL);
            long rate = std::clamp(mainRate + pTextFrag->State.RateAdj, -10L, 10L);
            long pitch = std::clamp(pTextFrag->State.PitchAdj.MiddleAdj, -10L, 10L);

            if (volume != 100 || rate != 0 || pitch != 0) // if not default value, add a prosody tag
            {
                m_ssml.append(L"<prosody");
                if (volume != 100)
                {
                    m_ssml.append(L" volume='");
                    m_ssml.append(std::to_wstring(volume - 100)); // 0~100 => -100%~0%
                    m_ssml.append(L"%'");
                }
                if (rate != 0)
                {
                    m_ssml.append(L" rate='");
                    m_ssml.append(std::to_wstring(rate >= 0 ? rate * 20 : rate * 20 / 3)); // -10~10 => -(2/3)~+200%
                    m_ssml.append(L"%'");
                }
                if (pitch != 0)
                {
                    m_ssml.append(L" pitch='");
                    m_ssml.append(std::to_wstring(pitch * 5)); // -10~10 => -50%~+50%
                    m_ssml.append(L"%'");
                }
                m_ssml.push_back('>');
                isInProsodyTag = true;
            }
        }

        if (!isInEmphasisTag && pTextFrag->State.EmphAdj && !isEdgeVoice)
        {
            m_ssml.append(L"<emphasis>"); // (not supported by offline TTS)
            isInEmphasisTag = true;
        }

        // NOTE: <say-as> tag cannot contain child tags.
        // if eAction is not Speak, a child tag will be added, which is incompatible with <say-as>
        // so only add <say-as> when eAction is Speak
        if (!isInSayAsTag && pTextFrag->State.Context.pCategory
            && pTextFrag->State.eAction == SPVA_Speak
            && !isEdgeVoice)
        {
            // map <context id='xxx'>...</context>
            // to <say-as interpret-as='xxx' format='xxx'>...</say-as>
            AppendSAPIContextToSsml(pTextFrag->State.Context);
            isInSayAsTag = true;
        }

        if (isEdgeVoice)
        {
            // Edge online voices only support some SSML tags
            // SSML that contains unrecognized tags will be rejected by the server
            // so we only keep the text that can be processed, and ignore the XML tags around it
            switch (pTextFrag->State.eAction)
            {
            case SPVA_Speak:
            case SPVA_SpellOut:
            case SPVA_Pronounce:
                m_offsetMappings.emplace_back(pTextFrag->ulTextSrcOffset, (ULONG)m_ssml.size());
                AppendTextFragToSsml(pTextFrag);
                lastSAPIOffset = pTextFrag->ulTextSrcOffset + pTextFrag->ulTextLen;
                break;

            case SPVA_Bookmark:
                // mark the position before this bookmark
                m_offsetMappings.emplace_back(lastSAPIOffset, (ULONG)m_ssml.size());
                // keep track of every bookmark, so we can simulate bookmark events later
                m_bookmarks.emplace_back(pTextFrag->ulTextSrcOffset, std::wstring(pTextFrag->pTextStart, pTextFrag->ulTextLen));
                break;
            }
        }
        else
        {
            switch (pTextFrag->State.eAction)
            {
            case SPVA_Speak:
                m_offsetMappings.emplace_back(pTextFrag->ulTextSrcOffset, (ULONG)m_ssml.size());
                AppendTextFragToSsml(pTextFrag);
                break;

            case SPVA_Silence: // insert a <break time='xxms'/> (not supported by offline TTS)
                m_ssml.append(L"<break time='");
                m_ssml.append(std::to_wstring(pTextFrag->State.SilenceMSecs));
                m_ssml.append(L"ms'/>");
                break;

            case SPVA_Bookmark: // insert a <bookmark mark='xx'/>
                m_ssml.append(L"<bookmark mark='");
                AppendTextFragToSsml(pTextFrag);
                m_ssml.append(L"'/>");
                break;

            case SPVA_SpellOut: // insert a <say-as interpret-as='characters'>...</say-as>
                m_ssml.append(L"<say-as interpret-as='characters'>");
                m_offsetMappings.emplace_back(pTextFrag->ulTextSrcOffset, (ULONG)m_ssml.size());
                AppendTextFragToSsml(pTextFrag);
                m_ssml.append(L"</say-as>");
                break;

            case SPVA_Pronounce: // insert a <phoneme alphabet='sapi' ph='xx'>...</phoneme>
                m_ssml.append(L"<phoneme alphabet='sapi' ph='");
                AppendPhonemesToSsml(pTextFrag->State.pPhoneIds);
                m_ssml.append(L"'>");
                m_offsetMappings.emplace_back(pTextFrag->ulTextSrcOffset, (ULONG)m_ssml.size());
                AppendTextFragToSsml(pTextFrag);
                m_ssml.append(L"</phoneme>");
                break;

            case SPVA_ParseUnknownTag: // insert it into SSML as-is
                // TODO: Custom XML tags may not be closed properly
                // when using VOICE tags to switch away from this voice
                m_ssml.append(pTextFrag->pTextStart, pTextFrag->ulTextLen);
                break;
            }
        }

        int preserveTagLevel = 0;

        if (pTextFrag->pNext)
        {
            auto& curState = pTextFrag->State;
            auto& nextState = pTextFrag->pNext->State;
            if (curState.Volume == nextState.Volume && curState.RateAdj == nextState.RateAdj
                && curState.PitchAdj.MiddleAdj == nextState.PitchAdj.MiddleAdj)
            {
                preserveTagLevel = 1; // if prosody is the same, no need to close it

                if (curState.EmphAdj == nextState.EmphAdj)
                {
                    preserveTagLevel = 2; // if emphasis is the same, no need to close it

                    if ((curState.Context.pCategory == nextState.Context.pCategory ||
                        (curState.Context.pCategory && nextState.Context.pCategory
                            && _wcsicmp(curState.Context.pCategory, nextState.Context.pCategory) == 0))
                        && nextState.eAction == SPVA_Speak)
                    {
                        // if context is the same, and the next fragment is still Speak, no need to close it
                        // if the next fragment isn't Speak, a child tag will be added, and we should close <say-as>
                        preserveTagLevel = 3;
                    }
                }
            }
        }

        // close tags
        if (isInSayAsTag && preserveTagLevel < 3)
        {
            m_ssml.append(L"</say-as>");
            isInSayAsTag = false;
        }
        if (isInEmphasisTag && preserveTagLevel < 2)
        {
            m_ssml.append(L"</emphasis>");
            isInEmphasisTag = false;
        }
        if (isInProsodyTag && preserveTagLevel < 1)
        {
            m_ssml.append(L"</prosody>");
            isInProsodyTag = false;
        }
    }

    if (!m_onlineVoiceName.empty())
    {
        m_ssml.append(L"</voice>");
    }

    m_ssml.append(L"</speak>");
}

// Convert offset and length in SSML text to those in SAPI text
void CTTSEngine::MapTextOffset(ULONG& ulSSMLOffset, ULONG& ulTextLen)
{
    if (m_offsetMappings.empty())
        return;

    ULONG endOffset = ulSSMLOffset + ulTextLen;
    const auto size = m_offsetMappings.size();

    // all mapping pairs in m_offsetMappings go from low offset to high offset,
    // so we just move the index forward as the speaking progresses
    // but if index goes beyond border, or the current offset surpasses the actual offset, reset index to 0
    if (m_mappingIndex >= size || m_offsetMappings[m_mappingIndex].ulSSMLTextOffset > ulSSMLOffset)
        m_mappingIndex = 0;

    // if we surpass the next offset, move forward, until the actual offset is between current and next
    // if there are multiple items with the same SSML offset, this will find the last item
    while (m_mappingIndex + 1 < size && ulSSMLOffset >= m_offsetMappings[m_mappingIndex + 1].ulSSMLTextOffset)
        m_mappingIndex++;

    const auto& mapping = m_offsetMappings[m_mappingIndex];
    // the same parameter is used for input & output
    auto& ulSAPIOffset = ulSSMLOffset;
    // if offset falls below zero, set it to zero
    if (mapping.ulSSMLTextOffset > mapping.ulSAPITextOffset && ulSSMLOffset < mapping.ulSSMLTextOffset - mapping.ulSAPITextOffset)
        ulSAPIOffset = 0;
    else
        ulSAPIOffset = ulSSMLOffset - mapping.ulSSMLTextOffset + mapping.ulSAPITextOffset;

    // if the end position (ulSSMLOffset + ulTextLen) also get remapped
    // (for example when '&' becomes '&amp;')
    // adjust ulTextLen as well
    // first find which range the end position is in, just like the above
    auto index = m_mappingIndex;
    while (index + 1 < size && endOffset >= m_offsetMappings[index + 1].ulSSMLTextOffset)
        index++;

    // If there are multiple items with the same SSML offset, go backwards and choose the first item.
    // This is because when simulating bookmark events for Edge voices,
    // the bookmark itself does not appear in the SSML text, only in the SAPI text,
    // so the same SSML offset will be paired with both the beginning and the end of the bookmark tag.
    // 
    // When mapping the starting offset, we need the last pair;
    // and when mapping the end offset, we need the first pair,
    // so that we can exclude the bookmark tag in boundary events.
    // 
    // However, if the bookmark tag is inside a word,
    // as Edge voices know nothing about bookmarks, they will still assume it's a whole word,
    // so we have to include the bookmark tag in the word.
    // Use the end offset of SAPI text to determine if the bookmark is inside the word.
    auto endSAPIOffset = ulSAPIOffset + ulTextLen;
    while (index > 0
        && m_offsetMappings[index - 1].ulSSMLTextOffset == m_offsetMappings[index].ulSSMLTextOffset  // previous same offset
        && m_offsetMappings[index - 1].ulSAPITextOffset >= endSAPIOffset  // previous not inside the word
        )
        index--;
    if (index <= m_mappingIndex)
        return;

    const auto& endMapping = m_offsetMappings[index];
    endOffset = endOffset - endMapping.ulSSMLTextOffset + endMapping.ulSAPITextOffset;
    ulTextLen = endOffset - ulSSMLOffset;
}

// Checks the result from speech operation, sets the error message if needed
HRESULT CTTSEngine::CheckSynthesisResult(const std::shared_ptr<SpeechSynthesisResult>& result)
{
    if (result->Reason != ResultReason::Canceled)
        return S_OK;

    auto details = SpeechSynthesisCancellationDetails::FromResult(result);
    if (details->Reason != CancellationReason::Error)
        return S_OK;

    Error(details->ErrorDetails.c_str());
    if (m_errorMode == ErrorMode::ShowMessageOnError)
    {
        MessageBoxA(NULL, details->ErrorDetails.c_str(), "NaturalVoiceSAPIAdapter", MB_ICONEXCLAMATION | MB_SYSTEMMODAL);
    }
    return E_FAIL;
}
