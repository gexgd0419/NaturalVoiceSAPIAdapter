#include "pch.h"
#include <wininet.h>
#include <winhttp.h>
#include <netlistmgr.h>
#include <regex>
#define ASIO_STANDALONE 1
#include <asio.hpp>
#include <asio/ssl.hpp>
#include "wrappers.h"
#include "NetUtils.h"
#include "StringTokenizer.h"
#include "StrUtils.h"
#pragma comment (lib, "wininet.lib")
#include "Logger.h"
#include "RegKey.h"
#include "SystemLibraryErrorCategory.h"

static std::string RegexEscape(std::string_view str)
{
	std::string ret;
	size_t startpos = 0;
	for (;;)
	{
		// Look for the next character to be escaped
		size_t pos = str.find_first_of("*#$()+.?[\\^{|", startpos);
		if (pos == str.npos)
			break;
		// Copy the part before this character
		ret.append(str, startpos, pos - startpos);
		// Prepend a '.' to each '*'; for other characters, prepend a backslash
		ret.push_back(str[pos] == '*' ? '.' : '\\');
		ret.push_back(str[pos]);
		startpos = pos + 1; // Skip to the next character
	}
	// Copy the rest of the string
	ret.append(str, startpos);
	return ret;
}

static std::string RegexEscape(const std::sub_match<std::string_view::const_iterator>& match)
{
	return RegexEscape(std::string_view(&*match.first, match.second - match.first));
}

static std::regex BuildBypassRegex(std::string_view bypassItem)
{
	// Each bypass item can contain [scheme://]host[:port]
	std::regex parser("^(.*://)?([^:]*)(:[0-9]{1,5})?$", std::regex_constants::icase);
	std::match_results<std::string_view::const_iterator> match;
	std::regex_match(bypassItem.cbegin(), bypassItem.cend(), match, parser);
	std::string regexStr;

	regexStr.push_back('^');

	if (match[1].matched) // scheme
		regexStr.append(RegexEscape(match[1]));
	else
		regexStr.append("(?:.*://)?");

	regexStr.append(RegexEscape(match[2])); // host

	if (match[3].matched) // port
		regexStr.append(RegexEscape(match[3]));
	else
		regexStr.append("(?::[0-9]{1,5})?");

	regexStr.append("(?:/.*)?$"); // followed by either '/' + more contents, or nothing

	return std::regex(regexStr, std::regex_constants::icase);
}

static bool ShouldBypassProxy(std::string_view url, std::string_view proxyBypass)
{
	if (proxyBypass.empty())
		return false;

	bool bypassLocal = false;

	// check every bypass item to see if the url should bypass
	for (std::string_view bypassItem : TokenizeString(proxyBypass, ';'))
	{
		bypassItem = TrimWhitespaces(bypassItem);

		if (EqualsIgnoreCase(bypassItem, "<local>"))
			bypassLocal = true;
		else if (std::regex_match(url.cbegin(), url.cend(), BuildBypassRegex(bypassItem)))
			return true; // matched, so should bypass
	}

	if (bypassLocal)
	{
		// if the host name does not contain dots, treat it as local
		size_t schemeDelimPos = url.find("://");
		size_t hostPos = schemeDelimPos == url.npos ? 0 : schemeDelimPos + 3;
		size_t hostEndDelimPos = url.find('/', hostPos);
		std::string_view host =
			hostEndDelimPos == url.npos ? url.substr(hostPos) : url.substr(hostPos, hostEndDelimPos - hostPos);
		return host.find('.') == host.npos;
	}

	return false;
}

static std::string_view GetProxyForUrl(std::string_view url, std::string_view proxySetting, std::string_view proxyBypass)
{
	if (proxySetting.empty())
		return {};
	if (ShouldBypassProxy(url, proxyBypass))
		return {};

	size_t schemeDelimPos = url.find("://");
	std::string_view scheme;
	if (schemeDelimPos == url.npos)
	{
		scheme = "http";
	}
	else
	{
		scheme = url.substr(0, schemeDelimPos);
		if (EqualsIgnoreCase(scheme, "ws"))
			scheme = "http";
		else if (EqualsIgnoreCase(scheme, "wss"))
			scheme = "https";
	}

	for (std::string_view proxy : TokenizeString(proxySetting, ';'))
	{
		proxy = TrimWhitespaces(proxy);
		size_t pos = proxy.find('=');
		if (pos == proxy.npos)
			return proxy;
		auto cur_scheme = proxy.substr(0, pos);
		if (EqualsIgnoreCase(scheme, cur_scheme))
			return proxy.substr(pos + 1);
	}

	return {};
}

static auto& wininet_category()
{
	return system_library_category("wininet");
}

static auto& winhttp_category()
{
	return system_library_category("winhttp");
}

struct GlobalDeleter
{
	void operator()(LPVOID p) noexcept { GlobalFree(p); }
};
typedef std::unique_ptr<char, GlobalDeleter> GlobalPStr;

static INTERNET_PER_CONN_OPTIONA GetInetPerConnOptionRaw(DWORD flags)
{
	INTERNET_PER_CONN_OPTIONA opt = { flags };
	INTERNET_PER_CONN_OPTION_LISTA optlist = { sizeof optlist };
	optlist.dwOptionCount = 1;
	optlist.pOptions = &opt;
	DWORD optlistsize = sizeof optlist;
	if (!InternetQueryOptionA(nullptr, INTERNET_OPTION_PER_CONNECTION_OPTION, &optlist, &optlistsize))
		throw std::system_error(GetLastError(), wininet_category());
	return opt;
}

static DWORD GetInetPerConnOptionDword(DWORD flags)
{
	return GetInetPerConnOptionRaw(flags).Value.dwValue;
}

static GlobalPStr GetInetPerConnOptionString(DWORD flags)
{
	return GlobalPStr(GetInetPerConnOptionRaw(flags).Value.pszValue);
}

static std::string GetProxyForUrl_PAC(std::string_view url, DWORD inetFlags)
{
	// Use WinHttp (if supported) to resolve auto proxy config
	static HandleWrapper<HMODULE, FreeLibrary> hWinHttp = LoadLibraryW(L"winhttp.dll");
	if (!hWinHttp)
	{
		if (inetFlags & PROXY_TYPE_AUTO_PROXY_URL)
		{
			// Warn for lack of support, if an explicit PAC URL is set
			static bool pacUnsupportedWarned = false;
			if (!pacUnsupportedWarned)
			{
				LogWarn("Proxy: PAC is not supported. Connections will be made directly.");
				pacUnsupportedWarned = true;
			}
		}
		return {};
	}

	static auto pfnWinHttpOpen
		= reinterpret_cast<decltype(WinHttpOpen)*>
		(GetProcAddress(hWinHttp, "WinHttpOpen"));
	static auto pfnWinHttpGetProxyForUrl
		= reinterpret_cast<decltype(WinHttpGetProxyForUrl)*>
		(GetProcAddress(hWinHttp, "WinHttpGetProxyForUrl"));
	static auto pfnWinHttpCloseHandle
		= reinterpret_cast<decltype(WinHttpCloseHandle)*>
		(GetProcAddress(hWinHttp, "WinHttpCloseHandle"));

	struct WinHttpSession
	{
		HINTERNET hSession;
		WinHttpSession()
		{
			hSession = pfnWinHttpOpen(nullptr, WINHTTP_ACCESS_TYPE_NO_PROXY, nullptr, nullptr, 0);
			if (!hSession)
				LogWarn("Proxy: WinHTTP initialization failed: {}",
					std::system_error(GetLastError(), winhttp_category()));
		}
		~WinHttpSession() noexcept { pfnWinHttpCloseHandle(hSession); }
		operator HINTERNET() { return hSession; }
	};

	// Use a single static WinHTTP session
	static WinHttpSession session;
	if (!session)
		return {};

	static DWORD lastFailedTicks = 0;

#pragma warning (suppress: 28159)  // Suppress GetTickCount deprecated warning
	if (lastFailedTicks != 0 && GetTickCount() - lastFailedTicks <= 60000 * 5)
	{
		// if there was no PAC file in the past 5 minutes, don't requery
		return {};
	}

	WINHTTP_AUTOPROXY_OPTIONS autoProxyOpts = {};

	if (inetFlags & PROXY_TYPE_AUTO_DETECT)
	{
		autoProxyOpts.dwFlags = WINHTTP_AUTOPROXY_AUTO_DETECT;
		autoProxyOpts.dwAutoDetectFlags = WINHTTP_AUTO_DETECT_TYPE_DHCP | WINHTTP_AUTO_DETECT_TYPE_DNS_A;
	}

	std::wstring autoConfigUrlW;
	if (inetFlags & PROXY_TYPE_AUTO_PROXY_URL)
	{
		GlobalPStr autoConfigUrl = GetInetPerConnOptionString(INTERNET_PER_CONN_AUTOCONFIG_URL);
		if (autoConfigUrl)
		{
			autoConfigUrlW = StringToWString(autoConfigUrl.get());
			autoProxyOpts.dwFlags |= WINHTTP_AUTOPROXY_CONFIG_URL;
			autoProxyOpts.lpszAutoConfigUrl = autoConfigUrlW.c_str();
		}
	}

	WINHTTP_PROXY_INFO proxyInfo;

	if (!pfnWinHttpGetProxyForUrl(session, StringToWString(url).c_str(), &autoProxyOpts, &proxyInfo))
	{
		DWORD err = GetLastError();
		if (err == ERROR_WINHTTP_AUTODETECTION_FAILED)
		{
			// A common error when proxy auto-detect is enabled but no PAC is set.
			// Don't throw or log at warning level in this case.
#pragma warning (suppress: 28159)  // Suppress GetTickCount deprecated warning
			lastFailedTicks = GetTickCount();  // Do not requery in the next 5 minutes
			LogDebug("Proxy: Auto detection failed");
			return {};
		}
		else
			throw std::system_error(err, winhttp_category());
	}

	lastFailedTicks = 0;

	ScopeGuard proxyInfoDeleter([&proxyInfo]()
		{
			if (proxyInfo.lpszProxy)
				GlobalFree(proxyInfo.lpszProxy);
			if (proxyInfo.lpszProxyBypass)
				GlobalFree(proxyInfo.lpszProxyBypass);
		});

	if (proxyInfo.dwAccessType != WINHTTP_ACCESS_TYPE_NAMED_PROXY)
		return {};

	std::string proxyServer, proxyBypass;
	if (proxyInfo.lpszProxy)
		proxyServer = WStringToString(proxyInfo.lpszProxy);
	if (proxyInfo.lpszProxyBypass)
		proxyBypass = WStringToString(proxyInfo.lpszProxyBypass);

	return std::string(GetProxyForUrl(url, proxyServer, proxyBypass));
}

std::string GetProxyForUrl(std::string_view url)
{
	try
	{
		if (RegKey key = RegOpenNetworkConfigKey(); key.GetDword(L"ProxyOverrideSystemDefault"))
		{
			std::string proxyServer = WStringToString(key.GetString(L"ProxyServer"));
			std::string proxyBypass = WStringToString(key.GetString(L"ProxyBypass"));
			return std::string(GetProxyForUrl(url, proxyServer, proxyBypass));
		}

		DWORD flags = GetInetPerConnOptionDword(INTERNET_PER_CONN_FLAGS);

		if (flags & PROXY_TYPE_PROXY)
		{
			GlobalPStr proxyServer = GetInetPerConnOptionString(INTERNET_PER_CONN_PROXY_SERVER);
			GlobalPStr proxyBypass = GetInetPerConnOptionString(INTERNET_PER_CONN_PROXY_BYPASS);

			return std::string(GetProxyForUrl(
				url,
				proxyServer ? proxyServer.get() : std::string_view(),
				proxyBypass ? proxyBypass.get() : std::string_view()
			));
		}
		else if (flags & (PROXY_TYPE_AUTO_PROXY_URL | PROXY_TYPE_AUTO_DETECT))
		{
			// WinHttp only accepts http(s) schemes. Convert ws(s) to http(s) first.

			size_t schemeDelimPos = url.find("://");
			std::string newUrl;
			if (schemeDelimPos == url.npos)
			{
				newUrl = "http://";
				newUrl += url;
			}
			else
			{
				auto scheme = url.substr(0, schemeDelimPos);
				if (EqualsIgnoreCase(scheme, "ws"))
					newUrl = "http://";
				else if (EqualsIgnoreCase(scheme, "wss"))
					newUrl = "https://";
				newUrl += url.substr(schemeDelimPos + 3);
			}

			return GetProxyForUrl_PAC(newUrl, flags);
		}
	}
	catch (const std::system_error& ex)
	{
		LogWarn("Proxy: Failed to get proxy settings, connections will be made directly: {}", ex);
	}

	return {}; // Empty string indicates no proxy (direct)
}

static bool TryOpenProxiedConnection(LPCSTR lpszUrl, asio::io_context& ioctx, asio::ssl::stream<asio::ip::tcp::socket>& sslstream)
{
	std::string proxy = GetProxyForUrl(lpszUrl);
	if (proxy.empty())
		return false;
	auto proxyUrl = ParseUrl(proxy);
	// Only HTTP proxies are supported (no scheme or http://)
	if (!proxyUrl.scheme.empty() && !EqualsIgnoreCase(proxyUrl.scheme, "http"))
		return false;
	auto& socket = sslstream.next_layer();
	try
	{
		// Resolve host name. The result cannot be empty.
		auto resolveret = asio::ip::tcp::resolver(ioctx).resolve(proxyUrl.host, proxyUrl.port);
		socket.connect(*resolveret);
	}
	catch (const std::system_error&)
	{
		return false;
	}
	try
	{
		asio::streambuf request;
		std::ostream request_stream(&request);
		auto url = ParseUrl(lpszUrl);
		request_stream << "CONNECT " << url.host << ':' << url.port << " HTTP/1.1\r\n\r\n";
		asio::write(socket, request);
		asio::streambuf response;
		asio::read_until(socket, response, "\r\n\r\n");
		std::string_view response_str(static_cast<const char*>(response.data().data()), response.size());
		if (!response_str.starts_with("HTTP/1.1 2"))
		{
			// If not HTTP 2XX code
			socket.close();
			return false;
		}
		sslstream.handshake(asio::ssl::stream_base::client);
		return true;
	}
	catch (const std::system_error&)
	{
		socket.close();
		return false;
	}
}

std::string DownloadToString(LPCSTR lpszUrl, LPCSTR lpszHeaders)
{
	// Use ASIO instead of WinInet to download from HTTPS server
	// because WinInet on Windows XP will be rejected by the server, possibly because of lack of support for TLSv1.2
	
	// only handle HTTPS urls
	if (_strnicmp(lpszUrl, "https://", 8) != 0)
		throw std::invalid_argument("Only HTTPS urls are supported");

	asio::io_context ioctx;
	asio::ssl::context sslctx(asio::ssl::context::sslv23_client);
	asio::ssl::stream<asio::ip::tcp::socket> sslstream(ioctx, sslctx);

	auto url = ParseUrl(lpszUrl);
	// Try proxy connection first. If failed or no proxy, try direct connection
	if (!TryOpenProxiedConnection(lpszUrl, ioctx, sslstream))
	{
		// Resolve host name. The result cannot be empty.
		auto resolveret = asio::ip::tcp::resolver(ioctx).resolve(url.host, url.port);

		// connect & handshake
		sslstream.next_layer().connect(*resolveret);
		sslstream.handshake(asio::ssl::stream_base::client);
	}

	// send a GET request
	asio::streambuf request;
	std::ostream request_stream(&request);
	request_stream <<
		"GET " << url.path << " HTTP/1.1\r\n"
		"Host: " << url.host << "\r\n"
		"Connection: close\r\n";
	if (lpszHeaders)
		request_stream << lpszHeaders;
	request_stream << "\r\n";
	asio::write(sslstream, request);

	std::string response;
	asio::error_code ec;
	asio::read(sslstream, asio::dynamic_string_buffer(response), ec);

	// stream_truncated means that TCP connection was terminated without proper notification on SSL channel
	// Ignore it anyway
	if (ec != asio::error::eof && ec != asio::ssl::error::stream_truncated)
		asio::detail::throw_error(ec);

	// Get the string in the underlying buffer
	if (!response.starts_with("HTTP/1.1 200 "))
		throw std::runtime_error("Server responded " + response.substr(0, response.find('\r')));

	size_t delimpos = response.find("\r\n\r\n");
	if (delimpos == response.npos)
		throw std::runtime_error("Invalid server response");
	response.erase(0, delimpos + 4);

	return response;
}