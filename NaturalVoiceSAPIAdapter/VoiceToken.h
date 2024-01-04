// VoiceToken.h: CVoiceToken 的声明

#pragma once
#include "resource.h"       // 主符号

#include "pch.h"

#include "DataKey.h"
#include "TTSEngine.h"
#include "VoiceTokenAutomation.h"

#include "NaturalVoiceSAPIAdapter_i.h"


using namespace ATL;

struct VoiceKeyTraits
{
	static constexpr HKEY RegRoot = HKEY_CURRENT_USER;
	static constexpr LPCWSTR RegPrefix = L"Software\\NaturalVoiceSAPIAdapter\\VoiceTokens\\";
	static HRESULT GetStringValueOverride(LPCWSTR pszSubkey, LPCWSTR pszValueName, LPWSTR* ppszValue)
	{
		if (*pszSubkey == '\0' && _wcsicmp(pszValueName, L"CLSID") == 0)
			return StringFromCLSID(CLSID_TTSEngine, ppszValue);
		return SPERR_NOT_FOUND;
	}
};

typedef CDataKey<ISpDataKey, VoiceKeyTraits> CVoiceKey;

// CVoiceToken

class ATL_NO_VTABLE CVoiceToken :
	public CDataKey<ISpObjectToken, VoiceKeyTraits>
{
public:
	CVoiceToken()
	{
	}

DECLARE_REGISTRY_RESOURCEID(108)

DECLARE_NOT_AGGREGATABLE(CVoiceToken)

BEGIN_COM_MAP(CVoiceToken)
	COM_INTERFACE_ENTRY(ISpDataKey)
	COM_INTERFACE_ENTRY(ISpObjectToken)
	COM_INTERFACE_ENTRY(IDataKeyInit)
	COM_INTERFACE_ENTRY_AGGREGATE(IID_IDispatch, m_pAutomation)
	COM_INTERFACE_ENTRY_AGGREGATE(IID_ISpeechObjectToken, m_pAutomation)
END_COM_MAP()



	DECLARE_PROTECT_FINAL_CONSTRUCT()

	HRESULT FinalConstruct()
	{
		RETONFAIL(CVoiceTokenAutomation::_CreatorClass::CreateInstance(GetControllingUnknown(), IID_IUnknown,
			reinterpret_cast<LPVOID*>(&m_pAutomation)));
		CComQIPtr<IDataKeyAutomationInit>(m_pAutomation)->SetParent(GetUnknown());
		return S_OK;
	}

	void FinalRelease()
	{
	}

public:
	STDMETHODIMP SetId(_In_opt_ LPCWSTR pszCategoryId, LPCWSTR pszTokenId, BOOL fCreateIfNotExist)
	{
		return SPERR_ALREADY_INITIALIZED;
	}

	STDMETHODIMP GetId(_Outptr_ LPWSTR* ppszCoMemTokenId)
	{
		if (!ppszCoMemTokenId)
			return E_POINTER;
		CSpDynamicString id = (SPCAT_VOICES L"\\TokenEnums\\NaturalVoiceEnumerator\\" + m_subkeyPath).c_str();
		if (!id)
			return E_OUTOFMEMORY;
		*ppszCoMemTokenId = id.Detach();
		return S_OK;
	}

	STDMETHODIMP GetCategory(_Outptr_ ISpObjectTokenCategory** ppTokenCategory)
	{
		if (!ppTokenCategory)
			return E_POINTER;
		return SpGetCategoryFromId(SPCAT_VOICES, ppTokenCategory);
	}

	STDMETHODIMP CreateInstance(IUnknown* pUnkOuter, DWORD dwClsContext, REFIID riid, void** ppvObject)
	{
		if (!ppvObject)
			return E_POINTER;
		CComPtr<ISpObjectWithToken> pInst;
		RETONFAIL(CTTSEngine::CreateInstance(pUnkOuter, &pInst));
		RETONFAIL(pInst->SetObjectToken(CComQIPtr<ISpObjectToken>(GetUnknown())));
		return pInst->QueryInterface(riid, ppvObject);
	}

	STDMETHODIMP GetStorageFileName(REFCLSID clsidCaller, _In_  LPCWSTR pszValueName,
		_In_opt_ LPCWSTR pszFileNameSpecifier, ULONG nFolder,
		_Outptr_ LPWSTR* ppszFilePath)
	{ return E_NOTIMPL; }

	STDMETHODIMP RemoveStorageFileName(REFCLSID clsidCaller, _In_ LPCWSTR pszKeyName, BOOL fDeleteFile)
	{ return E_NOTIMPL; }

	STDMETHODIMP Remove(_In_opt_ const CLSID* pclsidCaller)
	{
		// Removing the token programmatically (when pclsidCaller is null) is not allowed
		return pclsidCaller ? E_NOTIMPL : E_ACCESSDENIED;
	}

	STDMETHODIMP IsUISupported(LPCWSTR pszTypeOfUI, void* pvExtraData, ULONG cbExtraData, IUnknown* punkObject,
		BOOL* pfSupported)
	{
		if (!pfSupported)
			return E_POINTER;
		*pfSupported = FALSE;
		return S_OK;
	}

	STDMETHODIMP DisplayUI(HWND hwndParent, LPCWSTR pszTitle, LPCWSTR pszTypeOfUI, void* pvExtraData,
		ULONG cbExtraData, IUnknown* punkObject)
	{ return E_NOTIMPL; }

	STDMETHODIMP MatchesAttributes(LPCWSTR pszAttributes, BOOL* pfMatches)
	{
		if (!pfMatches)
			return E_POINTER;

		// Use SpObjectTokenEnumBuilder to help us do the property matching
		CComPtr<ISpObjectTokenEnumBuilder> pEnumBuilder;
		RETONFAIL(pEnumBuilder.CoCreateInstance(CLSID_SpObjectTokenEnum));
		pEnumBuilder->SetAttribs(pszAttributes, nullptr);
		CComQIPtr<ISpObjectToken> pSelf(this);
		pEnumBuilder->AddTokens(1, &pSelf.p); // Add this token itself to the enum
		ULONG count = 0;
		CComQIPtr<IEnumSpObjectTokens>(pEnumBuilder)->GetCount(&count);
		*pfMatches = count != 0; // If this token gets filtered out, it does not match the attributes
		return S_OK;
	}
};
