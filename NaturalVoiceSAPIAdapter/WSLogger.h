#pragma once
#include "Logger.h"
#include <websocketpp/logger/levels.hpp>

extern bool g_websocketppLogsEnabled;

namespace websocketpp {
namespace log {

// Custom logger for websocketpp that sends the logs to spdlog logger
template <channel_type_hint::value type, level static_channels>
class WSLogger
{
	level m_dynamic_channels = 0;
public:
	// We store the logger's type and static channel settings inside the template parameters
	// So in the constructors we do nothing
	WSLogger(channel_type_hint::value) noexcept
	{
	}
	WSLogger(level, channel_type_hint::value) noexcept
	{
	}

	void set_channels(level channels) noexcept
	{
		m_dynamic_channels |= (channels & static_channels);
	}
	void clear_channels(level channels) noexcept
	{
		m_dynamic_channels &= ~channels;
	}

	void write(level channel, std::string_view msg) noexcept
	{
		if (g_websocketppLogsEnabled && dynamic_test(channel))
		{
			try
			{
				// There will sometimes be system error messages (ANSI) in the log
				Log(map_channel_level(channel), "[websocketpp: {}] {}", channel_name(channel), AnsiToUTF8(msg));
			}
			catch (...)
			{
			}
		}
	}

	constexpr bool static_test(level channel) const noexcept
	{
		return (channel & static_channels) != 0;
	}
	bool dynamic_test(level channel) noexcept
	{
		return (channel & m_dynamic_channels) != 0;
	}

private:
	static constexpr spdlog::level::level_enum map_channel_level(level channel) noexcept
	{
		if constexpr (type == channel_type_hint::error)
		{
			switch (channel)
			{
			case elevel::devel:
				return spdlog::level::trace;
			case elevel::library:
				return spdlog::level::debug;
			case elevel::info:
				return spdlog::level::info;
			case elevel::warn:
				return spdlog::level::warn;
			case elevel::rerror:
				return spdlog::level::err;
			case elevel::fatal:
				return spdlog::level::critical;
			default:
				return spdlog::level::debug;
			}
		}
		else if constexpr (type == channel_type_hint::access)
		{
			switch (channel)
			{
			case alevel::devel:
				return spdlog::level::trace;
			case alevel::connect:
			case alevel::disconnect:
			case alevel::control:
			case alevel::frame_header:
			case alevel::frame_payload:
			case alevel::message_header:
			case alevel::message_payload:
			case alevel::endpoint:
			case alevel::debug_handshake:
			case alevel::debug_close:
			case alevel::http:
				return spdlog::level::debug;
			case alevel::fail:
				return spdlog::level::err;
			default:
				return spdlog::level::debug;
			}
		}
		else
		{
			static_assert(false, "invalid logger type");
		}
	}
	static constexpr const char* channel_name(level channel) noexcept
	{
		if constexpr (type == channel_type_hint::error)
			return elevel::channel_name(channel);
		else if constexpr (type == channel_type_hint::access)
			return alevel::channel_name(channel);
		else
			static_assert(false, "invalid logger type");
	}
};

}
}