#pragma once

#include <cstdint>

namespace aauto::session {

enum class SessionState : uint8_t {
    /// Transport assigned but not yet open.
    Idle,

    /// VERSION_REQUEST sent, awaiting VERSION_RESPONSE.
    VersionExchange,

    /// SSL/TLS handshake in progress (after VERSION exchange).
    SslHandshake,

    /// Fully operational. All messages delegated to services.
    /// ControlService handles ServiceDiscovery, ChannelOpen, Ping, etc.
    Running,

    /// Graceful shutdown. BYEBYE exchange in progress.
    Disconnecting,

    /// Terminal: clean disconnect.
    Disconnected,

    /// Terminal: error.
    Error
};

inline bool is_terminal(SessionState s) {
    return s == SessionState::Disconnected || s == SessionState::Error;
}

inline const char* to_string(SessionState s) {
    switch (s) {
        case SessionState::Idle:             return "Idle";
        case SessionState::VersionExchange:  return "VersionExchange";
        case SessionState::SslHandshake:     return "SslHandshake";
        case SessionState::Running:          return "Running";
        case SessionState::Disconnecting:    return "Disconnecting";
        case SessionState::Disconnected:     return "Disconnected";
        case SessionState::Error:            return "Error";
    }
    return "Unknown";
}

} // namespace aauto::session
