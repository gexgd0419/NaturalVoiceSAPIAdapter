#pragma once
#include <string_view>
#include <stringapiset.h>

template <typename CharT>
constexpr bool EqualsIgnoreCaseInternal(std::basic_string_view<CharT> a, const CharT* b) noexcept
{
	const CharT* pa = a.data(), * pb = b, * paend = pa + a.size();
	for (; pa != paend; pa++, pb++)
	{
		CharT ca = *pa, cb = *pb;
		if (ca >= 'A' && ca <= 'Z') ca += 'a' - 'A';
		if (cb >= 'A' && cb <= 'Z') cb += 'a' - 'A';
		if (ca != cb)
			return false;
	}
	return true;
}

template <typename CharT>
constexpr bool EqualsIgnoreCase(std::basic_string_view<CharT> a, std::basic_string_view<CharT> b) noexcept
{
	if (a.size() != b.size())
		return false;
	return EqualsIgnoreCaseInternal(a, b.data());
}

template <typename CharT, size_t N>
constexpr bool EqualsIgnoreCase(std::basic_string_view<CharT> a, const CharT(&b)[N]) noexcept
{
	if (a.size() != N)
		return false;
	return EqualsIgnoreCaseInternal(a, b);
}

inline std::wstring UTF8ToWString(std::string_view str)
{
	int size = MultiByteToWideChar(CP_UTF8, 0, str.data(), (int)str.size(), nullptr, 0);
	if (size <= 0) return {};
	std::wstring ret;
	ret.resize_and_overwrite(size, [str](wchar_t* buf, size_t size)
		{
			return MultiByteToWideChar(CP_UTF8, 0, str.data(), (int)str.size(), buf, (int)size);
		});
	return ret;
}

inline std::string WStringToUTF8(std::wstring_view str)
{
	int size = WideCharToMultiByte(CP_UTF8, 0, str.data(), (int)str.size(), nullptr, 0, nullptr, nullptr);
	if (size <= 0) return {};
	std::string ret;
	ret.resize_and_overwrite(size, [str](char* buf, size_t size)
		{
			return WideCharToMultiByte(CP_UTF8, 0, str.data(), (int)str.size(), buf, (int)size, nullptr, nullptr);
		});
	return ret;
}

inline std::string_view TrimWhitespaces(std::string_view stringview) noexcept
{
	const char* pStart = stringview.data(), * pEnd = pStart + stringview.size();
	for (; pStart != pEnd; pStart++)
	{
		if (!isspace(*pStart))
			break;
	}
	for (pEnd--; ; pEnd--)
	{
		if (!isspace(*pEnd))
			break;
		if (pEnd == pStart)
			break;
	}
	return std::string_view(pStart, pEnd + 1);
}