# NaturalVoiceSAPIAdapter

An [SAPI 5 text-to-speech (TTS) engine][1] that can utilize the [natural/neural voices][2] provided by the [Azure AI Speech Service][3].

After installation, any program that supports SAPI 5 voices can use the natural voices via this TTS engine.

This engine supports both online voices and [offline/embedded voices][4].

连接 [Azure AI 语音服务][3]，使第三方程序也能使用微软[自然语音][2]的 SAPI 5 TTS 引擎。支持在线语音和[离线语音(嵌入式语音)][4]。

## How this works

This TTS engine works by converting SAPI commands to [Speech Synthesis Markup Language (SSML)][5] text, then send it to Azure AI Speech Service via the [Speech SDK][6].

SAPI has a different XML format for TTS, see [XML TTS Tutorial (SAPI 5.3)][7]. Later versions of SAPI support SSML as well.

The SAPI framework always parses the XML text into fragments before passing them into the TTS engine. So even if the input text is already in SSML, it will still be parsed into fragments by SAPI, then be reassembled into SSML by this engine, before it can be sent to Azure AI Speech Service.

It also means that voice changing is handled locally by SAPI. `<voice>` elements won't be passed to the TTS engine or the Speech Service, so you cannot use `<voice>` to switch to an arbitrary voice supported by the Speech Service. You can still use `<voice>` to switch to another SAPI voice, including other NaturalVoiceSAPIAdapter voices.

## Supported SAPI features

- Basic TTS audio output. **Only the `24kHz 16Bit mono` audio format is supported**. If the SAPI client requests a different format, the SAPI framework will convert the audio to the specified format, but you can't get higher quality than `24kHz 16Bit mono`.
- **Volume and Rate adjustment**. Volume can be 0 to 100, and rate can be -10 (1/3x speed) to 10 (3x speed).
- **Pitch adjustment**. Pitch can be -10 (50% lower) to 10 (50% higher).
- **Word boundary event** that tells the client which word is being spoken right now.
- **Viseme event** that tells the client the current [viseme][8] being pronounced. The client can use them to show real-time mouth positions.
- **Bookmark event** that will be sent to the client whenever a bookmark tag `<bookmark mark="xx"/>` is reached.
- **`<silence>` tag** that pauses the voice for a specified duration.
- **`<emph>` tag** that emphasizes a section of text.
- **`<context>` tag** that tells the voice what a certain confusable part is supposed to mean. For example, what date `03/04/01` is.
- **`<pron>` tag** that inserts a specified pronunciation, when the engine doesn't know how to pronounce a word.


## Not supported SAPI features

- **Skipping**. The engine ignores all skipping requests.
- Applying volume and rate adjustments **during speaking**.
- **Phoneme event**. Viseme event, however, is supported.
- **`<partofsp>` tag** that specifies which part of speech a word is (noun, verb, etc.).


## Not supported features when using embedded voices

If you are using the local/offline/embedded natural voices, these features will be unavailable.

- `<silence>` tag (SAPI) / `<break>` tag (SSML)
- `<emph>` tag (SAPI) / `<emphasis>` tag (SSML)


## SAPI 5 TTS XML vs. SSML

The following table shows how this engine will translate SAPI 5 TTS XML into SSML.

| SAPI XML | SSML |
| - | - |
| `<volume level="80">` | `<prosody volume="-20%">` |
| `<rate speed="5">` | `<prosody rate="+100%">` |
| `<pitch middle="5">` | `<prosody pitch="+25%">` |
| `<emph>` | `<emphasize>` |
| `<spell>` | `<say-as interpret-as="characters">` |
| `<silence msec="500"/>` | `<break time="500ms"/>` |
| `<pron sym="h eh 1 l ow">` | `<phoneme alphabet="sapi" ph="h eh 1 l ow">` |
| `<bookmark mark="pos"/>` | `<bookmark mark="pos"/>` |
| `<partofsp part="noun">` | Not supported |
| `<context id="date_mdy">` | `<say-as interpret-as="date" format="mdy">` |
| `<context id="date_year">` | `<say-as interpret-as="date" format="y">` |
| `<context id="number_cardinal">` | `<say-as interpret-as="cardinal">` |
| `<context id="number_fraction">` | `<say-as interpret-as="fraction">` |
| `<context id="phone_number">` | `<say-as interpret-as="telephone">` |
| `<context id="other_format">` | `<say-as interpret-as="other_format">` |
| `<sapi>` | Handled by SAPI |
| `<voice required="Gender=Female">` | Handled by SAPI |
| `<lang langid="409">` | Handled by SAPI |

Note:
- Volume and rate can also be affected by the global settings set with `ISpVoice::SetVolume` and `ISpVoice::SetRate`.
- Other XML tags will be inserted into the SSML text as-is.

[1]: https://learn.microsoft.com/en-us/previous-versions/windows/desktop/ms717037(v=vs.85)
[2]: https://speech.microsoft.com/portal/voicegallery
[3]: https://learn.microsoft.com/azure/ai-services/speech-service/
[4]: https://learn.microsoft.com/azure/ai-services/speech-service/embedded-speech
[5]: https://learn.microsoft.com/azure/ai-services/speech-service/speech-synthesis-markup
[6]: https://learn.microsoft.com/azure/ai-services/speech-service/speech-sdk
[7]: https://learn.microsoft.com/en-us/previous-versions/windows/desktop/ms717077(v=vs.85)
[8]: https://learn.microsoft.com/azure/ai-services/speech-service/how-to-speech-synthesis-viseme