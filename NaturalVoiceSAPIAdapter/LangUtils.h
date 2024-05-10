#pragma once
#include <WinBase.h>
#include <string_view>
#include <algorithm>
#include <WinNls.h>

std::vector<LANGID> GetLangIDFallbacks(LANGID lang);
LANGID LangIDFromLocaleName(const std::wstring& locale);

constexpr LANGID HexLangToLangID(std::wstring_view hexlang)
{
	const WCHAR* p = hexlang.data(), * pEnd = p + hexlang.size();
	LANGID lang = 0;
	for (; p != pEnd; p++)
	{
		int digit;
		WCHAR ch = *p;
		if (ch >= '0' && ch <= '9') digit = ch - '0';
		else if (ch >= 'a' && ch <= 'f') digit = ch - 'a' + 10;
		else if (ch >= 'A' && ch <= 'F') digit = ch - 'A' + 10;
		else break;
		lang = (LANGID)((lang << 4) + digit);
	}
	return lang;
}

constexpr inline const char* HexDigits = "0123456789abcdef";

inline std::wstring LangIDToHexLang(LANGID lang)
{
	bool leading = true;
	std::wstring ret;
	for (int i = 3; i >= 0; i--)
	{
		int digit = (lang >> (i * 4)) & 0xF;
		if (digit == 0 && leading)
			continue;
		ret.push_back(HexDigits[digit]);
		leading = false;
	}
	return ret;
}