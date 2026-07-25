#pragma once
#include <string>
#include <future>
#include <functional>
#include <stop_token>
#include <cstdint>
#include "Mp3Decoder.h"
#include <nlohmann/json.hpp>

// Amazon Polly TTS REST API client.
// Interface mirrors SpeechRestAPI so TTSEngine can handle both uniformly.
class AmazonPollyAPI
{
public:
    // Audio/event callbacks – same signatures as SpeechRestAPI
    std::function<int(uint8_t*, uint32_t)>                          AudioReceivedCallback;
    std::function<void(uint64_t, uint32_t, uint32_t)>              WordBoundaryCallback;
    std::function<void(uint64_t, uint32_t, uint32_t)>              SentenceBoundaryCallback;
    std::function<void(uint64_t, std::wstring)>                    BookmarkCallback;
    std::function<void(uint64_t)>                                  SessionEndCallback;

    void SetCredentials(std::string accessKeyId, std::string secretKey, std::string region);

    // ssml     – SSML built by BuildSSML(); must remain valid until the future is done
    // voiceId  – Polly voice ID, e.g. "Joanna"
    // engine   – "neural" | "standard" | "long-form" | "generative"
    std::future<void> SpeakAsync(const std::wstring& ssml,
                                  const std::string&  voiceId,
                                  const std::string&  engine);
    void Stop();

    uint64_t GetWaveBytesWritten() const noexcept { return m_waveBytesWritten; }

    // Fetch the list of available voices directly from Polly (no local cache).
    // Returns a JSON array in the same shape as the Polly /v1/voices response.
    static nlohmann::json GetVoiceList(const std::string& accessKeyId,
                                        const std::string& secretKey,
                                        const std::string& region,
                                        const std::string& engine = "neural");

private:
    std::string m_accessKeyId;
    std::string m_secretKey;
    std::string m_region;

    std::wstring_view m_ssml;   // view into TTSEngine::m_ssml – valid while future runs
    std::string       m_voiceId;
    std::string       m_engine;

    std::stop_source m_stopSource;
    uint64_t         m_waveBytesWritten = 0;

    // AWS SigV4 result
    struct SigV4Result
    {
        std::string datetime;       // YYYYMMDDTHHmmssZ
        std::string authorization;  // full Authorization header value
    };

    SigV4Result ComputeSigV4(const std::string& method,
                              const std::string& path,
                              const std::string& query,
                              const std::string& host,
                              const std::string& body) const;

    void DoSpeakAsync();
};
