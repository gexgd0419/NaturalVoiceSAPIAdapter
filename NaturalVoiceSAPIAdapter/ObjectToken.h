// VoiceToken.h: CObjectToken 的声明

#pragma once
#include "resource.h"       // 主符号

#include "pch.h"

#include "DataKey.h"
#include "ObjectTokenAutomation.h"

#include "NaturalVoiceSAPIAdapter_i.h"


using namespace ATL;


// CObjectToken

template <class Traits>
class ATL_NO_VTABLE CObjectToken :
	public CDataKey<ISpObjectToken, Traits>
{
public:
	CObjectToken()
	{
	}

DECLARE_REGISTRY_RESOURCEID(108)

DECLARE_NOT_AGGREGATABLE(CObjectToken)

BEGIN_COM_MAP(CObjectToken)
	COM_INTERFACE_ENTRY(ISpDataKey)
	COM_INTERFACE_ENTRY(ISpObjectToken)
	COM_INTERFACE_ENTRY(IDataKeyInit)
	COM_INTERFACE_ENTRY_AUTOAGGREGATE(IID_IDispatch, m_pAutomation, CLSID_ObjectTokenAutomation)
	COM_INTERFACE_ENTRY_AUTOAGGREGATE(IID_ISpeechObjectToken, m_pAutomation, CLSID_ObjectTokenAutomation)
END_COM_MAP()



	//DECLARE_PROTECT_FINAL_CONSTRUCT()

public:
	STDMETHODIMP SetId(_In_opt_ LPCWSTR /*pszCategoryId*/, LPCWSTR /*pszTokenId*/, BOOL /*fCreateIfNotExist*/) noexcept override
	{
		return SPERR_ALREADY_INITIALIZED;
	}

	STDMETHODIMP GetId(_Outptr_ LPWSTR* ppszCoMemTokenId) noexcept override
	{
		if (!ppszCoMemTokenId)
			return E_POINTER;
		CSpDynamicString id;
		RETONFAIL(MergeIntoCoString(id, Traits::SpIdRoot, CDataKey<ISpObjectToken, Traits>::m_pData->path));
		*ppszCoMemTokenId = id.Detach();
		return S_OK;
	}

	STDMETHODIMP GetCategory(_Outptr_ ISpObjectTokenCategory** ppTokenCategory) noexcept override
	{
		if (!ppTokenCategory)
			return E_POINTER;
		return SpGetCategoryFromId(Traits::SpCategory, ppTokenCategory);
	}

	STDMETHODIMP CreateInstance(IUnknown* pUnkOuter, DWORD /*dwClsContext*/, REFIID riid, void** ppvObject) noexcept override
	{
		if (!ppvObject)
			return E_POINTER;
		CComPtr<ISpObjectWithToken> pInst;
		RETONFAIL(Traits::ComClass::CreateInstance(pUnkOuter, &pInst));
		RETONFAIL(pInst->SetObjectToken(CComQIPtr<ISpObjectToken>(GetUnknown())));
		return pInst->QueryInterface(riid, ppvObject);
	}

	STDMETHODIMP GetStorageFileName(REFCLSID /*clsidCaller*/, _In_  LPCWSTR /*pszValueName*/,
		_In_opt_ LPCWSTR /*pszFileNameSpecifier*/, ULONG /*nFolder*/,
		_Outptr_ LPWSTR* /*ppszFilePath*/) noexcept override
	{ return E_NOTIMPL; }

	STDMETHODIMP RemoveStorageFileName(REFCLSID /*clsidCaller*/, _In_ LPCWSTR /*pszKeyName*/, BOOL /*fDeleteFile*/)
		noexcept override
	{ return E_NOTIMPL; }

	STDMETHODIMP Remove(_In_opt_ const CLSID* pclsidCaller) noexcept override
	{
		// Removing the token programmatically (when pclsidCaller is null) is not allowed
		return pclsidCaller ? E_NOTIMPL : E_ACCESSDENIED;
	}

	STDMETHODIMP IsUISupported(LPCWSTR /*pszTypeOfUI*/, void* /*pvExtraData*/, ULONG /*cbExtraData*/,
		IUnknown* /*punkObject*/, BOOL* pfSupported) noexcept override
	{
		if (!pfSupported)
			return E_POINTER;
		*pfSupported = FALSE;
		return S_OK;
	}

	STDMETHODIMP DisplayUI(HWND /*hwndParent*/, LPCWSTR /*pszTitle*/, LPCWSTR /*pszTypeOfUI*/, void* /*pvExtraData*/,
		ULONG /*cbExtraData*/, IUnknown* /*punkObject*/) noexcept override
	{ return E_NOTIMPL; }

	STDMETHODIMP MatchesAttributes(LPCWSTR pszAttributes, BOOL* pfMatches) noexcept override
	{
		if (!pfMatches)
			return E_POINTER;

		// Use SpObjectTokenEnumBuilder to help us do the property matching
		CComPtr<ISpObjectTokenEnumBuilder> pEnumBuilder;
		RETONFAIL(pEnumBuilder.CoCreateInstance(CLSID_SpObjectTokenEnum));
		RETONFAIL(pEnumBuilder->SetAttribs(pszAttributes, nullptr));
		CComQIPtr<ISpObjectToken> pSelf(this);
		RETONFAIL(pEnumBuilder->AddTokens(1, &pSelf.p)); // Add this token itself to the enum
		ULONG count = 0;
		CComQIPtr<IEnumSpObjectTokens>(pEnumBuilder)->GetCount(&count);
		*pfMatches = count != 0; // If this token gets filtered out, it does not match the attributes
		return S_OK;
	}
public:
	static HRESULT Create(const std::shared_ptr<DataKeyData>& pData, ISpObjectToken** ppOut) noexcept
	{
		// Create a new ISpObjectToken that points to the correponding data
		CComPtr<ISpObjectToken> pToken;
		RETONFAIL(CObjectToken::_CreatorClass::CreateInstance(nullptr, IID_ISpObjectToken, reinterpret_cast<LPVOID*>(&pToken)));
		CComPtr<IDataKeyInit> pInit;
		RETONFAIL(pToken->QueryInterface(&pInit));
		RETONFAIL(pInit->InitKey(pData));
		*ppOut = pToken.Detach();
		return S_OK;
	}
};