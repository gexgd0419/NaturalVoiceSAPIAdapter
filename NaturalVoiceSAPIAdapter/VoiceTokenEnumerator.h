// VoiceTokenEnumerator.h: CVoiceTokenEnumerator 的声明

#pragma once
#include "resource.h"       // 主符号

#include "pch.h"
#include <optional>
#include "VoiceToken.h"

#include "NaturalVoiceSAPIAdapter_i.h"


using namespace ATL;

// CVoiceTokenEnumerator

class ATL_NO_VTABLE CVoiceTokenEnumerator :
	public CComObjectRootEx<CComMultiThreadModel>,
	public CComCoClass<CVoiceTokenEnumerator, &CLSID_VoiceTokenEnumerator>,
	public IEnumSpObjectTokens
{
public:
	CVoiceTokenEnumerator()
	{
	}

DECLARE_GET_CONTROLLING_UNKNOWN()
DECLARE_REGISTRY_RESOURCEID(107)

DECLARE_NOT_AGGREGATABLE(CVoiceTokenEnumerator)

BEGIN_COM_MAP(CVoiceTokenEnumerator)
	COM_INTERFACE_ENTRY(IEnumSpObjectTokens)
END_COM_MAP()



	DECLARE_PROTECT_FINAL_CONSTRUCT()

	HRESULT FinalConstruct();

	void FinalRelease()
	{
	}


private:
	CComPtr<IEnumSpObjectTokens> m_pEnum;
	static CComPtr<IEnumSpObjectTokens> EnumLocalVoices();
	static CComPtr<IEnumSpObjectTokens> EnumEdgeVoices(BOOL allLanguages);

public:
	// ISpObjectTokenEnumBuilder doesn't seem to support aggregation
	// so manually forward calls to inner enumerator object instead
	STDMETHODIMP Next(ULONG celt, ISpObjectToken** pelt, ULONG* pceltFetched) noexcept override
	{
		return m_pEnum->Next(celt, pelt, pceltFetched);
	}
	STDMETHODIMP Skip(ULONG celt) noexcept override
	{
		return m_pEnum->Skip(celt);
	}
	STDMETHODIMP Reset(void) noexcept override
	{
		return m_pEnum->Reset();
	}
	STDMETHODIMP Clone(IEnumSpObjectTokens** ppEnum) noexcept override
	{
		return m_pEnum->Clone(ppEnum);
	}
	STDMETHODIMP Item(ULONG Index, ISpObjectToken** ppToken) noexcept override
	{
		return m_pEnum->Item(Index, ppToken);
	}
	STDMETHODIMP GetCount(ULONG* pCount) noexcept override
	{
		return m_pEnum->GetCount(pCount);
	}
};

OBJECT_ENTRY_AUTO(__uuidof(VoiceTokenEnumerator), CVoiceTokenEnumerator)
