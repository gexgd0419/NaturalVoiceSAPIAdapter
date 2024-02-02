#pragma once
#define ASIO_STANDALONE 1
#include <websocketpp/client.hpp>
#include <websocketpp/config/asio_client.hpp>
#include <asio.hpp>
#include <optional>
#include "Mp3Decoder.h"
#include "nlohmann/json.hpp"
#include "SpeechServiceConstants.h"


class SpeechRestAPI
{
private:
	typedef websocketpp::client<websocketpp::config::asio_tls_client> WSClient;
	WSClient m_client;
	websocketpp::connection_hdl m_hConn;
	std::wstring_view m_ssml;
	std::string m_voiceListUrl, m_websocketUrl, m_key;
	size_t m_lastWordPos = 0, m_lastSentencePos = 0;
	Mp3Decoder m_mp3Decoder;
public:
	SpeechRestAPI();
	void Speak(const std::wstring& ssml);
	std::future<void> SpeakAsync(const std::wstring& ssml)
	{
		return std::async(std::launch::async, [this, &ssml]() { Speak(ssml); });
	}
	void SetSubscription(std::string key, const std::string& region)
	{
		m_key = std::move(key);
		m_voiceListUrl = std::string("https://") + region + AZURE_TTS_HOST_AFTER_REGION + AZURE_VOICE_LIST_PATH;
		m_websocketUrl = std::string("wss://") + region + AZURE_TTS_HOST_AFTER_REGION + AZURE_WEBSOCKET_PATH;
	}
	void SetWebsocketUrl(std::string key, std::string websocketUrl)
	{
		m_key = std::move(key);
		m_websocketUrl = std::move(websocketUrl);
	}
	void Disconnect()
	{
		std::error_code ec;
		m_client.close(m_hConn, websocketpp::close::status::normal, {}, ec);
	}
private:
	void OnOpen(websocketpp::connection_hdl hdl);
	void OnMessage(websocketpp::connection_hdl hdl, WSClient::message_ptr msg);
	void OnSynthEvent(const nlohmann::json& metadata);
	size_t FindWord(const std::string& utf8Word, size_t& lastPos);
public:
	typedef std::function<void(uint64_t offsetTicks, const std::string& bookmark)> BookmarkCallbackType;
	typedef std::function<void(uint64_t audioOffsetTicks, uint32_t textOffset, uint32_t textLength)> BoundaryCallbackType;
	typedef std::function<void(uint64_t offsetTicks, uint32_t visemeId)> VisemeCallbackType;
	typedef std::function<void(uint64_t offsetTicks)> SessionEndCallbackType;
	typedef std::function<int(uint8_t* data, uint32_t length)> AudioReceivedCallbackType;
public:
	BookmarkCallbackType BookmarkCallback;
	BoundaryCallbackType WordBoundaryCallback, SentenceBoundaryCallback, PunctuationBoundaryCallback;
	VisemeCallbackType VisemeCallback;
	SessionEndCallbackType SessionEndCallback;
	AudioReceivedCallbackType AudioReceivedCallback;
};