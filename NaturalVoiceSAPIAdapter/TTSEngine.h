// TTSEngine.h: CTTSEngine 的声明

#pragma once
#include "resource.h"       // 主符号

#include "pch.h"
#include <speechapi_cxx.h>
#include "SpeechRestAPI.h"
#include "Logger.h"
#include "SapiException.h"
#include "Mp3Decoder.h"

#include "NaturalVoiceSAPIAdapter_i.h"


#if defined(_WIN32_WCE) && !defined(_CE_DCOM) && !defined(_CE_ALLOW_SINGLE_THREADED_OBJECTS_IN_MTA)
#error "Windows CE 平台(如不提供完全 DCOM 支持的 Windows Mobile 平台)上无法正确支持单线程 COM 对象。定义 _CE_ALLOW_SINGLE_THREADED_OBJECTS_IN_MTA 可强制 ATL 支持创建单线程 COM 对象实现并允许使用其单线程 COM 对象实现。rgs 文件中的线程模型已被设置为“Free”，原因是该模型是非 DCOM Windows CE 平台支持的唯一线程模型。"
#endif

using namespace ATL;
using namespace Microsoft::CognitiveServices::Speech;
using namespace Microsoft::CognitiveServices::Speech::Audio;



struct TextOffsetMapping
{
	ULONG ulSAPITextOffset; // offset in source string from SAPI
	ULONG ulSSMLTextOffset; // offset in our SSML buffer
	constexpr TextOffsetMapping(ULONG ulSAPITextOffset, ULONG ulSSMLTextOffset) noexcept
		: ulSAPITextOffset(ulSAPITextOffset), ulSSMLTextOffset(ulSSMLTextOffset) {}
};


struct BookmarkInfo
{
	ULONG ulSAPITextOffset; // offset in source string from SAPI
	std::wstring name;
	constexpr BookmarkInfo(ULONG ulSAPITextOffset, std::wstring name) noexcept
		: ulSAPITextOffset(ulSAPITextOffset), name(name) {}
};


enum class ErrorMode : DWORD
{
	ProbeForError = 0,
	StopOnError,
	ShowMessageOnError
};


// CTTSEngine

class ATL_NO_VTABLE CTTSEngine :
	public CComObjectRootEx<CComMultiThreadModel>,
	public CComCoClass<CTTSEngine, &CLSID_TTSEngine>,
	public ISpTTSEngine,
	public ISpObjectWithToken,
	public ISupportErrorInfo
{
public:
	CTTSEngine()
	{
	}

DECLARE_REGISTRY_RESOURCEID(106)

DECLARE_NOT_AGGREGATABLE(CTTSEngine)

BEGIN_COM_MAP(CTTSEngine)
	COM_INTERFACE_ENTRY(ISpTTSEngine)
	COM_INTERFACE_ENTRY(ISpObjectWithToken)
	COM_INTERFACE_ENTRY(ISupportErrorInfo)
END_COM_MAP()



	DECLARE_PROTECT_FINAL_CONSTRUCT()

	HRESULT FinalConstruct()
	{
		return S_OK;
	}

	void FinalRelease()
	{
	}

public: // Interface implementation

	// ISpObjectWithToken 
	STDMETHODIMP SetObjectToken(ISpObjectToken* pToken) noexcept override;
	STDMETHODIMP GetObjectToken(ISpObjectToken** ppToken) noexcept override
	{
		return SpGenericGetObjectToken(ppToken, m_cpToken);
	}

	// ISpTTSEngine
	STDMETHODIMP Speak(DWORD dwSpeakFlags,
		REFGUID rguidFormatId, const WAVEFORMATEX* pWaveFormatEx,
		const SPVTEXTFRAG* pTextFragList, ISpTTSEngineSite* pOutputSite) noexcept override;
	STDMETHODIMP GetOutputFormat(const GUID* pTargetFormatId, const WAVEFORMATEX* pTargetWaveFormatEx,
		GUID* pDesiredFormatId, WAVEFORMATEX** ppCoMemDesiredWaveFormatEx) noexcept override;

	// ISupportErrorInfo
	STDMETHODIMP InterfaceSupportsErrorInfo(REFIID riid) noexcept override
	{
		if (InlineIsEqualGUID(riid, IID_ISpTTSEngine) || InlineIsEqualGUID(riid, IID_ISpObjectWithToken))
			return S_OK;
		return S_FALSE;
	}

private:
	ISpTTSEngineSite* m_pOutputSite = nullptr;
	std::recursive_mutex m_outputSiteMutex;

private: // Member variables

	CComPtr<ISpObjectToken> m_cpToken;
	CComPtr<ISpPhoneConverter> m_phoneConverter;
	std::shared_ptr<SpeechSynthesizer> m_synthesizer;
	std::unique_ptr<SpeechRestAPI> m_restApi;

	ErrorMode m_errorMode = ErrorMode::ProbeForError;
	bool m_isEdgeVoice = false;
	bool m_onlineDelayOptimization = false;
	bool m_compensatedSilenceWritten = false;
	ULONG m_lastSilentBytes = 0;
	ULONG m_compensatedSilentBytes = 0;
	DWORD m_lastSpeakCompletedTicks = 0;
	DWORD m_thisSpeakStartedTicks = 0;

	std::wstring m_localeName;
	std::wstring m_onlineVoiceName;

	std::wstring m_ssml; // translated SSML

	// SAPI XML will be translated into SSML,
	// but we need to keep track of the original text offsets
	// so that events like WordBoundary still work
	std::vector<TextOffsetMapping> m_offsetMappings;

	size_t m_mappingIndex = 0;

	// Edge voices do not support bookmarks.
	// We store the specified bookmark positions,
	// and when a word boundary
	std::vector<BookmarkInfo> m_bookmarks;
	size_t m_bookmarkIndex = 0;

private:
	int OnAudioData(uint8_t* data, uint32_t len);
	void OnBookmark(uint64_t offsetTicks, const std::wstring& bookmark);
	void OnBoundary(uint64_t audioOffsetTicks, uint32_t textOffset, uint32_t textLength, SPEVENTENUM boundaryType);
	void OnViseme(uint64_t offsetTicks, uint32_t visemeId);

private: // Private methods

	void InitPhoneConverter();
	void InitVoice();
	bool InitLocalVoice(ISpDataKey* pConfigKey);
	bool InitCloudVoiceSynthesizer(ISpDataKey* pConfigKey);
	bool InitCloudVoiceRestAPI(ISpDataKey* pConfigKey);
	void SetupSynthesizerEvents(ULONGLONG interests);
	void ClearSynthesizerEvents();
	void SetupRestAPIEvents(ULONGLONG interests);

	void AppendTextFragToSsml(const SPVTEXTFRAG* pTextFrag);
	void AppendPhonemesToSsml(const SPPHONEID* pPhoneIds);
	void AppendSAPIContextToSsml(const SPVCONTEXT& context);
	bool BuildSSML(const SPVTEXTFRAG* pTextFragList);

	void FinishSimulatingBookmarkEvents(ULONGLONG streamOffset);

	void MapTextOffset(ULONG& ulSSMLOffset, ULONG& ulTextLen);
	void CheckSynthesisResult(const std::shared_ptr<SpeechSynthesisResult>& result);

	template <class Exception, class... Args>
	HRESULT OnException(
		Exception&& exception,
		FormatStringT<Args..., Exception> logFormat,
		Args&&... logArgs) noexcept;

private: // Static members

	// 24kHz 16Bit mono
	static constexpr DWORD nWaveBytesPerMSec = 24000 * 16 / 8 / 1000;
	ULONGLONG WaveTicksToBytes(uint64_t ticks)
	{
		return ticks * nWaveBytesPerMSec / 10000 + m_compensatedSilentBytes;
	}
};

OBJECT_ENTRY_AUTO(__uuidof(TTSEngine), CTTSEngine)

template <class Exception, class... Args>
HRESULT CTTSEngine::OnException(
	Exception&& ex,
	FormatStringT<Args..., Exception> logFormat,
	Args&&... logArgs) noexcept
{
	using T = std::remove_cvref_t<Exception>;

	try
	{
		std::wstring wmsg = StringToWString(ex.what());
		Error(wmsg.c_str());
		LogErr(logFormat, std::forward<Args>(logArgs)..., ex);
		if (m_errorMode == ErrorMode::ShowMessageOnError)
		{
			if constexpr (std::is_base_of_v<std::system_error, T>)
			{
				auto& cat = ex.code().category();
				if (cat != std::system_category()
					&& cat != sapi_category()
					&& cat != azac_category()
					&& cat != mci_category())
					wmsg.insert(0, L"Network connection error:\r\n\r\n");
			}
			MessageBoxW(NULL, wmsg.c_str(), L"NaturalVoiceSAPIAdapter", MB_ICONEXCLAMATION | MB_SYSTEMMODAL);
		}
	}
	catch (...) {}

	if constexpr (std::is_base_of_v<std::system_error, T>)
	{
		auto& cat = ex.code().category();
		if (cat == std::system_category() || cat == asio::system_category())
			return HRESULT_FROM_WIN32(ex.code().value());
		else if (cat == azac_category())
			return ex.code().value() == AZAC_ERR_INVALID_ARG ? E_INVALIDARG : E_FAIL;
		return E_FAIL;
	}
	else if constexpr (std::is_base_of_v<std::invalid_argument, T>)
		return E_INVALIDARG;
	else
		return E_FAIL;
}
