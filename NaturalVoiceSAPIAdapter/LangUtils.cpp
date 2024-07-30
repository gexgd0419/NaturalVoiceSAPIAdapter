#include "pch.h"
#include "LangUtils.h"
#include <map>

static std::map<std::wstring, LANGID> s_LangIDMap;

typedef decltype(LocaleNameToLCID)* LocaleNameToLCID_t;

// If LocaleNameToLCID is supported, return the function pointer
// If not, enumerate all locales in system and build a name-LCID map
static LocaleNameToLCID_t InitLocaleNameToLCID()
{
	auto pfn = reinterpret_cast<LocaleNameToLCID_t>
		(GetProcAddress(GetModuleHandleW(L"kernel32"), "LocaleNameToLCID"));
	if (pfn)
		return pfn;

	// Build a name-LCID map when LocaleNameToLCID is not supported
	EnumSystemLocalesW([](LPWSTR lcidHexString) -> BOOL
		{
			LCID lcid = HexLangToLangID(lcidHexString);
			WCHAR lang[9], ctry[9];
			GetLocaleInfoW(lcid, LOCALE_SISO639LANGNAME, lang, 9);   // language e.g. en
			GetLocaleInfoW(lcid, LOCALE_SISO3166CTRYNAME, ctry, 9);  // country  e.g. US
			_wcslwr_s(ctry);  // make map keys lower case
			s_LangIDMap.try_emplace(lang, PRIMARYLANGID(lcid));  // neutral version
			s_LangIDMap.try_emplace(std::wstring(lang) + L'-' + ctry, LANGIDFROMLCID(lcid));
			return TRUE;
		}, LCID_SUPPORTED);

	return nullptr;
}

static const auto s_pfnLocaleNameToLCID = InitLocaleNameToLCID();
static const auto s_pfnGetLocaleInfoEx = reinterpret_cast<decltype(GetLocaleInfoEx)*>
	(GetProcAddress(GetModuleHandleW(L"kernel32"), "GetLocaleInfoEx"));

// Gets all levels of ancestor language IDs for fallbacks
std::vector<LANGID> GetLangIDFallbacks(LANGID lang)
{
	std::vector<LANGID> fallbacks;

	if (s_pfnLocaleNameToLCID)
	{
		WCHAR parentLocale[85];  // parent locale name
		if (!GetLocaleInfoW(MAKELCID(lang, SORT_DEFAULT), LOCALE_SPARENT, parentLocale, 85))
			return {};

		do
		{
			LCID lcid = s_pfnLocaleNameToLCID(parentLocale, LOCALE_ALLOW_NEUTRAL_NAMES);
			if (lcid != LOCALE_CUSTOM_UNSPECIFIED && lcid != 0)
				fallbacks.push_back(LANGIDFROMLCID(lcid));
		} while (
			s_pfnGetLocaleInfoEx(parentLocale, LOCALE_SPARENT, parentLocale, 85)
			&& parentLocale[0] != L'\0'    // no more parent locales if empty string
			);
	}
	else
	{
		// fallback when LocaleNameToLCID is not supported
		if (SUBLANGID(lang) != SUBLANG_NEUTRAL)
			fallbacks.push_back(MAKELANGID(PRIMARYLANGID(lang), SUBLANG_NEUTRAL));
	}

	return fallbacks;
}

// Returns LOCALE_CUSTOM_UNSPECIFIED if the locale is unknown
LANGID LangIDFromLocaleName(LPCWSTR locale)
{
	if (s_pfnLocaleNameToLCID)
	{
		// if LocaleNameToLCID is supported, use it

		LCID lcid = s_pfnLocaleNameToLCID(locale, LOCALE_ALLOW_NEUTRAL_NAMES);
		if (lcid != LOCALE_CUSTOM_UNSPECIFIED && lcid != 0)
			return LANGIDFROMLCID(lcid);

		// if locale not found, remove locale specifiers level by level
		//     e.g. zh-CN-liaoning > zh-CN > zh
		std::wstring loc(locale);
		for (;;)
		{
			auto pos = loc.rfind(L'-');
			if (pos == loc.npos)
				break;
			loc.erase(pos);

			lcid = s_pfnLocaleNameToLCID(loc.c_str(), LOCALE_ALLOW_NEUTRAL_NAMES);
			if (lcid != LOCALE_CUSTOM_UNSPECIFIED && lcid != 0)
				return LANGIDFROMLCID(lcid);
		}

		return LOCALE_CUSTOM_UNSPECIFIED;
	}
	else
	{
		// fallback to use the lookup table when LocaleNameToLCID is not supported

		std::wstring loc(locale);
		_wcslwr_s(loc.data(), loc.size() + 1); // map keys are lower case

		for (;;)
		{
			auto it = s_LangIDMap.find(loc);
			if (it != s_LangIDMap.end())
				return it->second;

			// if locale not found, remove locale specifiers level by level
			//     e.g. zh-CN-liaoning > zh-CN > zh
			auto pos = loc.rfind(L'-');
			if (pos == loc.npos)
				break;
			loc.erase(pos);
		}

		return LOCALE_CUSTOM_UNSPECIFIED;
	}
}
