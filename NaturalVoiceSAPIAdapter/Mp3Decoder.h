#pragma once
#include <MSAcm.h>
#include <system_error>
#include <functional>
#include "wrappers.h"

struct AcmStreamCloser
{
	void operator()(HACMSTREAM hAcm) const { acmStreamClose(hAcm, 0); }
};

class Mp3Decoder
{
private:
	std::unique_ptr<BYTE[]> m_pMp3Buf = nullptr, m_pWavBuf = nullptr;
	HandleWrapper<HACMSTREAM, AcmStreamCloser{}> m_hAcm = nullptr;
	ACMSTREAMHEADER m_header = { sizeof(ACMSTREAMHEADER) };
	DWORD m_cbMp3Buf = 0, m_cbWavBuf = 0, m_cbMp3Leftover = 0;
	void Init(const BYTE* pMp3Chunk, DWORD cbChunkSize);
public:
	Mp3Decoder() noexcept {}
	~Mp3Decoder() noexcept;

	// Copying not permitted
	Mp3Decoder(const Mp3Decoder&) = delete;
	Mp3Decoder& operator=(const Mp3Decoder&) = delete;

	typedef std::function<int(BYTE*, DWORD)> WriteCallbackType;
	void Convert(const BYTE* pMp3Chunk, DWORD cbChunkSize, const WriteCallbackType& writeWavCallback);
};

class mci_category_impl : public std::error_category
{
public:
	const char* name() const noexcept override { return "mci"; }
	std::string message(int ev) const override
	{
		char msg[256];
		mciGetErrorStringA(ev, msg, sizeof msg);
		return msg;
	}
};

inline const mci_category_impl& mci_category()
{
	static constexpr mci_category_impl impl;
	return impl;
}