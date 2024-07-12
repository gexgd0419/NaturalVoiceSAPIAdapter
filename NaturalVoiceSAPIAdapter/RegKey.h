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
	RegKey(HKEY hKey, LPCWSTR pSubKey, REGSAM samDesired)
		: m_hKey(nullptr)
	{
		LSTATUS stat = Create(hKey, pSubKey, samDesired);
		if (stat != ERROR_SUCCESS)
			throw std::system_error(stat, std::system_category());
	}
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
	}

public:
	DWORD GetDword(LPCWSTR pValueName, DWORD defaultValue = 0) const noexcept
	{
		DWORD result = 0;
		DWORD cb = sizeof result;
		if (RegQueryValueExW(m_hKey, pValueName, nullptr, nullptr, reinterpret_cast<LPBYTE>(&result), &cb)
			!= ERROR_SUCCESS)
			return defaultValue;
		return result;
	}
	std::wstring GetString(LPCWSTR pValueName, std::wstring_view defaultValue = {}) const
	{
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
		DWORD cb = 0;
		if (RegQueryValueExW(m_hKey, pValueName, 0, nullptr, nullptr, &cb) != ERROR_SUCCESS)
			return {};
		auto pMultiSZ = std::make_unique<WCHAR[]>(cb / sizeof(WCHAR) + 2);
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
			(wcslen(pString) + 1) * sizeof(WCHAR));
	}
	LSTATUS SetString(LPCWSTR pValueName, const std::wstring& string, DWORD regType = REG_SZ)
	{
		return RegSetValueExW(m_hKey, pValueName, 0, regType,
			reinterpret_cast<const BYTE*>(string.data()),
			(string.size() + 1) * sizeof(WCHAR));
	}
	template <class List>
	LSTATUS SetMultiStringList(LPCWSTR pValueName, List&& list)
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