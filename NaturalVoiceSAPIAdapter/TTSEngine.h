// TTSEngine.h: CTTSEngine 的声明

#pragma once
#include "resource.h"       // 主符号

#include "pch.h"
#include <speechapi_cxx.h>
#include "SpeechRestAPI.h"

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

private: // Member variables

	CComPtr<ISpObjectToken> m_cpToken;
	CComPtr<ISpPhoneConverter> m_phoneConverter;
	std::shared_ptr<SpeechSynthesizer> m_synthesizer;
	std::unique_ptr<SpeechRestAPI> m_restApi;
	ISpTTSEngineSite* m_pOutputSite = nullptr;

	ErrorMode m_errorMode = ErrorMode::ProbeForError;
	std::wstring m_localeName;
	std::wstring m_onlineVoiceName;

	std::wstring m_ssml; // translated SSML

	// SAPI XML will be translated into SSML,
	// but we need to keep track of the original text offsets
	// so that events like WordBoundary still work
	std::vector<TextOffsetMapping> m_offsetMappings;

	size_t m_mappingIndex = 0;

	std::atomic_bool m_synthesisCompleted = false;
	std::shared_ptr<SpeechSynthesisResult> m_synthesisResult;

private:
	int OnAudioData(uint8_t* data, uint32_t len);
	void OnBookmark(uint64_t offsetTicks, const std::string& bookmark);
	void OnBoundary(uint64_t audioOffsetTicks, uint32_t textOffset, uint32_t textLength, SPEVENTENUM boundaryType);
	void OnViseme(uint64_t offsetTicks, uint32_t visemeId);
	void OnSessionEnd(uint64_t offsetTicks);

private: // Private methods

	HRESULT InitPhoneConverter();
	HRESULT InitVoice();
	HRESULT InitLocalVoice(ISpDataKey* pConfigKey);
	HRESULT InitCloudVoiceSynthesizer(ISpDataKey* pConfigKey);
	HRESULT InitCloudVoiceRestAPI(ISpDataKey* pConfigKey);
	void SetupSynthesizerEvents(ULONGLONG interests);
	void ClearSynthesizerEvents();
	void SetupRestAPIEvents(ULONGLONG interests);

	void AppendTextFragToSsml(const SPVTEXTFRAG* pTextFrag);
	void AppendPhonemesToSsml(const SPPHONEID* pPhoneIds);
	void AppendSAPIContextToSsml(const SPVCONTEXT& context);
	void BuildSSML(const SPVTEXTFRAG* pTextFragList);

	void MapTextOffset(ULONG& ulSSMLOffset, ULONG& ulTextLen);
	HRESULT CheckSynthesisResult(const std::shared_ptr<SpeechSynthesisResult>& result);

private: // Static members

	// 24kHz 16Bit mono
	static constexpr DWORD nWaveBytesPerMSec = 24000 * 16 / 8 / 1000;
	static inline ULONGLONG WaveTicksToBytes(uint64_t ticks)
	{
		return ticks * nWaveBytesPerMSec / 10000;
	}
};

OBJECT_ENTRY_AUTO(__uuidof(TTSEngine), CTTSEngine)
