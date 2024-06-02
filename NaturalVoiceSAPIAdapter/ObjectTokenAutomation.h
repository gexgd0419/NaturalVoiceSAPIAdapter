#pragma once
#include "pch.h"
#include "DataKeyAutomation.h"

using namespace ATL;

// Implements IDispatch and ISpeechObjectToken - the automation interface of ISpObjectToken
// so that it is accessible to languages such as Visual Basic and scripting languages

inline const CLSID CLSID_ObjectTokenAutomation = { 0x799e1686, 0x86d6, 0x4257, 0xac, 0xd8, 0x04, 0xd0, 0xda, 0x28, 0x91, 0x82 };

class CObjectTokenAutomation :
	public CComObjectRootEx<CComMultiThreadModel>,
	public CComCoClass<CObjectTokenAutomation, &CLSID_ObjectTokenAutomation>,
	public IDispatchImpl<ISpeechObjectToken, &IID_ISpeechObjectToken, &LIBID_SpeechLib, /*wMajor =*/ 0xFFFF, /*wMinor =*/ 0xFFFF>
{
public:
	DECLARE_REGISTRY_RESOURCEID(IDR_OBJECTTOKENAUTOMATION)
	DECLARE_ONLY_AGGREGATABLE(CObjectTokenAutomation)
	BEGIN_COM_MAP(CObjectTokenAutomation)
		COM_INTERFACE_ENTRY(ISpeechObjectToken)
		COM_INTERFACE_ENTRY(IDispatch)
	END_COM_MAP()
	DECLARE_PROTECT_FINAL_CONSTRUCT()
private:
	CComPtr<ISpObjectToken> m_token;
public:
	HRESULT FinalConstruct() noexcept
	{
		return m_pOuterUnknown->QueryInterface(&m_token);
	}

	STDMETHODIMP get_Id(__RPC__deref_out_opt BSTR* ObjectId) noexcept override
	{
		if (!ObjectId)
			return E_POINTER;
		CSpDynamicString id;
		RETONFAIL(m_token->GetId(&id));
		return id.CopyToBSTR(ObjectId);
	}

	STDMETHODIMP get_DataKey(__RPC__deref_out_opt ISpeechDataKey** DataKey) noexcept override
	{
		if (!DataKey)
			return E_POINTER;
		// Create a CDataKeyAutomation that connects to the same parent
		return CDataKeyAutomation::_CreatorClass::CreateInstance(m_token, IID_ISpeechDataKey, reinterpret_cast<LPVOID*>(DataKey));
	}

	STDMETHODIMP get_Category(__RPC__deref_out_opt ISpeechObjectTokenCategory** Category) noexcept override
	{
		CComPtr<ISpObjectTokenCategory> pCat;
		RETONFAIL(m_token->GetCategory(&pCat));
		return pCat->QueryInterface(Category);
	}

	STDMETHODIMP GetDescription(long Locale, __RPC__deref_out_opt BSTR* Description) noexcept override
	{
		if (!Description)
			return E_POINTER;
		CSpDynamicString desc;
		RETONFAIL(SpGetDescription(m_token, &desc, Locale ? static_cast<LANGID>(Locale) : GetUserDefaultUILanguage()));
		return desc.CopyToBSTR(Description);
	}

	STDMETHODIMP SetId(__RPC__in BSTR Id, __RPC__in BSTR CategoryID = (BSTR)L"",
		VARIANT_BOOL CreateIfNotExist = 0) noexcept override
	{
		return m_token->SetId(Id, CategoryID, CreateIfNotExist);
	}

	STDMETHODIMP GetAttribute(__RPC__in BSTR AttributeName, __RPC__deref_out_opt BSTR* AttributeValue) noexcept override
	{
		if (!AttributeValue)
			return E_POINTER;
		CComPtr<ISpDataKey> pKey;
		RETONFAIL(m_token->OpenKey(SPTOKENKEY_ATTRIBUTES, &pKey));
		CSpDynamicString strValue;
		RETONFAIL(pKey->GetStringValue(AttributeName, &strValue));
		return strValue.CopyToBSTR(AttributeValue);
	}

	STDMETHODIMP CreateInstance(__RPC__in_opt IUnknown* pUnkOuter, SpeechTokenContext ClsContext, __RPC__deref_out_opt IUnknown** Object) noexcept override
	{
		return m_token->CreateInstance(pUnkOuter, ClsContext, IID_IUnknown, reinterpret_cast<LPVOID*>(Object));
	}

	STDMETHODIMP Remove(__RPC__in BSTR ObjectStorageCLSID) noexcept override
	{
		if (!ObjectStorageCLSID || !*ObjectStorageCLSID)
			return m_token->Remove(nullptr);
		CLSID clsid;
		RETONFAIL(CLSIDFromString(ObjectStorageCLSID, &clsid));
		return m_token->Remove(&clsid);
	}

	STDMETHODIMP GetStorageFileName(__RPC__in BSTR ObjectStorageCLSID, __RPC__in BSTR KeyName,
		__RPC__in BSTR FileName, SpeechTokenShellFolder Folder,
		__RPC__deref_out_opt BSTR* FilePath) noexcept override
	{
		if (!FilePath)
			return E_POINTER;
		CLSID clsid;
		RETONFAIL(CLSIDFromString(ObjectStorageCLSID, &clsid));
		CSpDynamicString strFileName;
		RETONFAIL(m_token->GetStorageFileName(clsid, KeyName, FileName, Folder, &strFileName));
		return strFileName.CopyToBSTR(FilePath);
	}

	STDMETHODIMP RemoveStorageFileName(__RPC__in BSTR ObjectStorageCLSID, __RPC__in BSTR KeyName,
		VARIANT_BOOL fDeleteFile) noexcept override
	{
		CLSID clsid;
		RETONFAIL(CLSIDFromString(ObjectStorageCLSID, &clsid));
		return m_token->RemoveStorageFileName(clsid, KeyName, fDeleteFile);
	}

	STDMETHODIMP IsUISupported(__RPC__in const BSTR TypeOfUI, __RPC__in const VARIANT* ExtraData,
		__RPC__in_opt IUnknown* Object, __RPC__out VARIANT_BOOL* Supported) noexcept override
	{
		if (!Supported)
			return E_POINTER;
		BOOL bSupported = FALSE;
		RETONFAIL(GetVariantData(ExtraData, [=, this, &bSupported](LPCVOID data, ULONG size)
			{ return m_token->IsUISupported(TypeOfUI, const_cast<LPVOID>(data), size, Object, &bSupported); }));
		*Supported = bSupported ? VARIANT_TRUE : VARIANT_FALSE;
		return S_OK;
	}

	STDMETHODIMP DisplayUI(long hWnd, __RPC__in BSTR Title, __RPC__in const BSTR TypeOfUI,
		__RPC__in const VARIANT* ExtraData = 0,
		__RPC__in_opt IUnknown* Object = 0) noexcept override
	{
		// We are converting long to possibly larger HWND.
		// Fortunately, handles are 32-bit even on 64-bit Windows.
		return GetVariantData(ExtraData, [=, this](LPCVOID data, ULONG size)
			{ return m_token->DisplayUI(static_cast<HWND>(LongToHandle(hWnd)),
				(Title && !*Title) ? nullptr : Title, // if Title is empty string, convert to nullptr
				TypeOfUI, const_cast<LPVOID>(data), size, Object); });
	}

	STDMETHODIMP MatchesAttributes(__RPC__in BSTR Attributes, __RPC__out VARIANT_BOOL* Matches) noexcept override
	{
		if (!Matches)
			return E_POINTER;
		BOOL match = FALSE;
		RETONFAIL(m_token->MatchesAttributes(Attributes, &match));
		*Matches = match ? VARIANT_TRUE : VARIANT_FALSE;
		return S_OK;
	}
};

OBJECT_ENTRY_AUTO(CLSID_ObjectTokenAutomation, CObjectTokenAutomation)
