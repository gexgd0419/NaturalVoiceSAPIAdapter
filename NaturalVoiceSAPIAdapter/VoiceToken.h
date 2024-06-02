#pragma once
#include "ObjectToken.h"
#include "TTSEngine.h"

struct VoiceTraits
{
	static constexpr HKEY RegRoot = HKEY_CURRENT_USER;
	static constexpr std::wstring_view RegPrefix = L"Software\\NaturalVoiceSAPIAdapter\\VoiceTokens\\";
	static HRESULT GetStringValueOverride(LPCWSTR pszSubkey, LPCWSTR pszValueName, LPWSTR* ppszValue) noexcept
	{
		if (*pszSubkey == '\0' && _wcsicmp(pszValueName, L"CLSID") == 0)
			return StringFromCLSID(CLSID_TTSEngine, ppszValue);
		return SPERR_NOT_FOUND;
	}
	static constexpr LPCWSTR SpCategory = SPCAT_VOICES;
	static constexpr std::wstring_view SpIdRoot = SPCAT_VOICES L"\\TokenEnums\\NaturalVoiceEnumerator\\";
	using ComClass = CTTSEngine;
};

typedef CDataKey<ISpDataKey, VoiceTraits> CVoiceKey;
typedef CObjectToken<VoiceTraits> CVoiceToken;