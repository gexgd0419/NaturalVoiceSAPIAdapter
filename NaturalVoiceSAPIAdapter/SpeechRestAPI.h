#pragma once
#define ASIO_STANDALONE 1
#include <websocketpp/client.hpp>
#include <websocketpp/config/asio_client.hpp>
#include <asio.hpp>
#include <queue>
#include <optional>
#include "nlohmann/json.hpp"
#include "SpeechServiceConstants.h"


class SpeechRestAPI
{
public: // Public methods
	SpeechRestAPI();
	~SpeechRestAPI();
	std::future<void> SpeakAsync(const std::wstring& ssml);
	void Stop();
	void SetSubscription(std::string key, const std::string& region);
	void SetWebsocketUrl(std::string key, std::string websocketUrl);

public: // Event callbacks
	typedef std::function<void(uint64_t offsetTicks, const std::string& bookmark)> BookmarkCallbackType;
	typedef std::function<void(uint64_t audioOffsetTicks, uint32_t textOffset, uint32_t textLength)> BoundaryCallbackType;
	typedef std::function<void(uint64_t offsetTicks, uint32_t visemeId)> VisemeCallbackType;
	typedef std::function<void(uint64_t offsetTicks)> SessionEndCallbackType;
	typedef std::function<int(uint8_t* data, uint32_t length)> AudioReceivedCallbackType;

	BookmarkCallbackType BookmarkCallback;
	BoundaryCallbackType WordBoundaryCallback, SentenceBoundaryCallback, PunctuationBoundaryCallback;
	VisemeCallbackType VisemeCallback;
	SessionEndCallbackType SessionEndCallback;
	AudioReceivedCallbackType AudioReceivedCallback;

private:
	std::wstring_view m_ssml;
	std::string m_voiceListUrl, m_websocketUrl, m_key;
	size_t m_lastWordPos = 0, m_lastSentencePos = 0;

private:
	typedef websocketpp::client<websocketpp::config::asio_tls_client> WSClient;
	typedef websocketpp::connection<websocketpp::config::asio_tls_client> WSConnection;
	WSClient m_client;
	std::shared_ptr<WSConnection> m_connection;
	bool IsCurrentConnection(const websocketpp::connection_hdl& hdl);

private: // threading

	// We use two background threads to run the IO loop for ASIO,
	// and the MP3 decoder loop that sends audio to SAPI.
	std::thread m_asioThread;
	std::thread m_mp3Thread;

	// We have to queue received audio data instead of sending them directly to SAPI.
	// SAPI will block write calls before it needs more data,
	// but the remote server disconnects after a few inactive seconds
	// even if not all data are sent.
	// So we have to store all received audio data in a queue temporarily,
	// then use another thread for sending the data to SAPI.
	std::queue<std::string> m_mp3Queue;
	std::mutex m_mp3QueueMutex;
	std::condition_variable m_mp3ThreadNotifier;
	bool m_isStopping = false;
	bool m_mp3QueueDone = false;
	bool m_firstDataReceived = false;
	bool m_allDataReceived = false;

	void AsioThread();
	void Mp3Thread();
	void Mp3QueuePush(std::string&& msg);
	void Mp3QueueDone();

	// asynchronous results returned through SpeakAsync()
	std::atomic_flag m_isPromiseSet;
	std::promise<void> m_speakPromise;
	void SpeakComplete();
	void SpeakError(std::exception_ptr ex);

private:
	void OnOpen(websocketpp::connection_hdl hdl);
	void OnMessage(websocketpp::connection_hdl hdl, WSClient::message_ptr msg);
	void OnClose(websocketpp::connection_hdl hdl);
	void OnFail(websocketpp::connection_hdl hdl);
	void OnSynthEvent(const nlohmann::json& metadata);
	size_t FindWord(const std::string& utf8Word, size_t& lastPos);
};