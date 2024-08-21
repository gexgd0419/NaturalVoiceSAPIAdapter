#pragma once
#include <MSAcm.h>
#include <system_error>
#include "wrappers.h"

class Mp3Decoder
{
private:
	struct AcmStreamCloser
	{
		void operator()(HACMSTREAM hAcm) const { acmStreamClose(hAcm, 0); }
	};
	std::unique_ptr<BYTE[]> m_pMp3Buf = nullptr, m_pWavBuf = nullptr;
	HandleWrapper<HACMSTREAM, AcmStreamCloser{}> m_hAcm = nullptr;
	ACMSTREAMHEADER m_header = { sizeof(ACMSTREAMHEADER) };
	DWORD m_cbMp3Buf = 0, m_cbWavBuf = 0, m_cbMp3Leftover = 0;
	WAVEFORMATEX m_wavefmt = {};

	void Init(const BYTE* pMp3Chunk, DWORD cbChunkSize);
	void UnprepareHeader()
	{
		if (m_header.fdwStatus & ACMSTREAMHEADER_STATUSF_PREPARED)
		{
			// Restore the original (maximum) buffer sizes in headers
			m_header.cbSrcLength = m_cbMp3Buf;
			m_header.cbDstLength = m_cbWavBuf;
			acmStreamUnprepareHeader(m_hAcm, &m_header, 0);
		}
	}

public:
	Mp3Decoder() noexcept {}
	~Mp3Decoder() noexcept
	{
		UnprepareHeader();
		// Other members will be cleaned up by their destructors
	}

	// Copying not permitted
	Mp3Decoder(const Mp3Decoder&) = delete;
	Mp3Decoder& operator=(const Mp3Decoder&) = delete;

	void Reset()
	{
		UnprepareHeader();
		m_hAcm.Close();
		m_pMp3Buf = nullptr;
		m_pWavBuf = nullptr;
		m_cbMp3Buf = 0;
		m_cbWavBuf = 0;
		m_cbMp3Leftover = 0;
	}

	template <class Func> requires std::invocable<Func, BYTE*, uint32_t>
	void Convert(const BYTE* pMp3Chunk, size_t cbChunkSize, Func&& writeWavCallback);

	const WAVEFORMATEX& GetWaveFormat() const noexcept { return m_wavefmt; }
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

template <class Func> requires std::invocable<Func, BYTE*, uint32_t>
void Mp3Decoder::Convert(const BYTE* pMp3Chunk, size_t cbChunkSize, Func&& writeWavCallback)
{
	if (!m_pMp3Buf)
		Init(pMp3Chunk, (DWORD)std::min<size_t>(cbChunkSize, std::numeric_limits<DWORD>::max()));

	while (cbChunkSize > 0)
	{
		DWORD cbMp3BufSpace = m_cbMp3Buf - m_cbMp3Leftover;
		DWORD cbMp3Copy = (DWORD)std::min<size_t>(cbMp3BufSpace, cbChunkSize);
		m_header.cbSrcLength = m_cbMp3Leftover + cbMp3Copy; // Change header to indicate size
		memcpy(m_pMp3Buf.get() + m_cbMp3Leftover, pMp3Chunk, cbMp3Copy); // Append new data after leftover
		pMp3Chunk += cbMp3Copy;
		cbChunkSize -= cbMp3Copy;

		MMRESULT mmr = acmStreamConvert(m_hAcm, &m_header, ACM_STREAMCONVERTF_BLOCKALIGN);
		if (mmr) throw std::system_error(mmr, mci_category());

		writeWavCallback(m_pWavBuf.get(), m_header.cbDstLengthUsed);

		// Some data may not be processed. Save the leftover for next conversion
		m_cbMp3Leftover = m_header.cbSrcLength - m_header.cbSrcLengthUsed;
		// Copy the leftover part to the beginning of the buffer
		if (m_cbMp3Leftover > 0)
			memcpy(m_pMp3Buf.get(), m_pMp3Buf.get() + m_header.cbSrcLengthUsed, m_cbMp3Leftover);
	}
}