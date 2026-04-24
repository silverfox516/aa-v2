#pragma once

#include <cstdarg>
#include <string>

namespace aauto {

enum class LogLevel { Debug, Info, Warn, Error };

using LogFunction = void(*)(LogLevel level, const char* tag,
                            const char* fmt, va_list args);

void set_log_function(LogFunction fn);
void log_impl(LogLevel level, const char* tag, const char* fmt, ...);

/// Set a per-thread session tag (e.g., "s1:SM-N981N").
/// Shown in every log line from this thread. Pass empty to clear.
void set_session_tag(const std::string& tag);

/// Get the current thread's session tag.
std::string get_session_tag();

} // namespace aauto

// Usage: define LOG_TAG before including this header.
//   #define LOG_TAG "Session"
//   #include "aauto/utils/Logger.hpp"
//   AA_LOG_I("state=%d", state);

#define AA_LOG_D(...) ::aauto::log_impl(::aauto::LogLevel::Debug, LOG_TAG, __VA_ARGS__)
#define AA_LOG_I(...) ::aauto::log_impl(::aauto::LogLevel::Info,  LOG_TAG, __VA_ARGS__)
#define AA_LOG_W(...) ::aauto::log_impl(::aauto::LogLevel::Warn,  LOG_TAG, __VA_ARGS__)
#define AA_LOG_E(...) ::aauto::log_impl(::aauto::LogLevel::Error, LOG_TAG, __VA_ARGS__)
