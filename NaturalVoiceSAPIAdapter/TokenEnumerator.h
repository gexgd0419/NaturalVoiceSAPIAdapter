#pragma once
#include "pch.h"

using namespace ATL;

// TokenEnumerator: base class for custom token enumerators

class ATL_NO_VTABLE TokenEnumerator : public IEnumSpObjectTokens
{
protected:
	TokenEnumerator()
	{
	}

protected:
	CComPtr<IEnumSpObjectTokens> m_pEnum;

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
