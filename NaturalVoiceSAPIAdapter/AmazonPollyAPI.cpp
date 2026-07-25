#include "pch.h"
#define ASIO_STANDALONE 1
#include <asio.hpp>
#include <asio/ssl.hpp>
#include "AmazonPollyAPI.h"
#include "NetUtils.h"
#include "StrUtils.h"
#include "Logger.h"
#include <openssl/sha.h>
#include <openssl/hmac.h>
#include <openssl/evp.h>
#include <format>
#include <map>

// ─────────────────────────────────────────────────────────────────────────────
// Internal crypto helpers
// ─────────────────────────────────────────────────────────────────────────────

static std::string HmacSha256Raw(std::string_view key, std::string_view data)
{
    unsigned char hash[32];
    unsigned int  len = 32;
    HMAC(EVP_sha256(),
         key.data(),  static_cast<int>(key.size()),
         reinterpret_cast<const unsigned char*>(data.data()), data.size(),
         hash, &len);
    return std::string(reinterpret_cast<char*>(hash), len);
}

static std::string Sha256Hex(std::string_view data)
{
    unsigned char hash[32];
    SHA256(reinterpret_cast<const unsigned char*>(data.data()), data.size(), hash);
    std::string out(64, '0');
    for (int i = 0; i < 32; ++i)
        std::format_to(out.begin() + i * 2, "{:02x}", hash[i]);
    return out;
}

static std::string HmacSha256Hex(std::string_view key, std::string_view data)
{
    auto raw = HmacSha256Raw(key, data);
    std::string out(64, '0');
    for (int i = 0; i < 32; ++i)
        std::format_to(out.begin() + i * 2, "{:02x}",
                        static_cast<unsigned char>(raw[i]));
    return out;
}

// ─────────────────────────────────────────────────────────────────────────────
// AWS Signature V4
// ─────────────────────────────────────────────────────────────────────────────

void AmazonPollyAPI::SetCredentials(std::string accessKeyId,
                                     std::string secretKey,
                                     std::string region)
{
    m_accessKeyId = std::move(accessKeyId);
    m_secretKey   = std::move(secretKey);
    m_region      = std::move(region);
}

AmazonPollyAPI::SigV4Result AmazonPollyAPI::ComputeSigV4(
    const std::string& method,
    const std::string& path,
    const std::string& query,
    const std::string& host,
    const std::string& body) const
{
    SYSTEMTIME st;
    GetSystemTime(&st);

    const std::string date = std::format("{:04}{:02}{:02}",
        st.wYear, st.wMonth, st.wDay);
    const std::string datetime = std::format("{:04}{:02}{:02}T{:02}{:02}{:02}Z",
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);

    const std::string bodyHash     = Sha256Hex(body);
    const std::string signedHdrs   = "content-type;host;x-amz-date";
    const std::string service      = "polly";

    // Step 1 – canonical request
    const std::string canonicalReq =
        method + "\n" +
        path   + "\n" +
        query  + "\n" +
        "content-type:application/json\n" +
        "host:" + host + "\n" +
        "x-amz-date:" + datetime + "\n" +
        "\n" +
        signedHdrs + "\n" +
        bodyHash;

    // Step 2 – string to sign
    const std::string credScope = date + "/" + m_region + "/" + service + "/aws4_request";
    const std::string strToSign =
        "AWS4-HMAC-SHA256\n" +
        datetime + "\n" +
        credScope + "\n" +
        Sha256Hex(canonicalReq);

    // Step 3 – derived signing key
    const std::string kDate    = HmacSha256Raw("AWS4" + m_secretKey, date);
    const std::string kRegion  = HmacSha256Raw(kDate,   m_region);
    const std::string kService = HmacSha256Raw(kRegion, service);
    const std::string kSigning = HmacSha256Raw(kService,"aws4_request");

    // Step 4 – signature
    const std::string signature = HmacSha256Hex(kSigning, strToSign);

    // Step 5 – Authorization header
    const std::string auth =
        "AWS4-HMAC-SHA256 Credential=" + m_accessKeyId + "/" + credScope +
        ", SignedHeaders=" + signedHdrs +
        ", Signature=" + signature;

    return { datetime, auth };
}

// ─────────────────────────────────────────────────────────────────────────────
// Minimal HTTP/1.1 helpers (ASIO + OpenSSL, same stack as NetUtils)
// ─────────────────────────────────────────────────────────────────────────────

struct ParsedHttpResponse
{
    int         statusCode = 0;
    std::map<std::string, std::string> headers;
    std::string body;   // raw bytes (may be binary)
};

static ParsedHttpResponse ParseHttpResponse(const std::string& raw)
{
    ParsedHttpResponse res;

    const size_t headerEnd = raw.find("\r\n\r\n");
    if (headerEnd == std::string::npos)
        throw std::runtime_error("Polly: invalid HTTP response (no header delimiter)");

    // Status line
    const size_t statusLineEnd = raw.find("\r\n");
    if (statusLineEnd > 9)
        res.statusCode = std::stoi(raw.substr(9, 3));

    // Headers
    size_t pos = statusLineEnd + 2;
    while (pos < headerEnd)
    {
        const size_t next  = raw.find("\r\n", pos);
        const size_t end   = (next == std::string::npos) ? headerEnd : next;
        const std::string line = raw.substr(pos, end - pos);
        const size_t colon = line.find(':');
        if (colon != std::string::npos)
        {
            std::string k = line.substr(0, colon);
            std::string v = line.substr(colon + 1);
            if (!v.empty() && v.front() == ' ') v.erase(v.begin());
            // Lowercase key for easy lookup
            for (char& c : k) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            res.headers[k] = std::move(v);
        }
        pos = end + 2;
    }

    // Body: after \r\n\r\n
    res.body = raw.substr(headerEnd + 4);

    // Dechunk if Transfer-Encoding: chunked
    auto it = res.headers.find("transfer-encoding");
    if (it != res.headers.end() &&
        it->second.find("chunked") != std::string::npos)
    {
        std::string dechunked;
        size_t p = 0;
        while (p < res.body.size())
        {
            // Chunk size line
            const size_t nl = res.body.find("\r\n", p);
            if (nl == std::string::npos) break;
            const size_t chunkSize = std::stoul(res.body.substr(p, nl - p), nullptr, 16);
            if (chunkSize == 0) break;
            p = nl + 2;
            if (p + chunkSize > res.body.size()) break;
            dechunked.append(res.body, p, chunkSize);
            p += chunkSize + 2; // skip trailing \r\n
        }
        res.body = std::move(dechunked);
    }

    return res;
}

// Open an SSL stream to host:443 and run the lambda with it.
// Registers a stop_callback that closes the socket on cancellation.
template <class Func>
static auto WithSslStream(const std::string& host,
                           std::stop_token    stopToken,
                           Func&&             fn)
    -> decltype(fn(std::declval<asio::ssl::stream<asio::ip::tcp::socket>&>()))
{
    asio::io_context        ioctx;
    asio::ssl::context      sslctx(asio::ssl::context::sslv23_client);
    asio::ssl::stream<asio::ip::tcp::socket> stream(ioctx, sslctx);

    // Cancel via socket close when stop is requested
    std::stop_callback stopCb(stopToken, [&stream]() {
        asio::error_code ec;
        stream.lowest_layer().close(ec);
    });

    if (stopToken.stop_requested())
        return {};

    auto resolved = asio::ip::tcp::resolver(ioctx).resolve(host, "443");
    stream.next_layer().connect(*resolved);
    stream.handshake(asio::ssl::stream_base::client);

    return fn(stream);
}

static std::string HttpsRequest(const std::string& method,
                                 const std::string& host,
                                 const std::string& pathAndQuery,
                                 const std::string& body,
                                 const std::string& datetime,
                                 const std::string& authorization,
                                 std::stop_token    stopToken = {})
{
    return WithSslStream(host, stopToken,
        [&](asio::ssl::stream<asio::ip::tcp::socket>& stream) -> std::string
        {
            std::string req =
                method + " " + pathAndQuery + " HTTP/1.1\r\n"
                "Host: " + host + "\r\n"
                "Content-Type: application/json\r\n"
                "X-Amz-Date: " + datetime + "\r\n"
                "Authorization: " + authorization + "\r\n"
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

static std::string UrlEncodeRFC3986(std::string_view value)
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
// SSML preprocessing for Polly
// Polly does not support <bookmark> (uses <mark> instead),
// does not support <phoneme alphabet='sapi'>, and has very limited
// <prosody> support (none at all for generative/long-form engines).
// We do minimal string-level cleanup here so that BuildSSML output is accepted.
// ─────────────────────────────────────────────────────────────────────────────

static std::string PreprocessSsmlForPolly(std::string ssml)
{
    // Remove <bookmark mark='...'/> tags – replace with nothing.
    // They are always self-closing and generated by BuildSSML in one consistent form.
    for (;;)
    {
        const size_t start = ssml.find("<bookmark ");
        if (start == std::string::npos) break;
        const size_t end = ssml.find("/>", start);
        if (end == std::string::npos) break;
        ssml.erase(start, end - start + 2);
    }

    // Remove <phoneme alphabet='sapi' ph='...'> ... </phoneme>  – keep inner text.
    for (;;)
    {
        // Look for the sapi phoneme opening tag
        const size_t tagStart = ssml.find("<phoneme alphabet='sapi'");
        if (tagStart == std::string::npos) break;
        const size_t tagEnd = ssml.find('>', tagStart);
        if (tagEnd == std::string::npos) break;

        const size_t closeTag = ssml.find("</phoneme>", tagEnd);
        if (closeTag == std::string::npos)
        {
            // Malformed – just remove the opening tag
            ssml.erase(tagStart, tagEnd - tagStart + 1);
        }
        else
        {
            // Keep the inner text, remove both tags
            const std::string innerText = ssml.substr(tagEnd + 1, closeTag - tagEnd - 1);
            ssml.replace(tagStart, closeTag - tagStart + 10 /*</phoneme>*/, innerText);
        }
    }

    // Remove <prosody ...> ... </prosody> tags – keep inner text.
    // Polly support by engine:
    //   generative / long-form : no <prosody> support at all → always errors
    //   neural                 : rate and volume only; pitch causes 400
    //   standard               : rate, pitch, volume – but values from SAPI rarely
    //                            translate correctly anyway
    // Stripping universally is safer than receiving a 400 "Unsupported feature".
    for (;;)
    {
        const size_t tagStart = ssml.find("<prosody");
        if (tagStart == std::string::npos) break;
        const size_t tagEnd = ssml.find('>', tagStart);
        if (tagEnd == std::string::npos) break;

        // Self-closing <prosody ... /> – just remove the tag
        if (ssml[tagEnd - 1] == '/')
        {
            ssml.erase(tagStart, tagEnd - tagStart + 1);
            continue;
        }

        const size_t closeTag = ssml.find("</prosody>", tagEnd);
        if (closeTag == std::string::npos)
        {
            // Malformed – just remove the opening tag
            ssml.erase(tagStart, tagEnd - tagStart + 1);
        }
        else
        {
            // Keep the inner text, remove both tags
            const std::string innerText = ssml.substr(tagEnd + 1, closeTag - tagEnd - 1);
            ssml.replace(tagStart, closeTag - tagStart + 10 /*</prosody>*/, innerText);
        }
    }

    return ssml;
}

// ─────────────────────────────────────────────────────────────────────────────
// SpeakAsync / DoSpeakAsync
// ─────────────────────────────────────────────────────────────────────────────

std::future<void> AmazonPollyAPI::SpeakAsync(const std::wstring& ssml,
                                               const std::string&  voiceId,
                                               const std::string&  engine)
{
    m_ssml             = ssml;   // wstring_view into TTSEngine::m_ssml
    m_voiceId          = voiceId;
    m_engine           = engine;
    m_waveBytesWritten = 0;
    m_stopSource       = {};

    return std::async(std::launch::async,
                      std::bind(&AmazonPollyAPI::DoSpeakAsync, this));
}

void AmazonPollyAPI::Stop()
{
    m_stopSource.request_stop();
}

void AmazonPollyAPI::DoSpeakAsync()
{
    const std::string host = "polly." + m_region + ".amazonaws.com";
    const std::string path = "/v1/speech";

    // Prepare SSML: convert to UTF-8 and strip Polly-incompatible tags
    const std::string ssmlUtf8 = PreprocessSsmlForPolly(WStringToUTF8(m_ssml));

    const nlohmann::json bodyJson = {
        {"Engine",       m_engine},
        {"OutputFormat", "mp3"},
        {"SampleRate",   "24000"},
        {"Text",         ssmlUtf8},
        {"TextType",     "ssml"},
        {"VoiceId",      m_voiceId}
    };
    const std::string body = bodyJson.dump();

    LogDebug("Polly: Speak request: engine={} voice={}", m_engine, m_voiceId);
    LogTrace("Polly: Request body: {}", body);

    const auto sig = ComputeSigV4("POST", path, "", host, body);

    if (m_stopSource.stop_requested()) return;

    const std::string rawResponse = HttpsRequest(
        "POST", host, path, body,
        sig.datetime, sig.authorization,
        m_stopSource.get_token());

    if (m_stopSource.stop_requested() || rawResponse.empty()) return;

    const auto resp = ParseHttpResponse(rawResponse);

    if (resp.statusCode != 200)
    {
        LogTrace("Polly: Error response body: {}", resp.body.substr(0, 500));
        std::string errMsg;
        try
        {
            auto errJson = nlohmann::json::parse(resp.body);
            errMsg = errJson.value("message", resp.body.substr(0, 300));
        }
        catch (...) { errMsg = resp.body.substr(0, 300); }
        throw std::runtime_error(
            "Polly API error " + std::to_string(resp.statusCode) + ": " + errMsg);
    }

    LogDebug("Polly: Response received, {} bytes of MP3", resp.body.size());

    // Decode MP3 -> PCM -> deliver via AudioReceivedCallback. Keep chunks small
    // to avoid large blocking SAPI writes in game hosts.
    Mp3Decoder mp3;
    constexpr size_t MP3_DECODE_CHUNK_SIZE = 2048;
    for (size_t offset = 0; offset < resp.body.size();)
    {
        if (m_stopSource.stop_requested()) return;

        const size_t chunkSize = std::min(MP3_DECODE_CHUNK_SIZE, resp.body.size() - offset);
        mp3.Convert(
            reinterpret_cast<const BYTE*>(resp.body.data() + offset),
            chunkSize,
            [this](BYTE* data, uint32_t size)
            {
                if (!AudioReceivedCallback || m_stopSource.stop_requested()) return;
                const int written = AudioReceivedCallback(data, size);
                m_waveBytesWritten += static_cast<uint32_t>(written);
            });
        offset += chunkSize;
    }

    if (SessionEndCallback)
        SessionEndCallback(m_waveBytesWritten);
}

// ─────────────────────────────────────────────────────────────────────────────
// Voice list
// ─────────────────────────────────────────────────────────────────────────────

nlohmann::json AmazonPollyAPI::GetVoiceList(const std::string& accessKeyId,
                                              const std::string& secretKey,
                                              const std::string& region,
                                              const std::string& engine)
{
    AmazonPollyAPI tmp;
    tmp.SetCredentials(accessKeyId, secretKey, region);

    const std::string host  = "polly." + region + ".amazonaws.com";
    const std::string path  = "/v1/voices";
    nlohmann::json allVoices = nlohmann::json::array();
    std::string nextToken;

    for (;;)
    {
        std::string query = "Engine=" + UrlEncodeRFC3986(engine);
        if (!nextToken.empty())
            query += "&NextToken=" + UrlEncodeRFC3986(nextToken);

        const auto sig = tmp.ComputeSigV4("GET", path, query, host, "");

        const std::string rawResponse = HttpsRequest(
            "GET", host, path + "?" + query, "",
            sig.datetime, sig.authorization);

        const auto resp = ParseHttpResponse(rawResponse);

        if (resp.statusCode != 200)
        {
            LogTrace("Polly: Voice list error response: {}", resp.body.substr(0, 500));
            throw std::runtime_error(
                "Polly voice list error " + std::to_string(resp.statusCode) +
                ": " + resp.body.substr(0, 200));
        }

        const auto json = nlohmann::json::parse(resp.body);
        LogTrace("Polly: Voice list response page: {}", resp.body.substr(0, 2000));

        for (const auto& voice : json.at("Voices"))
            allVoices.push_back(voice);

        if (json.contains("NextToken") && json["NextToken"].is_string())
            nextToken = json["NextToken"].get<std::string>();
        else
            break;
    }

    LogTrace("Polly: Voice list fetched: {} voices for engine={}", allVoices.size(), engine);
    return allVoices;
}
