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

/// Channel-specific message name tables. Each AAP channel defines its
/// own message-id namespace; the same numeric id (e.g., 32769) means
/// PLAYBACK_STATUS on media.playback (ch10), ROOT_NODE on media.browser
/// (ch12), and MEDIA_START on video/audio (ch1~4). Looking up by id
/// alone yields wrong labels for 10/12 — see CHANNEL_AWARE comment in
/// msg_type_name below.

inline const char* input_msg_name(uint16_t type) {
    switch (type) {
        case 32769: return "INPUT_REPORT";
        case 32770: return "KEY_BINDING_REQ";
        case 32771: return "KEY_BINDING_RESP";
        case 32772: return "INPUT_FEEDBACK";
        default:    return nullptr;
    }
}

inline const char* mediaplayback_msg_name(uint16_t type) {
    switch (type) {
        case 32769: return "PLAYBACK_STATUS";
        case 32770: return "PLAYBACK_INPUT";
        case 32771: return "PLAYBACK_METADATA";
        default:    return nullptr;
    }
}

inline const char* mediabrowser_msg_name(uint16_t type) {
    switch (type) {
        case 32769: return "ROOT_NODE";
        case 32770: return "SOURCE_NODE";
        case 32771: return "LIST_NODE";
        case 32772: return "SONG_NODE";
        case 32773: return "GET_NODE";
        case 32774: return "BROWSE_INPUT";
        default:    return nullptr;
    }
}

/// Format a message type as a readable string for the given channel.
///
/// CHANNEL_AWARE: AAP message ids are scoped per channel. Control-channel
/// ids (CHANNEL_OPEN_REQ etc.) are valid on every channel and matched
/// first. After that, channels with their own id namespace
/// (input ch5, media.playback ch10, media.browser ch12) are looked up in
/// dedicated tables — for the remaining channels we fall back to the
/// generic MediaMessageType set used by video/audio/sensor/microphone.
///
/// Returns pointer to a thread-local buffer (valid until next call on
/// the same thread).
inline const char* msg_type_name(uint8_t channel, uint16_t type) {
    auto ct = to_string(static_cast<ControlMessageType>(type));
    if (ct) return ct;

    switch (channel) {
        case 5: {
            auto n = input_msg_name(type);
            if (n) return n;
            break;
        }
        case 10: {
            auto n = mediaplayback_msg_name(type);
            if (n) return n;
            break;
        }
        case 12: {
            auto n = mediabrowser_msg_name(type);
            if (n) return n;
            break;
        }
        default: {
            auto mt = to_string(static_cast<MediaMessageType>(type));
            if (mt) return mt;
            break;
        }
    }

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
        case 15: return "video.cluster";
        default:
            static thread_local char buf[8];
            snprintf(buf, sizeof(buf), "ch%u", ch);
            return buf;
    }
}

} // namespace aauto
