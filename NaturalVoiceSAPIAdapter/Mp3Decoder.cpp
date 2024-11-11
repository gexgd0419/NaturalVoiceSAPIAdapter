#include "pch.h"
#include "Mp3Decoder.h"
#pragma comment (lib, "msacm32.lib")
#pragma comment (lib, "winmm.lib")

static constexpr WORD BitRates[2][3][15] =  // unit: kbps
{
	{ // Version 1
		{ // Layer 1
			0, 32, 64, 96, 128, 160, 192, 224, 256, 288, 320, 352, 384, 416, 448
		},
		{ // Layer 2
			0, 32, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 384
		},
		{ // Layer 3
			0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320
		}
	},
	{ // Version 2
		{ // Layer 1
			0, 32, 48, 56, 64, 80, 96, 112, 128, 144, 160, 176, 192, 224, 256
		},
		{ // Layer 2
			0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160
		},
		{ // Layer 3 (same as 2)
			0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160
		}
	}
};
static constexpr WORD SampleRates[4][3] =
{
	{ 11025, 12000, 8000 }, // MPEG 2.5
	{},
	{ 22050, 24000, 16000 }, // MPEG 2
	{ 44100, 48000, 32000 }, // MPEG 1
};
static constexpr WORD SamplesPerFrame[2][3] = 
{
	{ 384, 1152, 1152 },  // MPEG 1
	{ 384, 1152, 576 }    // Other
};

void Mp3Decoder::Init(const BYTE* pMp3Chunk, DWORD cbChunkSize)
{
	if (!pMp3Chunk || cbChunkSize == 0)
		return; // Ignore without initializing

	/*
	* MP3 frame format (4 bytes): AAAAAAAA AAABBCCD EEEEFFGH IIJJKLMM
	* where:
	* A: sync bits, all 1's
	* B: MPEG version
	* C: layer
	* D: protection
	* E: bitrate index
	* F: sample rate index
	* G: padding
	* H: private
	* I: channel mode
	* J: mode extension
	* K: copyright
	* L: original
	* M: emphasis
	* Reference: https://id3lib.sourceforge.net/id3/mp3frame.html
	*/

	const BYTE* pFrame = pMp3Chunk;
	// If the first 11 bits are not all 1's (sync bits)
	while (!(pFrame[0] == 0xFF && (pFrame[1] & 0b11100000) == 0b11100000))
	{
		// Look for next possible header position
		pFrame = static_cast<const BYTE*>(memchr(pFrame + 1, 0xFF, cbChunkSize - (pFrame - pMp3Chunk) - 1));
		if (!pFrame) // No valid MP3 frame header found
			throw std::system_error(ACMERR_NOTPOSSIBLE, mci_category());
	}

	int mpegVersion		= (pFrame[1] & 0b00011000) >> 3; // 0: MPEG 2.5; 2: MPEG 2; 3: MPEG 1
	int layer			= (pFrame[1] & 0b00000110) >> 1; // 1: Layer 3; 2: Layer 2; 3: Layer 1
	int bitRateIndex	= (pFrame[2] & 0b11110000) >> 4;
	int sampleRateIndex	= (pFrame[2] & 0b00001100) >> 2;
	int padding			= (pFrame[2] & 0b00000010) >> 1;
	int channelMode		= (pFrame[3] & 0b11000000) >> 6; // 0: Stereo; 1: Joint stereo; 2: Dual channel (2 mono); 3: Single channel (mono)
	int versionIndex	= mpegVersion == 3 ? 0 : 1;
	int layerIndex		= 3 - layer;

	if (mpegVersion == 1 || layer == 0 || bitRateIndex == 0 || bitRateIndex == 15 || sampleRateIndex == 3)
		throw std::system_error(ACMERR_NOTPOSSIBLE, mci_category());

	DWORD Mp3BitRate = BitRates[versionIndex][layerIndex][bitRateIndex] * 1000;
	DWORD SamplesPerSec = SampleRates[mpegVersion][sampleRateIndex];
	WORD FrameLength = (WORD)(SamplesPerFrame[versionIndex][layerIndex] * (Mp3BitRate / CHAR_BIT) / SamplesPerSec + padding);
	if (layerIndex == 0) // For Layer 1, this is needed. See: https://stackoverflow.com/questions/72416908/mp3-exact-frame-size-calculation
		FrameLength *= 4;

	constexpr WORD BitsPerSample = 16;
	WORD Channels = channelMode == 3 ? 1 : 2;

	MPEGLAYER3WAVEFORMAT mp3fmt =
	{
		.wfx = {
			.wFormatTag = WAVE_FORMAT_MPEGLAYER3,
			.nChannels = Channels,
			.nSamplesPerSec = SamplesPerSec,
			.nAvgBytesPerSec = Mp3BitRate / CHAR_BIT,
			.nBlockAlign = 1,
			.wBitsPerSample = 0,
			.cbSize = MPEGLAYER3_WFX_EXTRA_BYTES
		},
		.wID = MPEGLAYER3_ID_MPEG,
		.fdwFlags = MPEGLAYER3_FLAG_PADDING_ISO,
		.nBlockSize = FrameLength,
		.nFramesPerBlock = 1,
		.nCodecDelay = 0
	};

	m_wavefmt =
	{
		.wFormatTag = WAVE_FORMAT_PCM,
		.nChannels = Channels,
		.nSamplesPerSec = SamplesPerSec,
		.nAvgBytesPerSec = SamplesPerSec * Channels * (BitsPerSample / CHAR_BIT),
		.nBlockAlign = (WORD)(Channels * (BitsPerSample / CHAR_BIT)),
		.wBitsPerSample = BitsPerSample,
		.cbSize = 0
	};

	MMRESULT mmr = acmStreamOpen(&m_hAcm, nullptr, &mp3fmt.wfx, &m_wavefmt, nullptr, 0, 0, 0);
	if (mmr) throw std::system_error(mmr, mci_category());

	m_cbMp3Buf = cbChunkSize;
	m_pMp3Buf = std::make_unique_for_overwrite<BYTE[]>(m_cbMp3Buf);

	mmr = acmStreamSize(m_hAcm, m_cbMp3Buf, &m_cbWavBuf, ACM_STREAMSIZEF_SOURCE);
	if (mmr) throw std::system_error(mmr, mci_category());
	m_pWavBuf = std::make_unique_for_overwrite<BYTE[]>(m_cbWavBuf);

	m_header.pbSrc = m_pMp3Buf.get();
	m_header.cbSrcLength = m_cbMp3Buf;
	m_header.pbDst = m_pWavBuf.get();
	m_header.cbDstLength = m_cbWavBuf;
	mmr = acmStreamPrepareHeader(m_hAcm, &m_header, 0);
	if (mmr) throw std::system_error(mmr, mci_category());
}