// TTSEngine.cpp: CTTSEngine 的实现

#include "pch.h"
#include "TTSEngine.h"
#include "delaydll.h"

// CTTSEngine


// ISpObjectWithToken Implementation

// Initializes this instance of CTTSEngine to use the voice specified in registry
STDMETHODIMP CTTSEngine::SetObjectToken(ISpObjectToken* pToken) noexcept
{
    try
    {
        RETONFAIL(SpGenericSetObjectToken(pToken, m_cpToken));

        RETONFAIL(InitSynthesizer());

        SetupSynthesizerEvents();

        RETONFAIL(InitPhoneConverter());

        return S_OK;
    }
    catch (const std::bad_alloc&)
    {
        return E_OUTOFMEMORY;
    }
    catch (const std::exception& ex)
    {
        Error(ex.what());
        return E_FAIL;
    }
    catch (const DllDelayLoadError& ex)
    {
        return HRESULT_FROM_WIN32(ex.code());
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

STDMETHODIMP CTTSEngine::Speak(DWORD dwSpeakFlags,
    REFGUID rguidFormatId,
    const WAVEFORMATEX* pWaveFormatEx,
    const SPVTEXTFRAG* pTextFragList,
    ISpTTSEngineSite* pOutputSite) noexcept
{
    try
    {
        HRESULT hr = S_OK;

        // Check args
        if (SP_IS_BAD_INTERFACE_PTR(pOutputSite) ||
            SP_IS_BAD_READ_PTR(pTextFragList))
        {
            return E_INVALIDARG;
        }
        if (!m_synthesizer)
        {
            return SPERR_UNINITIALIZED;
        }

        m_pOutputSite = pOutputSite;
        BuildSSML(pTextFragList);
        m_synthesisCompleted = false;
        m_synthesisResult.reset();

        RETONFAIL(CheckSynthesisResult(m_synthesizer->StartSpeakingSsml(m_ssml)));

        while (!(pOutputSite->GetActions() & SPVES_ABORT) && !m_synthesisCompleted)
        {
            if (pOutputSite->GetActions() & SPVES_SKIP)
            {
                // Skipping is not supported
                pOutputSite->CompleteSkip(0);
            }
            Sleep(50); // reduce CPU usage
        }

        m_synthesizer->StopSpeakingAsync().wait();

        if (m_synthesisResult)
        {
            RETONFAIL(CheckSynthesisResult(m_synthesisResult));
        }

        return S_OK;
    }
    catch (const std::bad_alloc&)
    {
        return E_OUTOFMEMORY;
    }
    catch (...) // C++ exceptions should not cross COM boundary
    {
        return E_FAIL;
    }
} /* CTTSEngine::Speak */

STDMETHODIMP CTTSEngine::GetOutputFormat(const GUID* pTargetFormatId, const WAVEFORMATEX* pTargetWaveFormatEx,
    GUID* pDesiredFormatId, WAVEFORMATEX** ppCoMemDesiredWaveFormatEx) noexcept
{
    // Embedded voice only supports 24kHz 16Bit mono
    return SpConvertStreamFormatEnum(SPSF_24kHz16BitMono, pDesiredFormatId, ppCoMemDesiredWaveFormatEx);
}


// Other Member Functions

HRESULT CTTSEngine::InitPhoneConverter()
{
    CComPtr<ISpDataKey> pAttrKey;
    RETONFAIL(m_cpToken->OpenKey(SPTOKENKEY_ATTRIBUTES, &pAttrKey));

    CSpDynamicString pszLang;
    RETONFAIL(pAttrKey->GetStringValue(L"Language", &pszLang));

    LANGID lang = (LANGID)wcstoul(pszLang, nullptr, 16); // hexadecimal without 0x prefix, e.g. 409 (=0x409)
    if (lang == 0)
        return E_INVALIDARG;

    WCHAR szLocale[LOCALE_NAME_MAX_LENGTH] = { 0 };
    if (LCIDToLocaleName(MAKELCID(lang, SORT_DEFAULT), szLocale, LOCALE_NAME_MAX_LENGTH, 0) == 0)
        return E_INVALIDARG;
    m_localeName = szLocale;

    return SpCreatePhoneConverter(lang, nullptr, nullptr, &m_phoneConverter);
}

HRESULT CTTSEngine::InitSynthesizer()
{
    CComPtr<ISpDataKey> pConfigKey;
    CSpDynamicString pszRegion, pszKey, pszPath, pszVoice;

    auto audioConfig = AudioConfig::FromStreamOutput(AudioOutputStream::CreatePushStream(
        [=](uint8_t* data, uint32_t len) -> int // returns bytes written
        {
            ULONG written = 0;
            m_pOutputSite->Write(data, len, &written);
            return written;
        }
    ));

    RETONFAIL(m_cpToken->OpenKey(L"NaturalVoiceConfig", &pConfigKey)); // this key must exist

    DWORD dwErrorMode;
    HRESULT hr = pConfigKey->GetDWORD(L"ErrorMode", &dwErrorMode);
    if (FAILED(hr)) dwErrorMode = 0;
    m_errorMode = (ErrorMode)std::clamp(dwErrorMode, 0UL, 2UL);

    RETONFAIL(pConfigKey->GetStringValue(L"Key", &pszKey));

    using Microsoft::CognitiveServices::Speech::Utils::Details::to_string;

    if (SUCCEEDED(hr = pConfigKey->GetStringValue(L"Region", &pszRegion)))
    {
        // if Region is specified, use online Azure service, Key is the subscription key
        auto config = SpeechConfig::FromSubscription(to_string((LPCWSTR)pszKey), to_string((LPCWSTR)pszRegion));

        config->SetSpeechSynthesisOutputFormat(SpeechSynthesisOutputFormat::Riff24Khz16BitMonoPcm);
        config->SetProperty(PropertyId::SpeechServiceResponse_RequestSentenceBoundary, "true");

        RETONFAIL(pConfigKey->GetStringValue(L"Voice", &pszVoice));
        
        config->SetSpeechSynthesisVoiceName(to_string((LPCWSTR)pszVoice));
        m_onlineVoiceName = pszVoice;

        if (m_errorMode == ProbeForError)
        {
            auto synthesizer = SpeechSynthesizer::FromConfig(config, nullptr);
            RETONFAIL(CheckSynthesisResult(synthesizer->SpeakText(""))); // test for possible error
        }

        m_synthesizer = SpeechSynthesizer::FromConfig(config, audioConfig);
    }
    else if (SUCCEEDED(hr = pConfigKey->GetStringValue(L"Path", &pszPath)))
    {
        // if Path is specified, use local voice model stored in Path, Key is the decryption key
        auto config = EmbeddedSpeechConfig::FromPath(to_string((LPCWSTR)pszPath));

        config->SetSpeechSynthesisOutputFormat(SpeechSynthesisOutputFormat::Riff24Khz16BitMonoPcm);
        config->SetProperty(PropertyId::SpeechServiceResponse_RequestSentenceBoundary, "true");

        // get the voice name by loading it first
        auto synthesizer = SpeechSynthesizer::FromConfig(config);
        auto result = synthesizer->GetVoicesAsync().get();
        if (result->Voices.empty())
            return E_INVALIDARG;
        config->SetSpeechSynthesisVoice(result->Voices[0]->Name, to_string((LPCWSTR)pszKey));

        if (m_errorMode == ProbeForError)
        {
            auto synthesizer = SpeechSynthesizer::FromConfig(config, nullptr);
            RETONFAIL(CheckSynthesisResult(synthesizer->SpeakText(""))); // test for possible error
        }

        m_synthesizer = SpeechSynthesizer::FromConfig(config, audioConfig);
    }
    else
    {
        // neither Region nor Path is specified
        return E_INVALIDARG;
    }

    return hr;
}

void CTTSEngine::SetupSynthesizerEvents()
{
    m_synthesizer->BookmarkReached += [=](const SpeechSynthesisBookmarkEventArgs& arg)
        {
            SPEVENT ev = { 0 };
            ev.ullAudioStreamOffset = WaveTicksToBytes(arg.AudioOffset);
            ev.eEventId = SPEI_TTS_BOOKMARK;
            ev.elParamType = SPET_LPARAM_IS_STRING;
            std::wstring bookmark = Microsoft::CognitiveServices::Speech::Utils::Details::to_string(arg.Text);
            ev.lParam = (LPARAM)bookmark.c_str();
            ev.wParam = atol(arg.Text.c_str());
            m_pOutputSite->AddEvents(&ev, 1);
        };

    m_synthesizer->WordBoundary += [=](const SpeechSynthesisWordBoundaryEventArgs& arg)
        {
            if (arg.BoundaryType == SpeechSynthesisBoundaryType::Punctuation)
                return;
            SPEVENT ev = { 0 };
            ev.ullAudioStreamOffset = WaveTicksToBytes(arg.AudioOffset);
            ev.eEventId = arg.BoundaryType == SpeechSynthesisBoundaryType::Sentence ? SPEI_SENTENCE_BOUNDARY : SPEI_WORD_BOUNDARY;
            ev.elParamType = SPET_LPARAM_IS_UNDEFINED;
            ULONG offset = arg.TextOffset, length = arg.WordLength;
            MapTextOffset(offset, length);
            ev.lParam = offset;
            ev.wParam = length;
            m_pOutputSite->AddEvents(&ev, 1);
        };

    m_synthesizer->VisemeReceived += [=](const SpeechSynthesisVisemeEventArgs& arg)
        {
            SPEVENT ev = { 0 };
            ev.ullAudioStreamOffset = WaveTicksToBytes(arg.AudioOffset);
            ev.eEventId = SPEI_VISEME;
            ev.elParamType = SPET_LPARAM_IS_UNDEFINED;
            ev.wParam = 0;
            // Cognitive Speech uses the same viseme ID values as SAPI 
            ev.lParam = MAKELONG(arg.VisemeId, 0);
            m_pOutputSite->AddEvents(&ev, 1);
        };

    m_synthesizer->SynthesisCompleted += [=](const SpeechSynthesisEventArgs& arg)
        {
            m_synthesisResult = arg.Result;
            m_synthesisCompleted = true;
        };

    m_synthesizer->SynthesisCanceled += [=](const SpeechSynthesisEventArgs& arg)
        {
            m_synthesisResult = arg.Result;
            m_synthesisCompleted = true;
        };
}

void CTTSEngine::AppendTextFragToSsml(const SPVTEXTFRAG* pTextFrag)
{
    // entities are converted to characters before passing in, so we have to convert it back to XML
    // these entities are processed: &lt;&gt;&amp;&quot;&apos;

    LPCWSTR pEnd = pTextFrag->pTextStart + pTextFrag->ulTextLen;

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
        m_offsetMappings.push_back({ pTextFrag->ulTextSrcOffset + (ULONG)(pCh - pTextFrag->pTextStart) + 1, (ULONG)m_ssml.size() });
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

    LPCWSTR cat = context.pCategory;

    // standard: 'date_xxx'
    // when parsed from SSML, 'date:xxx' is used when format isn't standard
    if (_wcsnicmp(cat, L"date", 4) == 0
        && (cat[4] == '_' || cat[4] == ':')) 
    {
        // <context id='date_dmy'> to <say-as interpret-as='date' format='dmy'>
        m_ssml.append(L"date' format='");
        if (_wcsicmp(cat + 5, L"year") == 0)
            m_ssml.push_back('y');
        else
            m_ssml.append(cat + 5);
    }
    else if (_wcsicmp(cat, L"number_cardinal") == 0)
        m_ssml.append(L"cardinal");
    else if (_wcsicmp(cat, L"number_fraction") == 0)
        m_ssml.append(L"fraction");
    else if (_wcsicmp(cat, L"phone_number") == 0)
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

    m_offsetMappings.clear();
    m_mappingIndex = 0;

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

        if (!isInEmphasisTag && pTextFrag->State.EmphAdj)
        {
            m_ssml.append(L"<emphasis>"); // (not supported by offline TTS)
            isInEmphasisTag = true;
        }

        // NOTE: <say-as> tag cannot contain child tags.
        // if eAction is not Speak, a child tag will be added, which is incompatible with <say-as>
        // so only add <say-as> when eAction is Speak
        if (!isInSayAsTag && pTextFrag->State.Context.pCategory
            && pTextFrag->State.eAction == SPVA_Speak)
        {
            // map <context id='xxx'>...</context>
            // to <say-as interpret-as='xxx' format='xxx'>...</say-as>
            AppendSAPIContextToSsml(pTextFrag->State.Context);
            isInSayAsTag = true;
        }

        switch (pTextFrag->State.eAction)
        {
        case SPVA_Speak:
            m_offsetMappings.push_back({ pTextFrag->ulTextSrcOffset, (ULONG)m_ssml.size() });
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
            m_offsetMappings.push_back({ pTextFrag->ulTextSrcOffset, (ULONG)m_ssml.size() });
            AppendTextFragToSsml(pTextFrag);
            m_ssml.append(L"</say-as>");
            break;

        case SPVA_Pronounce: // insert a <phoneme alphabet='sapi' ph='xx'>...</phoneme>
            m_ssml.append(L"<phoneme alphabet='sapi' ph='");
            AppendPhonemesToSsml(pTextFrag->State.pPhoneIds);
            m_ssml.append(L"'>");
            m_offsetMappings.push_back({ pTextFrag->ulTextSrcOffset, (ULONG)m_ssml.size() });
            AppendTextFragToSsml(pTextFrag);
            m_ssml.append(L"</phoneme>");
            break;

        case SPVA_ParseUnknownTag: // insert it into SSML as-is
            // TODO: Custom XML tags may not be closed properly
            // when using VOICE tags to switch away from this voice
            m_ssml.append(pTextFrag->pTextStart, pTextFrag->ulTextLen);
            break;
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

    // all mapping pairs in m_offsetMappings go from low offset to high offset,
    // so we just move the index forward as the speaking progresses
    // but if index goes beyond border, or the current offset surpasses the actual offset, reset index to 0
    if (m_mappingIndex >= m_offsetMappings.size() || m_offsetMappings[m_mappingIndex].ulSSMLTextOffset > ulSSMLOffset)
        m_mappingIndex = 0;

    // if we surpass the next offset, move forward, until the actual offset is between current and next
    while (m_mappingIndex + 1 < m_offsetMappings.size() && ulSSMLOffset >= m_offsetMappings[m_mappingIndex + 1].ulSSMLTextOffset)
        m_mappingIndex++;

    const auto& mapping = m_offsetMappings[m_mappingIndex];
    // if offset falls below zero, set it to zero
    if (mapping.ulSSMLTextOffset > mapping.ulSAPITextOffset && ulSSMLOffset < mapping.ulSSMLTextOffset - mapping.ulSAPITextOffset)
        ulSSMLOffset = 0;
    else
        ulSSMLOffset = ulSSMLOffset - mapping.ulSSMLTextOffset + mapping.ulSAPITextOffset;

    // if the end position (ulSSMLOffset + ulTextLen) also get remapped
    // (for example when '&' becomes '&amp;')
    // adjust ulTextLen as well
    // first find which range the end position is in, just like the above
    auto index = m_mappingIndex;
    while (index + 1 < m_offsetMappings.size() && endOffset >= m_offsetMappings[index + 1].ulSSMLTextOffset)
        index++;
    if (index == m_mappingIndex)
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
    if (m_errorMode == ShowMessageOnError)
    {
        MessageBoxA(NULL, details->ErrorDetails.c_str(), "NaturalVoiceSAPIAdapter", MB_ICONEXCLAMATION | MB_SYSTEMMODAL);
    }
    return E_FAIL;
}
