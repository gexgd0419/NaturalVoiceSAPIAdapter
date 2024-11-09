#include "pch.h"
#include "WSConnectionPool.h"
#include "RegKey.h"
#include "SpeechServiceConstants.h"
#include <openssl/sha.h>

extern TaskScheduler g_taskScheduler;

// Time delta between the remote server time and the local time.
static std::atomic<LONGLONG> s_responseTimeDelta = 0;

static void ReplaceString(std::string& str, std::string_view from, const std::string& to)
{
	size_t pos = 0;
	while ((pos = str.find(from, pos)) != std::string::npos)
	{
		str.replace(pos, from.size(), to);
		pos += to.size();
	}
}

static std::string GetGECToken()
{
	// algorithm for generating the Sec-MS-GEC token
	// see: https://github.com/rany2/edge-tts/issues/290#issuecomment-2464956570

	FILETIME ft;
	GetSystemTimeAsFileTime(&ft);
	ULARGE_INTEGER ul = { ft.dwLowDateTime, ft.dwHighDateTime };
	// add the time delta to the local time
	ul.QuadPart += s_responseTimeDelta.load(std::memory_order_relaxed);
	ul.QuadPart -= ul.QuadPart % 3'000'000'000;  // round down to the nearest 5 minute

	// concatenate ticks & trusted client token into a string
	// then calculate SHA-256 hash
	std::string str = std::to_string(ul.QuadPart) + EDGE_TRUSTED_CLIENT_TOKEN;
	BYTE hash[SHA256_DIGEST_LENGTH];
	if (!SHA256(reinterpret_cast<const BYTE*>(str.data()), str.size(), hash))
		throw std::runtime_error("GEC token generation failed");

	// convert the hash to a hexadecimal string
	std::string result(SHA256_DIGEST_LENGTH * 2, '0');
	char* pCh = result.data();
	constexpr const char* HexDigits = "0123456789ABCDEF";
	for (BYTE byte : hash)
	{
		*pCh++ = HexDigits[(byte & 0xF0) >> 4];
		*pCh++ = HexDigits[byte & 0x0F];
	}

	return result;
}

WSConnectionPool::WSConnectionPool()
{
	m_client.init_asio();
	m_client.set_tls_init_handler([](websocketpp::connection_hdl)
		{
			auto ctx = std::make_shared<asio::ssl::context>(asio::ssl::context::sslv23_client);
			return ctx;
		});
	m_asioThread = std::thread(std::bind(&WSConnectionPool::AsioThread, this));

	using namespace std::chrono;
	using namespace std::chrono_literals;

	auto key = RegOpenConfigKey();

	m_minCount = key.GetDword(L"ConnectionPoolMinCount", 3);
	m_maxCount = key.GetDword(L"ConnectionPoolMaxCount", 10);
	m_keepAliveInterval = seconds(
		key.GetDword(L"ConnectionKeepAliveInterval",
			(DWORD)duration_cast<seconds>(20s).count())
	);
	m_keepAliveDuration = seconds(
		key.GetDword(L"ConnectionKeepAliveDuration",
			(DWORD)duration_cast<seconds>(10min).count())
	);
	m_maxConnectionAge = seconds(
		key.GetDword(L"ConnectionMaxAge",
			(DWORD)duration_cast<seconds>(3min + 30s).count())
	);

	DWORD intervalMs = (DWORD)duration_cast<milliseconds>(m_keepAliveInterval).count();
	if (intervalMs != 0 && m_keepAliveDuration != clock::duration::zero())
	{
		m_keepAliveTimer = g_taskScheduler.StartNewTask(
			intervalMs, intervalMs,
			std::bind_front(&WSConnectionPool::KeepConnectionsAlive, this)
		);
	}
}

WSConnectionPool::~WSConnectionPool()
{
	// close all connections
	std::lock_guard outer_lock(m_mutex);
	for (auto& host : m_hosts)
	{
		std::lock_guard inner_lock(host.second.mutex);
		for (auto& conn : host.second.connections)
		{
			conn->terminate(std::error_code());
		}
	}
	m_hosts.clear();
	m_client.stop_perpetual();
	m_client.stop();
	if (m_asioThread.joinable())
		m_asioThread.join();

	if (m_keepAliveTimer)
		g_taskScheduler.CancelTask(m_keepAliveTimer, false);
}

void WSConnectionPool::AsioThread()
{
	m_client.start_perpetual();
	for (;;) // allows IO loop to recover from exceptions thrown from handlers
	{
		try
		{
			m_client.run();
			break; // exit the thread on normal return
		}
		catch (const std::system_error& ex)
		{
			// log the exception, then rejoin the IO loop
			LogErr("Connection pool: {}", ex);
		}
		catch (const std::exception& ex)
		{
			// log the exception, then rejoin the IO loop
			LogErr("Connection pool: {}", ex);
		}
	}
}

// Check every connection in the list, and return the first open one
WSConnectionPtr WSConnectionPool::GetConnection(HostInfo& info)
{
	auto& conns = info.connections;
	WSConnectionPtr result = nullptr;
	const auto now = clock::now();

	auto it = std::find_if(conns.begin(), conns.end(),
		[](const WSConnectionUPtr& conn) { return conn->get_state() == websocketpp::session::state::open; });

	// choose the first working connection
	if (it != conns.end())
	{
		// take it out of the list, without decreasing the actual count
		result = MakeConnectionPtr(info, std::move(*it));
		conns.erase(it);
		info.lastActiveTime = clock::now();
		LogDebug("Connection pool: Connection {} taken from pool ({}/{})",
			result->m_conn, conns.size(), info.count);
	}

	return result;
}

// Take the first usable connection from the list. If there isn't any, wait for a new connection.
WSConnectionPtr WSConnectionPool::TakeConnection(
	const std::string& url,
	const std::string& key,
	std::stop_token stop_token)
{
	decltype(m_hosts)::iterator host_it;

	{
		HostId host_id(url, key);
		std::lock_guard lock(m_mutex);
		host_it = m_hosts.try_emplace(std::move(host_id)).first;  // if not found, add a new host
	}

	auto& info = host_it->second;

	std::stop_callback stopCallback(stop_token, [&info]()
		{
			{ std::lock_guard lock(info.mutex); }  // lock and unlock
			info.connectionChanged.notify_all();
		});

	// Define unique_lock AFTER stop_callback.
	// This makes sure that the lock is unlocked BEFORE stop_callback is destroyed.
	// Because the destructor of stop_callback will wait for the callback to complete,
	// which can cause deadlocks.
	std::unique_lock lock(info.mutex);

	if (stop_token.stop_requested())
		return {};

	for (;;)
	{
		auto conn = GetConnection(info);

		// Replenish connections to the minimum count.
		// If there's no spare connection, add an extra one,
		// unless we are at the maxCount limit.
		const size_t replenishToCount = (conn == nullptr)
			? std::clamp(info.count + 1, m_minCount, m_maxCount)
			: m_minCount;
		for (size_t n = info.count; n < replenishToCount; n++)
		{
			CreateConnection(url, key, info);
		}

		// Return the chosen connection, if there is one
		if (conn)
			return conn;

		// If there isn't, wait for new connections and try again
		LogDebug("Connection pool: Waiting for new connection...");
		info.connectionChanged.wait(lock);
		if (stop_token.stop_requested())
			return {};
		if (info.lastException)
			std::rethrow_exception(info.lastException);
	}
}

void WSConnectionPool::PutBackConnection(HostInfo& info, WSConnectionUPtr&& conn)
{
	std::lock_guard lock(info.mutex);
	conn->set_message_handler(nullptr);
	conn->set_close_handler(nullptr);
	if (conn->get_state() == websocketpp::session::state::open)
	{
		LogDebug("Connection pool: Connection {} put back to pool ({}/{})",
			conn->m_conn, info.connections.size(), info.count);
		info.connections.push_back(std::move(conn));  // put at the end of the pool
		info.connectionChanged.notify_all();
	}
	else
	{
		info.count--;
		LogDebug("Connection pool: Connection {} closed and dropped ({}/{})",
			conn->m_conn, info.connections.size(), info.count);

		// remove it (reset its handlers) instead of putting it back to the pool
		// connection count won't be decreased again, because it's not in the pool
		RemoveConnection(info, conn.get());
		conn.reset();
	}
}

// Create a shared_ptr that put the connection back to the pool automatically
WSConnectionPtr WSConnectionPool::MakeConnectionPtr(HostInfo& info, WSConnectionUPtr&& conn)
{
	// A shared_ptr that keeps a reference to the original connection (pointer),
	// with a deleter that puts the connection back to the pool
	std::shared_ptr<WSConnectionUPtr> ptr(
		new WSConnectionUPtr(std::move(conn)),
		[&info](WSConnectionUPtr* pPtr)
		{
			PutBackConnection(info, std::move(*pPtr));
			delete pPtr;
		}
	);

	// Returns an aliasing pointer that points to the actual connection object
	return WSConnectionPtr(ptr, ptr->get());
}

void WSConnectionPool::CreateConnection(
	std::string url,
	const std::string& key,
	HostInfo& info)
{
	ReplaceString(url, "{Sec-MS-GEC}", GetGECToken());

	std::error_code ec;
	auto conn = m_client.get_connection(url, ec);
	if (ec)
		throw std::system_error(ec);

	WSConnectionUPtr wrapper(new WSConnection(conn));

	SetConnectionHandlers(info, wrapper.get());

	if (!key.empty())
		conn->append_header("Ocp-Apim-Subscription-Key", key);

	std::string proxy = GetProxyForUrl(url);
	if (!proxy.empty())
	{
		size_t schemeDelimPos = proxy.find("://");
		if (schemeDelimPos == proxy.npos)
		{
			conn->set_proxy("http://" + std::move(proxy));
		}
		else
		{
			std::string_view scheme(proxy.data(), schemeDelimPos);
			if (EqualsIgnoreCase(scheme, "http"))
				conn->set_proxy(proxy);
		}
	}

	m_client.connect(conn);
	info.connections.push_back(std::move(wrapper));
	info.count++;
	LogDebug("Connection pool: New connection {} created and put into pool ({}/{})",
		conn, info.connections.size(), info.count);
}

void WSConnectionPool::SetConnectionHandlers(HostInfo& info, WSConnection* wrapper)
{
	// Here we assume that the wrapper will live as long as the connection does.
	// When the wrapper is going to be destroyed, reset the connection's handlers
	// using RemoveConnection().

	auto& conn = wrapper->m_conn;

	conn->set_open_handler([&info](websocketpp::connection_hdl hdl)
		{
			std::lock_guard lock(info.mutex);
			info.lastException = nullptr;
			LogDebug("Connection pool: Connection {} opened", hdl);
			info.connectionChanged.notify_all();
		});

	conn->set_message_handler([wrapper](websocketpp::connection_hdl hdl, WSClient::message_ptr msg)
		{
			wrapper->on_message(hdl, msg);
		});

	conn->set_close_handler([&info, wrapper](websocketpp::connection_hdl hdl)
		{
			wrapper->on_close(hdl);
			std::lock_guard lock(info.mutex);
			info.lastException = nullptr;
			LogDebug("Connection pool: Connection {} closed, removed from pool ({}/{})",
				hdl, info.connections.size(), info.count);
			RemoveConnection(info, wrapper);
			info.connectionChanged.notify_all();
		});

	conn->set_fail_handler([&info, wrapper](websocketpp::connection_hdl hdl)
		{
			FILETIME ftNow;
			GetSystemTimeAsFileTime(&ftNow);
			LONGLONG llNow = ULARGE_INTEGER{ ftNow.dwLowDateTime, ftNow.dwHighDateTime }.QuadPart;

			std::lock_guard lock(info.mutex);
			auto ec = wrapper->get_ec();
			std::system_error ex(ec);
			info.lastException = ec ? std::make_exception_ptr(ex) : nullptr;
			LogDebug("Connection pool: Connection {} failed, removed from pool: {}", hdl, ex);
			if (ec == websocketpp::processor::error::invalid_http_status)
			{
				// The server rejected the connection. The local time may be incorrect.
				// Re-calculate the time delta so that the next attempt may succeed.
				LONGLONG llResponse = wrapper->get_response_filetime();
				if (llResponse != 0)
					s_responseTimeDelta.store(llResponse - llNow, std::memory_order_relaxed);
			}
			RemoveConnection(info, wrapper);
			info.connectionChanged.notify_all();
		});
}

void WSConnectionPool::RemoveConnection(HostInfo& info, WSConnection* wrapper)
{
	// reset handlers to prevent them being called after free
	auto& conn = wrapper->m_conn;
	conn->set_open_handler(nullptr);
	conn->set_message_handler(nullptr);
	conn->set_close_handler(nullptr);
	conn->set_fail_handler(nullptr);

	// here the wrapper object and the connection are freed
	info.count -=
		info.connections.remove_if([wrapper](const WSConnectionUPtr& other) { return other.get() == wrapper; });
}

void WSConnectionPool::KeepConnectionsAlive()
{
	// Edge voice connections can be closed after 30 seconds of inactivity.
	// Sending empty 'speech.config' can keep it alive longer,
	// but still no more than 4 minutes.
	// So here we check if any connection lives long enough,
	// if so, replace it with a new connection.

	const auto now = clock::now();

	std::lock_guard outer_lock(m_mutex);
	for (auto& host : m_hosts)
	{
		auto& info = host.second;
		std::lock_guard inner_lock(info.mutex);

		if (now - info.lastActiveTime > m_keepAliveDuration)
			continue;  // no need to keep this host group alive

		std::vector<decltype(info.connections)::iterator> connectionsToClose;

		for (auto it = info.connections.begin(); it != info.connections.end(); ++it)
		{
			auto& conn = *it;
			if (conn->get_state() != websocketpp::session::state::open)  // already dead
				continue;

			// keep the connection alive
			conn->send(
				"Content-Type:application/json; charset=utf-8\r\n"
				"Path:speech.config\r\n\r\n"
				"{}", 70,
				websocketpp::frame::opcode::text);

			if (now - conn->get_creation_time() > m_maxConnectionAge)
			{
				// should be closed later
				connectionsToClose.push_back(it);
			}
		}

		// some of the connections can be closed now
		if (!connectionsToClose.empty() && info.count > m_minCount)
		{
			const size_t closeCount = std::min(info.count - m_minCount, connectionsToClose.size());
			if (closeCount < connectionsToClose.size())
			{
				// If we cannot close all of them, sort to get the oldest N connections we can close
				std::sort(connectionsToClose.begin(), connectionsToClose.end(),
					[](auto& a, auto& b) { return (*a)->get_creation_time() > (*b)->get_creation_time(); });
			}
			// close the oldest N connections
			for (size_t i = 0; i < closeCount; i++)
			{
				(*connectionsToClose[i])->close(websocketpp::close::status::normal, {});
			}
		}

		// Replenish new connections as if all connections to close had been closed.
		const size_t replenishToCount = std::min(
			std::max(info.count - connectionsToClose.size(), m_minCount) + connectionsToClose.size(),
			m_maxCount);
		for (size_t n = info.count; n < replenishToCount; n++)
		{
			CreateConnection(host.first.url, host.first.key, info);
		}
	}
}
