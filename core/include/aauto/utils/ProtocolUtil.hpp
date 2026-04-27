#pragma once

#include "aauto/utils/ProtocolConstants.hpp"

namespace aauto {

/// Human-readable name for a control channel message type.
inline const char* to_string(ControlMessageType t) {
    switch (t) {
        case ControlMessageType::VersionRequest:           return "VERSION_REQ";
        case ControlMessageType::VersionResponse:          return "VERSION_RESP";
        case ControlMessageType::EncapsulatedSsl:          return "SSL_HANDSHAKE";
        case ControlMessageType::AuthComplete:             return "AUTH_COMPLETE";
        case ControlMessageType::ServiceDiscoveryRequest:  return "SERVICE_DISCOVERY_REQ";
        case ControlMessageType::ServiceDiscoveryResponse: return "SERVICE_DISCOVERY_RESP";
        case ControlMessageType::ChannelOpenRequest:       return "CHANNEL_OPEN_REQ";
        case ControlMessageType::ChannelOpenResponse:      return "CHANNEL_OPEN_RESP";
        case ControlMessageType::ChannelCloseNotification: return "CHANNEL_CLOSE";
        case ControlMessageType::PingRequest:              return "PING_REQ";
        case ControlMessageType::PingResponse:             return "PING_RESP";
        case ControlMessageType::NavFocusRequest:          return "NAV_FOCUS_REQ";
        case ControlMessageType::NavFocusNotification:     return "NAV_FOCUS_NOTIF";
        case ControlMessageType::ByeByeRequest:            return "BYEBYE_REQ";
        case ControlMessageType::ByeByeResponse:           return "BYEBYE_RESP";
        case ControlMessageType::VoiceSessionNotification: return "VOICE_SESSION";
        case ControlMessageType::AudioFocusRequest:        return "AUDIO_FOCUS_REQ";
        case ControlMessageType::AudioFocusNotification:   return "AUDIO_FOCUS_NOTIF";
        case ControlMessageType::BatteryStatusNotification:return "BATTERY_STATUS";
        default:                                           return nullptr;
    }
}

/// Human-readable name for a media message type.
inline const char* to_string(MediaMessageType t) {
    switch (t) {
        case MediaMessageType::Data:                   return "MEDIA_DATA";
        case MediaMessageType::CodecConfig:            return "CODEC_CONFIG";
        case MediaMessageType::Setup:                  return "MEDIA_SETUP";
        case MediaMessageType::Start:                  return "MEDIA_START";
        case MediaMessageType::Stop:                   return "MEDIA_STOP";
        case MediaMessageType::Config:                 return "MEDIA_CONFIG";
        case MediaMessageType::Ack:                    return "MEDIA_ACK";
        case MediaMessageType::MicrophoneRequest:      return "MIC_REQ";
        case MediaMessageType::MicrophoneResponse:     return "MIC_RESP";
        case MediaMessageType::VideoFocusRequest:      return "VIDEO_FOCUS_REQ";
        case MediaMessageType::VideoFocusNotification: return "VIDEO_FOCUS_NOTIF";
        default:                                       return nullptr;
    }
}

/// Format a message type as a readable string.
/// Tries control types first, then media types, falls back to hex.
/// Returns pointer to a thread-local buffer (valid until next call).
inline const char* msg_type_name(uint16_t type) {
    auto ct = to_string(static_cast<ControlMessageType>(type));
    if (ct) return ct;
    auto mt = to_string(static_cast<MediaMessageType>(type));
    if (mt) return mt;

    static thread_local char buf[16];
    snprintf(buf, sizeof(buf), "0x%04x", type);
    return buf;
}

/// Human-readable channel name.
/// Returns pointer to a thread-local buffer.
inline const char* channel_name(uint8_t ch) {
    switch (ch) {
        case 0: return "control";
        case 1: return "video";
        case 2: return "audio.media";
        case 3: return "audio.guidance";
        case 4: return "audio.system";
        case 5: return "input";
        case 6: return "sensor";
        case 7:  return "microphone";
        case 8:  return "nav.status";
        case 9:  return "phone.status";
        case 10: return "media.playback";
        case 11: return "notification";
        case 12: return "media.browser";
        case 13: return "bluetooth";
        case 14: return "vendor.ext";
        default:
            static thread_local char buf[8];
            snprintf(buf, sizeof(buf), "ch%u", ch);
            return buf;
    }
}

} // namespace aauto
