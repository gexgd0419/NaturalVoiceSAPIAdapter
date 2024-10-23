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
typedef WSClient::connection_ptr WSConnection;

class WSConnectionPool
{
public:
	WSConnectionPool();
	~WSConnectionPool();
	WSConnection TakeConnection(
		const std::string& url,
		const std::string& key = {},
		std::stop_token stop_token = {});

private:
	using clock = std::chrono::steady_clock;
	using duration = clock::duration;

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

	struct ConnInfo
	{
		websocketpp::connection_hdl hdl;
		clock::time_point creation_time;
		explicit ConnInfo(websocketpp::connection_hdl hdl)
			: hdl(hdl), creation_time(clock::now())
		{}
	};

	struct HostInfo
	{
		std::list<ConnInfo> connections;
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
	WSConnection CheckAndGetConnection(HostInfo& info);
	void PutBackConnection(HostInfo& info, ConnInfo&& conninfo, const WSConnection& conn);
	WSConnection MakeConnectionPtr(HostInfo& info, ConnInfo&& conninfo, WSConnection&& conn);
	void CreateConnection(
		const std::string& url,
		const std::string& key,
		HostInfo& info);
	void SetConnectionHandlers(HostInfo& info, const WSConnection& conn);
	void RemoveConnection(HostInfo& info, const websocketpp::connection_hdl& hdl);
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

