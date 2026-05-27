#pragma once

#include <chrono>
#include <cstdint>
#include <string>

enum class ELogLevel : std::uint8_t
{
	Trace,
	Debug,
	Info,
	Warning,
	Error,
	Critical,
};

enum class ELogSource : std::uint8_t
{
	System,
	External,
};

using LogLevelMask = std::uint32_t;

struct LogEntry
{
	std::uint64_t Sequence = 0;
	ELogLevel Level = ELogLevel::Info;
	ELogSource Source = ELogSource::System;
	std::chrono::system_clock::time_point Time;
	std::string Message;
	std::string FormattedMessage;
};

constexpr LogLevelMask LOG_LEVEL_MASK_TRACE = 1u << static_cast<std::uint32_t>(ELogLevel::Trace);
constexpr LogLevelMask LOG_LEVEL_MASK_DEBUG = 1u << static_cast<std::uint32_t>(ELogLevel::Debug);
constexpr LogLevelMask LOG_LEVEL_MASK_INFO = 1u << static_cast<std::uint32_t>(ELogLevel::Info);
constexpr LogLevelMask LOG_LEVEL_MASK_WARNING = 1u << static_cast<std::uint32_t>(ELogLevel::Warning);
constexpr LogLevelMask LOG_LEVEL_MASK_ERROR = 1u << static_cast<std::uint32_t>(ELogLevel::Error);
constexpr LogLevelMask LOG_LEVEL_MASK_CRITICAL = 1u << static_cast<std::uint32_t>(ELogLevel::Critical);
constexpr LogLevelMask LOG_LEVEL_MASK_ALL =
	LOG_LEVEL_MASK_TRACE |
	LOG_LEVEL_MASK_DEBUG |
	LOG_LEVEL_MASK_INFO |
	LOG_LEVEL_MASK_WARNING |
	LOG_LEVEL_MASK_ERROR |
	LOG_LEVEL_MASK_CRITICAL;
