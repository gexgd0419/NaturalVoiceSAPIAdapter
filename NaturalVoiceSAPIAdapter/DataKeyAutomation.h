#pragma once
#include "pch.h"
#include <atlsafe.h>
#include "NaturalVoiceSAPIAdapter_i.h"

using namespace ATL;

template <typename Func>
	requires std::is_invocable_r_v<HRESULT, Func, LPCVOID, ULONG>  // HRESULT(LPCVOID data, ULONG size)
HRESULT GetVariantData(const VARIANT* pvar, Func&& dataRecvFunc)
{
	if (!pvar)
		return dataRecvFunc(nullptr, 0);

	ULONG cbElem;
	switch (pvar->vt & VT_TYPEMASK)
	{
	case VT_I1:
	case VT_UI1:
		cbElem = 1; break;
	case VT_I2:
	case VT_UI2:
		cbElem = 2; break;
	case VT_I4:
	case VT_UI4:
		cbElem = 4; break;
	case VT_I8:
	case VT_UI8:
		cbElem = 8; break;
	default: // other types are not accepted
		return E_INVALIDARG;
	}
	VARTYPE vtFlags = pvar->vt & ~VT_TYPEMASK;
	if (vtFlags == VT_ARRAY)
	{
		if (pvar->parray->cDims == 0)
			return E_INVALIDARG;
		ULONG cElems = pvar->parray->rgsabound[0].cElements;
		for (USHORT i = 1; i < pvar->parray->cDims; i++)
			cElems *= pvar->parray->rgsabound[i].cElements;
		if (cElems == 0)
			return E_INVALIDARG;
		void* pArrayData;
		RETONFAIL(SafeArrayAccessData(pvar->parray, &pArrayData));
		HRESULT hr = dataRecvFunc(static_cast<LPCVOID>(pArrayData), cbElem * cElems);
		SafeArrayUnaccessData(pvar->parray);
		return hr;
	}
	else if (vtFlags == 0) // only a single element
	{
		return dataRecvFunc(static_cast<LPCVOID>(&pvar->bVal), cbElem);
	}
	else // if other flags such as VT_BYREF exist
	{
		return E_INVALIDARG;
	}
}

// Implements IDispatch and ISpeechDataKey - the automation interface of ISpDataKey
// so that it is accessible to languages such as Visual Basic and scripting languages

inline const CLSID CLSID_DataKeyAutomation = { 0x3ffa3701, 0x95b0, 0x4628, 0x98, 0xec, 0x47, 0x84, 0x91, 0x97, 0x30, 0x9e };

class CDataKeyAutomation :
	public CComObjectRootEx<CComMultiThreadModel>,
	public CComCoClass<CDataKeyAutomation, &CLSID_DataKeyAutomation>,
	public IDispatchImpl<ISpeechDataKey, &IID_ISpeechDataKey, &LIBID_SpeechLib, /*wMajor =*/ 0xFFFF, /*wMinor =*/ 0xFFFF>
{
public:
	DECLARE_REGISTRY_RESOURCEID(IDR_DATAKEYAUTOMATION)
	DECLARE_ONLY_AGGREGATABLE(CDataKeyAutomation)
	BEGIN_COM_MAP(CDataKeyAutomation)
		COM_INTERFACE_ENTRY(ISpeechDataKey)
		COM_INTERFACE_ENTRY(IDispatch)
	END_COM_MAP()
	DECLARE_PROTECT_FINAL_CONSTRUCT()
private:
	CComPtr<ISpDataKey> m_dataKey;
public:
	HRESULT FinalConstruct() noexcept
	{
		return m_pOuterUnknown->QueryInterface(&m_dataKey);
	}

	STDMETHODIMP SetBinaryValue(__RPC__in const BSTR ValueName, VARIANT Value) noexcept override
	{
		return GetVariantData(&Value, [this, ValueName](LPCVOID data, ULONG size)
			{ return m_dataKey->SetData(ValueName, size, static_cast<const BYTE*>(data)); });
	}

	STDMETHODIMP GetBinaryValue(__RPC__in const BSTR ValueName, __RPC__out VARIANT* Value) noexcept override
	{
		if (!Value)
			return E_POINTER;

		ULONG cb = 0;
		RETONFAIL(m_dataKey->GetData(ValueName, &cb, nullptr));
		CSpCoTaskMemPtr<BYTE> pData;
		RETONFAIL(pData.Alloc(cb));
		RETONFAIL(m_dataKey->GetData(ValueName, &cb, pData));

		CComSafeArray<BYTE> array;
		RETONFAIL(array.Create(cb));
		void* pArrayData;
		RETONFAIL(SafeArrayAccessData(array, &pArrayData));
		memcpy(pArrayData, pData, cb);
		SafeArrayUnaccessData(array);

		VariantInit(Value);
		Value->vt = VT_ARRAY | VT_UI1;
		Value->parray = array.Detach();
		return S_OK;
	}

	STDMETHODIMP SetStringValue(__RPC__in const BSTR ValueName, __RPC__in const BSTR Value) noexcept override
	{
		return m_dataKey->SetStringValue(ValueName, Value);
	}

	STDMETHODIMP GetStringValue(__RPC__in const BSTR ValueName, __RPC__deref_out_opt BSTR* Value) noexcept override
	{
		if (!Value) return E_POINTER;
		CSpDynamicString value;
		RETONFAIL(m_dataKey->GetStringValue(ValueName, &value));
		return value.CopyToBSTR(Value);
	}

	STDMETHODIMP SetLongValue(__RPC__in const BSTR ValueName, long Value) noexcept override
	{
		return m_dataKey->SetDWORD(ValueName, Value);
	}

	STDMETHODIMP GetLongValue(__RPC__in const BSTR ValueName, __RPC__out long* Value) noexcept override
	{
		return m_dataKey->GetDWORD(ValueName, reinterpret_cast<DWORD*>(Value));
	}

	STDMETHODIMP OpenKey(__RPC__in const BSTR SubKeyName, __RPC__deref_out_opt ISpeechDataKey** SubKey) noexcept override
	{
		CComPtr<ISpDataKey> subkey;
		RETONFAIL(m_dataKey->OpenKey(SubKeyName, &subkey));
		return subkey->QueryInterface(SubKey);
	}

	STDMETHODIMP CreateKey(__RPC__in const BSTR SubKeyName, __RPC__deref_out_opt ISpeechDataKey** SubKey) noexcept override
	{
		ISpDataKey* subkey = nullptr;
		RETONFAIL(m_dataKey->CreateKey(SubKeyName, &subkey));
		return subkey->QueryInterface(SubKey);
	}

	STDMETHODIMP DeleteKey(__RPC__in const BSTR SubKeyName) noexcept override
	{
		return m_dataKey->DeleteKey(SubKeyName);
	}

	STDMETHODIMP DeleteValue(__RPC__in const BSTR ValueName) noexcept override
	{
		return m_dataKey->DeleteValue(ValueName);
	}

	STDMETHODIMP EnumKeys(long Index, __RPC__deref_out_opt BSTR* SubKeyName) noexcept override
	{
		if (!SubKeyName) return E_POINTER;
		CSpDynamicString name;
		RETONFAIL(m_dataKey->EnumKeys(Index, &name));
		return name.CopyToBSTR(SubKeyName);
	}

	STDMETHODIMP EnumValues(long Index, __RPC__deref_out_opt BSTR* ValueName) noexcept override
	{
		if (!ValueName) return E_POINTER;
		CSpDynamicString name;
		RETONFAIL(m_dataKey->EnumValues(Index, &name));
		return name.CopyToBSTR(ValueName);
	}
};

OBJECT_ENTRY_AUTO(CLSID_DataKeyAutomation, CDataKeyAutomation)
