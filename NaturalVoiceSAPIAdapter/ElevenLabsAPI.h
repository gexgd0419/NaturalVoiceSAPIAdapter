#pragma once
#include <string>
#include <future>
#include <functional>
#include <stop_token>
#include <cstdint>
#include <nlohmann/json.hpp>

// ElevenLabs TTS REST API client.
// Authentication: xi-api-key HTTP header.
// Output audio: PCM 24 kHz 16-bit mono (output_format=pcm_24000) – no decoding needed.
// SSML: not supported by ElevenLabs; all XML tags are stripped, plain text is sent.
class ElevenLabsAPI
{
public:
    // Audio output callback – returns number of bytes consumed.
    std::function<int(uint8_t*, uint32_t)> AudioReceivedCallback;

    // Called when synthesis finishes, with total PCM bytes written.
    std::function<void(uint32_t)> SessionEndCallback;

    void SetCredentials(std::string apiKey, std::string model);

    // ssml    – SSML built by BuildSSML(); XML tags are stripped, plain text is sent.
    //           The referenced wstring must remain valid until the returned future completes.
    // voiceId – ElevenLabs voice ID, e.g. "JBFqnCBsd6RMkjVDRZzb"
    std::future<void> SpeakAsync(const std::wstring& ssml,
                                  const std::string&  voiceId);
    void Stop();

    uint64_t GetWaveBytesWritten() const noexcept { return m_waveBytesWritten; }

    // Fetch all voices for the given API key (handles pagination automatically).
    // Returns a JSON array of voice objects matching the /v2/voices response schema.
    static nlohmann::json GetVoiceList(const std::string& apiKey);

private:
    std::string  m_apiKey;
    std::string  m_model;

    std::wstring_view m_ssml;    // view into TTSEngine::m_ssml – valid while future runs
    std::string       m_voiceId;

    std::stop_source m_stopSource;
    uint64_t         m_waveBytesWritten = 0;

    void DoSpeakAsync();
};
