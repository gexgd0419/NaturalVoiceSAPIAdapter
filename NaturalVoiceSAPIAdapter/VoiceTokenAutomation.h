#pragma once
#include "pch.h"

using namespace ATL;

// Implements IDispatch and ISpeechObjectToken - the automation interface of ISpObjectToken
// so that it is accessible to languages such as Visual Basic and scripting languages

class CVoiceTokenAutomation :
	public CComObjectRootEx<CComMultiThreadModel>,
	public IDispatchImpl<ISpeechObjectToken, &IID_ISpeechObjectToken, &LIBID_SpeechLib, /*wMajor =*/ 0xFFFF, /*wMinor =*/ 0xFFFF>,
	public IDataKeyAutomationInit
{
public:
	DECLARE_AGGREGATABLE(CVoiceTokenAutomation)
	BEGIN_COM_MAP(CVoiceTokenAutomation)
		COM_INTERFACE_ENTRY(ISpeechObjectToken)
		COM_INTERFACE_ENTRY(IDispatch)
		COM_INTERFACE_ENTRY(IDataKeyAutomationInit)
	END_COM_MAP()
	DECLARE_PROTECT_FINAL_CONSTRUCT()
private:
	CComPtr<ISpObjectToken> m_token;
public:
	HRESULT FinalConstruct()
	{
		return S_OK;
	}

	void SetParent(IUnknown* pOuterUnknown)
	{
		pOuterUnknown->QueryInterface(&m_token);
	}

	STDMETHODIMP get_Id(__RPC__deref_out_opt BSTR* ObjectId)
	{
		if (!ObjectId)
			return E_POINTER;
		CSpDynamicString id;
		RETONFAIL(m_token->GetId(&id));
		return id.CopyToBSTR(ObjectId);
	}

	STDMETHODIMP get_DataKey(__RPC__deref_out_opt ISpeechDataKey** DataKey)
	{
		if (!DataKey)
			return E_POINTER;
		// Create a non-aggregated instance of CDataKeyAutomation
		RETONFAIL(CDataKeyAutomation::_CreatorClass::CreateInstance(nullptr, IID_ISpeechDataKey, reinterpret_cast<LPVOID*>(DataKey)));
		CComQIPtr<IDataKeyAutomationInit>(*DataKey)->SetParent(m_token);
		return S_OK;
	}

	STDMETHODIMP get_Category(__RPC__deref_out_opt ISpeechObjectTokenCategory** Category)
	{
		CComPtr<ISpObjectTokenCategory> pCat;
		RETONFAIL(m_token->GetCategory(&pCat));
		return pCat->QueryInterface(Category);
	}

	STDMETHODIMP GetDescription(long Locale, __RPC__deref_out_opt BSTR* Description)
	{
		if (!Description)
			return E_POINTER;
		CSpDynamicString desc;
		RETONFAIL(SpGetDescription(m_token, &desc, Locale ? static_cast<LANGID>(Locale) : GetUserDefaultUILanguage()));
		return desc.CopyToBSTR(Description);
	}

	STDMETHODIMP SetId(__RPC__in BSTR Id, __RPC__in BSTR CategoryID = (BSTR)L"", VARIANT_BOOL CreateIfNotExist = 0)
	{
		return SPERR_ALREADY_INITIALIZED;
	}

	STDMETHODIMP GetAttribute(__RPC__in BSTR AttributeName, __RPC__deref_out_opt BSTR* AttributeValue)
	{
		if (!AttributeValue)
			return E_POINTER;
		CComPtr<ISpDataKey> pKey;
		RETONFAIL(m_token->OpenKey(SPTOKENKEY_ATTRIBUTES, &pKey));
		CSpDynamicString strValue;
		RETONFAIL(pKey->GetStringValue(AttributeName, &strValue));
		return strValue.CopyToBSTR(AttributeValue);
	}

	STDMETHODIMP CreateInstance(__RPC__in_opt IUnknown* pUnkOuter, SpeechTokenContext ClsContext, __RPC__deref_out_opt IUnknown** Object)
	{
		return m_token->CreateInstance(pUnkOuter, ClsContext, IID_IUnknown, reinterpret_cast<LPVOID*>(Object));
	}

	STDMETHODIMP Remove(__RPC__in BSTR ObjectStorageCLSID)
	{
		if (!ObjectStorageCLSID || !*ObjectStorageCLSID)
			return m_token->Remove(nullptr);
		CLSID clsid;
		RETONFAIL(CLSIDFromString(ObjectStorageCLSID, &clsid));
		return m_token->Remove(&clsid);
	}

	STDMETHODIMP GetStorageFileName(__RPC__in BSTR ObjectStorageCLSID, __RPC__in BSTR KeyName, __RPC__in BSTR FileName,
		SpeechTokenShellFolder Folder, __RPC__deref_out_opt BSTR* FilePath)
	{
		return E_NOTIMPL;
	}

	STDMETHODIMP RemoveStorageFileName(__RPC__in BSTR ObjectStorageCLSID, __RPC__in BSTR KeyName, VARIANT_BOOL DeleteFile)
	{
		return E_NOTIMPL;
	}

	STDMETHODIMP IsUISupported(__RPC__in const BSTR TypeOfUI, __RPC__in const VARIANT* ExtraData, __RPC__in_opt IUnknown* Object,
		__RPC__out VARIANT_BOOL* Supported)
	{
		if (!Supported)
			return E_POINTER;
		*Supported = VARIANT_FALSE;
		return S_OK;
	}

	STDMETHODIMP DisplayUI(long hWnd, __RPC__in BSTR Title, __RPC__in const BSTR TypeOfUI, __RPC__in const VARIANT* ExtraData = 0,
		__RPC__in_opt IUnknown* Object = 0)
	{
		return E_NOTIMPL;
	}

	STDMETHODIMP MatchesAttributes(__RPC__in BSTR Attributes, __RPC__out VARIANT_BOOL* Matches)
	{
		if (!Matches)
			return E_POINTER;
		BOOL match = FALSE;
		RETONFAIL(m_token->MatchesAttributes(Attributes, &match));
		*Matches = match ? VARIANT_TRUE : VARIANT_FALSE;
		return S_OK;
	}
};