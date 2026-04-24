#include "aauto/utils/Logger.hpp"

#include <cstdio>
#include <cstdarg>
#include <chrono>
#include <ctime>
#include <string>

namespace aauto {

namespace {

thread_local std::string g_session_tag;

void default_log(LogLevel level, const char* tag, const char* fmt,
                 va_list args) {
    const char* level_str = "?";
    switch (level) {
        case LogLevel::Debug: level_str = "D"; break;
        case LogLevel::Info:  level_str = "I"; break;
        case LogLevel::Warn:  level_str = "W"; break;
        case LogLevel::Error: level_str = "E"; break;
    }

    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count() % 1000;
    struct tm tm_buf;
    localtime_r(&time_t_now, &tm_buf);

    if (g_session_tag.empty()) {
        std::fprintf(stderr, "%02d:%02d:%02d.%03d [%s/%s] ",
                     tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec,
                     static_cast<int>(ms), level_str, tag);
    } else {
        std::fprintf(stderr, "%02d:%02d:%02d.%03d %s [%s/%s] ",
                     tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec,
                     static_cast<int>(ms), g_session_tag.c_str(),
                     level_str, tag);
    }
    std::vfprintf(stderr, fmt, args);
    std::fputc('\n', stderr);
}

LogFunction g_log_fn = default_log;

} // anonymous namespace

void set_log_function(LogFunction fn) {
    g_log_fn = fn ? fn : default_log;
}

void log_impl(LogLevel level, const char* tag, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    g_log_fn(level, tag, fmt, args);
    va_end(args);
}

void set_session_tag(const std::string& tag) {
    g_session_tag = tag;
}

std::string get_session_tag() {
    return g_session_tag;
}

} // namespace aauto
