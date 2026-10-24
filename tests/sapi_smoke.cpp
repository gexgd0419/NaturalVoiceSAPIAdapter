// Direct DLL loading and memory-only SAPI tokens: no registration/installation.
#include <windows.h>
#include <sapi.h>
#include <sapiddk.h>
#include <sphelper.h>
#include <atlbase.h>
#include <stdio.h>
#include "../NaturalVoiceSAPIAdapter/SpeechServiceConstants.h"

static HRESULT Copy(LPCWSTR s, LPWSTR* out) {
    size_t bytes = (wcslen(s) + 1) * sizeof(wchar_t);
    *out = (LPWSTR)CoTaskMemAlloc(bytes);
    if (!*out) return E_OUTOFMEMORY;
    memcpy(*out, s, bytes);
    return S_OK;
}
class Key : public ISpDataKey {
    LONG refs = 1;
    unsigned kind;
    LPCWSTR voice;

  public:
    Key(unsigned k, LPCWSTR v) : kind(k), voice(v) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** p) override {
        *p = nullptr;
        if (iid != IID_IUnknown && iid != __uuidof(ISpDataKey)) return E_NOINTERFACE;
        *p = this;
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&refs); }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG n = InterlockedDecrement(&refs);
        if (!n) delete this;
        return n;
    }
    HRESULT STDMETHODCALLTYPE SetData(LPCWSTR, ULONG, const BYTE*) override { return E_ACCESSDENIED; }
    HRESULT STDMETHODCALLTYPE GetData(LPCWSTR, ULONG*, BYTE*) override { return SPERR_NOT_FOUND; }
    HRESULT STDMETHODCALLTYPE SetStringValue(LPCWSTR, LPCWSTR) override { return E_ACCESSDENIED; }
    HRESULT STDMETHODCALLTYPE GetStringValue(LPCWSTR name, LPWSTR* out) override {
        *out = nullptr;
        if (!name || !*name) return Copy(L"Memory-only test voice", out);
        if (kind == 1 && !_wcsicmp(name, L"Voice")) return Copy(voice, out);
        if (kind == 1 && !_wcsicmp(name, L"WebsocketURL")) return Copy(EDGE_WEBSOCKET_URL, out);
        if (kind == 2 && !_wcsicmp(name, L"Language")) return Copy(!wcsncmp(voice, L"zh-", 3) ? L"804" : L"409", out);
        if (kind == 2 && !_wcsicmp(name, L"Locale")) return Copy(!wcsncmp(voice, L"zh-", 3) ? L"zh-CN" : L"en-US", out);
        return SPERR_NOT_FOUND;
    }
    HRESULT STDMETHODCALLTYPE SetDWORD(LPCWSTR, DWORD) override { return E_ACCESSDENIED; }
    HRESULT STDMETHODCALLTYPE GetDWORD(LPCWSTR name, DWORD* out) override {
        if (kind == 1 && (!_wcsicmp(name, L"ErrorMode") || !_wcsicmp(name, L"IsEdgeVoice"))) {
            *out = 1;
            return S_OK;
        }
        return SPERR_NOT_FOUND;
    }
    HRESULT STDMETHODCALLTYPE OpenKey(LPCWSTR name, ISpDataKey** out) override {
        *out = nullptr;
        if (kind) return SPERR_NOT_FOUND;
        unsigned k = !_wcsicmp(name, L"NaturalVoiceConfig") ? 1 : !_wcsicmp(name, L"Attributes") ? 2 : 0;
        if (!k) return SPERR_NOT_FOUND;
        *out = new Key(k, voice);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE CreateKey(LPCWSTR, ISpDataKey**) override { return E_ACCESSDENIED; }
    HRESULT STDMETHODCALLTYPE DeleteKey(LPCWSTR) override { return E_ACCESSDENIED; }
    HRESULT STDMETHODCALLTYPE DeleteValue(LPCWSTR) override { return E_ACCESSDENIED; }
    HRESULT STDMETHODCALLTYPE EnumKeys(ULONG, LPWSTR*) override { return SPERR_NO_MORE_ITEMS; }
    HRESULT STDMETHODCALLTYPE EnumValues(ULONG, LPWSTR*) override { return SPERR_NO_MORE_ITEMS; }
};

class Site : public ISpTTSEngineSite {
  public:
    ULONG bytes = 0, words = 0, sentences = 0, bookmarks = 0, visemes = 0;
    bool nonzero = false;
    ULONGLONG start = GetTickCount64(), abortAfter = 60000;
    FILE* audio = nullptr;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** p) override {
        *p = nullptr;
        if (iid != IID_IUnknown && iid != __uuidof(ISpTTSEngineSite) && iid != __uuidof(ISpEventSink))
            return E_NOINTERFACE;
        *p = this;
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return 2; }
    ULONG STDMETHODCALLTYPE Release() override { return 1; }
    HRESULT STDMETHODCALLTYPE AddEvents(const SPEVENT* events, ULONG count) override {
        for (ULONG i = 0; i < count; i++) {
            auto& e = events[i];
            if (e.eEventId == SPEI_WORD_BOUNDARY) words++;
            if (e.eEventId == SPEI_SENTENCE_BOUNDARY) sentences++;
            if (e.eEventId == SPEI_TTS_BOOKMARK) bookmarks++;
            if (e.eEventId == SPEI_VISEME) visemes++;
            printf("event=%u audio=%llu text=%lld length=%llu\n", e.eEventId, e.ullAudioStreamOffset,
                   (long long)e.lParam, (unsigned long long)e.wParam);
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetEventInterest(ULONGLONG* p) override {
        *p = SPFEI_ALL_TTS_EVENTS;
        return S_OK;
    }
    DWORD STDMETHODCALLTYPE GetActions() override { return GetTickCount64() - start >= abortAfter ? SPVES_ABORT : 0; }
    HRESULT STDMETHODCALLTYPE Write(const void* p, ULONG n, ULONG* written) override {
        *written = n;
        bytes += n;
        const BYTE* data = (const BYTE*)p;
        for (ULONG i = 0; i < n; i++)
            if (data[i]) {
                nonzero = true;
                break;
            }
        if (audio && fwrite(p, 1, n, audio) != n) return E_FAIL;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetRate(long* p) override {
        *p = 0;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetVolume(USHORT* p) override {
        *p = 100;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetSkipInfo(SPVSKIPTYPE*, long*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE CompleteSkip(long) override { return S_OK; }
};

static void Check(HRESULT hr, const char* action) {
    if (FAILED(hr)) {
        printf("FAIL: %s HRESULT=0x%08lX\n", action, (ULONG)hr);
        ExitProcess(1);
    }
}
int wmain(int argc, wchar_t** argv) {
    if (argc < 3) {
        puts("Usage: sapi_smoke DLL-path voice-name [abort-ms] [wav-path]");
        return 2;
    }
    Check(CoInitializeEx(nullptr, COINIT_MULTITHREADED), "COM init");
    HMODULE dll = LoadLibraryExW(argv[1], nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!dll) {
        printf("LoadLibrary error=%lu\n", GetLastError());
        return 1;
    }
    auto getClass = (HRESULT(WINAPI*)(REFCLSID, REFIID, void**))GetProcAddress(dll, "DllGetClassObject");
    if (!getClass) return 1;
    CLSID cls;
    Check(CLSIDFromString(L"{013ab33b-ad1a-401c-8bee-f6e2b046a94e}", &cls), "CLSID");
    CComPtr<IClassFactory> factory;
    Check(getClass(cls, IID_PPV_ARGS(&factory)), "factory");
    CComPtr<ISpTTSEngine> engine;
    Check(factory->CreateInstance(nullptr, IID_PPV_ARGS(&engine)), "engine");
    CComPtr<ISpObjectTokenInit> tokenInit;
    CComPtr<ISpDataKey> key;
    CComPtr<ISpObjectToken> token;
    bool local = !_wcsicmp(argv[2], L"local");
    if (local) {
        CLSID enumCls;
        Check(CLSIDFromString(L"{b8b9e38f-e5a2-4661-9fde-4ac7377aa6f6}", &enumCls), "enumerator CLSID");
        CComPtr<IClassFactory> enumFactory;
        Check(getClass(enumCls, IID_PPV_ARGS(&enumFactory)), "enumerator factory");
        CComPtr<IEnumSpObjectTokens> voices;
        Check(enumFactory->CreateInstance(nullptr, IID_PPV_ARGS(&voices)), "enumerate voices");
        ULONG count = 0;
        Check(voices->GetCount(&count), "voice count");
        printf("Enumerated %lu tokens\n", count);
        for (ULONG i = 0; i < count; i++) {
            CComPtr<ISpObjectToken> candidate;
            CComPtr<ISpDataKey> config;
            CSpDynamicString path;
            Check(voices->Item(i, &candidate), "voice item");
            if (SUCCEEDED(candidate->OpenKey(L"NaturalVoiceConfig", &config)) &&
                SUCCEEDED(config->GetStringValue(L"Path", &path))) {
                token = candidate;
                wprintf(L"Local voice path: %ls\n", (LPCWSTR)path);
                break;
            }
        }
        if (!token) {
            puts("SKIP: no local Narrator natural voice available");
            return 3;
        }
    } else {
        Check(tokenInit.CoCreateInstance(CLSID_SpObjectToken), "token");
        key.Attach(new Key(0, argv[2]));
        Check(tokenInit->InitFromDataKey(SPCAT_VOICES, L"MemoryOnlyNaturalVoiceTest", key), "memory token");
        Check(tokenInit->QueryInterface(&token), "token interface");
    }
    CComQIPtr<ISpObjectWithToken> withToken(engine);
    Check(withToken->SetObjectToken(token), "SetObjectToken");
    GUID format;
    WAVEFORMATEX* wave = nullptr;
    Check(engine->GetOutputFormat(nullptr, nullptr, &format, &wave), "format");
    printf("format: rate=%lu bits=%u channels=%u\n", wave->nSamplesPerSec, wave->wBitsPerSample, wave->nChannels);
    const wchar_t* text = !wcsncmp(argv[2], L"zh-", 3) ? L"你好，世界。这是联网朗读测试。"
                                                       : L"Hello world. This is an online speech test.";
    SPVTEXTFRAG frag = {};
    frag.State.eAction = SPVA_Speak;
    frag.State.LangID = !wcsncmp(argv[2], L"zh-", 3) ? 0x804 : 0x409;
    frag.State.Volume = 100;
    frag.pTextStart = text;
    frag.ulTextLen = (ULONG)wcslen(text);
    SPVTEXTFRAG bookmark = {};
    bookmark.State = frag.State;
    bookmark.State.eAction = SPVA_Bookmark;
    bookmark.pTextStart = L"test-bookmark";
    bookmark.ulTextLen = 13;
    bookmark.ulTextSrcOffset = frag.ulTextLen;
    frag.pNext = &bookmark;
    Site site;
    if (argc > 3) site.abortAfter = wcstoul(argv[3], nullptr, 10);
    if (argc > 4) {
        _wfopen_s(&site.audio, argv[4], L"wb");
        if (!site.audio) return 1;
        BYTE header[44] = {};
        fwrite(header, 1, 44, site.audio);
    }
    HRESULT hr = engine->Speak(0, format, wave, &frag, &site);
    if (site.audio) {
        DWORD riff = site.bytes + 36, fmt = 16;
        rewind(site.audio);
        fwrite("RIFF", 1, 4, site.audio);
        fwrite(&riff, 4, 1, site.audio);
        fwrite("WAVEfmt ", 1, 8, site.audio);
        fwrite(&fmt, 4, 1, site.audio);
        fwrite(wave, 1, 16, site.audio);
        fwrite("data", 1, 4, site.audio);
        fwrite(&site.bytes, 4, 1, site.audio);
        fclose(site.audio);
    }
    printf(
        "Speak HRESULT=0x%08lX bytes=%lu nonzero=%u words=%lu sentences=%lu bookmarks=%lu visemes=%lu elapsed=%llu "
        "ms\n",
        (ULONG)hr, site.bytes, site.nonzero, site.words, site.sentences, site.bookmarks, site.visemes,
        GetTickCount64() - site.start);
    bool cancelled = site.abortAfter < 60000;
    bool pass = SUCCEEDED(hr) &&
                (cancelled ? GetTickCount64() - site.start < 10000
                           : site.nonzero && site.words && (local || site.sentences) && site.bookmarks && site.visemes);
    CoTaskMemFree(wave);
    withToken.Release();
    engine.Release();
    factory.Release();
    token.Release();
    tokenInit.Release();
    key.Release();
    // Leave the DLL loaded until process exit: its workers use process detach cleanup.
    CoUninitialize();
    return pass ? 0 : 1;
}
