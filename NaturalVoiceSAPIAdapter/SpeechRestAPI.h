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
#include "WSConnectionPool.h"
#include "BlockingQueue.h"


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

private: // threading

	std::stop_source m_stopSource;

	// We have to queue received audio data instead of sending them directly to SAPI.
	// SAPI will block write calls before it needs more data,
	// but the remote server disconnects after a few inactive seconds
	// even if not all data are sent.
	// So we have to store all received audio data in a queue temporarily,
	// then use another thread for sending the data to SAPI.
	bool m_firstDataReceived = false;
	bool m_allDataReceived = false;

	void DoSpeakAsync();
	void Mp3ProcessLoop(BlockingQueue<std::string>& queue, std::stop_token token);
	void ProcessWaveData(const WAVEFORMATEX& wfx, BYTE* waveData, uint32_t waveSize);

private:
	void SendRequest(WSConnection& conn);
	void OnMessage(BlockingQueue<std::string>& queue, WSConnection& conn, WSClient::message_ptr msg);
	void OnClose(BlockingQueue<std::string>& queue, WSConnection& conn);
	void OnSynthEvent(const nlohmann::json& metadata);
	size_t FindWord(const std::string& utf8Word, size_t& lastPos);
};