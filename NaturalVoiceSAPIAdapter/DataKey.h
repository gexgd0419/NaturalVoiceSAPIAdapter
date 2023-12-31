﻿#pragma once
#include "resource.h"       // 主符号

#include "DataKeyAutomation.h"


using namespace ATL;


typedef std::vector<std::pair<LPCWSTR, CComPtr<ISpDataKey>>> SubkeyCollection;
typedef std::vector<std::pair<LPCWSTR, std::wstring>> StringPairCollection;


// Private, non COM-compliant interface for initializing CDataKey instances
// Note that these methods MAY throw exceptions
MIDL_INTERFACE("4B88C5F0-B73A-41D9-9439-E229AB8A7C6D")
IDataKeyInit : public IUnknown
{
	virtual void InitKey(const StringPairCollection& values, const SubkeyCollection& subkeys) = 0;
	virtual void SetPath(LPCWSTR lpszCurrentPath) = 0;
};


// CDataKey: An ISpDataKey implementation that stores virtualized keys in memory, but also supports storing changes in registry

template <class Base, class Traits>
class ATL_NO_VTABLE CDataKey :
	public CComObjectRootEx<CComMultiThreadModel>,
	public Base,
	public IDataKeyInit
{
public:
	CDataKey()
	{
	}

DECLARE_GET_CONTROLLING_UNKNOWN()
DECLARE_NOT_AGGREGATABLE(CDataKey)

BEGIN_COM_MAP(CDataKey)
	COM_INTERFACE_ENTRY(ISpDataKey)
	COM_INTERFACE_ENTRY(IDataKeyInit)
	COM_INTERFACE_ENTRY_AGGREGATE(IID_IDispatch, m_pAutomation)
	COM_INTERFACE_ENTRY_AGGREGATE(IID_ISpeechDataKey, m_pAutomation)
END_COM_MAP()



	DECLARE_PROTECT_FINAL_CONSTRUCT()

	HRESULT FinalConstruct()
	{
		RETONFAIL(CDataKeyAutomation::_CreatorClass::CreateInstance(GetControllingUnknown(), IID_IUnknown,
			reinterpret_cast<LPVOID*>(&m_pAutomation)));
		CComQIPtr<IDataKeyAutomationInit>(m_pAutomation)->SetParent(GetUnknown());
		return S_OK;
	}

	void FinalRelease()
	{
	}

protected:
	CComPtr<ISpDataKey> m_pKey;
	std::wstring m_subkeyPath;
	StringPairCollection m_values;
	SubkeyCollection m_subkeys;
	CComPtr<IUnknown> m_pAutomation;

	void InitKey(const StringPairCollection& values, const SubkeyCollection& subkeys)
	{
		m_values = values;
		m_subkeys = subkeys;
	}

	void SetPath(LPCWSTR lpszCurrentPath)
	{
		m_subkeyPath = lpszCurrentPath;
		m_pKey.Release();
		HKEY hKey = nullptr;
		if (RegOpenKeyExW(Traits::RegRoot, (Traits::RegPrefix + m_subkeyPath).c_str(),
			0, KEY_READ | KEY_WRITE, &hKey) == ERROR_SUCCESS)
		{
			CComPtr<ISpRegDataKey> pKey;
			if (SUCCEEDED(pKey.CoCreateInstance(CLSID_SpDataKey))
				&& SUCCEEDED(pKey->SetKey(hKey, FALSE)))
			{
				pKey->QueryInterface(&m_pKey);
			}
		}
		for (const auto& subkey : m_subkeys)
		{
			CComQIPtr<IDataKeyInit>(subkey.second)->SetPath((std::wstring(lpszCurrentPath) + L'\\' + subkey.first).c_str());
		}
	}

	HRESULT EnsureKey()
	{
		if (m_pKey) return S_OK;
		CComPtr<ISpRegDataKey> pKey;
		RETONFAIL(pKey.CoCreateInstance(CLSID_SpDataKey));
		HKEY hKey = nullptr;
		LSTATUS stat = RegCreateKeyExW(Traits::RegRoot, (Traits::RegPrefix + m_subkeyPath).c_str(),
			0, nullptr, 0, KEY_READ | KEY_WRITE, nullptr, &hKey, nullptr);
		if (stat != ERROR_SUCCESS) return HRESULT_FROM_WIN32(stat);
		RETONFAIL(pKey->SetKey(hKey, FALSE));
		return pKey->QueryInterface(&m_pKey);
	}

	ISpDataKey* FindSubkey(LPCWSTR pszSubKeyName)
	{
		for (const auto& pair : m_subkeys)
		{
			if (_wcsicmp(pair.first, pszSubKeyName) == 0)
				return pair.second;
		}
		return nullptr;
	}

	LPCWSTR FindValue(LPCWSTR pszValueName)
	{
		if (!pszValueName)
			pszValueName = L"";
		for (const auto& pair : m_values)
		{
			if (_wcsicmp(pair.first, pszValueName) == 0)
				return pair.second.c_str();
		}
		return nullptr;
	}

public:
	// ISpDataKey implementation

	STDMETHODIMP SetData(LPCWSTR pszValueName, ULONG cbData, const BYTE* pData)
	{
		RETONFAIL(EnsureKey());
		return m_pKey->SetData(pszValueName, cbData, pData);
	}

	STDMETHODIMP GetData(LPCWSTR pszValueName, ULONG* pcbData, BYTE* pData)
	{
		if (m_pKey)
			return m_pKey->GetData(pszValueName, pcbData, pData);
		return SPERR_NOT_FOUND;
	}

	STDMETHODIMP SetStringValue(_In_opt_ LPCWSTR pszValueName, LPCWSTR pszValue)
	{
		RETONFAIL(EnsureKey());
		return m_pKey->SetStringValue(pszValueName, pszValue);
	}

	STDMETHODIMP GetStringValue(_In_opt_ LPCWSTR pszValueName, _Outptr_ LPWSTR* ppszValue)
	{
		if (!ppszValue)
			return E_POINTER;

		// some attributes are not overridable
		HRESULT hr = Traits::GetStringValueOverride(m_subkeyPath.c_str(), pszValueName, ppszValue);
		if (hr != SPERR_NOT_FOUND)
			return hr;

		// Other values can be overridden by registry values if exist
		if (m_pKey)
		{
			hr = m_pKey->GetStringValue(pszValueName, ppszValue);
			if (SUCCEEDED(hr)) return hr;
		}
		if (LPCWSTR value = FindValue(pszValueName))
		{
			CSpDynamicString str = value;
			if (!str)
				return E_OUTOFMEMORY;
			*ppszValue = str.Detach();
			return S_OK;
		}
		return SPERR_NOT_FOUND;
	}

	STDMETHODIMP SetDWORD(LPCWSTR pszValueName, DWORD dwValue)
	{
		RETONFAIL(EnsureKey());
		return m_pKey->SetDWORD(pszValueName, dwValue);
	}

	STDMETHODIMP GetDWORD(LPCWSTR pszValueName, DWORD* pdwValue)
	{
		if (!pdwValue)
			return E_POINTER;
		if (m_pKey)
		{
			HRESULT hr = m_pKey->GetDWORD(pszValueName, pdwValue);
			if (SUCCEEDED(hr)) return hr;
		}
		if (LPCWSTR value = FindValue(pszValueName))
		{
			*pdwValue = wcstoul(value, nullptr, 10);
			return S_OK;
		}
		return SPERR_NOT_FOUND;
	}

	STDMETHODIMP OpenKey(LPCWSTR pszSubKeyName, _Outptr_ ISpDataKey** ppSubKey)
	{
		if (!pszSubKeyName || !ppSubKey)
			return E_POINTER;
		if (ISpDataKey* key = FindSubkey(pszSubKeyName))
		{
			key->AddRef();
			*ppSubKey = key;
			return S_OK;
		}
		if (m_pKey)
			return m_pKey->OpenKey(pszSubKeyName, ppSubKey);
		return SPERR_NOT_FOUND;
	}

	STDMETHODIMP CreateKey(LPCWSTR pszSubKey, _Outptr_ ISpDataKey** ppSubKey)
	{
		HRESULT hr = OpenKey(pszSubKey, ppSubKey);
		if (hr == SPERR_NOT_FOUND && m_pKey)
			return m_pKey->CreateKey(pszSubKey, ppSubKey);
		return hr;
	}

	STDMETHODIMP DeleteKey(LPCWSTR pszSubKey)
	{
		if (!pszSubKey)
			return E_POINTER;
		if (FindSubkey(pszSubKey))
			return E_ACCESSDENIED;
		if (m_pKey)
			return m_pKey->DeleteKey(pszSubKey);
		return SPERR_NOT_FOUND;
	}

	STDMETHODIMP DeleteValue(LPCWSTR pszValueName)
	{
		if (!pszValueName)
			return E_POINTER;
		if (FindValue(pszValueName))
			return E_ACCESSDENIED;
		if (m_pKey)
			return m_pKey->DeleteValue(pszValueName);
		return SPERR_NOT_FOUND;
	}

	STDMETHODIMP EnumKeys(ULONG Index, _Outptr_ LPWSTR* ppszSubKeyName)
	{
		// Enumerated keys include both virtual keys and actual registry keys
		// Virtual keys come first, indexes greater are for actual keys
		// Actual key that share the same name with a virtual key will be merged into the virtual key

		if (!ppszSubKeyName)
			return E_POINTER;

		// Enumerate virtual keys first
		if (Index < m_subkeys.size())
		{
			CSpDynamicString name(m_subkeys[Index].first);
			if (!name)
				return E_OUTOFMEMORY;
			*ppszSubKeyName = name.Detach();
			return S_OK;
		}
		else if (m_pKey)
		{
			// Out of virtual key range, calculate the index for actual registry key
			Index -= static_cast<ULONG>(m_subkeys.size());
			ULONG idx = 0;
			// We should advance to next item 'Index' times
			for (ULONG i = 0; i <= Index; i++)
			{
				for (;;)
				{
					CSpDynamicString subkeyName;
					RETONFAIL(m_pKey->EnumKeys(idx, &subkeyName));
					// If this key has the same name as one of the virtual keys, skip it (idx++)
					if (!FindSubkey(subkeyName))
						break;
					idx++;
				}
				idx++;
			}
			return m_pKey->EnumKeys(idx - 1, ppszSubKeyName);
		}
		return SPERR_NO_MORE_ITEMS;
	}

	STDMETHODIMP EnumValues(ULONG Index, _Outptr_ LPWSTR* ppszValueName)
	{
		// Enumerated values include both virtual values and actual registry values
		// Virtual values come first, indexes greater are for actual values
		// Actual value that share the same name with a virtual value will be merged into the virtual value

		if (!ppszValueName)
			return E_POINTER;

		// Enumerate virtual values first
		if (Index < m_values.size())
		{
			CSpDynamicString name(m_values[Index].first);
			if (!name)
				return E_OUTOFMEMORY;
			*ppszValueName = name.Detach();
			return S_OK;
		}
		else if (m_pKey)
		{
			// Out of virtual value range, calculate the index for actual registry value
			Index -= static_cast<ULONG>(m_values.size());
			ULONG idx = 0;
			// We should advance to next item 'Index' times
			for (ULONG i = 0; i <= Index; i++)
			{
				for (;;)
				{
					CSpDynamicString valueName;
					RETONFAIL(m_pKey->EnumValues(idx, &valueName));
					// If this value has the same name as one of the virtual values, skip it (idx++)
					if (!FindValue(valueName))
						break;
					idx++;
				}
				idx++;
			}
			return m_pKey->EnumValues(idx - 1, ppszValueName);
		}
		return SPERR_NO_MORE_ITEMS;
	}

};

