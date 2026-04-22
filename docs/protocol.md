# AAP Protocol — Verified Behavior

> Living document. Records protocol sequences verified on real devices.
> Each entry notes the device, date, and observed behavior.

---

## 1. Handshake Sequence

**Verified**: Samsung SM-N981N (2026-04-20)

```
HU → Phone:  VERSION_REQUEST   [0,1,0,1] (v1.1)       plaintext, ch 0
Phone → HU:  VERSION_RESPONSE  [0,1,0,7,0,0] (v1.7)   plaintext, ch 0
HU → Phone:  SSL ClientHello                            plaintext, ch 0 (type 3)
Phone → HU:  SSL ServerHello + Certificate + ...        plaintext, ch 0 (type 3)
HU → Phone:  SSL Finished                               plaintext, ch 0 (type 3)
HU → Phone:  AUTH_COMPLETE     {status=0}               plaintext, ch 0 (type 4)
--- all subsequent messages encrypted ---
```

**Key facts**:
- VERSION_REQUEST payload is raw bytes `[major:2 BE][minor:2 BE]`, NOT protobuf
- VERSION_RESPONSE is raw bytes `[major:2 BE][minor:2 BE][status:2 BE]`
- AAP handshake order: VERSION → SSL → AUTH (not SSL → VERSION)
- AUTH_COMPLETE is sent by HU (not phone), as the last plaintext message
- After AUTH_COMPLETE, all channels (including control ch 0) are encrypted

## 2. Service Discovery

**Verified**: Samsung SM-N981N (2026-04-20)

```
Phone → HU:  ServiceDiscoveryRequest   (device info + icons)    encrypted, ch 0
HU → Phone:  ServiceDiscoveryResponse  (HU capabilities)       encrypted, ch 0
```

**ServiceDiscoveryRequest** (839-879 bytes):
- `device_name`: "samsung SM-N981N"
- `label_text`: "Android"
- `phone_info`: present with v1.6+, absent with v1.1
- Bulk of payload is icon data (small/medium/large_icon)

**ServiceDiscoveryResponse** must include:
- Video sink (H264 BP)
- Audio sinks (media, guidance, system)
- Input source (touchscreen)
- Sensor source (driving status, night mode)
- **Microphone source** (PCM stub) — Samsung refuses to proceed without this

## 3. Audio Focus Exchange

**Verified**: Samsung SM-N981N (2026-04-20)

```
Phone → HU:  AudioFocusRequest(RELEASE=4)              encrypted, ch 0
HU → Phone:  AudioFocusNotification(LOSS=3)             encrypted, ch 0
```

Occurs immediately after ServiceDiscoveryResponse, before ChannelOpen.
Phone sends RELEASE (type=4) as initial state — "I hold no audio focus".
HU must respond with LOSS (type=3). Responding with GAIN causes phone to stall.

**AudioFocus mapping** (HU grants unconditionally):

| Phone Request | HU Response |
|---------------|-------------|
| GAIN (1) | STATE_GAIN (1) |
| GAIN_TRANSIENT (2) | STATE_GAIN_TRANSIENT (2) |
| GAIN_TRANSIENT_MAY_DUCK (3) | STATE_GAIN_TRANSIENT_GUIDANCE_ONLY (7) |
| RELEASE (4) | STATE_LOSS (3) |

## 4. Channel Open

**Verified**: Samsung SM-N981N (2026-04-20)

```
Phone → HU:  ChannelOpenRequest   {priority, service_id}   encrypted, ch N (target)
HU → Phone:  ChannelOpenResponse  {status=SUCCESS}          encrypted, ch N (target)
  (repeated for each service channel)
```

**Key facts**:
- Phone initiates ChannelOpenRequest (not HU)
- Request is sent on the **target channel** (ch 1 for video, ch 2 for audio, etc.), NOT on ch 0
- HU responds on the same target channel
- Phone opens all advertised channels sequentially (ch 1 → 2 → 3 → 4 → 5 → 6 → 7)
- Requires MicrophoneService to be advertised; otherwise phone never sends ChannelOpenRequest

**Verified channel assignment**:

| Channel | Service | ServiceConfiguration field |
|---------|---------|---------------------------|
| 0 | Control | (implicit, not in response) |
| 1 | Video sink | media_sink_service (H264 BP) |
| 2 | Audio sink (media) | media_sink_service (PCM) |
| 3 | Audio sink (guidance) | media_sink_service (PCM) |
| 4 | Audio sink (system) | media_sink_service (PCM) |
| 5 | Input source | input_source_service |
| 6 | Sensor source | sensor_source_service |
| 7 | Microphone source | media_source_service (PCM) |

## 5. Complete Session Startup Sequence

**Verified**: Samsung SM-N981N (2026-04-22)

```
 1. HU �� Phone:  VERSION_REQUEST (v1.1)
 2. Phone → HU:  VERSION_RESPONSE (v1.7, status=0)
 3. HU ↔ Phone:  SSL/TLS handshake (3 round-trips)
 4. HU → Phone:  AUTH_COMPLETE (plaintext)
    --- encrypted from here ---
 5. Phone → HU:  ServiceDiscoveryRequest
 6. HU → Phone:  ServiceDiscoveryResponse (7 channels)
 7. Phone → HU:  ChannelOpenRequest (ch 1-7, sequential)
 8. HU → Phone:  ChannelOpenResponse (SUCCESS, per channel)
    HU → Phone:  VideoFocusNotification (PROJECTED, on ch 1)
    HU → Phone:  DrivingStatus (UNRESTRICTED, on ch 6)
 9. Phone → HU:  AudioFocusRequest (RELEASE)
    HU → Phone:  AudioFocusNotification (LOSS)
10. Phone → HU:  MediaSetup (audio/video channels)
    HU → Phone:  MediaConfig (READY, max_unacked=10) per channel
11. Phone → HU:  SensorStartRequest (DRIVING_STATUS)
    HU → Phone:  SensorStartResponse (SUCCESS)
    HU → Phone:  DrivingStatus (UNRESTRICTED)
12. Phone → HU:  SensorStartRequest (NIGHT_MODE)
    HU → Phone:  SensorStartResponse (SUCCESS)
13. Phone → HU:  MediaStart (video, session_id=0)
14. Phone → HU:  VideoFocusRequest
15. Phone → HU:  CodecConfig (H.264 SPS/PPS, 29 bytes)
    HU → Phone:  MEDIA_ACK
16. Phone → HU:  MediaData (H.264 frames, ~30fps)
    HU → Phone:  MEDIA_ACK (per frame, credit return)
    --- continuous video + audio streaming ---
```

## 6. Media Streaming

**Verified**: Samsung SM-N981N (2026-04-21)

```
Phone → HU:  MEDIA_SETUP     {codec_type}                 per media channel
HU → Phone:  MEDIA_CONFIG    {status=READY, max_unacked=5, config_indices=[0]}
Phone → HU:  MEDIA_START     {session_id, config_index}
Phone → HU:  CODEC_CONFIG    (H.264 SPS/PPS for video)    type=0x0001
Phone → HU:  MEDIA_DATA      [timestamp:8][payload]        type=0x0000
HU → Phone:  MEDIA_ACK       {session_id, ack=1}          every max_unacked frames
```

**Key facts**:
- MEDIA_DATA is prefixed by 8-byte int64 timestamp (microseconds)
- CodecConfig (SPS/PPS) arrives as message type 0x0001 — same as VERSION_REQ
  on channel 0, but on video channel it means CODEC_CONFIG
- Phone uses credit-based flow control (see ACK section below)

## 7. AAP Wire Frame Format

Every message on the USB transport is wrapped in an AAP frame:

```
Byte 0:     channel_id (uint8)
Byte 1:     flags (see below)
Byte 2-3:   payload_length (uint16, big-endian)
Byte 4...:  payload (payload_length bytes)
              └─ [message_type:2 BE][protobuf body]
                 (encrypted as a whole after SSL handshake)
```

- **channel_id**: identifies which service this frame belongs to (0=control, 1=video, ...)
- **payload**: after SSL, the entire payload (including the 2-byte message type) is
  TLS-encrypted. The header (bytes 0-3) is always plaintext.
- **Fragmentation**: payloads larger than 16 KiB are split into multiple frames.
  The first fragment has FragInfo=First, intermediate ones Continuation, last one Last.
  Unfragmented messages use FragInfo=Unfragmented (3).
- **Multi-first header**: First fragments (First, NOT Unfragmented) carry an extra
  4-byte `total_size` field between the header and payload. This field is NOT
  included in `payload_length`. Wire layout: `[header:4][total_size:4][data:payload_length]`.
- **Encryption**: each fragment must be decrypted individually via SSL_read
  (per-fragment decrypt model). Do NOT reassemble ciphertext before decrypting —
  TLS records do not align with AAP message boundaries.

### Flags byte (byte 1):
```
bit[0-1]: FragInfo (0=continuation, 1=first, 2=last, 3=unfragmented)
bit[2]:   control-on-media flag (0x04)
bit[3]:   encrypted flag (0x08)
```

| Scenario | Flags |
|----------|-------|
| Plaintext handshake | 0x03 (first\|last) |
| Encrypted on ch 0 | 0x0b (first\|last\|encrypted) |
| Control msg on media ch (e.g., ChannelOpenResponse on ch 1) | 0x0f (first\|last\|encrypted\|control) |
| Non-control msg on media ch (e.g., VideoFocusNotification) | 0x0b (first\|last\|encrypted) |

Control message type range: 0x0001-0x0013.

## 8. ACK and Flow Control

**Verified**: Samsung SM-N981N (2026-04-22)

Phone uses **credit-based flow control** for media streams:

- `max_unacked` in MEDIA_CONFIG: window size (max frames in flight)
- `ack` field in MEDIA_ACK: credits returned to phone
- Phone starts with `max_unacked` credits. Each frame sent decrements credits.
  When credits reach 0, phone stops sending until ACK arrives.

**Critical**: HU must ACK every frame immediately. If HU batches ACKs
(e.g., ACK every 10 frames), the phone exhausts credits after the initial
burst and falls back to ~3fps (one frame per ACK round-trip).

```
HU → Phone:  MEDIA_CONFIG {max_unacked=10}     "you can buffer 10"
Phone → HU:  MEDIA_DATA (frame 1)               credit: 9
Phone → HU:  MEDIA_DATA (frame 2)               credit: 8
HU → Phone:  MEDIA_ACK {ack=1}                  credit: 9
Phone → HU:  MEDIA_DATA (frame 3)               credit: 8
HU → Phone:  MEDIA_ACK {ack=1}                  credit: 9
...
```

## 9. Required Responses

Messages the phone sends that REQUIRE a response. Missing any of these
causes the phone to stall or delay video start.

| Phone sends | HU must respond | Effect if missing |
|-------------|-----------------|-------------------|
| ServiceDiscoveryRequest | ServiceDiscoveryResponse | No ChannelOpen |
| ChannelOpenRequest | ChannelOpenResponse(SUCCESS) | Channel not active |
| AudioFocusRequest(RELEASE) | AudioFocusNotification(LOSS) | Phone stalls |
| MediaSetup | MediaConfig(READY) | No MediaStart |
| SensorStartRequest | **SensorStartResponse(SUCCESS)** + data | Video start delayed |
| KeyBindingRequest | **KeyBindingResponse(SUCCESS)** | Input not active |
| MEDIA_DATA | **MEDIA_ACK** (every frame) | fps drops to ~3 |
| PingRequest | PingResponse (echo timestamp) | Session timeout |

---

## Device-Specific Notes

### Samsung SM-N981N (Galaxy Note 20, Android 13)

- Reports AAP v1.7
- **Requires MicrophoneService**: will not send ChannelOpenRequest without
  media_source_service in ServiceDiscoveryResponse
- Sends AudioFocusRequest(RELEASE) before ChannelOpen — must respond LOSS
- Requires VideoFocusNotification(PROJECTED) + DrivingStatus(UNRESTRICTED) before
  sending MediaSetup
- ChannelOpenResponse must have control-on-media flag (0x04) on non-control channels
- SensorStartResponse required before video starts (causes multi-second delay if missing)
- Credit-based ACK: must ACK every frame for 30fps
- ServiceDiscoveryRequest includes large icon data (~800 bytes)

### Telechips TCC803x (Android 10 IVI)

- **ubsan mul-overflow**: `MediaCodec.releaseOutputBuffer(idx, true)` crashes in
  libstagefright. Use explicit timestamp: `releaseOutputBuffer(idx, System.nanoTime())`
- **android.car.usb.handler**: System app intercepts ALL USB attach events via
  `UsbProfileGroupSettingsManager` fixed handler routing. Crashes with NPE when
  device has already re-enumerated (AOA switch). Workaround: launch our activity
  with `FLAG_ACTIVITY_REORDER_TO_FRONT` to reclaim foreground.
- **Non-protected broadcast**: Apps with `persistent="true"` run in system-like
  context. Custom broadcast actions are flagged as non-protected and may not be
  delivered. Use direct callbacks instead of `sendBroadcast()`.
- **SELinux**: Native daemon needs `seclabel u:r:su:s0` in init.rc for eng builds.
  Without it, the daemon is killed before registering with ServiceManager.
- **sharedUserId**: App must use `android.uid.system` to access BT/WiFi system
  APIs. Without it, `BluetoothManagerService` rejects calls with misleading
  "System has not boot yet" error.
- **NETWORK_STACK**: Required for `startSoftAp()`. Must be in both manifest
  and `privapp-permissions-*.xml`.

## 10. Wireless Android Auto (AAW)

**Verified**: Samsung SM-N981N (2026-04-22) — RFCOMM + WiFi handshake OK,
AAP version mismatch (under investigation)

### Connection Sequence

```
1. User enables BT + WiFi AP on HU
2. HU: RFCOMM server listening on AAW UUID
3. Phone: discovers HU via BT SDP, connects RFCOMM
4. HU → Phone:  VERSION_REQUEST (AAW, not AAP)
5. HU → Phone:  START_REQUEST {ip, port}
6. Phone → HU:  INFO_REQUEST
7. HU → Phone:  INFO_RESPONSE {ssid, password, bssid, security, ap_type}
8. Phone → HU:  CONNECTION_STATUS
9. Phone → HU:  START_RESPONSE {status=0}
10. Phone connects to WiFi AP → TCP port 5277
11. AAP handshake over TCP (VERSION → SSL → AUTH) — same as USB
```

### AAW RFCOMM Protocol

- **UUID**: `4DE17A00-52CB-11E6-BDF4-0800200C9A66`
- **Frame**: `[2B length][2B msgId][protobuf payload]`
- **Message IDs**: 1=START_REQ, 2=INFO_REQ, 3=INFO_RSP, 4=VER_REQ,
  5=VER_RSP, 6=CONN_STATUS, 7=START_RSP
- **TCP port**: 5277
- **After handshake**: RFCOMM stays open as keep-alive channel

### Key Facts
- No BLE required — classic BT RFCOMM with SDP service record
- WiFi AP must be enabled before RFCOMM listen (HU needs hotspot config)
- Phone initiates RFCOMM connection (HU is server)
- AAP protocol over TCP is identical to USB — transport-agnostic
