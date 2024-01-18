#pragma once
#include "pch.h"
#include <atlsafe.h>

using namespace ATL;

MIDL_INTERFACE("495582EE-0AB5-4C2D-8A91-B31B375E4AE1")
IDataKeyAutomationInit : public IUnknown
{
	virtual void SetParent(IUnknown * pOuterUnknown) = 0;
};

// Implements IDispatch and ISpeechDataKey - the automation interface of ISpDataKey
// so that it is accessible to languages such as Visual Basic and scripting languages

class CDataKeyAutomation :
	public CComObjectRootEx<CComMultiThreadModel>,
	public IDispatchImpl<ISpeechDataKey, &IID_ISpeechDataKey, &LIBID_SpeechLib, /*wMajor =*/ 0xFFFF, /*wMinor =*/ 0xFFFF>,
	public IDataKeyAutomationInit
{
public:
	DECLARE_AGGREGATABLE(CDataKeyAutomation)
	BEGIN_COM_MAP(CDataKeyAutomation)
		COM_INTERFACE_ENTRY(ISpeechDataKey)
		COM_INTERFACE_ENTRY(IDispatch)
		COM_INTERFACE_ENTRY(IDataKeyAutomationInit)
	END_COM_MAP()
	DECLARE_PROTECT_FINAL_CONSTRUCT()
private:
	CComPtr<ISpDataKey> m_dataKey;
public:
	void SetParent(IUnknown* pOuterUnknown) override
	{
		pOuterUnknown->QueryInterface(&m_dataKey);
	}

	STDMETHODIMP SetBinaryValue(__RPC__in const BSTR ValueName, VARIANT Value) noexcept override
	{
		int cbElem;
		switch (Value.vt & VT_TYPEMASK)
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
		default:
			return E_INVALIDARG;
		}
		if (Value.vt & VT_ARRAY)
		{
			if (Value.parray->cDims == 0)
				return E_INVALIDARG;
			ULONG cElem = Value.parray->rgsabound[0].cElements;
			for (USHORT i = 1; i < Value.parray->cDims; i++)
				cElem *= Value.parray->rgsabound[i].cElements;
			if (cElem == 0)
				return E_INVALIDARG;
			void* pArrayData;
			RETONFAIL(SafeArrayAccessData(Value.parray, &pArrayData));
			HRESULT hr = m_dataKey->SetData(ValueName, cbElem * cElem, reinterpret_cast<const BYTE*>(pArrayData));
			SafeArrayUnaccessData(Value.parray);
			return hr;
		}
		else
		{
			return m_dataKey->SetData(ValueName, cbElem, &Value.bVal);
		}
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