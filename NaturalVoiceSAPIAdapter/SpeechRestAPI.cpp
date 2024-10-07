#include "pch.h"
#include "SpeechRestAPI.h"
#include "NetUtils.h"
#include "StrUtils.h"
#include <format>
#include "Logger.h"
#include "WSConnectionPool.h"

std::unique_ptr<WSConnectionPool> g_pConnectionPool;
static std::once_flag s_initOnce;

static std::string MakeRandomUuid()
{
	GUID guid;
	(void)CoCreateGuid(&guid);
	return std::format("{:08x}{:04x}{:04x}{:02x}{:02x}{:02x}{:02x}{:02x}{:02x}{:02x}{:02x}",
		guid.Data1, guid.Data2, guid.Data3,
		guid.Data4[0], guid.Data4[1], guid.Data4[2], guid.Data4[3], guid.Data4[4], guid.Data4[5], guid.Data4[6], guid.Data4[7]);
}

static std::string GetTimeStamp()
{
	SYSTEMTIME tm;
	GetSystemTime(&tm);
	return std::format("{:04}-{:02}-{:02}T{:02}:{:02}:{:02}.{:03}Z",
		tm.wYear, tm.wMonth, tm.wDay, tm.wHour, tm.wMinute, tm.wSecond, tm.wMilliseconds);
}

SpeechRestAPI::SpeechRestAPI()
{
	std::call_once(s_initOnce, []()
		{
			if (!g_pConnectionPool)
				g_pConnectionPool = std::make_unique<WSConnectionPool>();
		});
}

SpeechRestAPI::~SpeechRestAPI()
{
	m_stopSource.request_stop();
}

std::future<void> SpeechRestAPI::SpeakAsync(const std::wstring& ssml)
{
	m_ssml = ssml;
	m_lastWordPos = 0;
	m_lastSentencePos = 0;
	m_events = {};
	m_waveBytesWritten = 0;
	m_stopSource = {};
	m_firstDataReceived = false;
	m_allDataReceived = false;

	auto fut = std::async(std::launch::async, std::bind(&SpeechRestAPI::DoSpeakAsync, this));

	return fut;
}

void SpeechRestAPI::Stop()
{
	m_stopSource.request_stop();
}

void SpeechRestAPI::SetSubscription(std::string key, const std::string& region)
{
	m_key = std::move(key);
	m_voiceListUrl = std::string("https://") + region + AZURE_TTS_HOST_AFTER_REGION + AZURE_VOICE_LIST_PATH;
	m_websocketUrl = std::string("wss://") + region + AZURE_TTS_HOST_AFTER_REGION + AZURE_WEBSOCKET_PATH;
}

void SpeechRestAPI::SetWebsocketUrl(std::string key, std::string websocketUrl)
{
	m_key = std::move(key);
	m_websocketUrl = std::move(websocketUrl);
}

void SpeechRestAPI::DoSpeakAsync()
{
	auto conn = g_pConnectionPool->TakeConnection(m_websocketUrl, m_key, m_stopSource.get_token());
	if (!conn)
		return;
	ScopeGuard connectionCloser([this, &conn]()
		{
			// conn will be reset to nullptr by OnMessage when all data has been received.
			// If not, close the connection before returning.
			if (conn)
				conn->terminate({});
		});

	BlockingQueue<std::string> queue;

	conn->set_message_handler([this, &conn, &queue](websocketpp::connection_hdl, WSClient::message_ptr msg)
		{ OnMessage(queue, conn, msg); });
	conn->set_close_handler([this, &conn, &queue](websocketpp::connection_hdl)
		{ OnClose(queue, conn); });

	SendRequest(conn);

	// does not return until all MP3 data are written
	Mp3ProcessLoop(queue, m_stopSource.get_token());
}

void SpeechRestAPI::ProcessWaveData(const WAVEFORMATEX& wfx, BYTE* waveData, uint32_t waveSize)
{
	// Most of the time, the server will send event data before audio data.
	// But sometimes, event data may fall behind audio data.
	// So if there's no existing event at this time position,
	// we wait for at most 10ms for possible events.

	// Wait at most once
	bool hasWaited = false;
	for (;;)
	{
		uint64_t evtBytes;
		EventInfo evt;

		{
			std::unique_lock lock(m_eventsMutex);
			if (m_events.empty())
			{
				if (WordBoundaryCallback && !hasWaited)
				{
					lock.unlock();
					Sleep(10);  // wait for possible events
					hasWaited = true;
					continue;
				}
				else
					break;
			}
			evtBytes = m_events.top().first * wfx.nAvgBytesPerSec / 10'000'000;

			// align evtBytes to wave blocks
			WORD blockAlign = wfx.nBlockAlign;
			evtBytes = evtBytes - (evtBytes % blockAlign);

			if (m_waveBytesWritten + waveSize <= evtBytes)  // we haven't reached the next event yet
			{
				if (WordBoundaryCallback && !hasWaited)
				{
					lock.unlock();
					Sleep(10);  // wait for possible events
					hasWaited = true;
					continue;
				}
				else
					break;
			}

			// Use const_cast so that it can be moved, as priority_queue::top() returns a const reference
			evt = std::move(const_cast<EventInfo&>(m_events.top()));
			m_events.pop();
		}

		// Send the audio part before this event's time point
		if (evtBytes < m_waveBytesWritten)
			LogDebug("Rest API: Events out of order");
		uint32_t waveSizeBefore = evtBytes >= m_waveBytesWritten ? (uint32_t)(evtBytes - m_waveBytesWritten) : 0;
		if (waveSizeBefore != 0)
		{
			// Writing can fail sometimes. Advance the pointer by actual written bytes. We will try again later.
			int written = AudioReceivedCallback(waveData, waveSizeBefore);
			m_waveBytesWritten += written;
			waveData += written;
			waveSize -= written;
		}

		// Send the event
		OnSynthEvent(evt.second);
	}

	// write the rest of the audio data
	if (waveSize != 0)
	{
		int written = AudioReceivedCallback(waveData, waveSize);
		m_waveBytesWritten += waveSize;
		// If the audio still couldn't be written, log it
		if ((uint32_t)written != waveSize)
			LogWarn("Rest API: {} bytes of wave data could not be written at byte position {}",
				waveSize, m_waveBytesWritten);
	}
}

void SpeechRestAPI::Mp3ProcessLoop(BlockingQueue<std::string>& queue, std::stop_token token)
{
	Mp3Decoder mp3Decoder;

	for (;;)
	{
		auto msg = queue.take(token);
		if (!msg.has_value())
			return;

		// msg is the whole message (including header) from server
		// after "Path:audio\r\n" are audio binary data
		// Note that the first 2 bytes are not part of the header string
		std::string_view mp3data = *msg;
		mp3data.remove_prefix(2);
		size_t delimPos = mp3data.find("Path:audio\r\n");
		if (delimPos == mp3data.npos) continue;
		mp3data.remove_prefix(delimPos + 12);

		if (!m_firstDataReceived)
		{
			LogDebug("Rest API: MP3 data received");
			m_firstDataReceived = true;
		}

		// Sending audio data to SAPI can block, so do this without lock
		mp3Decoder.Convert(reinterpret_cast<const BYTE*>(mp3data.data()), mp3data.size(),
			std::bind_front(&SpeechRestAPI::ProcessWaveData, this, std::ref(mp3Decoder.GetWaveFormat())));
	}
}

void SpeechRestAPI::OnClose(BlockingQueue<std::string>& queue, WSConnection& conn)
{
	LogDebug("Rest API: Connection closed");
	if (!m_allDataReceived && !m_stopSource.stop_requested())
	{
		LogWarn("Rest API: Connection closed before all data could be received.");
	}

	auto code = conn->get_remote_close_code();
	if (code == websocketpp::close::status::invalid_payload)
	{
		auto msg = std::string("Payload rejected by server: ") + UTF8ToAnsi(conn->get_remote_close_reason());
		queue.fail(std::make_exception_ptr(std::runtime_error(msg)));
	}
	queue.complete();
}

// Send configuration and wait for audio data response
void SpeechRestAPI::SendRequest(WSConnection& conn)
{
	m_allDataReceived = false;

	nlohmann::json json = {
		{"context", {
			{"synthesis", {
				{"audio", {
					{"metadataOptions", {
						{"bookmarkEnabled", (bool)BookmarkCallback},
						{"punctuationBoundaryEnabled", (bool)PunctuationBoundaryCallback},
						{"sentenceBoundaryEnabled", (bool)SentenceBoundaryCallback},
						{"wordBoundaryEnabled", (bool)WordBoundaryCallback},
						{"visemeEnabled", (bool)VisemeCallback},
					}},
					{"outputFormat", "audio-24khz-96kbitrate-mono-mp3"}
				}},
				{"language", {
					{"autoDetection", false}
				}}
			}}
		}}
	};

	std::string reqId = MakeRandomUuid();

	conn->send(
		"X-Timestamp:" + GetTimeStamp() + "\r\n"
		"Content-Type:application/json; charset=utf-8\r\n"
		"Path:speech.config\r\n\r\n"
		+ json.dump(),
		websocketpp::frame::opcode::text);

	conn->send(
		"X-Timestamp:" + GetTimeStamp() + "\r\n"
		"X-RequestId:" + reqId + "\r\n"
		"Content-Type:application/ssml+xml\r\n"
		"Path:ssml\r\n\r\n"
		+ WStringToUTF8(m_ssml),
		websocketpp::frame::opcode::text);
}

void SpeechRestAPI::OnMessage(BlockingQueue<std::string>& queue, WSConnection& conn, WSClient::message_ptr msg)
{
	try
	{
		if (msg->get_opcode() == websocketpp::frame::opcode::binary)
		{
			// If the message is binary, place this message in the queue to let the MP3 thread process it
			queue.push(std::move(msg->get_raw_payload()));
		}
		else
		{
			// If not, after "Path:xxx\r\n\r\n" are JSON texts
			std::string_view text = msg->get_payload();
			size_t pathStartPos = text.find("Path:");
			if (pathStartPos == text.npos) return;
			pathStartPos += 5;
			size_t pathEndPos = text.find("\r\n\r\n", pathStartPos);
			if (pathEndPos == text.npos) return;

			std::string_view path = text.substr(pathStartPos, pathEndPos - pathStartPos);
			if (path == "audio.metadata")
			{
				auto json = nlohmann::json::parse(text.substr(pathEndPos + 4));
				std::lock_guard lock(m_eventsMutex);
				for (auto& event : json.at("Metadata"))
				{
					// use audio offset ticks to sort the events
					auto& data = event.at("Data");
					auto it = data.find("Offset");
					if (it != data.end())
						m_events.emplace(it->get<uint64_t>(), std::move(event));
				}
			}
			else if (path == "turn.end")
			{
				// Data receiving completed
				m_allDataReceived = true;
				queue.complete();
				conn.reset();  // return the connection to the pool
			}
		}
	}
	catch (...)
	{
		queue.fail(std::current_exception());
	}
}

void SpeechRestAPI::OnSynthEvent(const nlohmann::json& metadata)
{
	std::string type = metadata.at("Type");
	auto& data = metadata.at("Data");
	uint64_t offset = data.at("Offset");

	if (type == "Viseme")
	{
		if (VisemeCallback)
			VisemeCallback(offset, data.at("VisemeId"));
	}
	else if (type == "WordBoundary")
	{
		auto& info = data.at("text");
		if (info.at("BoundaryType").get<std::string>() == "PunctuationBoundary")
		{
			if (PunctuationBoundaryCallback)
				PunctuationBoundaryCallback(offset, (uint32_t)FindWord(info.at("Text"), m_lastWordPos), info.at("Length"));
		}
		else
		{
			if (WordBoundaryCallback)
				WordBoundaryCallback(offset, (uint32_t)FindWord(info.at("Text"), m_lastWordPos), info.at("Length"));
		}
	}
	else if (type == "SentenceBoundary")
	{
		auto& info = data.at("text");
		if (SentenceBoundaryCallback)
			SentenceBoundaryCallback(offset, (uint32_t)FindWord(info.at("Text"), m_lastSentencePos), info.at("Length"));
	}
	else if (type == "SessionEnd")
	{
		if (SessionEndCallback)
			SessionEndCallback(offset);
	}
	else if (type == "Bookmark")
	{
		if (BookmarkCallback)
			BookmarkCallback(offset, data.at("Bookmark"));
	}
}

static std::wstring XmlEscape(const std::wstring& str)
{
	std::wstring ret;
	ret.reserve(str.size());
	LPCWSTR pEnd = str.c_str() + str.size();

	for (LPCWSTR pCh = str.c_str(); pCh != pEnd; pCh++)
	{
		switch (*pCh)
		{
		case '<': ret.append(L"&lt;"); break;
		case '>': ret.append(L"&gt;"); break;
		case '&': ret.append(L"&amp;"); break;
		case '"': ret.append(L"&quot;"); break;
		case '\'': ret.append(L"&apos;"); break;
		default: ret.push_back(*pCh); break;
		}
	}

	return ret;
}

// Speech API only returns the word text and word length in a WordBoundary event,
// so we have to calculate the text offset of the word ourselves.
// Returned offset is in WCHARs.
// Argument lastPos [in, out] is the variable that records the last boundary position.
size_t SpeechRestAPI::FindWord(const std::string& utf8Word, size_t& lastPos)
{
	// Escape XML chars, otherwise words such as "you're" will not be matched
	std::wstring word = XmlEscape(UTF8ToWString(utf8Word));
	std::wstring_view ssml = m_ssml;
	size_t startpos = lastPos;
	size_t wordPos;
	while ((wordPos = ssml.find(word, startpos)) != ssml.npos) // look for an occurrence of the word
	{
		// check if there's unmatched "<>" pair before this word
		std::wstring_view beforeWord = ssml.substr(startpos, wordPos - startpos);
		for (;;)
		{
			// look for a '<'
			size_t tagStart = beforeWord.find('<');
			if (tagStart == beforeWord.npos) // no more '<', meaning all "<>" matched or there's no "<>"
			{
				lastPos = wordPos + word.size();
				return wordPos; // exit here
			}

			// look for the matching '>'
			size_t tagEnd = beforeWord.find('>', tagStart + 1);
			if (tagEnd == beforeWord.npos) // no matching '>', so the word is inside a "<>" pair
			{
				break;
			}
			beforeWord.remove_prefix(tagEnd + 1); // look for the next "<>" pair
		}

		// Now we confirmed that the word is inside a "<>" pair
		// Skip to the next '>' and continue searching
		startpos = ssml.find('>', wordPos + word.size());
	}
	return ssml.npos;
}