#pragma once
#include "StrUtils.h"

struct UrlComponents
{
	std::string_view scheme;
	std::string_view host;
	std::string_view port;
	std::string_view path;
};

constexpr UrlComponents ParseUrl(std::string_view url) noexcept
{
	size_t schemeDelim = url.find("://");
	size_t hostStart = schemeDelim != url.npos ? schemeDelim + 3 : 0;
	size_t firstSlash = url.find('/', hostStart);
	size_t pathStart = firstSlash != url.npos ? firstSlash : url.size();
	auto authority = url.substr(hostStart, firstSlash != url.npos ? firstSlash - hostStart : url.npos);
	size_t portColon = authority.rfind(':');
	auto scheme = url.substr(0, schemeDelim != url.npos ? schemeDelim : 0);
	std::string_view port;
	if (portColon != authority.npos)
		port = authority.substr(portColon + 1);
	else if (EqualsIgnoreCase(scheme, "https"))
		port = "443";
	else if (scheme.empty() || EqualsIgnoreCase(scheme, "http"))
		port = "80";
	return
	{
		.scheme = scheme,
		.host = authority.substr(0, portColon != authority.npos ? portColon : authority.npos),
		.port = port,
		.path = url.substr(pathStart)
	};
}

std::string GetProxyForUrl(std::string_view url);

std::string DownloadToString(LPCSTR lpszUrl, LPCSTR lpszHeaders);