#pragma once
#include <WinBase.h>
#include <string_view>
#include <algorithm>
#include <WinNls.h>

// Special LANGID fallbacks that does not follow the PRIMARYLANGID pattern
// (from YY-Thunks, from reverse engineering of Nlsdl.dll)
// Sorted so can be binary-searched
constexpr inline std::pair<LANGID, LANGID> LangIDFallbackTable[] =
{
	{ 0x0004, 0x7804 }, //zh-Hans -> zh
	{ 0x0404, 0x7C04 }, //zh-TW -> zh-Hant 
	{ 0x0414, 0x7C14 }, //nb-NO -> nb 
	{ 0x0428, 0x7C28 }, //tg-Cyrl-TJ -> tg-Cyrl
	{ 0x042C, 0x782C }, //az-Latn-AZ -> az-Latn
	{ 0x0443, 0x7C43 }, //uz-Latn-UZ -> uz-Latn
	{ 0x0450, 0x7850 }, //mn-MN -> mn-Cyrl
	{ 0x045D, 0x785D }, //iu-Cans-CA -> iu-Cans
	{ 0x0468, 0x7C68 }, //ha-Latn-NG -> ha-Latn
	{ 0x0501, 0x0009 }, //qps-ploc -> en 
	{ 0x05FE, 0x0011 }, //qps-ploca -> ja 
	{ 0x0814, 0x7814 }, //nn-NO -> nn 
	{ 0x081A, 0x701A }, //sr-Latn-CS -> sr-Latn 
	{ 0x082C, 0x742C }, //az-Cyrl-AZ -> az-Cyrl 
	{ 0x082E, 0x7C2E }, //dsb-DE -> dsb 
	{ 0x0843, 0x7843 }, //uz-Cyrl-UZ -> uz-Cyrl
	{ 0x0850, 0x7C50 }, //mn-Mong-CN -> mn-Mong
	{ 0x085D, 0x7C5D }, //iu-Latn-CA -> iu-Latn
	{ 0x085F, 0x7C5F }, //tzm-Latn-DZ -> tzm-Latn
	{ 0x09FF, 0x0001 }, //qps-plocm -> ar 
	{ 0x0C04, 0x7C04 }, //zh-HK -> zh-Hant 
	{ 0x0C1A, 0x6C1A }, //sr-Cyrl-CS -> sr-Cyrl
	{ 0x103B, 0x7C3B }, //smj-NO -> smj 
	{ 0x1404, 0x7C04 }, //zh-MO -> zh-Hant 
	{ 0x141A, 0x681A }, //bs-Latn-BA -> bs-Latn
	{ 0x143B, 0x7C3B }, //smj-SE -> smj 
	{ 0x181A, 0x701A }, //sr-Latn-BA -> sr-Latn
	{ 0x183B, 0x783B }, //sma-NO -> sma 
	{ 0x1C1A, 0x6C1A }, //sr-Cyrl-BA -> sr-Cyrl
	{ 0x1C3B, 0x783B }, //sma-SE -> sma 
	{ 0x201A, 0x641A }, //bs-Cyrl-BA -> bs-Cyrl
	{ 0x203B, 0x743B }, //sms-FI -> sms 
	{ 0x241A, 0x701A }, //sr-Latn-RS -> sr-Latn
	{ 0x243B, 0x703B }, //smn-FI -> smn 
	{ 0x281A, 0x6C1A }, //sr-Cyrl-RS -> sr-Cyrl
	{ 0x2C1A, 0x701A }, //sr-Latn-ME -> sr-Latn
	{ 0x301A, 0x6C1A }, //sr-Cyrl-ME -> sr-Cyrl
	{ 0x641A, 0x781A }, //bs-Cyrl -> bs
	{ 0x681A, 0x781A }, //bs-Latn -> bs
	{ 0x6C1A, 0x7C1A }, //sr-Cyrl -> sr
	{ 0x701A, 0x7C1A }, //sr-Latn -> sr
	{ 0x7C04, 0x7804 }, //zh-Hant -> zh
};

constexpr LANGID GetLangIDFallback(LANGID lang)
{
	auto it = std::lower_bound(
		std::begin(LangIDFallbackTable),
		std::end(LangIDFallbackTable),
		lang,
		[](std::pair<LANGID, LANGID> a, LANGID b) { return a.first < b; }
	);
	if (it == std::end(LangIDFallbackTable) || it->first != lang)
		return PRIMARYLANGID(lang);
	return it->second;
}

inline LANGID LangIDFromLocaleName(std::wstring_view locale)
{
	for (;;)
	{
		LCID lcid = LocaleNameToLCID(std::wstring(locale).c_str(), LOCALE_ALLOW_NEUTRAL_NAMES);
		if (lcid != LOCALE_CUSTOM_UNSPECIFIED)
			return LANGIDFROMLCID(lcid);
		size_t pos = locale.rfind('-');
		if (pos == locale.npos)
			return 0;
		locale.remove_suffix(locale.size() - pos);
	}
}

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