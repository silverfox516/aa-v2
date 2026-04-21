#pragma once

#include <cstdint>
#include <system_error>
#include <string>
#include <vector>
#include <memory>
#include <functional>

namespace aauto {

// ===== AAP Frame Constants =====
constexpr uint8_t  kControlChannelId    = 0;
constexpr uint16_t kMaxChannelCount     = 256;
constexpr uint32_t kMaxFramePayloadSize = 16384;  // 16 KiB
constexpr uint32_t kFrameHeaderSize     = 4;       // channel(1) + flags(1) + length(2)

// ===== FragInfo (matches FragInfo.proto) =====
enum class FragInfo : uint8_t {
    Continuation = 0,
    First        = 1,
    Last         = 2,
    Unfragmented = 3
};

// ===== Control Channel Message Types (matches ControlMessageType.proto) =====
enum class ControlMessageType : uint16_t {
    VersionRequest            = 1,
    VersionResponse           = 2,
    EncapsulatedSsl           = 3,
    AuthComplete              = 4,
    ServiceDiscoveryRequest   = 5,
    ServiceDiscoveryResponse  = 6,
    ChannelOpenRequest        = 7,
    ChannelOpenResponse       = 8,
    ChannelCloseNotification  = 9,
    PingRequest               = 11,
    PingResponse              = 12,
    NavFocusRequest           = 13,
    NavFocusNotification      = 14,
    ByeByeRequest             = 15,
    ByeByeResponse            = 16,
    VoiceSessionNotification  = 17,
    AudioFocusRequest         = 18,
    AudioFocusNotification    = 19,
    CarConnectedDevicesReq    = 20,
    CarConnectedDevicesResp   = 21,
    UserSwitchRequest         = 22,
    BatteryStatusNotification = 23,
    CallAvailabilityStatus    = 24,
    UserSwitchResponse        = 25,
    ServiceDiscoveryUpdate    = 26,
    UnexpectedMessage         = 255,
    FramingError              = 65535
};

// ===== Media Message Types (matches MediaMessageId.proto) =====
enum class MediaMessageType : uint16_t {
    Data                   = 0,
    CodecConfig            = 1,
    Setup                  = 32768,
    Start                  = 32769,
    Stop                   = 32770,
    Config                 = 32771,
    Ack                    = 32772,
    MicrophoneRequest      = 32773,
    MicrophoneResponse     = 32774,
    VideoFocusRequest      = 32775,
    VideoFocusNotification = 32776,
    UpdateUiConfigRequest  = 32777,
    UpdateUiConfigReply    = 32778,
    AudioUnderflow         = 32779
};

// ===== Input Message Types =====
enum class InputMessageType : uint16_t {
    InputReport        = 32769,
    KeyBindingRequest  = 32770,
    KeyBindingResponse = 32771,
    InputFeedback      = 32772
};

// ===== Sensor Message Types =====
enum class SensorMessageType : uint16_t {
    Request  = 32769,
    Response = 32770,
    Batch    = 32771,
    Error    = 32772
};

// ===== Navigation Message Types =====
enum class NavigationMessageType : uint16_t {
    InstrumentClusterStart = 32769,
    Stop                   = 32770,
    NavigationStatus       = 32771,
    NavigationState        = 32774,
    CurrentPosition        = 32775
};

// ===== Phone Status Message Types =====
enum class PhoneStatusMessageType : uint16_t {
    PhoneStatus      = 32769,
    PhoneStatusInput = 32770
};

// ===== Bluetooth Message Types =====
enum class BluetoothMessageType : uint16_t {
    PairingRequest       = 32769,
    PairingResponse      = 32770,
    AuthenticationData   = 32771,
    AuthenticationResult = 32772
};

// ===== MessageStatus (matches MessageStatus.proto) =====
enum class MessageStatus : int32_t {
    UnsolicitedMessage  =   1,
    Success             =   0,
    NoCompatibleVersion =  -1,
    CertificateError    =  -2,
    AuthFailure         =  -3,
    InvalidService      =  -4,
    InvalidChannel      =  -5,
    InvalidPriority     =  -6,
    InternalError       =  -7,
    MediaConfigMismatch =  -8,
    InvalidSensor       =  -9,
    BtPairingDelayed    = -10,
    BtUnavailable       = -11,
    PingTimeout         = -25,
    CommandNotSupported = -250,
    FramingErrorStatus  = -251,
    UnexpectedMsg       = -253,
    Busy                = -254,
    OutOfMemory         = -255
};

// ===== Protocol version =====
constexpr uint16_t kProtocolVersionMajor = 1;
constexpr uint16_t kProtocolVersionMinor = 6;

// ===== AAP error category for std::error_code =====
enum class AapErrc {
    Success = 0,
    TransportClosed,
    TransportReadError,
    TransportWriteError,
    SslHandshakeFailed,
    VersionMismatch,
    AuthFailed,
    ServiceDiscoveryFailed,
    ChannelOpenFailed,
    PingTimeout,
    FramingError,
    DecryptionFailed,
    ProtobufParseError,
    SessionTerminated,
    ByeByeReceived,
    InternalError
};

const std::error_category& aap_category() noexcept;

inline std::error_code make_error_code(AapErrc e) noexcept {
    return {static_cast<int>(e), aap_category()};
}

// ===== Frame flags =====
// Flags byte layout: [bit0-1: FragInfo] [bit2: control-on-media] [bit3: encrypted]
constexpr uint8_t kFlagEncrypted      = 0x08;
constexpr uint8_t kFlagControlOnMedia = 0x04;

/// Compute flags byte for an outgoing frame.
/// Session calls this; Framer uses the result as-is.
inline uint8_t compute_frame_flags(uint8_t channel, uint16_t msg_type,
                                   bool encrypted) {
    uint8_t flags = static_cast<uint8_t>(FragInfo::Unfragmented);
    if (encrypted) flags |= kFlagEncrypted;
    // Control-type messages (type 1-19) on non-control channels need the
    // control-on-media bit so the phone routes them correctly.
    if (channel != kControlChannelId &&
        msg_type >= 1 && msg_type <= 0x0013) {
        flags |= kFlagControlOnMedia;
    }
    return flags;
}

// ===== Convenience aliases =====
using ByteBuffer = std::vector<uint8_t>;

template<typename T>
using Ptr = std::shared_ptr<T>;

} // namespace aauto

namespace std {
template<> struct is_error_code_enum<aauto::AapErrc> : true_type {};
}
