#pragma once
#include "resource.h"       // 主符号

#include "DataKeyAutomation.h"
#include "StrUtils.h"


using namespace ATL;


// Data storage type for the root CObjectToken and each sub level of CDataKey.
// Each CObjectToken has its own DataKeyData tree,
// and its every existing CDataKey instances will reference the data tree using shared_ptr
// to prevent the data tree from being destroyed.
struct DataKeyData
{
	std::wstring path;
	std::vector<std::pair<std::wstring, std::wstring>> values;
	std::vector<std::pair<std::wstring, DataKeyData>> subkeys;
};

// Private, non COM-compliant interface for initializing CDataKey instances
// Note that these methods MAY throw exceptions
MIDL_INTERFACE("4B88C5F0-B73A-41D9-9439-E229AB8A7C6D")
IDataKeyInit : public IUnknown
{
	virtual HRESULT InitKey(const std::shared_ptr<DataKeyData>& pData) noexcept = 0;
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
	COM_INTERFACE_ENTRY_AUTOAGGREGATE(IID_IDispatch, m_pAutomation, CLSID_DataKeyAutomation)
	COM_INTERFACE_ENTRY_AUTOAGGREGATE(IID_ISpeechDataKey, m_pAutomation, CLSID_DataKeyAutomation)
END_COM_MAP()



	DECLARE_PROTECT_FINAL_CONSTRUCT()

protected:
	CComPtr<ISpDataKey> m_pKey;
	CComPtr<IUnknown> m_pAutomation;

	// Keeps a reference to the main data tree, but aliased to point to this key's specific node
	std::shared_ptr<DataKeyData> m_pData;

	HRESULT InitKey(const std::shared_ptr<DataKeyData>& pData) noexcept override
	{
		m_pData = pData;
		m_pKey.Release();
		HKEY hKey = nullptr;
		CSpDynamicString keyPath;
		RETONFAIL(MergeIntoCoString(keyPath, Traits::RegPrefix, m_pData->path));
		if (RegOpenKeyExW(Traits::RegRoot, keyPath,
			0, KEY_READ | KEY_WRITE, &hKey) == ERROR_SUCCESS)
		{
			CComPtr<ISpRegDataKey> pKey;
			if (SUCCEEDED(pKey.CoCreateInstance(CLSID_SpDataKey))
				&& SUCCEEDED(pKey->SetKey(hKey, FALSE)))
			{
				return pKey->QueryInterface(&m_pKey);
			}
			else
			{
				RegCloseKey(hKey);
			}
		}
		return S_OK;
	}

	HRESULT EnsureKey() noexcept
	{
		if (m_pKey) return S_OK;
		CComPtr<ISpRegDataKey> pKey;
		RETONFAIL(pKey.CoCreateInstance(CLSID_SpDataKey));
		HKEY hKey = nullptr;
		CSpDynamicString keyPath;
		RETONFAIL(MergeIntoCoString(keyPath, Traits::RegPrefix, m_pData->path));
		LSTATUS stat = RegCreateKeyExW(Traits::RegRoot, keyPath,
			0, nullptr, 0, KEY_READ | KEY_WRITE, nullptr, &hKey, nullptr);
		if (stat != ERROR_SUCCESS) return HRESULT_FROM_WIN32(stat);
		RETONFAIL(pKey->SetKey(hKey, FALSE));
		return pKey->QueryInterface(&m_pKey);
	}

	DataKeyData* FindSubkey(LPCWSTR pszSubKeyName) noexcept
	{
		for (auto& pair : m_pData->subkeys)
		{
			if (_wcsicmp(pair.first.c_str(), pszSubKeyName) == 0)
				return &pair.second;
		}
		return nullptr;
	}

	LPCWSTR FindValue(LPCWSTR pszValueName) noexcept
	{
		if (!pszValueName)
			pszValueName = L"";
		for (const auto& pair : m_pData->values)
		{
			if (_wcsicmp(pair.first.c_str(), pszValueName) == 0)
				return pair.second.c_str();
		}
		return nullptr;
	}

public:
	// ISpDataKey implementation

	STDMETHODIMP SetData(LPCWSTR pszValueName, ULONG cbData, const BYTE* pData) noexcept override
	{
		RETONFAIL(EnsureKey());
		return m_pKey->SetData(pszValueName, cbData, pData);
	}

	STDMETHODIMP GetData(LPCWSTR pszValueName, ULONG* pcbData, BYTE* pData) noexcept override
	{
		if (m_pKey)
			return m_pKey->GetData(pszValueName, pcbData, pData);
		return SPERR_NOT_FOUND;
	}

	STDMETHODIMP SetStringValue(_In_opt_ LPCWSTR pszValueName, LPCWSTR pszValue) noexcept override
	{
		RETONFAIL(EnsureKey());
		return m_pKey->SetStringValue(pszValueName, pszValue);
	}

	STDMETHODIMP GetStringValue(_In_opt_ LPCWSTR pszValueName, _Outptr_ LPWSTR* ppszValue) noexcept override
	{
		if (!ppszValue)
			return E_POINTER;

		// some attributes are not overridable
		HRESULT hr = Traits::GetStringValueOverride(m_pData->path.c_str(), pszValueName, ppszValue);
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

	STDMETHODIMP SetDWORD(LPCWSTR pszValueName, DWORD dwValue) noexcept override
	{
		RETONFAIL(EnsureKey());
		return m_pKey->SetDWORD(pszValueName, dwValue);
	}

	STDMETHODIMP GetDWORD(LPCWSTR pszValueName, DWORD* pdwValue) noexcept override
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

	STDMETHODIMP OpenKey(LPCWSTR pszSubKeyName, _Outptr_ ISpDataKey** ppSubKey) noexcept override
	{
		if (!pszSubKeyName || !ppSubKey)
			return E_POINTER;
		if (DataKeyData* pKeyData = FindSubkey(pszSubKeyName))
		{
			// Pass a shared_ptr that references the same data tree, but aliased to point to the key's specific data
			return Create(std::shared_ptr<DataKeyData>(m_pData, pKeyData), ppSubKey);
		}
		if (m_pKey)
			return m_pKey->OpenKey(pszSubKeyName, ppSubKey);
		return SPERR_NOT_FOUND;
	}

	STDMETHODIMP CreateKey(LPCWSTR pszSubKey, _Outptr_ ISpDataKey** ppSubKey) noexcept override
	{
		HRESULT hr = OpenKey(pszSubKey, ppSubKey);
		if (hr == SPERR_NOT_FOUND)
		{
			RETONFAIL(EnsureKey());
			return m_pKey->CreateKey(pszSubKey, ppSubKey);
		}
		return hr;
	}

	STDMETHODIMP DeleteKey(LPCWSTR pszSubKey) noexcept override
	{
		if (!pszSubKey)
			return E_POINTER;
		if (FindSubkey(pszSubKey))
			return E_ACCESSDENIED;
		if (m_pKey)
			return m_pKey->DeleteKey(pszSubKey);
		return SPERR_NOT_FOUND;
	}

	STDMETHODIMP DeleteValue(LPCWSTR pszValueName) noexcept override
	{
		if (!pszValueName)
			return E_POINTER;
		if (FindValue(pszValueName))
			return E_ACCESSDENIED;
		if (m_pKey)
			return m_pKey->DeleteValue(pszValueName);
		return SPERR_NOT_FOUND;
	}

	STDMETHODIMP EnumKeys(ULONG Index, _Outptr_ LPWSTR* ppszSubKeyName) noexcept override
	{
		return EnumItems<&ISpObjectToken::EnumKeys>(m_pData->subkeys, Index, ppszSubKeyName);
	}

	STDMETHODIMP EnumValues(ULONG Index, _Outptr_ LPWSTR* ppszValueName) noexcept override
	{
		return EnumItems<&ISpObjectToken::EnumValues>(m_pData->values, Index, ppszValueName);
	}

private:
	template <HRESULT (__stdcall ISpDataKey::*EnumFunc)(ULONG, LPWSTR*), class T>
	HRESULT EnumItems(
		const std::vector<std::pair<std::wstring, T>>& items,
		ULONG Index, _Outptr_ LPWSTR* ppszItemName) noexcept
	{
		// Enumerated items include both virtual items and actual registry items
		// Virtual items come first, indexes greater are for actual items
		// Actual item that share the same name with a virtual item will be merged into the virtual item

		if (!ppszItemName)
			return E_POINTER;

		// Enumerate virtual items first
		if (Index < items.size())
		{
			CSpDynamicString name(items[Index].first.c_str());
			if (!name)
				return E_OUTOFMEMORY;
			*ppszItemName = name.Detach();
			return S_OK;
		}
		else if (m_pKey)
		{
			// Out of virtual item range, calculate the index for actual registry item
			Index -= static_cast<ULONG>(items.size());
			ULONG idx = 0;
			// We should advance to next item 'Index' times
			for (ULONG i = 0; i <= Index; i++)
			{
				for (;;)
				{
					CSpDynamicString itemName;
					RETONFAIL((m_pKey->*EnumFunc)(idx, &itemName));
					// If this item has the same name as one of the virtual items, skip it (idx++)
					if (!std::any_of(items.begin(), items.end(),
						[itemName](const auto& item) { return _wcsicmp(item.first.c_str(), itemName) == 0; }))
						break;
					idx++;
				}
				idx++;
			}
			return (m_pKey->*EnumFunc)(idx - 1, ppszItemName);
		}
		return SPERR_NO_MORE_ITEMS;
	}
public:
	static HRESULT Create(const std::shared_ptr<DataKeyData>& pData, ISpDataKey** ppOut) noexcept
	{
		// Create a new ISpDataKey that points to the correponding data
		CComPtr<ISpDataKey> pKey;

		// Force to use ISpDataKey as the base class,
		// because CObjectToken inherits from CDataKey<ISpObjectToken, Traits>.
		// Use a pair of parentheses around the function name, to avoid the preprocessor
		// breaking at the template argument list comma ("CDataKey<ISpDataKey" and "Traits>::_CreatorClass...")
		RETONFAIL((CDataKey<ISpDataKey, Traits>::_CreatorClass::CreateInstance)
			(nullptr, IID_ISpDataKey, reinterpret_cast<LPVOID*>(&pKey)));

		CComPtr<IDataKeyInit> pInit;
		RETONFAIL(pKey->QueryInterface(&pInit));
		// Pass a shared_ptr that references the same data tree, but aliased to point to the key's specific data
		RETONFAIL(pInit->InitKey(pData));
		*ppOut = pKey.Detach();
		return S_OK;
	}
};

