#pragma once

// The following keys are extracted from package MicrosoftWindows.Client.CBS_cw5n1h2txyewy
// They are not intended to be public and they may be changed in future versions
// DO NOT use them in production

// Extracted from SpeechSynthesizerExtension.dll
constexpr inline const wchar_t* MS_TTS_KEY
	= L"ZCjZ7nHDSLvf4gpELteM4AnzaWUjTpn7UkV7D@vvksl0w1SNgon6d1905WANbktDc9S39oaA4r29HJNayXvTq8fJsq";

// Extracted from LiveCaptionsBackend.dll
constexpr inline const wchar_t* MS_SR_KEY
	= L"XUw7C0rcZAIQvG837YP4F1KHz2RqYuQgtyXrcbFhsWFNGjG08HJElmPGesxNMbib0s8y39NEti3q3RwPNRbuDv75ejZbTa9yLcTAUixC";

constexpr inline const char* EDGE_VOICE_LIST_URL = "https://speech.platform.bing.com/consumer/speech/synthesize/readaloud/voices/list?trustedclienttoken=6A5AA1D4EAFF4E9FB37E23D68491D6F4";

constexpr inline const wchar_t* EDGE_WEBSOCKET_URL = L"wss://speech.platform.bing.com/consumer/speech/synthesize/readaloud/edge/v1?TrustedClientToken=6A5AA1D4EAFF4E9FB37E23D68491D6F4";

constexpr inline const char* AZURE_TTS_HOST_AFTER_REGION = ".tts.speech.microsoft.com";

constexpr inline const char* AZURE_VOICE_LIST_PATH = "/cognitiveservices/voices/list";

constexpr inline const char* AZURE_WEBSOCKET_PATH = "/cognitiveservices/websocket/v1";

constexpr inline const char* EDGE_HEADERS =
	"Origin: chrome-extension://jdiccldimpdaibmpdkjnbmckianbfold\0"
	"User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/103.0.5060.66 Safari/537.36 Edg/103.0.1264.44\0";