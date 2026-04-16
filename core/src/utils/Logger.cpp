#include "aauto/utils/Logger.hpp"

#include <cstdio>
#include <cstdarg>

namespace aauto {

namespace {

void default_log(LogLevel level, const char* tag, const char* fmt,
                 va_list args) {
    const char* level_str = "?";
    switch (level) {
        case LogLevel::Debug: level_str = "D"; break;
        case LogLevel::Info:  level_str = "I"; break;
        case LogLevel::Warn:  level_str = "W"; break;
        case LogLevel::Error: level_str = "E"; break;
    }
    std::fprintf(stderr, "[%s/%s] ", level_str, tag);
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

} // namespace aauto
