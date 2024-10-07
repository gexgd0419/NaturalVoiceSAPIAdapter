#include "pch.h"
#include "WSConnectionPool.h"
#include "RegKey.h"

extern TaskScheduler g_taskScheduler;

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
		m_keepAliveTimer = g_taskScheduler.StartNewTask(
			intervalMs, intervalMs,
			std::bind_front(&WSConnectionPool::KeepConnectionsAlive, this)
		);
}

WSConnectionPool::~WSConnectionPool()
{
	// close all connections
	std::lock_guard outer_lock(m_mutex);
	for (auto& host : m_hosts)
	{
		std::lock_guard inner_lock(host.second.mutex);
		for (auto& conninfo : host.second.connections)
		{
			std::error_code ec;
			auto conn = m_client.get_con_from_hdl(conninfo.hdl, ec);
			if (conn)
			{
				conn->terminate(std::error_code());
			}
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

// Check every connection in the list, remove the dead ones, and return the first open one
WSConnection WSConnectionPool::CheckAndGetConnection(HostInfo& info)
{
	auto& conns = info.connections;
	WSConnection result = nullptr;
	const auto now = clock::now();

	for (auto it = conns.begin(); it != conns.end(); )
	{
		std::error_code ec;
		auto conn = m_client.get_con_from_hdl(it->hdl, ec);
		if (!conn || conn->get_state() == websocketpp::session::state::closed)
		{
			// remove the current, skip to the next
			auto curr = it;
			++it;
			conns.erase(curr);
			--info.count;  // decrease the actual count
			LogDebug("Connection pool: Removed dead connection {}", conn);
			continue;
		}

		if (conn->get_state() == websocketpp::session::state::open)
		{
			// choose the first working connection
			if (!result)
			{
				// take it out of the list, without decreasing the actual count
				auto curr = it;
				++it;
				result = MakeConnectionPtr(info, std::move(*curr), std::move(conn));
				conns.erase(curr);
				info.lastActiveTime = clock::now();
				LogDebug("Connection pool: Connection {} taken from pool ({}/{})",
					result, conns.size(), info.count);
				continue;
			}
		}

		++it;
	}

	return result;
}

// Take the first usable connection from the list. If there isn't any, wait for a new connection.
WSConnection WSConnectionPool::TakeConnection(
	const std::string& url,
	const std::string& key,
	std::stop_token stop_token)
{
	decltype(m_hosts)::iterator host_it;

	{
		HostId host_id(url, key);
		std::lock_guard lock(m_mutex);
		host_it = m_hosts.find(host_id);
		if (host_it == m_hosts.end())  // if not found, add a new host
			host_it = m_hosts.try_emplace(std::move(host_id)).first;
	}

	auto& info = host_it->second;

	std::stop_callback stopCallback(stop_token, [this, &info]()
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
		auto conn = CheckAndGetConnection(info);

		// Replenish connections to the minimum count.
		// If there's no spare connection, add an extra one,
		// unless we are at the maxCount limit.
		const size_t replenishToCount = info.connections.empty()
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

void WSConnectionPool::PutBackConnection(HostInfo& info, ConnInfo&& conninfo, const WSConnection& conn)
{
	std::lock_guard lock(info.mutex);
	SetConnectionHandlers(info, conn);
	if (conn->get_state() == websocketpp::session::state::open)
	{
		info.connections.push_back(std::move(conninfo));  // put at the end of the pool
		LogDebug("Connection pool: Connection {} put back to pool ({}/{})",
			conn, info.connections.size(), info.count);
		info.connectionChanged.notify_all();
	}
	else
	{
		info.count--;
		LogDebug("Connection pool: Connection {} closed and dropped ({}/{})",
			conn, info.connections.size(), info.count);
	}
}

// Create a shared_ptr that put the connection back to the pool automatically
WSConnection WSConnectionPool::MakeConnectionPtr(HostInfo& info, ConnInfo&& conninfo, WSConnection&& conn)
{
	// A shared_ptr that keeps a reference to the original connection (pointer),
	// with a deleter that puts the connection back to the pool
	std::shared_ptr<WSConnection> ptr(
		new WSConnection(std::move(conn)),
		[this, &info, conninfo = std::move(conninfo)](WSConnection* pConn) mutable
		{
			PutBackConnection(info, std::move(conninfo), *pConn);
			delete pConn;
		}
	);

	// Returns an aliasing pointer that points to the actual connection object
	return WSConnection(ptr, ptr->get());
}

void WSConnectionPool::CreateConnection(
	const std::string& url,
	const std::string& key,
	HostInfo& info)
{
	std::error_code ec;
	auto conn = m_client.get_connection(url, ec);
	if (ec)
		throw std::system_error(ec);

	conn->set_open_handler([this, &info](websocketpp::connection_hdl hdl)
		{
			std::lock_guard lock(info.mutex);
			info.lastException = nullptr;
			LogDebug("Connection pool: Connection {} opened", hdl);
			info.connectionChanged.notify_all();
		});
	SetConnectionHandlers(info, conn);

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
	info.connections.emplace_back(conn->get_handle());
	info.count++;
	LogDebug("Connection pool: New connection {} created and put into pool ({}/{})",
		conn, info.connections.size(), info.count);
}

void WSConnectionPool::SetConnectionHandlers(HostInfo& info, const WSConnection& conn)
{
	conn->set_message_handler(nullptr);
	conn->set_close_handler([this, &info](websocketpp::connection_hdl hdl)
		{
			std::lock_guard lock(info.mutex);
			RemoveConnection(info, hdl);
			info.lastException = nullptr;
			LogDebug("Connection pool: Connection {} closed, removed from pool ({}/{})",
				hdl, info.connections.size(), info.count);
			info.connectionChanged.notify_all();
		});
	conn->set_fail_handler([this, &info](websocketpp::connection_hdl hdl)
		{
			std::lock_guard lock(info.mutex);
			RemoveConnection(info, hdl);
			auto ec = m_client.get_con_from_hdl(hdl)->get_ec();
			std::system_error ex(ec);
			info.lastException = ec ? std::make_exception_ptr(ex) : nullptr;
			LogDebug("Connection pool: Connection {} failed, removed from pool: {}", hdl, ex);
			info.connectionChanged.notify_all();
		});
}

void WSConnectionPool::RemoveConnection(HostInfo& info, const websocketpp::connection_hdl& hdl)
{
	auto& conns = info.connections;
	auto it = std::find_if(conns.begin(), conns.end(),
		[ptr = hdl.lock()](const ConnInfo& conninfo)
		{
			return ptr == conninfo.hdl.lock();
		});
	if (it != conns.end())
	{
		conns.erase(it);
		info.count--;
	}
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
			std::error_code ec;
			auto conn = m_client.get_con_from_hdl(it->hdl, ec);
			if (!conn || conn->get_state() != websocketpp::session::state::open)  // already dead
				continue;

			// keep the connection alive
			conn->send(
				"Content-Type:application/json; charset=utf-8\r\n"
				"Path:speech.config\r\n\r\n"
				"{}", 70,
				websocketpp::frame::opcode::text);

			if (now - it->creation_time > m_maxConnectionAge)
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
					[](auto& a, auto& b) { return a->creation_time > b->creation_time; });
			}
			// close the oldest N connections
			for (size_t i = 0; i < closeCount; i++)
			{
				std::error_code ec;
				auto conn = m_client.get_con_from_hdl(connectionsToClose[i]->hdl, ec);
				if (conn)
					conn->close(websocketpp::close::status::normal, {});
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
