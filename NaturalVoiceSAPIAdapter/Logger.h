#pragma once
#include <format>
#include <spdlog/spdlog.h>
#include <sphelper.h>
#include "StrUtils.h"

extern spdlog::logger logger;

template <class Type, template <class...> class Template>
constexpr bool is_specialization_v = false;
template <template <class...> class Template, class... Types>
constexpr bool is_specialization_v<Template<Types...>, Template> = true;

// spdlog assumes that everything is UTF8.
// Unfortunately on Windows, many strings are wide,
// and std::exception::what() often returns char* but ANSI.
// Here we convert their encodings to UTF8.
template <class ArgT>
constexpr decltype(auto) FormatLogArg(ArgT&& arg)
{
	using T = std::remove_cvref_t<ArgT>;
	if constexpr (std::is_convertible_v<T, std::wstring_view>)
		return WStringToUTF8(std::forward<ArgT>(arg));
	else if constexpr (std::is_base_of_v<std::system_error, T>)
		return AnsiToUTF8(std::format("({}:{}) {}", arg.code().category().name(), (unsigned)arg.code().value(), arg.what()));
	else if constexpr (std::is_base_of_v<std::exception, T>)
		return AnsiToUTF8(arg.what());
	else if constexpr (std::is_convertible_v<T, ISpObjectToken*>)
	{
		CSpDynamicString id;
		if (SUCCEEDED(arg->GetId(&id)))
			return WStringToUTF8(id.m_psz);
		else
			return std::string("(unknown)");
	}
	else if constexpr (is_specialization_v<T, std::unique_ptr> || is_specialization_v<T, std::shared_ptr>)
		return (void*)arg.get();
	else if constexpr (is_specialization_v<T, std::weak_ptr>)
		return (void*)arg.lock().get();
	else
		return std::forward<ArgT>(arg);
}

template <class T>
T DeductionHelper(T&&) noexcept
{ static_assert(false, "used for deducing template argument type; should not be called"); }

// Each template argument should follow the deduction rule.
// For example, 'DeductionHelper(1);' instantiates 'DeductionHelper<int>(int&&)', where T = int, not int&&.
// FormatLogArg can return r-value references by forwarding, so we use the helper function to convert the type.
// Otherwise, errors like 'cannot convert std::format_string<int&&> to std::format_string<int>' can occur.
template <class... Args>
using FormatStringT = spdlog::format_string_t<decltype(DeductionHelper(FormatLogArg(std::declval<Args>())))...>;

// Make sure all logging functions won't throw, because they will be used in catch clauses

template <class... Args>
void Log(spdlog::level::level_enum lvl, FormatStringT<Args...> fmt, Args&&... args) noexcept
{
	if (!logger.should_log(lvl))
		return;

	try
	{
		logger.log(lvl, spdlog::fmt_lib::format(fmt, FormatLogArg(std::forward<Args>(args))...));
	}
	catch (const std::exception& ex)
	{
		try { logger.error(ex.what()); }
		catch (...) {}
	}
	catch (...)
	{
		try { logger.error("Unknown logging error"); }
		catch (...) {}
	}
}

inline void Log(spdlog::level::level_enum lvl, spdlog::string_view_t msg) noexcept
{
	if (!logger.should_log(lvl))
		return;

	try
	{
		logger.log(lvl, msg);
	}
	catch (const std::exception& ex)
	{
		try { logger.error(ex.what()); }
		catch (...) {}
	}
	catch (...)
	{
		try { logger.error("Unknown logging error"); }
		catch (...) {}
	}
}

template <class... Args>
inline void LogCritical(FormatStringT<Args...> fmt, Args&&... args) noexcept
{
	Log(spdlog::level::critical, fmt, std::forward<Args>(args)...);
}

template <class... Args>
inline void LogErr(FormatStringT<Args...> fmt, Args&&... args) noexcept
{
	Log(spdlog::level::err, fmt, std::forward<Args>(args)...);
}

template <class... Args>
inline void LogWarn(FormatStringT<Args...> fmt, Args&&... args) noexcept
{
	Log(spdlog::level::warn, fmt, std::forward<Args>(args)...);
}

template <class... Args>
inline void LogInfo(FormatStringT<Args...> fmt, Args&&... args) noexcept
{
	Log(spdlog::level::info, fmt, std::forward<Args>(args)...);
}

template <class... Args>
inline void LogDebug(FormatStringT<Args...> fmt, Args&&... args) noexcept
{
	Log(spdlog::level::debug, fmt, std::forward<Args>(args)...);
}

template <class... Args>
inline void LogTrace(FormatStringT<Args...> fmt, Args&&... args) noexcept
{
	Log(spdlog::level::trace, fmt, std::forward<Args>(args)...);
}

inline void LogCritical(spdlog::string_view_t msg) noexcept
{
	Log(spdlog::level::critical, msg);
}

inline void LogErr(spdlog::string_view_t msg) noexcept
{
	Log(spdlog::level::err, msg);
}

inline void LogWarn(spdlog::string_view_t msg) noexcept
{
	Log(spdlog::level::warn, msg);
}

inline void LogInfo(spdlog::string_view_t msg) noexcept
{
	Log(spdlog::level::info, msg);
}

inline void LogDebug(spdlog::string_view_t msg) noexcept
{
	Log(spdlog::level::debug, msg);
}

inline void LogTrace(spdlog::string_view_t msg) noexcept
{
	Log(spdlog::level::trace, msg);
}

class ScopeTracer
{
	spdlog::string_view_t exitMsg;
public:
	ScopeTracer(spdlog::string_view_t enterMsg, spdlog::string_view_t exitMsg) noexcept
		: exitMsg(exitMsg)
	{
		LogTrace(enterMsg);
	}
	~ScopeTracer()
	{
		LogTrace(exitMsg);
	}
};