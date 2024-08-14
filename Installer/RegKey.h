#pragma once
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <string>
#include <vector>
#include <system_error>

class RegKey
{
	HKEY m_hKey;

public:
	LSTATUS Create(HKEY hKey, LPCWSTR pSubKey, REGSAM samDesired) noexcept
	{
		Close();
		return RegCreateKeyExW(hKey, pSubKey, 0, nullptr, 0, samDesired, nullptr, &m_hKey, nullptr);
	}
	LSTATUS Open(HKEY hKey, LPCWSTR pSubKey, REGSAM samDesired) noexcept
	{
		Close();
		return RegOpenKeyExW(hKey, pSubKey, 0, samDesired, &m_hKey);
	}
	void Close() noexcept
	{
		if (m_hKey)
		{
			RegCloseKey(m_hKey);
			m_hKey = nullptr;
		}
	}
	RegKey() noexcept : m_hKey(nullptr) {}
	RegKey(const RegKey&) = delete;
	RegKey(RegKey&& other) noexcept
	{
		m_hKey = other.m_hKey;
		other.m_hKey = nullptr;
	}
	~RegKey() noexcept
	{
		Close();
	}
	RegKey& operator=(const RegKey&) = delete;
	RegKey& operator=(RegKey&& other) noexcept
	{
		Close();
		m_hKey = other.m_hKey;
		other.m_hKey = nullptr;
		return *this;
	}
	explicit operator bool() const { return m_hKey != nullptr; }

public:
	static RegKey OpenForRead(HKEY hKey, LPCWSTR pSubKey, REGSAM samDesired = KEY_QUERY_VALUE) noexcept
	{
		RegKey key;
		key.Open(hKey, pSubKey, samDesired);
		return key;
	}

public:
	DWORD GetDword(LPCWSTR pValueName, DWORD defaultValue = 0) const noexcept
	{
		if (!m_hKey)
			return defaultValue;
		DWORD result = 0;
		DWORD cb = sizeof result;
		if (RegQueryValueExW(m_hKey, pValueName, nullptr, nullptr, reinterpret_cast<LPBYTE>(&result), &cb)
			!= ERROR_SUCCESS)
			return defaultValue;
		return result;
	}
	std::wstring GetString(LPCWSTR pValueName, std::wstring_view defaultValue = {}) const
	{
		if (!m_hKey)
			return std::wstring(defaultValue);
		DWORD cb = 0;
		if (RegQueryValueExW(m_hKey, pValueName, nullptr, nullptr, nullptr, &cb)
			!= ERROR_SUCCESS)
			return std::wstring(defaultValue);
		std::wstring result(cb / sizeof(WCHAR), L'\0');
		if (RegQueryValueExW(m_hKey, pValueName, nullptr, nullptr, reinterpret_cast<LPBYTE>(result.data()), &cb)
			!= ERROR_SUCCESS)
			return std::wstring(defaultValue);
		if (result.back() == L'\0')
			result.pop_back();
		return result;
	}
	std::vector<std::wstring> GetMultiStringList(LPCWSTR pValueName) const
	{
		if (!m_hKey)
			return {};
		DWORD cb = 0;
		if (RegQueryValueExW(m_hKey, pValueName, 0, nullptr, nullptr, &cb) != ERROR_SUCCESS)
			return {};
		std::unique_ptr<WCHAR[]> pMultiSZ(new WCHAR[cb / sizeof(WCHAR) + 2]);
		if (RegQueryValueExW(m_hKey, pValueName, 0, nullptr, reinterpret_cast<LPBYTE>(pMultiSZ.get()), &cb)
			!= ERROR_SUCCESS)
			return {};
		std::vector<std::wstring> result;
		for (LPCWSTR pItem = pMultiSZ.get(); *pItem != L'\0'; pItem += result.back().size() + 1)
			result.emplace_back(pItem);
		return result;
	}
	LSTATUS SetDword(LPCWSTR pValueName, DWORD value)
	{
		return RegSetValueExW(m_hKey, pValueName, 0, REG_DWORD,
			reinterpret_cast<const BYTE*>(&value),
			sizeof value);
	}
	LSTATUS SetString(LPCWSTR pValueName, LPCWSTR pString, DWORD regType = REG_SZ)
	{
		return RegSetValueExW(m_hKey, pValueName, 0, regType,
			reinterpret_cast<const BYTE*>(pString),
			static_cast<DWORD>((wcslen(pString) + 1) * sizeof(WCHAR)));
	}
	LSTATUS SetString(LPCWSTR pValueName, const std::wstring& string, DWORD regType = REG_SZ)
	{
		return RegSetValueExW(m_hKey, pValueName, 0, regType,
			reinterpret_cast<const BYTE*>(string.data()),
			static_cast<DWORD>((string.size() + 1) * sizeof(WCHAR)));
	}
	template <class List>
	LSTATUS SetMultiStringList(LPCWSTR pValueName, const List& list)
	{
		std::wstring result;
		for (auto& item : list)
		{
			result.append(item);
			result.push_back(L'\0');
		}
		return SetString(pValueName, result, REG_MULTI_SZ);
	}
};

inline RegKey RegOpenConfigKey() noexcept
{
	return RegKey::OpenForRead(HKEY_CURRENT_USER, L"Software\\NaturalVoiceSAPIAdapter");
}

inline RegKey RegOpenEnumeratorConfigKey() noexcept
{
	return RegKey::OpenForRead(HKEY_CURRENT_USER, L"Software\\NaturalVoiceSAPIAdapter\\Enumerator");
}