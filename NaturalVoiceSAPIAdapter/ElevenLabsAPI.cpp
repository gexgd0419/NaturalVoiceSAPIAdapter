#include "pch.h"
#define ASIO_STANDALONE 1
#include <asio.hpp>
#include <asio/ssl.hpp>
#include "ElevenLabsAPI.h"
#include "StrUtils.h"
#include "Logger.h"
#include <array>
#include <format>
#include <limits>

static constexpr const char* ELEVENLABS_HOST = "api.elevenlabs.io";

// ─────────────────────────────────────────────────────────────────────────────
// Minimal HTTP/1.1 helpers (same ASIO + OpenSSL stack as AmazonPollyAPI)
// ─────────────────────────────────────────────────────────────────────────────

struct EL_ParsedHttpResponse
{
    int         statusCode = 0;
    std::string body;
};

static EL_ParsedHttpResponse EL_ParseHttpResponse(const std::string& raw)
{
    EL_ParsedHttpResponse res;

    const size_t headerEnd = raw.find("\r\n\r\n");
    if (headerEnd == std::string::npos)
        throw std::runtime_error("ElevenLabs: invalid HTTP response (no header delimiter)");

    // Status line: "HTTP/1.1 200 OK"
    if (raw.size() > 12)
        res.statusCode = std::stoi(raw.substr(9, 3));

    // Collect headers needed for dechunking
    std::string transferEncoding;
    size_t pos = raw.find("\r\n") + 2;
    while (pos < headerEnd)
    {
        const size_t next = raw.find("\r\n", pos);
        const size_t end  = (next == std::string::npos) ? headerEnd : next;
        const std::string line = raw.substr(pos, end - pos);
        const size_t colon = line.find(':');
        if (colon != std::string::npos)
        {
            std::string k = line.substr(0, colon);
            std::string v = line.substr(colon + 1);
            if (!v.empty() && v.front() == ' ') v.erase(v.begin());
            for (char& c : k) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (k == "transfer-encoding") transferEncoding = v;
        }
        pos = end + 2;
    }

    res.body = raw.substr(headerEnd + 4);

    // Dechunk if Transfer-Encoding: chunked
    if (transferEncoding.find("chunked") != std::string::npos)
    {
        std::string dechunked;
        size_t p = 0;
        while (p < res.body.size())
        {
            const size_t nl = res.body.find("\r\n", p);
            if (nl == std::string::npos) break;
            const size_t chunkSize = std::stoul(res.body.substr(p, nl - p), nullptr, 16);
            if (chunkSize == 0) break;
            p = nl + 2;
            if (p + chunkSize > res.body.size()) break;
            dechunked.append(res.body, p, chunkSize);
            p += chunkSize + 2;
        }
        res.body = std::move(dechunked);
    }

    return res;
}

template <class Func>
static auto EL_WithSslStream(const std::string& host,
                              std::stop_token    stopToken,
                              Func&&             fn)
    -> decltype(fn(std::declval<asio::ssl::stream<asio::ip::tcp::socket>&>()))
{
    asio::io_context        ioctx;
    asio::ssl::context      sslctx(asio::ssl::context::sslv23_client);
    asio::ssl::stream<asio::ip::tcp::socket> stream(ioctx, sslctx);

    std::stop_callback stopCb(stopToken, [&stream]() {
        asio::error_code ec;
        stream.lowest_layer().close(ec);
    });

    if (stopToken.stop_requested()) return {};

    auto resolved = asio::ip::tcp::resolver(ioctx).resolve(host, "443");
    stream.next_layer().connect(*resolved);
    stream.handshake(asio::ssl::stream_base::client);

    return fn(stream);
}

static std::string EL_HttpsRequest(const std::string& method,
                                    const std::string& host,
                                    const std::string& pathAndQuery,
                                    const std::string& body,
                                    const std::string& apiKey,
                                    std::stop_token    stopToken = {})
{
    return EL_WithSslStream(host, stopToken,
        [&](asio::ssl::stream<asio::ip::tcp::socket>& stream) -> std::string
        {
            std::string req =
                method + " " + pathAndQuery + " HTTP/1.1\r\n"
                "Host: " + host + "\r\n"
                "xi-api-key: " + apiKey + "\r\n"
                "Content-Type: application/json\r\n"
                "Connection: close\r\n";
            if (!body.empty())
                req += "Content-Length: " + std::to_string(body.size()) + "\r\n";
            req += "\r\n";
            req += body;

            asio::write(stream, asio::buffer(req));

            std::string response;
            asio::error_code ec;
            asio::read(stream, asio::dynamic_string_buffer(response), ec);

            if (ec != asio::error::eof &&
                ec != asio::ssl::error::stream_truncated &&
                ec)
            {
                if (stopToken.stop_requested()) return {};
                asio::detail::throw_error(ec);
            }
            return response;
        });
}

struct EL_HttpResponseHeader
{
    int  statusCode = 0;
    bool chunked    = false;
};

static std::string EL_ExtractErrorMessage(const std::string& body);

static EL_HttpResponseHeader EL_ParseHttpResponseHeader(std::string_view header)
{
    EL_HttpResponseHeader result;

    const size_t statusLineEnd = header.find("\r\n");
    if (statusLineEnd == std::string_view::npos || header.size() < 12)
        throw std::runtime_error("ElevenLabs: invalid HTTP response status line");

    result.statusCode = std::stoi(std::string(header.substr(9, 3)));

    size_t pos = statusLineEnd + 2;
    while (pos < header.size())
    {
        const size_t lineEnd = header.find("\r\n", pos);
        if (lineEnd == std::string_view::npos || lineEnd == pos)
            break;

        const std::string_view line = header.substr(pos, lineEnd - pos);
        const size_t colon = line.find(':');
        if (colon != std::string_view::npos)
        {
            std::string name(line.substr(0, colon));
            std::string value(line.substr(colon + 1));
            for (char& c : name)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            for (char& c : value)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

            if (name == "transfer-encoding" && value.find("chunked") != std::string::npos)
                result.chunked = true;
        }
        pos = lineEnd + 2;
    }

    return result;
}

// Sends a request to an endpoint that returns audio as HTTP chunks.  Audio is
// forwarded as it arrives instead of waiting for the server to close the
// connection, which keeps cancellation responsive during long utterances.
template <class AudioCallback>
static bool EL_HttpsRequestStream(const std::string& method,
                                  const std::string& host,
                                  const std::string& pathAndQuery,
                                  const std::string& body,
                                  const std::string& apiKey,
                                  std::stop_token    stopToken,
                                  AudioCallback&&    onAudio)
{
    return EL_WithSslStream(host, stopToken,
        [&](asio::ssl::stream<asio::ip::tcp::socket>& stream) -> bool
        {
            std::string req =
                method + " " + pathAndQuery + " HTTP/1.1\r\n"
                "Host: " + host + "\r\n"
                "xi-api-key: " + apiKey + "\r\n"
                "Content-Type: application/json\r\n"
                "Connection: close\r\n";
            if (!body.empty())
                req += "Content-Length: " + std::to_string(body.size()) + "\r\n";
            req += "\r\n";
            req += body;

            asio::write(stream, asio::buffer(req));

            std::string pending;
            asio::error_code ec;
            asio::read_until(stream, asio::dynamic_string_buffer(pending), "\r\n\r\n", ec);
            if (ec)
            {
                if (stopToken.stop_requested()) return false;
                asio::detail::throw_error(ec);
            }

            const size_t headerEnd = pending.find("\r\n\r\n");
            if (headerEnd == std::string::npos)
                throw std::runtime_error("ElevenLabs: invalid HTTP response (no header delimiter)");

            const std::string header = pending.substr(0, headerEnd + 4);
            pending.erase(0, headerEnd + 4);
            const EL_HttpResponseHeader responseHeader = EL_ParseHttpResponseHeader(header);

            if (responseHeader.statusCode != 200)
            {
                asio::read(stream, asio::dynamic_string_buffer(pending), ec);
                if (ec != asio::error::eof &&
                    ec != asio::ssl::error::stream_truncated &&
                    ec)
                {
                    if (stopToken.stop_requested()) return false;
                    asio::detail::throw_error(ec);
                }

                const auto response = EL_ParseHttpResponse(header + pending);
                LogTrace("ElevenLabs: Error response body: {}", response.body.substr(0, 500));
                throw std::runtime_error(
                    "ElevenLabs API error " + std::to_string(response.statusCode) +
                    ": " + EL_ExtractErrorMessage(response.body));
            }

            std::array<char, 4096> pcmBuffer;
            size_t pcmSize = 0;
            auto appendAudio = [&](const char* data, size_t size) -> bool
            {
                while (size != 0)
                {
                    const size_t copied = std::min(size, pcmBuffer.size() - pcmSize);
                    memcpy(pcmBuffer.data() + pcmSize, data, copied);
                    pcmSize += copied;
                    data += copied;
                    size -= copied;

                    if (pcmSize == pcmBuffer.size())
                    {
                        if (stopToken.stop_requested() ||
                            !onAudio(reinterpret_cast<const uint8_t*>(pcmBuffer.data()),
                                     static_cast<uint32_t>(pcmSize)))
                            return false;
                        pcmSize = 0;
                    }
                }
                return true;
            };

            auto flushAudio = [&]() -> bool
            {
                if (pcmSize == 0) return true;
                if (pcmSize % 2 != 0)
                    throw std::runtime_error("ElevenLabs: streamed PCM response has an incomplete sample");
                if (stopToken.stop_requested() ||
                    !onAudio(reinterpret_cast<const uint8_t*>(pcmBuffer.data()),
                             static_cast<uint32_t>(pcmSize)))
                    return false;
                pcmSize = 0;
                return true;
            };

            auto readMore = [&]() -> bool
            {
                std::array<char, 4096> buffer;
                const size_t read = stream.read_some(asio::buffer(buffer), ec);
                if (read != 0)
                    pending.append(buffer.data(), read);

                if (ec)
                {
                    if (stopToken.stop_requested()) return false;
                    if (ec == asio::error::eof || ec == asio::ssl::error::stream_truncated)
                        return false;
                    asio::detail::throw_error(ec);
                }
                return read != 0;
            };

            auto readLine = [&]() -> std::optional<std::string>
            {
                for (;;)
                {
                    const size_t lineEnd = pending.find("\r\n");
                    if (lineEnd != std::string::npos)
                    {
                        std::string line = pending.substr(0, lineEnd);
                        pending.erase(0, lineEnd + 2);
                        return line;
                    }
                    if (!readMore())
                    {
                        if (stopToken.stop_requested()) return std::nullopt;
                        throw std::runtime_error("ElevenLabs: incomplete HTTP chunk header");
                    }
                }
            };

            auto readChunkData = [&](size_t size) -> bool
            {
                while (size != 0)
                {
                    if (pending.empty())
                    {
                        if (!readMore())
                        {
                            if (stopToken.stop_requested()) return false;
                            throw std::runtime_error("ElevenLabs: incomplete HTTP chunk data");
                        }
                    }

                    const size_t consumed = std::min(size, pending.size());
                    if (!appendAudio(pending.data(), consumed)) return false;
                    pending.erase(0, consumed);
                    size -= consumed;
                }
                return true;
            };

            if (!responseHeader.chunked)
            {
                for (;;)
                {
                    if (!pending.empty())
                    {
                        if (!appendAudio(pending.data(), pending.size())) return false;
                        pending.clear();
                        continue;
                    }
                    if (!readMore()) break;
                }
                return stopToken.stop_requested() ? false : flushAudio();
            }

            for (;;)
            {
                const auto line = readLine();
                if (!line) return false;

                const size_t extensionStart = line->find(';');
                const std::string chunkSizeText = line->substr(0, extensionStart);
                size_t parsed = 0;
                const unsigned long long chunkSize = std::stoull(chunkSizeText, &parsed, 16);
                if (parsed != chunkSizeText.size() ||
                    chunkSize > static_cast<unsigned long long>(std::numeric_limits<size_t>::max()))
                    throw std::runtime_error("ElevenLabs: invalid HTTP chunk size");

                if (chunkSize == 0)
                {
                    // Discard optional trailer headers.
                    for (;;)
                    {
                        const auto trailer = readLine();
                        if (!trailer) return false;
                        if (trailer->empty())
                            return stopToken.stop_requested() ? false : flushAudio();
                    }
                }

                if (!readChunkData(static_cast<size_t>(chunkSize))) return false;

                std::array<char, 2> chunkTerminator;
                for (size_t offset = 0; offset < chunkTerminator.size();)
                {
                    if (pending.empty())
                    {
                        if (!readMore())
                        {
                            if (stopToken.stop_requested()) return false;
                            throw std::runtime_error("ElevenLabs: incomplete HTTP chunk terminator");
                        }
                    }
                    const size_t copied = std::min(chunkTerminator.size() - offset, pending.size());
                    memcpy(chunkTerminator.data() + offset, pending.data(), copied);
                    pending.erase(0, copied);
                    offset += copied;
                }
                if (chunkTerminator[0] != '\r' || chunkTerminator[1] != '\n')
                    throw std::runtime_error("ElevenLabs: invalid HTTP chunk terminator");
            }
        });
}

static void EL_AppendUtf8Codepoint(std::string& out, uint32_t cp)
{
    if (cp <= 0x7F)
    {
        out.push_back(static_cast<char>(cp));
    }
    else if (cp <= 0x7FF)
    {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
    else if (cp <= 0xFFFF)
    {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
    else if (cp <= 0x10FFFF)
    {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

static bool EL_TryParseEntityCodepoint(std::string_view entity, uint32_t& cp)
{
    if (entity.size() < 2 || entity[0] != '#')
        return false;

    const bool isHex = entity.size() >= 3 && (entity[1] == 'x' || entity[1] == 'X');
    const size_t firstDigit = isHex ? 2 : 1;
    if (firstDigit >= entity.size())
        return false;

    cp = 0;
    for (size_t i = firstDigit; i < entity.size(); ++i)
    {
        unsigned digit;
        const char c = entity[i];
        if (c >= '0' && c <= '9')
            digit = c - '0';
        else if (isHex && c >= 'a' && c <= 'f')
            digit = c - 'a' + 10;
        else if (isHex && c >= 'A' && c <= 'F')
            digit = c - 'A' + 10;
        else
            return false;

        if ((!isHex && digit >= 10) || cp > 0x10FFFF / (isHex ? 16 : 10))
            return false;

        cp = cp * (isHex ? 16 : 10) + digit;
    }

    return cp <= 0x10FFFF && !(cp >= 0xD800 && cp <= 0xDFFF);
}

static std::string EL_DecodeXmlEntities(std::string_view text)
{
    std::string decoded;
    decoded.reserve(text.size());

    for (size_t i = 0; i < text.size(); ++i)
    {
        if (text[i] != '&')
        {
            decoded.push_back(text[i]);
            continue;
        }

        const size_t semi = text.find(';', i + 1);
        if (semi == std::string_view::npos || semi - i > 16)
        {
            decoded.push_back(text[i]);
            continue;
        }

        const std::string_view entity = text.substr(i + 1, semi - i - 1);
        if (entity == "amp")
            decoded.push_back('&');
        else if (entity == "lt")
            decoded.push_back('<');
        else if (entity == "gt")
            decoded.push_back('>');
        else if (entity == "quot")
            decoded.push_back('"');
        else if (entity == "apos")
            decoded.push_back('\'');
        else
        {
            uint32_t cp;
            if (!EL_TryParseEntityCodepoint(entity, cp))
            {
                decoded.append(text.data() + i, semi - i + 1);
                i = semi;
                continue;
            }
            EL_AppendUtf8Codepoint(decoded, cp);
        }

        i = semi;
    }

    return decoded;
}

static std::string EL_UrlEncode(std::string_view value)
{
    static constexpr char hex[] = "0123456789ABCDEF";
    std::string encoded;
    encoded.reserve(value.size());
    for (unsigned char c : value)
    {
        if ((c >= 'A' && c <= 'Z') ||
            (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~')
        {
            encoded.push_back(static_cast<char>(c));
        }
        else
        {
            encoded.push_back('%');
            encoded.push_back(hex[c >> 4]);
            encoded.push_back(hex[c & 0x0F]);
        }
    }
    return encoded;
}

// ─────────────────────────────────────────────────────────────────────────────
// SSML → plain text
// ElevenLabs HTTP endpoint does not support SSML.
// Strip all XML tags and normalize whitespace.
// ─────────────────────────────────────────────────────────────────────────────

static std::string SsmlToPlainText(std::wstring_view ssml)
{
    const std::string utf8 = WStringToUTF8(ssml);
    std::string result;
    result.reserve(utf8.size());

    bool inTag = false;
    for (unsigned char c : utf8)
    {
        if (c == '<') { inTag = true;  continue; }
        if (c == '>') { inTag = false; continue; }
        if (!inTag) result += static_cast<char>(c);
    }

    const std::string decoded = EL_DecodeXmlEntities(result);

    // Collapse runs of whitespace to a single space and trim ends
    std::string normalized;
    normalized.reserve(decoded.size());
    bool lastWasSpace = true; // true trims leading whitespace
    for (unsigned char c : decoded)
    {
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
        {
            if (!lastWasSpace) { normalized += ' '; lastWasSpace = true; }
        }
        else
        {
            normalized += static_cast<char>(c);
            lastWasSpace = false;
        }
    }
    if (!normalized.empty() && normalized.back() == ' ')
        normalized.pop_back();

    return normalized;
}

// ─────────────────────────────────────────────────────────────────────────────
// Extract error message from ElevenLabs error JSON:
//   {"detail": {"message": "..."}} or {"detail": "..."}
// ─────────────────────────────────────────────────────────────────────────────

static std::string EL_ExtractErrorMessage(const std::string& body)
{
    try
    {
        auto j = nlohmann::json::parse(body);
        if (j.contains("detail"))
        {
            auto& detail = j["detail"];
            if (detail.is_string())
                return detail.get<std::string>();
            if (detail.is_object() && detail.contains("message"))
                return detail["message"].get<std::string>();
        }
        return body.substr(0, 300);
    }
    catch (...) { return body.substr(0, 300); }
}

// ─────────────────────────────────────────────────────────────────────────────
// Credentials
// ─────────────────────────────────────────────────────────────────────────────

void ElevenLabsAPI::SetCredentials(std::string apiKey, std::string model)
{
    m_apiKey = std::move(apiKey);
    m_model  = std::move(model);
}

// ─────────────────────────────────────────────────────────────────────────────
// SpeakAsync / DoSpeakAsync
// ─────────────────────────────────────────────────────────────────────────────

std::future<void> ElevenLabsAPI::SpeakAsync(const std::wstring& ssml,
                                              const std::string&  voiceId)
{
    m_ssml             = ssml;
    m_voiceId          = voiceId;
    m_waveBytesWritten = 0;
    m_stopSource       = {};

    return std::async(std::launch::async,
                      std::bind(&ElevenLabsAPI::DoSpeakAsync, this));
}

void ElevenLabsAPI::Stop()
{
    m_stopSource.request_stop();
}

void ElevenLabsAPI::DoSpeakAsync()
{
    const std::string path =
        "/v1/text-to-speech/" + m_voiceId + "/stream?output_format=pcm_24000";

    const std::string text = SsmlToPlainText(m_ssml);
    if (text.empty())
        return; // nothing to speak

    const nlohmann::json bodyJson = {
        {"text",     text},
        {"model_id", m_model}
    };
    const std::string body = bodyJson.dump();

    LogDebug("ElevenLabs: Speak request: model={} voice={}", m_model, m_voiceId);
    LogTrace("ElevenLabs: Request body: {}", body);

    if (m_stopSource.stop_requested()) return;

    const bool completed = EL_HttpsRequestStream(
        "POST", ELEVENLABS_HOST, path, body, m_apiKey,
        m_stopSource.get_token(),
        [this](const uint8_t* data, uint32_t size)
        {
            if (m_stopSource.stop_requested() || !AudioReceivedCallback)
                return false;

            const int written = AudioReceivedCallback(const_cast<uint8_t*>(data), size);
            if (written > 0)
                m_waveBytesWritten += static_cast<uint32_t>(written);
            return !m_stopSource.stop_requested();
        });

    if (m_stopSource.stop_requested() || !completed) return;

    LogDebug("ElevenLabs: Streaming response completed, {} bytes of PCM", m_waveBytesWritten);

    if (SessionEndCallback)
        SessionEndCallback(m_waveBytesWritten);
}

// ─────────────────────────────────────────────────────────────────────────────
// Voice list  (GET /v2/voices, paginated)
// ─────────────────────────────────────────────────────────────────────────────

nlohmann::json ElevenLabsAPI::GetVoiceList(const std::string& apiKey)
{
    nlohmann::json allVoices = nlohmann::json::array();
    std::string nextPageToken;

    for (;;)
    {
        std::string path = "/v2/voices?page_size=100&include_total_count=false";
        if (!nextPageToken.empty())
            path += "&next_page_token=" + EL_UrlEncode(nextPageToken);

        const std::string rawResponse = EL_HttpsRequest(
            "GET", ELEVENLABS_HOST, path, "", apiKey);

        if (rawResponse.empty())
            throw std::runtime_error("ElevenLabs: empty response fetching voice list");

        const auto resp = EL_ParseHttpResponse(rawResponse);

        if (resp.statusCode != 200)
        {
            LogTrace("ElevenLabs: Voice list error response: {}", resp.body.substr(0, 500));
            throw std::runtime_error(
                "ElevenLabs voice list error " + std::to_string(resp.statusCode) +
                ": " + EL_ExtractErrorMessage(resp.body));
        }

        const auto json = nlohmann::json::parse(resp.body);
        LogTrace("ElevenLabs: Voice list page response (first 2000 chars): {}",
                 resp.body.substr(0, 2000));

        for (const auto& voice : json.at("voices"))
            allVoices.push_back(voice);

        const bool hasMore = json.value("has_more", false);
        if (!hasMore) break;

        if (json.contains("next_page_token") && json["next_page_token"].is_string())
            nextPageToken = json["next_page_token"].get<std::string>();
        else
            break;
    }

    LogDebug("ElevenLabs: Voice list fetched: {} voices total", allVoices.size());
    return allVoices;
}
