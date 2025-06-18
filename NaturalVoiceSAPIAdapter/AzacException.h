#pragma once

// Azure SDK throws an AZACHR (which is just an integer) if the AZACHR comes without an error message.
// Here we convert such integers into std::system_error's,
// by replacing the __AZAC_THROW_HR macro defined in azac_debug.h.
// This header should be included before any other Azure SDK headers.
// See also: https://devblogs.microsoft.com/oldnewthing/20240119-00/?p=109292

#include <system_error>
#include <azac_error.h>
#define AZAC_SUPPRESS_DEBUG_INCLUDE_FROM_COMMON  // postpone including azac_debug.h
#include <azac_api_c_error.h>
#include <format>
#include <algorithm>

class azac_category_impl : public std::error_category
{
private:
	static constexpr std::pair<AZACHR, LPCSTR> messages[] =
	{
		{ AZAC_ERR_UNINITIALIZED, "The object has not been properly initialized." },
		{ AZAC_ERR_ALREADY_INITIALIZED, "The object has already been initialized." },
		{ AZAC_ERR_UNHANDLED_EXCEPTION, "An unhandled exception was detected." },
		{ AZAC_ERR_NOT_FOUND, "The object or property was not found." },
		{ AZAC_ERR_INVALID_ARG, "One or more arguments are not valid." },
		{ AZAC_ERR_TIMEOUT, "The specified timeout value has elapsed." },
		{ AZAC_ERR_ALREADY_IN_PROGRESS, "The asynchronous operation is already in progress." },
		{ AZAC_ERR_FILE_OPEN_FAILED, "The attempt to open the file failed." },
		{ AZAC_ERR_UNEXPECTED_EOF, "The end of the file was reached unexpectedly." },
		{ AZAC_ERR_INVALID_HEADER, "Invalid audio header encountered." },
		{ AZAC_ERR_AUDIO_IS_PUMPING, "The requested operation cannot be performed while audio is pumping" },
		{ AZAC_ERR_UNSUPPORTED_FORMAT, "Unsupported audio format." },
		{ AZAC_ERR_ABORT, "Operation aborted." },
		{ AZAC_ERR_MIC_NOT_AVAILABLE, "Microphone is not available." },
		{ AZAC_ERR_INVALID_STATE, "An invalid state was encountered." },
		{ AZAC_ERR_UUID_CREATE_FAILED, "Attempting to create a UUID failed." },
		{ AZAC_ERR_SETFORMAT_UNEXPECTED_STATE_TRANSITION, "An unexpected session state transition was encountered when setting the session audio format." },
		{ AZAC_ERR_PROCESS_AUDIO_INVALID_STATE, "An unexpected session state was encountered in while processing audio." },
		{ AZAC_ERR_START_RECOGNIZING_INVALID_STATE_TRANSITION, "An unexpected state transition was encountered while attempting to start recognizing." },
		{ AZAC_ERR_UNEXPECTED_CREATE_OBJECT_FAILURE, "An unexpected error was encountered when trying to create an internal object." },
		{ AZAC_ERR_MIC_ERROR, "An error in the audio-capturing system." },
		{ AZAC_ERR_NO_AUDIO_INPUT, "The requested operation cannot be performed; there is no audio input." },
		{ AZAC_ERR_UNEXPECTED_USP_SITE_FAILURE, "An unexpected error was encountered when trying to access the USP site." },
		{ AZAC_ERR_UNEXPECTED_LU_SITE_FAILURE, "An unexpected error was encountered when trying to access the LU site." },
		{ AZAC_ERR_BUFFER_TOO_SMALL, "The buffer is too small." },
		{ AZAC_ERR_OUT_OF_MEMORY, "A method failed to allocate memory." },
		{ AZAC_ERR_RUNTIME_ERROR, "An unexpected runtime error occurred." },
		{ AZAC_ERR_INVALID_URL, "The url specified is invalid." },
		{ AZAC_ERR_INVALID_REGION, "The region specified is invalid or missing." },
		{ AZAC_ERR_SWITCH_MODE_NOT_ALLOWED, "Switch between single shot and continuous recognition is not supported." },
		{ AZAC_ERR_CHANGE_CONNECTION_STATUS_NOT_ALLOWED, "Changing connection status is not supported in the current recognition state." },
		{ AZAC_ERR_EXPLICIT_CONNECTION_NOT_SUPPORTED_BY_RECOGNIZER, "Explicit connection management is not supported by the specified recognizer." },
		{ AZAC_ERR_INVALID_HANDLE, "The handle is invalid." },
		{ AZAC_ERR_INVALID_RECOGNIZER, "The recognizer is invalid." },
		{ AZAC_ERR_OUT_OF_RANGE, "The value is out of range." },
		{ AZAC_ERR_EXTENSION_LIBRARY_NOT_FOUND, "Extension library not found." },
		{ AZAC_ERR_UNEXPECTED_TTS_ENGINE_SITE_FAILURE, "An unexpected error was encountered when trying to access the TTS engine site." },
		{ AZAC_ERR_UNEXPECTED_AUDIO_OUTPUT_FAILURE, "An unexpected error was encountered when trying to access the audio output stream." },
		{ AZAC_ERR_GSTREAMER_INTERNAL_ERROR, "Gstreamer internal error." },
		{ AZAC_ERR_CONTAINER_FORMAT_NOT_SUPPORTED_ERROR, "Compressed container format not supported." },
		{ AZAC_ERR_GSTREAMER_NOT_FOUND_ERROR, "Codec extension or gstreamer not found." },
		{ AZAC_ERR_INVALID_LANGUAGE, "The language specified is missing." },
		{ AZAC_ERR_UNSUPPORTED_API_ERROR, "The API is not applicable." },
		{ AZAC_ERR_RINGBUFFER_DATA_UNAVAILABLE, "The ring buffer is unavailable." },
		{ AZAC_ERR_UNEXPECTED_CONVERSATION_SITE_FAILURE, "An unexpected error was encountered when trying to access the Conversation site." },
		{ AZAC_ERR_UNEXPECTED_CONVERSATION_TRANSLATOR_SITE_FAILURE, "An unexpected error was encountered when trying to access the Conversation site." },
		{ AZAC_ERR_CANCELED, "An asynchronous operation was canceled before it was executed." },
		{ AZAC_ERR_COMPRESS_AUDIO_CODEC_INITIFAILED, "Codec for compression could not be initialized." },
		{ AZAC_ERR_DATA_NOT_AVAILABLE, "Data not available." },
		{ AZAC_ERR_INVALID_RESULT_REASON, "Invalid result reason." },
		{ AZAC_ERR_UNEXPECTED_RNNT_SITE_FAILURE, "An unexpected error was encountered when trying to access the RNN-T site." },
		{ AZAC_ERR_NETWORK_SEND_FAILED, "Sending of a network message failed." },
		{ AZAC_ERR_AUDIO_SYS_LIBRARY_NOT_FOUND, "Audio extension library not found." },
		{ AZAC_ERR_LOUDSPEAKER_ERROR, "An error in the audio-rendering system." },
		{ AZAC_ERR_VISION_SITE_FAILURE, "An unexpected error was encountered when trying to access the Vision site." },
		{ AZAC_ERR_MEDIA_INVALID_STREAM, "Stream number provided was invalid in the current context." },
		{ AZAC_ERR_MEDIA_INVALID_OFFSET, "Offset required is invalid in the current context." },
		{ AZAC_ERR_MEDIA_NO_MORE_DATA, "No more data is available in source." },
		{ AZAC_ERR_MEDIA_NOT_STARTED, "Source has not been started." },
		{ AZAC_ERR_MEDIA_ALREADY_STARTED, "Source has already been started." },
		{ AZAC_ERR_MEDIA_DEVICE_CREATION_FAILED, "Media device creation failed." },
		{ AZAC_ERR_MEDIA_NO_DEVICE_AVAILABLE, "No devices of the selected category are available." },
		{ AZAC_ERR_VAD_CANNOT_BE_USED_WITH_KEYWORD_RECOGNIZER, "Enabled Voice Activity Detection while using keyword recognition is not allowed." },
		{ AZAC_ERR_COULD_NOT_CREATE_ENGINE_ADAPTER, "The specified RecoEngineAdapter could not be created." },
		{ AZAC_ERR_INPUT_FILE_SIZE_IS_ZERO_BYTES, "The input file has a size of 0 bytes." },
		{ AZAC_ERR_FAILED_TO_OPEN_INPUT_FILE_FOR_READING, "Cannot open the input media file for reading. Does it exist?" },
		{ AZAC_ERR_FAILED_TO_READ_FROM_INPUT_FILE, "Failed to read from the input media file." },
		{ AZAC_ERR_INPUT_FILE_TOO_LARGE, "Input media file is too large." },
		{ AZAC_ERR_UNSUPPORTED_URL_PROTOCOL, "The input URL is unsupported. It should start with `http://`, `https://` or `rtsp://`." },
		{ AZAC_ERR_EMPTY_NULLABLE, "The Nullable value is empty. Check HasValue() before getting the value." },
		{ AZAC_ERR_INVALID_MODEL_VERSION_FORMAT, R"(The given model version string is not in the expected format. The format is specified by the regular expression `^(latest|\d{4}-\d{2}-\d{2})(-preview)?$`.)" },
		{ AZAC_ERR_NETWORK_MALFORMED, "Malformed network message" },
		{ AZAC_ERR_NETWORK_PROTOCOL_VIOLATION, "Unexpected message received" },
		{ AZAC_ERR_MAS_LIBRARY_NOT_FOUND, "MAS extension library not found." },
		{ AZAC_ERR_NOT_IMPL, "The function is not implemented." },
	};

	static_assert(std::is_sorted(std::begin(messages), std::end(messages),
		[](const auto& a, const auto& b) { return a.first < b.first; }));

public:
	const char* name() const noexcept override { return "azac"; }
	std::string message(int ev) const override
	{
		auto code = static_cast<AZACHR>(ev);
		auto it = std::lower_bound(
			std::begin(messages), std::end(messages), code,
			[](const auto& a, auto b) { return a.first < b; });

		if (it == std::end(messages) || it->first != code)
			return std::format("AZACHR: {:#x}", ev);

		return it->second;
	}
};

inline const azac_category_impl& azac_category()
{
	static constexpr azac_category_impl cat;
	return cat;
}

inline void ThrowAzacException(AZACHR hr)
{
	auto handle = reinterpret_cast<AZAC_HANDLE>(hr);
	auto error = error_get_error_code(handle);
	std::string errorMsg;

	if (error != AZAC_ERR_NONE)
	{
		auto what = error_get_message(handle);
		if (what)
		{
			try
			{
				errorMsg.assign(what);
			}
			catch (...)
			{ }
		}
		error_release(handle);
	}

	throw std::system_error(static_cast<int>(hr), azac_category(), errorMsg);
}

#define __AZAC_THROW_HR(hr) ThrowAzacException(hr)
#include <azac_debug.h>  // include this AFTER defining __AZAC_THROW_HR