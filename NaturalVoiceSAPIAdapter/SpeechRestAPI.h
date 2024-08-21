#pragma once
#define ASIO_STANDALONE 1
#include <websocketpp/client.hpp>
#include <websocketpp/config/asio_client.hpp>
#include <asio.hpp>
#include <queue>
#include "nlohmann/json.hpp"
#include "SpeechServiceConstants.h"
#include "WSLogger.h"
#include "Mp3Decoder.h"


// Custom ASIO config mainly for overwriting the default logger that outputs to stdout.
// See Logging Reference: https://docs.websocketpp.org/reference_8logging.html
struct WSConfig : websocketpp::config::asio_tls_client
{
	typedef WSConfig type;
	typedef websocketpp::config::asio_tls_client base;

	// static log levels; levels excluded here cannot be enabled runtime and will likely be optimized out
	static constexpr websocketpp::log::level elog_level =  // error log level
		websocketpp::log::elevel::all & ~websocketpp::log::elevel::devel;
	static constexpr websocketpp::log::level alog_level =  // access log level
		websocketpp::log::alevel::all &
		~(websocketpp::log::alevel::devel
			| websocketpp::log::alevel::frame_header | websocketpp::log::alevel::frame_payload);

	// use our own logger
	typedef websocketpp::log::WSLogger
		<websocketpp::log::channel_type_hint::access, alog_level> alog_type;
	typedef websocketpp::log::WSLogger
		<websocketpp::log::channel_type_hint::error, elog_level> elog_type;

	struct transport_config : public base::transport_config
	{
		typedef type::alog_type alog_type;
		typedef type::elog_type elog_type;
	};

	typedef websocketpp::transport::asio::endpoint<transport_config>
		transport_type;
};

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
	// Store each event's audio tick (uint64) and json "Metadata" node into a sorted queue (priority_queue)
	// so that we can dispatch the event at the correct audio time
	typedef std::pair<uint64_t, nlohmann::json> EventInfo;
	struct EventComparer
	{
		bool operator()(const EventInfo& a, const EventInfo& b) const noexcept
		{
			// sort by audio tick value, with the smallest tick at top
			return a.first > b.first;
		}
	};
	std::priority_queue<EventInfo, std::vector<EventInfo>, EventComparer> m_events;
	std::mutex m_eventsMutex;
	uint64_t m_waveBytesWritten = 0;
public:
	uint64_t GetWaveBytesWritten() const noexcept { return m_waveBytesWritten; }

private:
	std::wstring_view m_ssml;
	std::string m_voiceListUrl, m_websocketUrl, m_key;
	size_t m_lastWordPos = 0, m_lastSentencePos = 0;

private:
	typedef websocketpp::client<WSConfig> WSClient;
	typedef websocketpp::connection<WSConfig> WSConnection;
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
	Mp3Decoder m_mp3Decoder;
	bool m_isStopping = false;
	bool m_mp3QueueDone = false;
	bool m_firstDataReceived = false;
	bool m_allDataReceived = false;

	void AsioThread();
	void Mp3Thread();
	void ProcessWaveData(BYTE* waveData, uint32_t waveSize);
	void Mp3QueuePush(std::string&& msg);
	void Mp3QueueDone();

	// asynchronous results returned through SpeakAsync()
	std::atomic_flag m_isPromiseSet;
	std::promise<void> m_speakPromise;
	void SpeakComplete();
	void SpeakError(std::exception_ptr ex);

private:
	void SendRequest(websocketpp::connection_hdl hdl);
	void OnOpen(websocketpp::connection_hdl hdl);
	void OnMessage(websocketpp::connection_hdl hdl, WSClient::message_ptr msg);
	void OnClose(websocketpp::connection_hdl hdl);
	void OnFail(websocketpp::connection_hdl hdl);
	void OnSynthEvent(const nlohmann::json& metadata);
	size_t FindWord(const std::string& utf8Word, size_t& lastPos);
};