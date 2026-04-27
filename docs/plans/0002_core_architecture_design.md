# 0002 — Core Architecture Design

> Created: 2026-04-16
> Status: IMPLEMENTED (verified on real device 2026-04-21; refined through F.16 — see architecture_review.md)
> Approach: First-principles design from proto definitions + F.1~F.11 decisions
> Reference code: NOT consulted (by design — see 0001 Phase 0a)

---

## 1. Module Dependency Graph

```
                    +-----------------------------+
                    |        app/android/          |
                    |  AaService, UsbMonitor,      |
                    |  SessionManager, UI          |
                    +--------------+---------------+
                                   | depends on
                    +--------------v---------------+
                    |       impl/android/          |
                    |  AndroidUsbTransport,        |
                    |  AMediaCodecVideoSink,       |
                    |  AAudioSink,                 |
                    |  AidlEngineController,       |
                    |  TouchInputSource            |
                    +--------------+---------------+
                                   | depends on
                    +--------------v---------------+
                    |       impl/common/           |
                    |  OpenSslCryptoStrategy       |
                    +--------------+---------------+
                                   | depends on
    +--------------v------------------------------------------+
    |                      core/                               |
    |  +--------+  +--------+  +---------+  +-----------+     |
    |  |engine/ |  |session/|  |service/ |  |transport/ |     |
    |  |Engine  |--|Session |--|IService |  |ITransport |     |
    |  |IEngine |  |Framer  |  |Video/   |  +-----------+     |
    |  |Contrlr |  |        |  |Audio/.. |                    |
    |  +--------+  +--------+  +---------+                    |
    |  +--------+  +--------+  +---------+                    |
    |  |sink/   |  |source/ |  |crypto/  |                    |
    |  |IVideo  |  |IInput  |  |ICrypto  |                    |
    |  |IAudio  |  |ISensor |  |         |                    |
    |  +--------+  +--------+  +---------+                    |
    |  +--------+                                             |
    |  |utils/  |  Logger, ProtocolConstants, ByteBuffer      |
    |  +--------+                                             |
    +---------------------------+-----------------------------+
                                | depends on
                    +-----------v--------------+
                    |   third_party/asio/       |
                    |   (header-only, vendored) |
                    +--------------------------+
```

**Rule**: `app -> impl -> core -> third_party/asio`. Never reversed.
No `#ifdef __ANDROID__` in core/.

---

## 2. Common Types and Constants

File: `core/include/aauto/utils/ProtocolConstants.hpp`

```cpp
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

// ===== AAP error category for std::error_code integration =====
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

// ===== Convenience aliases =====
using ByteBuffer = std::vector<uint8_t>;

template<typename T>
using Ptr = std::shared_ptr<T>;

} // namespace aauto

namespace std {
template<> struct is_error_code_enum<aauto::AapErrc> : true_type {};
}
```

---

## 3. Port Interfaces

### 3.1 ITransport (Inbound Port — async byte stream)

File: `core/include/aauto/transport/ITransport.hpp`

```cpp
#pragma once

#include <asio.hpp>
#include <cstdint>
#include <functional>
#include <system_error>

namespace aauto::transport {

using ReadHandler  = std::function<void(const std::error_code& ec,
                                        std::size_t bytes_read)>;
using WriteHandler = std::function<void(const std::error_code& ec,
                                        std::size_t bytes_written)>;

/// Inbound port: async byte stream transport (USB, TCP, etc.)
///
/// Lifecycle contract:
///   1. Constructed in closed state
///   2. Caller opens transport (platform-specific, outside this interface)
///   3. Once open, async_read/async_write are valid
///   4. close() may be called from any state; cancels pending operations
///   5. After close(), no further async_read/async_write calls
///
/// Threading contract:
///   All callbacks dispatched on the associated executor (io_context thread).
///   Implementations must NOT call handlers inline during async_read/async_write.
///   One outstanding async_read and one async_write at a time.
class ITransport {
public:
    virtual ~ITransport() = default;

    /// Async read into buffer. Exactly one handler invocation per call.
    /// Buffer must remain valid until handler fires.
    /// ec == asio::error::operation_aborted on close().
    virtual void async_read(asio::mutable_buffer buffer,
                            ReadHandler handler) = 0;

    /// Async write from buffer. Exactly one handler invocation per call.
    /// Buffer must remain valid until handler fires.
    virtual void async_write(asio::const_buffer buffer,
                             WriteHandler handler) = 0;

    /// Close transport. Cancels pending ops. Idempotent.
    virtual void close() = 0;

    /// Returns true if transport is open and usable.
    virtual bool is_open() const = 0;

    /// Executor for dispatching callbacks.
    virtual asio::any_io_executor get_executor() = 0;
};

} // namespace aauto::transport
```

### 3.2 IVideoSink (Outbound Port)

File: `core/include/aauto/sink/IVideoSink.hpp`

```cpp
#pragma once

#include <cstdint>
#include <cstddef>

namespace aauto::sink {

struct VideoConfig {
    uint32_t width;
    uint32_t height;
    uint32_t fps;
    uint32_t density;
    uint32_t codec_type;  // MediaCodecType enum value
};

/// Outbound port: receives video data (H.264 NALUs etc.)
///
/// Lifecycle:
///   1. Constructed (idle)
///   2. on_configure() — sink prepares decoder
///   3. on_codec_config() — SPS/PPS or equivalent
///   4. on_video_data() — repeated NAL units
///   5. on_stop() — release decoder resources
///   6. on_configure() may be called again (reconfiguration)
///
/// Threading: all on_* called from io_context thread.
/// Implementations must not block (queue to decoder thread if needed).
class IVideoSink {
public:
    virtual ~IVideoSink() = default;

    virtual void on_configure(const VideoConfig& config) = 0;
    virtual void on_codec_config(const uint8_t* data, std::size_t size,
                                 int64_t timestamp_us) = 0;
    virtual void on_video_data(const uint8_t* data, std::size_t size,
                               int64_t timestamp_us) = 0;
    virtual void on_stop() = 0;
};

} // namespace aauto::sink
```

### 3.3 IAudioSink (Outbound Port)

File: `core/include/aauto/sink/IAudioSink.hpp`

```cpp
#pragma once

#include <cstdint>
#include <cstddef>

namespace aauto::sink {

struct AudioConfig {
    uint32_t sample_rate;     // 8000, 16000, 44100, 48000
    uint32_t bit_depth;       // 16, 24, 32
    uint32_t channel_count;   // 1, 2, 4, 6, 8
    uint32_t codec_type;      // MediaCodecType enum value
};

enum class AudioStreamType : uint32_t {
    Media    = 1,
    Guidance = 2,
    System   = 3,
    Call     = 4
};

/// Outbound port: receives audio data (PCM or compressed).
///
/// Lifecycle: same as IVideoSink. Threading: must not block.
class IAudioSink {
public:
    virtual ~IAudioSink() = default;

    virtual void on_configure(const AudioConfig& config,
                              AudioStreamType stream_type) = 0;
    virtual void on_codec_config(const uint8_t* data, std::size_t size) = 0;
    virtual void on_audio_data(const uint8_t* data, std::size_t size,
                               int64_t timestamp_us) = 0;
    virtual void on_stop() = 0;
};

} // namespace aauto::sink
```

### 3.4 IInputSource (Inbound Port)

File: `core/include/aauto/source/IInputSource.hpp`

```cpp
#pragma once

#include <cstdint>
#include <functional>

namespace aauto::source {

struct TouchEvent {
    uint32_t action;       // DOWN=0, UP=1, MOVE=2
    uint32_t x;
    uint32_t y;
    uint32_t pointer_id;
    int64_t  timestamp_ns;
};

struct KeyEvent {
    uint32_t keycode;
    uint32_t action;       // DOWN=0, UP=1
    int64_t  timestamp_ns;
};

using InputEventCallback = std::function<void(const TouchEvent&)>;
using KeyEventCallback   = std::function<void(const KeyEvent&)>;

/// Inbound port: platform input events -> AAP input channel.
///
/// Lifecycle:
///   1. Constructed (idle)
///   2. start(touch_cb, key_cb) — begin forwarding events
///   3. stop() — stop forwarding
///
/// Threading: callbacks may come from any thread.
/// Service posts them onto the session strand.
class IInputSource {
public:
    virtual ~IInputSource() = default;

    virtual void start(InputEventCallback touch_cb,
                       KeyEventCallback key_cb) = 0;
    virtual void stop() = 0;
};

} // namespace aauto::source
```

### 3.5 ISensorSource (Inbound Port)

File: `core/include/aauto/source/ISensorSource.hpp`

```cpp
#pragma once

#include <cstdint>
#include <functional>
#include <vector>

namespace aauto::source {

enum class SensorType : uint32_t {
    Location      = 1,
    Compass       = 2,
    Speed         = 3,
    Rpm           = 4,
    Odometer      = 5,
    Fuel          = 6,
    ParkingBrake  = 7,
    Gear          = 8,
    NightMode     = 10,
    DrivingStatus = 13,
    Gyroscope     = 20,
    GpsSatellite  = 21
};

struct SensorData {
    SensorType type;
    int64_t    timestamp_ns;
    std::vector<uint8_t> payload;  // serialized protobuf for specific sensor
};

using SensorDataCallback = std::function<void(const SensorData&)>;

/// Inbound port: vehicle/platform sensor data -> AAP sensor channel.
///
/// Lifecycle:
///   1. Constructed (idle)
///   2. start_sensor(type, interval_ms, cb) — begin reporting
///   3. stop_sensor(type) — stop specific sensor
///   4. stop_all() — cleanup
class ISensorSource {
public:
    virtual ~ISensorSource() = default;

    virtual void start_sensor(SensorType type, uint32_t interval_ms,
                              SensorDataCallback cb) = 0;
    virtual void stop_sensor(SensorType type) = 0;
    virtual void stop_all() = 0;
};

} // namespace aauto::source
```

### 3.6 ICryptoStrategy (Internal Port)

File: `core/include/aauto/crypto/ICryptoStrategy.hpp`

```cpp
#pragma once

#include <cstdint>
#include <cstddef>
#include <functional>
#include <string>
#include <system_error>
#include <vector>

namespace aauto::crypto {

struct HandshakeResult {
    std::vector<uint8_t> output_bytes;  // bytes to send to peer (may be empty)
    bool                 complete;       // true = handshake finished
};

using HandshakeStepHandler = std::function<void(const std::error_code& ec,
                                                HandshakeResult result)>;
using CryptoHandler = std::function<void(const std::error_code& ec,
                                         std::vector<uint8_t> output)>;

/// Internal port: SSL/TLS handshake + encrypt/decrypt.
///
/// Lifecycle:
///   1. Constructed with certificate/key material
///   2. handshake_step() called repeatedly until complete==true
///   3. After handshake: encrypt()/decrypt() for payload protection
///   4. reset() to prepare for new handshake (session restart)
///
/// Threading: all methods called from session strand.
class ICryptoStrategy {
public:
    virtual ~ICryptoStrategy() = default;

    /// Feed handshake bytes from peer (empty for initial step).
    /// Handler called with bytes to send back and completion status.
    virtual void handshake_step(const uint8_t* input_data,
                                std::size_t input_size,
                                HandshakeStepHandler handler) = 0;

    /// Encrypt plaintext payload.
    virtual void encrypt(const uint8_t* plaintext, std::size_t size,
                         CryptoHandler handler) = 0;

    /// Decrypt ciphertext payload.
    virtual void decrypt(const uint8_t* ciphertext, std::size_t size,
                         CryptoHandler handler) = 0;

    /// True after handshake completes.
    virtual bool is_established() const = 0;

    /// Reset for reuse.
    virtual void reset() = 0;
};

struct CryptoConfig {
    std::string cert_pem_path;  // HU certificate (PEM)
    std::string key_pem_path;   // HU private key (PEM)
    // Empty = use build-time injected defaults
};

} // namespace aauto::crypto
```

### 3.7 IEngineController (Driving Port)

File: `core/include/aauto/engine/IEngineController.hpp`

```cpp
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <system_error>

namespace aauto::engine {

enum class SessionStatus : uint32_t {
    Connecting    = 0,
    Handshaking   = 1,
    Running       = 2,
    Disconnecting = 3,
    Disconnected  = 4,
    Error         = 5
};

/// Callback interface: engine -> app notifications.
/// Implemented by AidlEngineController / DbusEngineController.
class IEngineCallback {
public:
    virtual ~IEngineCallback() = default;

    virtual void on_session_state_changed(uint32_t session_id,
                                          SessionStatus status) = 0;
    virtual void on_session_error(uint32_t session_id,
                                  const std::error_code& ec,
                                  const std::string& detail) = 0;
    virtual void on_phone_identified(uint32_t session_id,
                                     const std::string& device_name,
                                     const std::string& instance_id) = 0;
};

/// Driving port: app -> engine commands.
///
/// Lifecycle:
///   1. Engine starts, exposes this interface via IPC
///   2. App registers callback
///   3. App calls start_session on phone discovery
///   4. App calls stop_session/stop_all on user action
///
/// Threading: methods may come from IPC threads.
/// Implementations post to engine's io_context internally.
class IEngineController {
public:
    virtual ~IEngineController() = default;

    virtual void register_callback(std::shared_ptr<IEngineCallback> cb) = 0;

    /// Create session for transport descriptor (e.g., "usb:fd=42" or "tcp:192.168.1.100:5277").
    /// Returns session_id. Connection proceeds asynchronously.
    virtual uint32_t start_session(const std::string& transport_descriptor) = 0;

    virtual void stop_session(uint32_t session_id) = 0;
    virtual void stop_all() = 0;
    virtual void set_active_session(uint32_t session_id) = 0;
};

} // namespace aauto::engine
```

---

## 4. Session State Machine

File: `core/include/aauto/session/SessionState.hpp`

```cpp
#pragma once

#include <cstdint>

namespace aauto::session {

enum class SessionState : uint8_t {
    /// Transport assigned but not yet open.
    Idle,

    /// SSL/TLS handshake in progress.
    SslHandshake,

    /// VERSION_REQUEST sent, awaiting VERSION_RESPONSE.
    VersionExchange,

    /// SERVICE_DISCOVERY_REQUEST sent, awaiting response.
    ServiceDiscovery,

    /// Opening channels for discovered services.
    ChannelSetup,

    /// Fully operational. Media, input, sensor, ping active.
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

} // namespace aauto::session
```

### State Transition Diagram

```
                    +-------------------------------------------+
                    |        ERROR (from any non-terminal)       |
                    |  transport error / timeout / unrecoverable |
                    +-------------------------------------------+
                       ^     ^      ^      ^      ^      ^
                       |     |      |      |      |      |
  +------+ open  +----+---+ | +----+----+ | +----+----+ | +--------+
  | Idle |------>| Ssl    | | |Version  | | |Service  | | |Channel |
  |      |       |Handshk | | |Exchange | | |Discovry | | |Setup   |
  +------+       +---+----+ | +----+----+ | +----+----+ | +---+----+
                      |      |      |      |      |      |     |
                  complete   |  ver ok +   |  disc resp  | all open
                      |      |  auth      |      |      |     |
                      v      |      v      |      v      |     v
                      +------+  send SD    |  open ch    | +--------+
                                req  |      |      |      | |Running |
                                     v      |      v      | |        |
                                     +------+  +--+------++ +---+----+
                                                                |
                                     byebye req / app stop     |
                                          +---------------------+
                                          v
                                   +-----------+
                                   |Disconnctng|
                                   +-----+-----+
                                         |
                                   byebye done / timeout
                                         v
                                   +-----------+
                                   |Disconnctd |
                                   +-----------+
```

### Per-State Timeouts

| State | Timeout (ms) | On Timeout |
|-------|-------------|------------|
| SslHandshake | 10000 | -> Error(SslHandshakeFailed) |
| VersionExchange | 5000 | -> Error(VersionMismatch) |
| ServiceDiscovery | 5000 | -> Error(ServiceDiscoveryFailed) |
| ChannelSetup | 10000 | -> Error(ChannelOpenFailed) |
| Running | ping_timeout_ms | -> Error(PingTimeout) |
| Disconnecting | 3000 | -> Disconnected (forced) |

---

## 5. Framer Design

File: `core/include/aauto/session/Framer.hpp`

### Wire Format

```
Byte 0:    channel_id (uint8)
Byte 1:    flags
           bits[0-1]: FragInfo (0=continuation, 1=first, 2=last, 3=unfragmented)
           bit[3]:    encrypted flag
Byte 2-3:  payload_length (uint16 big-endian)
Byte 4...: payload (payload_length bytes)
```

### Crypto Ordering

**Encrypt-then-frame** (send): Session encrypts `[message_type:2][body]` ->
Framer fragments ciphertext into wire frames.

**Reassemble-then-decrypt** (receive): Framer reassembles fragments into
complete ciphertext blob -> Session decrypts.

Framer is **crypto-unaware**. Operates on opaque bytes.

### Fragmentation

If payload > `kMaxFramePayloadSize` (16KB):
- Split into [FIRST][chunk_1] [CONTINUATION][chunk_2...] [LAST][chunk_n]
- Per-channel reassembly buffer, max 512KB bound

```cpp
#pragma once

#include "aauto/utils/ProtocolConstants.hpp"
#include <cstdint>
#include <cstddef>
#include <functional>
#include <map>
#include <system_error>
#include <vector>

namespace aauto::session {

struct AapFrame {
    uint8_t              channel_id;
    bool                 encrypted;
    uint16_t             message_type;  // first 2 bytes of payload
    std::vector<uint8_t> payload;       // full reassembled payload
};

struct OutboundFrame {
    uint8_t              channel_id;
    bool                 encrypt;
    std::vector<uint8_t> payload;       // [message_type:2][body]
};

using FrameReceivedHandler = std::function<void(const std::error_code& ec,
                                                AapFrame frame)>;

class Framer {
public:
    Framer();
    ~Framer();

    /// Feed raw bytes. Calls on_frame for each complete reassembled frame.
    void feed(const uint8_t* data, std::size_t size,
              FrameReceivedHandler on_frame);

    /// Encode outbound frame. Fragments if needed. Returns wire buffers.
    std::vector<std::vector<uint8_t>> encode(const OutboundFrame& frame);

    /// Reset all reassembly state.
    void reset();

private:
    struct ReassemblyContext {
        std::vector<uint8_t> buffer;
        bool in_progress = false;
        bool encrypted   = false;
    };

    std::vector<uint8_t> recv_buffer_;
    std::size_t          recv_offset_ = 0;
    std::map<uint8_t, ReassemblyContext> reassembly_;

    bool try_parse_frame(FrameReceivedHandler& on_frame,
                         std::error_code& ec);
};

} // namespace aauto::session
```

---

## 6. Async Data Flow

### 6.1 Inbound (phone -> HU)

```
Transport            Session (strand)              Service          Sink(s)
    |                     |                          |                |
    | async_read(buf,cb)  |                          |                |
    |<--------------------|                          |                |
    |                     |                          |                |
    | cb(ec, bytes)       |                          |                |
    |-------------------->|                          |                |
    |                     |                          |                |
    |               framer_.feed(data, size,         |                |
    |                 [](ec, AapFrame) {              |                |
    |                     |                          |                |
    |               if encrypted:                    |                |
    |                 crypto_->decrypt(payload,       |                |
    |                   [](ec, plaintext) {           |                |
    |                     |                          |                |
    |               parse msg_type = plaintext[0:2]  |                |
    |               lookup service by channel_id     |                |
    |                     |                          |                |
    |                     | svc->on_message(         |                |
    |                     |   msg_type, payload)     |                |
    |                     |------------------------->|                |
    |                     |                          |                |
    |                     |                          | for sink in    |
    |                     |                          | sinks_:        |
    |                     |                          |--on_video_---->|
    |                     |                          |  data()        |
    |                     |                          |                |
    |               schedule next async_read         |                |
    |<--------------------|                          |                |
```

### 6.2 Outbound (HU -> phone)

```
Source/Service       Session (strand)              Transport
    |                     |                          |
    | send_message(       |                          |
    |   ch, type, data)   |                          |
    |-------------------->|                          |
    |                     |                          |
    |               build: [type:2][data]            |
    |                     |                          |
    |               if needs encryption:             |
    |                 crypto_->encrypt(plaintext,     |
    |                   [](ec, ciphertext) {          |
    |                     |                          |
    |               frames = framer_.encode(         |
    |                 {ch, encrypt, ciphertext})      |
    |                     |                          |
    |               enqueue in write_queue_          |
    |                     |                          |
    |               if !write_in_progress:           |
    |                 transport_->async_write(        |
    |                   front, cb)                   |
    |                     |------------------------->|
    |                     |                          |
    |               cb(ec): pop front,               |
    |                 write next if queue not empty   |
```

### Key Async Contract

- All Session methods run on its `asio::strand` — no mutexes
- One outstanding `async_read` at a time (re-posted after each completion)
- One outstanding `async_write` at a time (write queue serializes)
- Services run on the same strand (called synchronously from dispatch)
- Sink `on_*` calls run on the strand; sinks queue to own threads if needed

---

## 7. Session Design

File: `core/include/aauto/session/Session.hpp`

```cpp
#pragma once

#include "aauto/session/SessionState.hpp"
#include "aauto/session/Framer.hpp"
#include "aauto/transport/ITransport.hpp"
#include "aauto/crypto/ICryptoStrategy.hpp"
#include "aauto/service/IService.hpp"

#include <asio.hpp>
#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <queue>
#include <string>
#include <system_error>
#include <vector>

namespace aauto::session {

/// Session -> Engine notification interface.
class ISessionObserver {
public:
    virtual ~ISessionObserver() = default;
    virtual void on_session_state_changed(uint32_t session_id,
                                          SessionState state) = 0;
    virtual void on_session_error(uint32_t session_id,
                                  const std::error_code& ec) = 0;
};

struct SessionConfig {
    uint32_t session_id;
    uint32_t ssl_handshake_timeout_ms    = 10000;
    uint32_t version_exchange_timeout_ms = 5000;
    uint32_t service_discovery_timeout_ms = 5000;
    uint32_t channel_setup_timeout_ms    = 10000;
    uint32_t byebye_timeout_ms           = 3000;
    uint32_t ping_interval_ms            = 5000;
    uint32_t ping_timeout_ms             = 10000;
};

/// One AAP session with one phone.
///
/// Owns: Framer, strand, timers.
/// References: ITransport, ICryptoStrategy, services, observer.
/// All async ops serialized via strand.
class Session : public std::enable_shared_from_this<Session> {
public:
    Session(asio::any_io_executor executor,
            SessionConfig config,
            std::shared_ptr<transport::ITransport> transport,
            std::shared_ptr<crypto::ICryptoStrategy> crypto,
            ISessionObserver* observer);
    ~Session();

    void start();
    void stop();

    void register_service(uint8_t channel_id,
                          std::shared_ptr<service::IService> svc);

    void send_message(uint8_t channel_id, uint16_t message_type,
                      const std::vector<uint8_t>& payload);
    void send_raw(uint8_t channel_id, uint16_t message_type,
                  const uint8_t* data, std::size_t size);

    SessionState state() const;
    uint32_t session_id() const { return config_.session_id; }

private:
    // Read loop
    void start_read();
    void on_read_complete(const std::error_code& ec, std::size_t bytes);
    void dispatch_frame(AapFrame frame);
    void dispatch_decrypted(uint8_t channel_id, uint16_t msg_type,
                            std::vector<uint8_t> payload);

    // Write queue
    void enqueue_write(std::vector<uint8_t> wire_data);
    void do_write_next();
    void on_write_complete(const std::error_code& ec, std::size_t bytes);

    // State machine
    void transition_to(SessionState new_state);
    void handle_error(const std::error_code& ec);
    void start_state_timer(uint32_t timeout_ms);
    void on_state_timeout();

    // Handshake
    void begin_ssl_handshake();
    void on_ssl_data_received(const uint8_t* data, std::size_t size);
    void on_ssl_complete();
    void send_version_request();
    void on_version_response(const std::vector<uint8_t>& payload);
    void on_auth_complete(const std::vector<uint8_t>& payload);
    void send_service_discovery_request();
    void on_service_discovery_response(const std::vector<uint8_t>& payload);
    void open_channels();
    void on_channel_open_response(const std::vector<uint8_t>& payload);

    // Ping
    void start_ping_timer();
    void send_ping();
    void on_ping_response(const std::vector<uint8_t>& payload);
    void on_ping_timeout();

    // Control message dispatch
    void handle_control_message(uint16_t msg_type,
                                const std::vector<uint8_t>& payload);

    // Members
    asio::strand<asio::any_io_executor>      strand_;
    SessionConfig                             config_;
    std::shared_ptr<transport::ITransport>    transport_;
    std::shared_ptr<crypto::ICryptoStrategy>  crypto_;
    ISessionObserver*                         observer_;
    Framer                                    framer_;
    SessionState                              state_ = SessionState::Idle;

    std::array<uint8_t, 16384>                read_buffer_;
    std::queue<std::vector<uint8_t>>          write_queue_;
    bool                                      write_in_progress_ = false;

    std::map<uint8_t, std::shared_ptr<service::IService>> services_;

    asio::steady_timer                        state_timer_;
    asio::steady_timer                        ping_timer_;
    asio::steady_timer                        ping_timeout_timer_;

    std::map<int32_t, uint8_t>                service_id_to_channel_;
    uint8_t                                   next_channel_id_ = 1;
    int                                       pending_channel_opens_ = 0;
};

} // namespace aauto::session
```

### Handshake Sequence Implementation

```
Session::start()
  |
  +-- transition_to(SslHandshake)
  +-- start_state_timer(ssl_timeout)
  +-- crypto_->handshake_step(nullptr, 0, handler)
  |     handler: if output_bytes not empty -> send as EncapsulatedSsl(type=3) on ch 0
  |              if complete -> on_ssl_complete()
  |              else -> wait for more handshake data
  |
  +-- [receive EncapsulatedSsl on ch 0]
  |     crypto_->handshake_step(data, size, same_handler)
  |
  +-- on_ssl_complete():
  |     transition_to(VersionExchange)
  |     send_version_request()   // type=1 on ch 0
  |     start_state_timer(version_timeout)
  |
  +-- [receive VersionResponse (type=2)]
  |     on_version_response(): parse, check compatibility
  |
  +-- [receive AuthComplete (type=4)]
  |     on_auth_complete():
  |       transition_to(ServiceDiscovery)
  |       send_service_discovery_request()  // type=5
  |       start_state_timer(discovery_timeout)
  |
  +-- [receive ServiceDiscoveryResponse (type=6)]
  |     on_service_discovery_response():
  |       parse ServiceConfigurations
  |       transition_to(ChannelSetup)
  |       open_channels()
  |       // for each needed service:
  |       //   send ChannelOpenRequest(type=7, service_id, priority)
  |       //   pending_channel_opens_++
  |       start_state_timer(channel_setup_timeout)
  |
  +-- [receive ChannelOpenResponse (type=8)] x N
        on_channel_open_response():
          register_service(channel_id, service)
          service->on_channel_open(channel_id)
          pending_channel_opens_--
          if pending_channel_opens_ == 0:
            transition_to(Running)
            start_ping_timer()
```

---

## 8. Service Pattern

File: `core/include/aauto/service/IService.hpp`

```cpp
#pragma once

#include <cstdint>
#include <cstddef>
#include <functional>
#include <vector>

namespace aauto::service {

enum class ServiceType : uint32_t {
    Control             = 0,
    MediaSink           = 1,
    MediaSource         = 2,
    InputSource         = 3,
    SensorSource        = 4,
    BluetoothService    = 5,
    NavigationStatus    = 6,
    PhoneStatus         = 7,
    MediaBrowser        = 8,
    MediaPlayback       = 9,
    RadioService        = 10,
    VendorExtension     = 11,
    GenericNotification = 12,
    WifiProjection      = 13
};

/// Callback for services to send messages through Session.
using SendMessageFn = std::function<void(uint8_t channel_id,
                                         uint16_t message_type,
                                         const std::vector<uint8_t>& payload)>;

/// Base interface for all AAP channel services.
///
/// Services receive SendMessageFn (not Session*) to enforce unidirectional dependency.
class IService {
public:
    virtual ~IService() = default;

    virtual void on_channel_open(uint8_t channel_id) = 0;

    /// message_type: 2-byte type ID. payload: body after type stripped.
    virtual void on_message(uint16_t message_type,
                            const uint8_t* payload,
                            std::size_t payload_size) = 0;

    virtual void on_channel_close() = 0;

    virtual ServiceType type() const = 0;
};

} // namespace aauto::service
```

File: `core/include/aauto/service/ServiceBase.hpp`

```cpp
#pragma once

#include "aauto/service/IService.hpp"
#include <map>

namespace aauto::service {

/// Optional base class with message dispatch table.
/// Services may use this or implement IService directly.
class ServiceBase : public IService {
public:
    using MessageHandler = std::function<void(const uint8_t*, std::size_t)>;

    explicit ServiceBase(SendMessageFn send_fn)
        : send_fn_(std::move(send_fn)) {}

    void on_channel_open(uint8_t channel_id) override {
        channel_id_ = channel_id;
    }

    void on_message(uint16_t message_type,
                    const uint8_t* payload,
                    std::size_t payload_size) override {
        auto it = handlers_.find(message_type);
        if (it != handlers_.end()) {
            it->second(payload, payload_size);
        }
        // Unknown types silently ignored (logged in derived class if needed)
    }

    void on_channel_close() override {
        channel_id_ = 0;
    }

protected:
    void register_handler(uint16_t msg_type, MessageHandler handler) {
        handlers_[msg_type] = std::move(handler);
    }

    void send(uint16_t msg_type, const std::vector<uint8_t>& payload) {
        if (send_fn_) {
            send_fn_(channel_id_, msg_type, payload);
        }
    }

    uint8_t       channel_id_ = 0;
    SendMessageFn send_fn_;

private:
    std::map<uint16_t, MessageHandler> handlers_;
};

} // namespace aauto::service
```

### VideoService

File: `core/include/aauto/service/VideoService.hpp`

```cpp
#pragma once

#include "aauto/service/ServiceBase.hpp"
#include "aauto/sink/IVideoSink.hpp"
#include <memory>
#include <vector>

namespace aauto::service {

/// Receives H.264/VP9/H265 data from phone, multicasts to video sinks.
///
/// Flow: SETUP -> CONFIG -> START -> [CODEC_CONFIG] -> DATA* -> STOP
/// Flow control: phone sends max_unacked in CONFIG. HU sends ACK after that many frames.
class VideoService : public ServiceBase {
public:
    VideoService(SendMessageFn send_fn,
                 std::vector<std::shared_ptr<sink::IVideoSink>> sinks);

    ServiceType type() const override { return ServiceType::MediaSink; }
    void on_channel_open(uint8_t channel_id) override;
    void on_channel_close() override;

private:
    void on_setup(const uint8_t* data, std::size_t size);
    void on_config(const uint8_t* data, std::size_t size);
    void on_start(const uint8_t* data, std::size_t size);
    void on_codec_config(const uint8_t* data, std::size_t size);
    void on_data(const uint8_t* data, std::size_t size);
    void on_stop(const uint8_t* data, std::size_t size);
    void send_ack();

    std::vector<std::shared_ptr<sink::IVideoSink>> sinks_;
    uint32_t max_unacked_   = 1;
    uint32_t unacked_count_ = 0;
    int32_t  session_id_    = 0;
    bool     started_       = false;
    sink::VideoConfig current_config_{};
};

} // namespace aauto::service
```

### AudioService

File: `core/include/aauto/service/AudioService.hpp`

```cpp
#pragma once

#include "aauto/service/ServiceBase.hpp"
#include "aauto/sink/IAudioSink.hpp"
#include <memory>
#include <vector>

namespace aauto::service {

/// Receives PCM/AAC from phone for one audio stream type, multicasts to sinks.
/// Each audio stream (media, guidance, call) = separate AudioService instance.
class AudioService : public ServiceBase {
public:
    AudioService(SendMessageFn send_fn,
                 sink::AudioStreamType stream_type,
                 std::vector<std::shared_ptr<sink::IAudioSink>> sinks);

    ServiceType type() const override { return ServiceType::MediaSink; }
    void on_channel_open(uint8_t channel_id) override;
    void on_channel_close() override;

private:
    void on_setup(const uint8_t* data, std::size_t size);
    void on_config(const uint8_t* data, std::size_t size);
    void on_start(const uint8_t* data, std::size_t size);
    void on_codec_config(const uint8_t* data, std::size_t size);
    void on_data(const uint8_t* data, std::size_t size);
    void on_stop(const uint8_t* data, std::size_t size);
    void send_ack();

    sink::AudioStreamType                          stream_type_;
    std::vector<std::shared_ptr<sink::IAudioSink>> sinks_;
    uint32_t max_unacked_   = 1;
    uint32_t unacked_count_ = 0;
    int32_t  session_id_    = 0;
    bool     started_       = false;
    sink::AudioConfig current_config_{};
};

} // namespace aauto::service
```

---

## 9. Engine Design

File: `core/include/aauto/engine/Engine.hpp`

```cpp
#pragma once

#include "aauto/engine/IEngineController.hpp"
#include "aauto/session/Session.hpp"
#include "aauto/crypto/ICryptoStrategy.hpp"
#include "aauto/transport/ITransport.hpp"

#include <asio.hpp>
#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace aauto::engine {

/// Factory: creates platform-specific transports.
class ITransportFactory {
public:
    virtual ~ITransportFactory() = default;
    virtual std::shared_ptr<transport::ITransport>
        create(asio::any_io_executor executor,
               const std::string& descriptor) = 0;
};

/// Factory: creates crypto strategy with certs.
class ICryptoFactory {
public:
    virtual ~ICryptoFactory() = default;
    virtual std::shared_ptr<crypto::ICryptoStrategy>
        create(const crypto::CryptoConfig& config) = 0;
};

/// Factory: creates services for a session.
class IServiceFactory {
public:
    virtual ~IServiceFactory() = default;
    virtual std::map<int32_t, std::shared_ptr<service::IService>>
        create_services(service::SendMessageFn send_fn) = 0;
};

struct HeadunitConfig {
    std::string hu_make      = "TCC";
    std::string hu_model     = "TCC803x";
    std::string hu_sw_ver    = "1.0.0";
    std::string display_name = "Android Auto";

    uint32_t video_width   = 800;
    uint32_t video_height  = 480;
    uint32_t video_fps     = 30;
    uint32_t video_density = 160;

    uint32_t audio_sample_rate = 48000;
    uint32_t audio_bit_depth   = 16;
    uint32_t audio_channels    = 2;

    crypto::CryptoConfig crypto_config;
};

/// Top-level engine. Owns io_context, manages sessions.
///
/// Multi-session: map<session_id, Session>.
/// One "active" session has sinks attached; others dormant.
///
/// Threading: io_context on configurable thread count (default 1).
/// Session logic is strand-protected.
class Engine : public IEngineController,
               public session::ISessionObserver {
public:
    Engine(HeadunitConfig config,
           std::shared_ptr<ITransportFactory> transport_factory,
           std::shared_ptr<ICryptoFactory> crypto_factory,
           std::shared_ptr<IServiceFactory> service_factory);
    ~Engine();

    // IEngineController
    void register_callback(std::shared_ptr<IEngineCallback> cb) override;
    uint32_t start_session(const std::string& transport_descriptor) override;
    void stop_session(uint32_t session_id) override;
    void stop_all() override;
    void set_active_session(uint32_t session_id) override;

    /// Run event loop. Blocks until shutdown().
    void run(unsigned int thread_count = 1);

    /// Stop all sessions and event loop.
    void shutdown();

    // ISessionObserver
    void on_session_state_changed(uint32_t session_id,
                                  session::SessionState state) override;
    void on_session_error(uint32_t session_id,
                          const std::error_code& ec) override;

private:
    void do_start_session(const std::string& descriptor, uint32_t sid);
    void cleanup_session(uint32_t session_id);

    asio::io_context io_context_;
    asio::executor_work_guard<asio::io_context::executor_type> work_guard_;
    std::vector<std::thread> threads_;

    HeadunitConfig config_;
    std::shared_ptr<ITransportFactory> transport_factory_;
    std::shared_ptr<ICryptoFactory>    crypto_factory_;
    std::shared_ptr<IServiceFactory>   service_factory_;
    std::shared_ptr<IEngineCallback>   callback_;

    std::map<uint32_t, std::shared_ptr<session::Session>> sessions_;
    uint32_t active_session_id_ = 0;
    std::atomic<uint32_t> next_session_id_{1};
};

} // namespace aauto::engine
```

---

## 10. Error Propagation Model

```
Transport error
    |
    v
Session::on_read_complete(ec) / on_write_complete(ec)
    |
    +-- ec == operation_aborted? -> session closing, ignore
    |
    +-- Session::handle_error(ec)
          |
          +-- transition_to(Error)
          +-- cancel all timers
          +-- transport_->close()
          +-- for each service: svc->on_channel_close()
          +-- observer_->on_session_error(session_id, ec)
                |
                v
          Engine::on_session_error(session_id, ec)
                |
                +-- cleanup_session(session_id)
                +-- callback_->on_session_error(session_id, ec, detail)
                      |
                      v
                IPC to App (AIDL / D-Bus)

Service error (unknown message type)
    -> log warning, continue

Channel-critical error
    -> send CHANNEL_CLOSE_NOTIFICATION for that channel
    -> session continues if control + 1 media remain

Session-critical error (control channel failure)
    -> handle_error(AapErrc::InternalError) -> same as transport error

Ping timeout
    -> handle_error(AapErrc::PingTimeout) -> same as transport error

Graceful disconnect (ByeBye)
    -> transition_to(Disconnecting) -> exchange ByeBye -> Disconnected
```

### Recovery Policy

- Session does NOT auto-retry. Session is single-use.
- App layer (SessionManager) owns retry logic.
- Engine notifies app, app decides reconnect policy.

---

## 11. Logger

File: `core/include/aauto/utils/Logger.hpp`

```cpp
#pragma once

namespace aauto {

// Platform-agnostic logging interface.
// Implementations provided by impl/ layer (Android: __android_log_print,
// Linux: syslog/stderr).

enum class LogLevel { Debug, Info, Warn, Error };

using LogFunction = void(*)(LogLevel level, const char* tag,
                            const char* fmt, ...);

// Set by platform layer at startup.
void set_log_function(LogFunction fn);

// Internal: do not call directly, use macros.
void log_impl(LogLevel level, const char* tag, const char* fmt, ...);

} // namespace aauto

// Usage: #define LOG_TAG "Session"
// AA_LOG_I("state=%d", state);
#define AA_LOG_D(fmt, ...) ::aauto::log_impl(::aauto::LogLevel::Debug, LOG_TAG, fmt, ##__VA_ARGS__)
#define AA_LOG_I(fmt, ...) ::aauto::log_impl(::aauto::LogLevel::Info,  LOG_TAG, fmt, ##__VA_ARGS__)
#define AA_LOG_W(fmt, ...) ::aauto::log_impl(::aauto::LogLevel::Warn,  LOG_TAG, fmt, ##__VA_ARGS__)
#define AA_LOG_E(fmt, ...) ::aauto::log_impl(::aauto::LogLevel::Error, LOG_TAG, fmt, ##__VA_ARGS__)
```

---

## 12. Design Decisions & Rationale

| # | Decision | Rationale |
|---|----------|-----------|
| 1 | Encrypt-then-frame (not per-fragment) | Framer crypto-unaware; AAP encrypts logical messages |
| 2 | Handshake in Session (no Handshaker class) | Needs transport/crypto/framer/timers; extracting = tight coupling |
| 3 | Strand per session (not mutex) | Natural for asio async chains; no deadlock risk |
| 4 | SendMessageFn callback (not Session*) | Unidirectional dependency; isolated testing |
| 5 | ServiceBase optional | Complex services (BT pairing) need custom dispatch |
| 6 | Engine owns io_context | Composition root; simple run()/shutdown() |
| 7 | Factory interfaces for DI | Hexagonal adapter injection; mock-friendly |
| 8 | Separate AudioService per stream | Matches AAP: each stream = separate channel + flow control |
| 9 | std::error_code + AapErrc | Standard C++ error handling; integrates with asio |
| 10 | LogFunction pointer (not virtual) | Zero overhead in release; set once at startup |

---

## 13. F.1~F.11 Cross-Reference

| Decision | Where Reflected |
|----------|-----------------|
| F.1 C++17 | All code; no coroutines/concepts |
| F.2 Android.bp + CMake | Build files (not in this doc, Phase 1) |
| F.3 Single process | Engine owns single io_context, N sessions |
| F.4 standalone asio | ITransport uses asio types; strand per session |
| F.5 Native daemon, zero JNI | IEngineController as driving port; AIDL in impl/ |
| F.6 Android first | impl/android/ prioritized (Phase 1) |
| F.7 New code, reference only | This design is reference-free |
| F.8 Multi-cast sink | vector<shared_ptr<ISink>> in services |
| F.9 Build-time cert inject | CryptoConfig with paths; empty = defaults |
| F.10 QuirksProfile | Not in Phase 0a; extensibility point preserved |
| F.11 Hexagonal | Inbound/outbound/driving ports explicit |

---

## 14. Complete File Tree

```
core/
  include/aauto/
    utils/
      ProtocolConstants.hpp
      Logger.hpp
    transport/
      ITransport.hpp
    crypto/
      ICryptoStrategy.hpp
    sink/
      IVideoSink.hpp
      IAudioSink.hpp
    source/
      IInputSource.hpp
      ISensorSource.hpp
    session/
      SessionState.hpp
      Framer.hpp
      Session.hpp
    service/
      IService.hpp
      ServiceBase.hpp
      VideoService.hpp
      AudioService.hpp
      InputService.hpp
      SensorService.hpp
      NavigationService.hpp
      PhoneStatusService.hpp
      BluetoothService.hpp
    engine/
      IEngineController.hpp
      Engine.hpp
  src/
    utils/
      ProtocolConstants.cpp
      Logger.cpp
    session/
      Framer.cpp
      Session.cpp
    service/
      VideoService.cpp
      AudioService.cpp
      InputService.cpp
      SensorService.cpp
      NavigationService.cpp
      PhoneStatusService.cpp
      BluetoothService.cpp
    engine/
      Engine.cpp
```
