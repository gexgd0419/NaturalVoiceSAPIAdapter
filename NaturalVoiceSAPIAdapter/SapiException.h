#pragma once
#include <system_error>
#include <sperror.h>
#include <minwindef.h>
#include <format>
#include <algorithm>

class sapi_category_impl : public std::error_category
{
private:
	// HRESULT is signed long, here use unsigned long to make sorting easier
	static constexpr std::pair<ULONG, LPCSTR> messages[] =
	{
		{ SP_END_OF_STREAM, "The operation has reached the end of stream." },
		{ SP_INSUFFICIENT_DATA, "SP_INSUFFICIENT_DATA" },
		{ SP_AUDIO_PAUSED, "This will be returned only on input (read) streams when the stream is paused.  Reads on paused streams will not block, and this return code indicates that all of the data has been removed from the stream." },
		{ SP_AUDIO_CONVERSION_ENABLED, "The operation was successful, but only with automatic stream format conversion." },
		{ SP_NO_HYPOTHESIS_AVAILABLE, "There is currently no hypothesis recognition available." },
		{ SP_ALREADY_IN_LEX, "The word, pronunciation, or POS pair being added is already in lexicon." },
		{ SP_LEX_NOTHING_TO_SYNC, "The client is currently synced with the lexicon." },
		{ SP_REQUEST_PENDING, "This success code indicates that an SR method called with the SPRIF_ASYNC flag is being processed.  When it has finished processing, an SPFEI_ASYNC_COMPLETED event will be generated." },
		{ SP_NO_PARSE_FOUND, "Parse path cannot be parsed given the currently active rules." },
		{ SP_UNSUPPORTED_ON_STREAM_INPUT, "The operation is not supported for stream input." },
		{ SP_WORD_EXISTS_WITHOUT_PRONUNCIATION, "The word exists but without pronunciation." },
		{ SP_RECOGNIZER_INACTIVE, "Operation could not be completed because the recognizer is inactive. It is inactive either because the recognition state is currently inactive or because no rules are active ." },
		{ SP_PARTIAL_PARSE_FOUND, "A grammar-ending parse has been found that does not use all available words." },
		{ SP_NO_RULE_ACTIVE, "An attempt to parse when no rule was active." },
		{ SP_STREAM_UNINITIALIZED, "An attempt to activate a rule/dictation/etc without calling SetInput first in the inproc case." },
		{ SP_AUDIO_STOPPED, "This can be returned from Read or Write calls audio streams when the stream is stopped." },
		{ SP_COMPLETE_BUT_EXTENDABLE, "Parse is valid but could be extendable (internal use only)" },
		{ SP_NO_RULES_TO_ACTIVATE, "The grammar does not have any root or top-level active rules to activate." },
		{ SP_NO_WORDENTRY_NOTIFICATION, "The engine does not need SAPI word entry handles for this grammar" },
		{ S_LIMIT_REACHED, "The word being normalized has generated more than the maximum number of allowed normalized results Indicates that returned list is not exhaustive, but contains as many alternatives as the engine is willing to provide." },
		{ S_NOTSUPPORTED, "We currently don't support this combination of function call + input" },
		{ SPERR_UNINITIALIZED, "The object has not been properly initialized." },
		{ SPERR_ALREADY_INITIALIZED, "The object has already been initialized." },
		{ SPERR_UNSUPPORTED_FORMAT, "The caller has specified an unsupported format." },
		{ SPERR_INVALID_FLAGS, "The caller has specified invalid flags for this operation." },
		{ SPERR_DEVICE_BUSY, "The wave device is busy." },
		{ SPERR_DEVICE_NOT_SUPPORTED, "The wave device is not supported." },
		{ SPERR_DEVICE_NOT_ENABLED, "The wave device is not enabled." },
		{ SPERR_NO_DRIVER, "There is no wave driver installed." },
		{ SPERR_FILE_MUST_BE_UNICODE, "The file must be Unicode." },
		{ SPERR_INVALID_PHRASE_ID, "The phrase ID specified does not exist or is out of range." },
		{ SPERR_BUFFER_TOO_SMALL, "The caller provided a buffer too small to return a result." },
		{ SPERR_FORMAT_NOT_SPECIFIED, "Caller did not specify a format prior to opening a stream." },
		{ SPERR_AUDIO_STOPPED, "This method is deprecated. Use SP_AUDIO_STOPPED instead." },
		{ SPERR_RULE_NOT_FOUND, "Invalid rule name passed to ActivateGrammar." },
		{ SPERR_TTS_ENGINE_EXCEPTION, "An exception was raised during a call to the current TTS driver." },
		{ SPERR_TTS_NLP_EXCEPTION, "An exception was raised during a call to an application sentence filter." },
		{ SPERR_ENGINE_BUSY, "In speech recognition, the current method can not be performed while a grammar rule is active." },
		{ SPERR_CANT_CREATE, "Can not create a new object instance for the specified object category." },
		{ SPERR_NOT_IN_LEX, "The word does not exist in the lexicon." },
		{ SPERR_LEX_VERY_OUT_OF_SYNC, "The client is excessively out of sync with the lexicon. Mismatches may not be incrementally sync'd." },
		{ SPERR_UNDEFINED_FORWARD_RULE_REF, "A rule reference in a grammar was made to a named rule that was never defined." },
		{ SPERR_EMPTY_RULE, "A non-dynamic grammar rule that has no body." },
		{ SPERR_GRAMMAR_COMPILER_INTERNAL_ERROR, "The grammar compiler failed due to an internal state error." },
		{ SPERR_RULE_NOT_DYNAMIC, "An attempt was made to modify a non-dynamic rule." },
		{ SPERR_DUPLICATE_RULE_NAME, "A rule name was duplicated." },
		{ SPERR_DUPLICATE_RESOURCE_NAME, "A resource name was duplicated for a given rule." },
		{ SPERR_TOO_MANY_GRAMMARS, "Too many grammars have been loaded." },
		{ SPERR_CIRCULAR_REFERENCE, "Circular reference in import rules of grammars." },
		{ SPERR_INVALID_IMPORT, "A rule reference to an imported grammar that could not be resolved." },
		{ SPERR_INVALID_WAV_FILE, "The format of the WAV file is not supported." },
		{ SPERR_ALL_WORDS_OPTIONAL, "A grammar rule was defined with a null path through the rule.  That is, it is possible to satisfy the rule conditions with no words." },
		{ SPERR_INSTANCE_CHANGE_INVALID, "It is not possible to change the current engine or input." },
		{ SPERR_RULE_NAME_ID_CONFLICT, "A rule exists with matching IDs (names) but different names (IDs).  " },
		{ SPERR_NO_RULES, "A grammar contains no top-level, dynamic, or exported rules.  There is no possible way to activate or otherwise use any rule in this grammar." },
		{ SPERR_CIRCULAR_RULE_REF, "Rule 'A' refers to a second rule 'B' which, in turn, refers to rule 'A'. " },
		{ SPERR_INVALID_HANDLE, "Parse path cannot be parsed given the currently active rules." },
		{ SPERR_REMOTE_CALL_TIMED_OUT, "A marshaled remote call failed to respond." },
		{ SPERR_AUDIO_BUFFER_OVERFLOW, "This will only be returned on input (read) streams when the stream is paused because the SR driver has not retrieved data recently." },
		{ SPERR_NO_AUDIO_DATA, "The result does not contain any audio, nor does the portion of the element chain of the result contain any audio." },
		{ SPERR_DEAD_ALTERNATE, "This alternate is no longer a valid alternate to the result it was obtained from. Returned from ISpPhraseAlt methods." },
		{ SPERR_HIGH_LOW_CONFIDENCE, "The result does not contain any audio, nor does the portion of the element chain of the result contain any audio.  Returned from ISpResult::GetAudio and ISpResult::SpeakAudio." },
		{ SPERR_INVALID_FORMAT_STRING, "The XML format string for this RULEREF is invalid, e.g. not a GUID or REFCLSID." },
		{ SPERR_APPLEX_READ_ONLY, "The operation is invalid for all but newly created application lexicons." },
		{ SPERR_NO_TERMINATING_RULE_PATH, "SPERR_NO_TERMINATING_RULE_PATH" },
		{ SPERR_STREAM_CLOSED, "An operation was attempted on a stream object that has been closed." },
		{ SPERR_NO_MORE_ITEMS, "When enumerating items, the requested index is greater than the count of items." },
		{ SPERR_NOT_FOUND, "The requested data item (data key, value, etc.) was not found." },
		{ SPERR_INVALID_AUDIO_STATE, "Audio state passed to SetState() is invalid." },
		{ SPERR_GENERIC_MMSYS_ERROR, "A generic MMSYS error not caught by _MMRESULT_TO_HRESULT." },
		{ SPERR_MARSHALER_EXCEPTION, "An exception was raised during a call to the marshaling code." },
		{ SPERR_NOT_DYNAMIC_GRAMMAR, "Attempt was made to manipulate a non-dynamic grammar." },
		{ SPERR_AMBIGUOUS_PROPERTY, "Cannot add ambiguous property." },
		{ SPERR_INVALID_REGISTRY_KEY, "The key specified is invalid." },
		{ SPERR_INVALID_TOKEN_ID, "The token specified is invalid." },
		{ SPERR_XML_BAD_SYNTAX, "The xml parser failed due to bad syntax." },
		{ SPERR_XML_RESOURCE_NOT_FOUND, "The xml parser failed to load a required resource (e.g., voice, phoneconverter, etc.)." },
		{ SPERR_TOKEN_IN_USE, "Attempted to remove registry data from a token that is already in use elsewhere." },
		{ SPERR_TOKEN_DELETED, "Attempted to perform an action on an object token that has had associated registry key deleted." },
		{ SPERR_MULTI_LINGUAL_NOT_SUPPORTED, "The selected voice was registered as multi-lingual. SAPI does not support multi-lingual registration. " },
		{ SPERR_EXPORT_DYNAMIC_RULE, "Exported rules cannot refer directly or indirectly to a dynamic rule." },
		{ SPERR_STGF_ERROR, "Error parsing the SAPI Text Grammar Format (XML grammar)." },
		{ SPERR_WORDFORMAT_ERROR, "Incorrect word format, probably due to incorrect pronunciation string." },
		{ SPERR_STREAM_NOT_ACTIVE, "Methods associated with active audio stream cannot be called unless stream is active." },
		{ SPERR_ENGINE_RESPONSE_INVALID, "Arguments or data supplied by the engine are in an invalid format or are inconsistent." },
		{ SPERR_SR_ENGINE_EXCEPTION, "An exception was raised during a call to the current SR engine." },
		{ SPERR_STREAM_POS_INVALID, "Stream position information supplied from engine is inconsistent." },
		{ SPERR_REMOTE_CALL_ON_WRONG_THREAD, "When making a remote call to the server, the call was made on the wrong thread." },
		{ SPERR_REMOTE_PROCESS_TERMINATED, "The remote process terminated unexpectedly." },
		{ SPERR_REMOTE_PROCESS_ALREADY_RUNNING, "The remote process is already running; it cannot be started a second time." },
		{ SPERR_LANGID_MISMATCH, "An attempt to load a CFG grammar with a LANGID different than other loaded grammars." },
		{ SPERR_NOT_TOPLEVEL_RULE, "An attempt to deactivate or activate a non-toplevel rule." },
		{ SPERR_LEX_REQUIRES_COOKIE, "An attempt to ask a container lexicon for all words at once." },
		{ SPERR_UNSUPPORTED_LANG, "The requested language is not supported." },
		{ SPERR_VOICE_PAUSED, "The operation cannot be performed because the voice is currently paused." },
		{ SPERR_AUDIO_BUFFER_UNDERFLOW, "This will only be returned on input (read) streams when the real time audio device stops returning data for a long period of time." },
		{ SPERR_AUDIO_STOPPED_UNEXPECTEDLY, "An audio device stopped returning data from the Read() method even though it was in the run state.  This error is only returned in the END_SR_STREAM event." },
		{ SPERR_NO_WORD_PRONUNCIATION, "The SR engine is unable to add this word to a grammar. The application may need to supply  an explicit pronunciation for this word." },
		{ SPERR_ALTERNATES_WOULD_BE_INCONSISTENT, "An attempt to call ScaleAudio on a recognition result having previously called GetAlternates. Allowing the call to succeed would result in the previously created alternates located in incorrect audio stream positions." },
		{ SPERR_NOT_SUPPORTED_FOR_SHARED_RECOGNIZER, "The method called is not supported for the shared recognizer. For example, ISpRecognizer::GetInputStream()." },
		{ SPERR_TIMEOUT, "A task could not complete because the SR engine had timed out." },
		{ SPERR_REENTER_SYNCHRONIZE, "A SR engine called synchronize while inside of a synchronize call." },
		{ SPERR_STATE_WITH_NO_ARCS, "The grammar contains a node no arcs." },
		{ SPERR_NOT_ACTIVE_SESSION, "Neither audio output and input is supported for non-active console sessions." },
		{ SPERR_ALREADY_DELETED, "The object is a stale reference and is invalid to use." },
		{ SPERR_RECOXML_GENERATION_FAIL, "The Recognition Parse Tree couldn't be genrated. For example, that the rule name begins with a digit. XML parser doesn't allow element name beginning with a digit." },
		{ SPERR_SML_GENERATION_FAIL, "The SML couldn't be genrated. For example, the transformation xslt template is not well formed." },
		{ SPERR_NOT_PROMPT_VOICE, "The current voice is not a prompt voice, so the ISpPromptVoice functions don't work." },
		{ SPERR_ROOTRULE_ALREADY_DEFINED, "There is already a root rule for this grammar Defining another root rule will fail." },
		{ SPERR_SCRIPT_DISALLOWED, "Support for embedded script not supported because browser security settings have disabled it" },
		{ SPERR_REMOTE_CALL_TIMED_OUT_START, "A time out occurred starting the sapi server" },
		{ SPERR_REMOTE_CALL_TIMED_OUT_CONNECT, "A timeout occurred obtaining the lock for starting or connecting to sapi server " },
		{ SPERR_SECMGR_CHANGE_NOT_ALLOWED, "When there is a cfg grammar loaded, we don't allow changing the security manager" },
		{ SPERR_FAILED_TO_DELETE_FILE, "Tried and failed to delete an existing file." },
		{ SPERR_SHARED_ENGINE_DISABLED, "The user has chosen to disable speech from running on the machine, or the  system is not set up to run speech {e.g. initial setup and tutorial has not been run}." },
		{ SPERR_RECOGNIZER_NOT_FOUND, "No recognizer is installed." },
		{ SPERR_AUDIO_NOT_FOUND, "No audio device is installed." },
		{ SPERR_NO_VOWEL, "No Vowel in a word" },
		{ SPERR_UNSUPPORTED_PHONEME, "Unknown phoneme" },
		{ SPERR_WORD_NEEDS_NORMALIZATION, "The word passed to the GetPronunciations interface needs normalizing first" },
		{ SPERR_CANNOT_NORMALIZE, "The word passed to the normalize interface cannot be normalized" },
		{ SPERR_TOPIC_NOT_ADAPTABLE, "This topic is not adaptable" },
		{ SPERR_PHONEME_CONVERSION, "Cannot convert the phonemes to the specified phonetic alphabet." },
		{ SPERR_NOT_SUPPORTED_FOR_INPROC_RECOGNIZER, "The method called is not supported for the in-process recognizer. For example: SetTextFeedback" },
		{ SPERR_OVERLOAD, "The operation cannot be carried out due to overload and should be attempted again." },
		{ SPERR_LEX_INVALID_DATA, "The lexicon data is invalid or corrupted." },
		{ SPERR_CFG_INVALID_DATA, "The data in the CFG grammar is invalid or corrupted" },
		{ SPERR_LEX_UNEXPECTED_FORMAT, "The lexicon is not in the expected format." },
		{ SPERR_STRING_TOO_LONG, "The string is too long." },
		{ SPERR_STRING_EMPTY, "The string cannot be empty." },
		{ SPERR_NON_WORD_TRANSITION, "One of the parse transitions is not a word transition." },
		{ SPERR_SISR_ATTRIBUTES_NOT_ALLOWED, "Attributes are not allowed at the top level." },
		{ SPERR_SISR_MIXED_NOT_ALLOWED, "Mixed content is not allowed at the top level." },
		{ SPERR_VOICE_NOT_FOUND, "NO given voice is found" },
	};

	static_assert(std::is_sorted(std::begin(messages), std::end(messages),
		[](const auto& a, const auto& b) { return a.first < b.first; }));

public:
	const char* name() const noexcept override { return "sapi"; }
	std::string message(int ev) const override
	{
		auto code = static_cast<ULONG>(ev);
		auto it = std::lower_bound(
			std::begin(messages), std::end(messages), code,
			[](const auto& a, auto b) { return a.first < b; });

		if (it == std::end(messages) || it->first != code)
			return std::system_category().message(ev);  // fall back to system HRESULT message

		return it->second;
	}
};

inline const sapi_category_impl& sapi_category()
{
	static constexpr sapi_category_impl cat;
	return cat;
}

inline void CheckSapiHr(HRESULT hr)
{
	if (FAILED(hr)) throw std::system_error(hr, sapi_category());
}