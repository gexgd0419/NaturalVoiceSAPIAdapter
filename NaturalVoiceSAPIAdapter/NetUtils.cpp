#include "pch.h"
#include <wininet.h>
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

std::string GetProxyForUrl(std::string_view url)
{
	INTERNET_PER_CONN_OPTIONA opt = { INTERNET_PER_CONN_FLAGS };
	INTERNET_PER_CONN_OPTION_LISTA optlist = { sizeof optlist };
	optlist.dwOptionCount = 1;
	optlist.pOptions = &opt;
	DWORD optlistsize = sizeof optlist;
	if (!InternetQueryOptionA(nullptr, INTERNET_OPTION_PER_CONNECTION_OPTION, &optlist, &optlistsize))
		return {};

	struct GlobalDeleter
	{
		void operator()(LPVOID p) { GlobalFree(p); }
	};
	typedef std::unique_ptr<char, GlobalDeleter> GlobalPStr;

	// We are only supporting explicitly set proxy right now, no PAC support
	if (opt.Value.dwValue & PROXY_TYPE_PROXY)
	{
		opt.dwOption = INTERNET_PER_CONN_PROXY_SERVER;
		if (!InternetQueryOptionA(nullptr, INTERNET_OPTION_PER_CONNECTION_OPTION, &optlist, &optlistsize))
			return {};
		GlobalPStr proxyServer(opt.Value.pszValue);

		opt.dwOption = INTERNET_PER_CONN_PROXY_BYPASS;
		if (!InternetQueryOptionA(nullptr, INTERNET_OPTION_PER_CONNECTION_OPTION, &optlist, &optlistsize))
			return {};
		GlobalPStr proxyBypass(opt.Value.pszValue);

		return std::string(GetProxyForUrl(url, proxyServer.get(), proxyBypass.get()));
	}
	else if (opt.Value.dwValue & PROXY_TYPE_AUTO_PROXY_URL)
	{
		static bool pacUnsupportedWarned = false;
		if (!pacUnsupportedWarned)
		{
			LogWarn("Proxy: PAC is not supported. Connections will be made directly.");
			pacUnsupportedWarned = true;
		}
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

	asio::streambuf response;
	asio::error_code ec;
	asio::read(sslstream, response, ec);

	// stream_truncated means that TCP connection was terminated without proper notification on SSL channel
	// Ignore it anyway
	if (ec != asio::error::eof && ec != asio::ssl::error::stream_truncated)
		asio::detail::throw_error(ec);

	// Get the string in the underlying buffer
	std::string_view response_str(static_cast<const char*>(response.data().data()), response.size());
	if (!response_str.starts_with("HTTP/1.1 200 "))
		throw std::runtime_error("Server responded "
			+ std::string(response_str.substr(0, response_str.find('\r'))));

	size_t delimpos = response_str.find("\r\n\r\n");
	if (delimpos == response_str.npos)
		throw std::runtime_error("Invalid server response");
	response_str.remove_prefix(delimpos + 4);

	return std::string(response_str);
}