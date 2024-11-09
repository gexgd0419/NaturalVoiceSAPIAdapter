#pragma once
#define ASIO_STANDALONE 1
#include <websocketpp/client.hpp>
#include <websocketpp/config/asio_client.hpp>
#include <asio.hpp>
#include "WSLogger.h"
#include "Logger.h"
#include <map>
#include "NetUtils.h"
#include "TaskScheduler.h"
#include <wininet.h>

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

typedef websocketpp::client<WSConfig> WSClient;

// A wrapper for websocketpp connection, that will be returned to the connection user.
// Limits what can be done by the user, and makes changing the handler thread-safe.
class WSConnection
{
private:
	friend class WSConnectionPool;
	WSConnection(WSClient::connection_ptr conn)  // only WSConnectionPool can create this
		: m_conn(conn), m_creation_time(std::chrono::steady_clock::now())
	{}

public:
	typedef WSClient::message_handler message_handler;
	typedef websocketpp::close_handler close_handler;

public:
	~WSConnection()
	{
		m_conn->terminate({});
		m_conn.reset();
	}
	void set_message_handler(message_handler handler)
	{
		std::lock_guard lock(m_mutex);
		m_message_handler = std::move(handler);
	}
	void set_close_handler(close_handler handler)
	{
		std::lock_guard lock(m_mutex);
		m_close_handler = std::move(handler);
	}
	void on_message(websocketpp::connection_hdl hdl, WSClient::message_ptr msg)
	{
		std::lock_guard lock(m_mutex);
		if (m_message_handler)
			m_message_handler(hdl, msg);
	}
	void on_close(websocketpp::connection_hdl hdl)
	{
		std::lock_guard lock(m_mutex);
		if (m_close_handler)
			m_close_handler(hdl);
	}
	std::chrono::steady_clock::time_point get_creation_time() const
	{
		return m_creation_time;
	}
	websocketpp::session::state::value get_state() const
	{
		return m_conn->get_state();
	}
	std::error_code send(std::string const& payload, websocketpp::frame::opcode::value op =
		websocketpp::frame::opcode::text)
	{
		return m_conn->send(payload, op);
	}
	std::error_code send(void const* payload, size_t len, websocketpp::frame::opcode::value
		op = websocketpp::frame::opcode::binary)
	{
		return m_conn->send(payload, len, op);
	}
	void close(websocketpp::close::status::value const code, std::string const& reason)
	{
		m_conn->close(code, reason);
	}
	void close(websocketpp::close::status::value const code, std::string const& reason,
		std::error_code& ec)
	{
		m_conn->close(code, reason, ec);
	}
	websocketpp::close::status::value get_remote_close_code() const
	{
		return m_conn->get_remote_close_code();
	}
	std::string const& get_remote_close_reason() const
	{
		return m_conn->get_remote_close_reason();
	}
	std::error_code get_ec() const
	{
		return m_conn->get_ec();
	}
	void terminate(std::error_code const& ec)
	{
		m_conn->terminate(ec);
	}
	LONGLONG get_response_filetime()
	{
		auto& datestr = m_conn->get_response_header("Date");
		if (datestr.empty())
			return 0;

		SYSTEMTIME st;
		if (!InternetTimeToSystemTimeA(datestr.c_str(), &st, 0))
			return 0;
		FILETIME ftResponse;
		if (!SystemTimeToFileTime(&st, &ftResponse))
			return 0;

		return ULARGE_INTEGER{ ftResponse.dwLowDateTime, ftResponse.dwHighDateTime }.QuadPart;
	}

private:
	WSClient::connection_ptr m_conn;
	std::chrono::steady_clock::time_point m_creation_time;
	std::recursive_mutex m_mutex;
	message_handler m_message_handler;
	close_handler m_close_handler;
};

typedef std::shared_ptr<WSConnection> WSConnectionPtr;

class WSConnectionPool
{
public:
	WSConnectionPool();
	~WSConnectionPool();
	WSConnectionPtr TakeConnection(
		const std::string& url,
		const std::string& key = {},
		std::stop_token stop_token = {});

private:
	using clock = std::chrono::steady_clock;
	using duration = clock::duration;
	using WSConnectionUPtr = std::unique_ptr<WSConnection>;

	struct HostId
	{
		std::string url;
		std::string key;
		HostId (const std::string& url, const std::string& key)
			: url(url), key(key)
		{}
		auto operator<=>(const HostId& other) const
		{
			auto ret = url <=> other.url;
			if (ret != 0)
				return ret;
			return key <=> other.key;
		}
	};

	struct HostInfo
	{
		std::list<WSConnectionUPtr> connections;
		std::mutex mutex;
		std::condition_variable connectionChanged;
		std::exception_ptr lastException;
		clock::time_point lastActiveTime;
		size_t count;
		HostInfo()
			: lastActiveTime(clock::now()), count(0)
		{ }
	};

	void AsioThread();
	static WSConnectionPtr GetConnection(HostInfo& info);
	static void PutBackConnection(HostInfo& info, WSConnectionUPtr&& conn);
	static WSConnectionPtr MakeConnectionPtr(HostInfo& info, WSConnectionUPtr&& conn);
	void CreateConnection(
		std::string url,
		const std::string& key,
		HostInfo& info);
	static void SetConnectionHandlers(HostInfo& info, WSConnection* wrapper);
	static void RemoveConnection(HostInfo& info, WSConnection* wrapper);
	void KeepConnectionsAlive();

	WSClient m_client;
	std::thread m_asioThread;

	std::map<HostId, HostInfo> m_hosts;

	std::mutex m_mutex;

	TaskScheduler::TaskHandle m_keepAliveTimer = {};
	duration m_keepAliveInterval;
	duration m_keepAliveDuration;
	duration m_maxConnectionAge;
	size_t m_minCount = 3;
	size_t m_maxCount = 10;
};

