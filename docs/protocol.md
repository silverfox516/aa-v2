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

**Verified**: Samsung SM-N981N (2026-04-20)

```
1.  HU → Phone:  VERSION_REQUEST (v1.1)
2.  Phone → HU:  VERSION_RESPONSE (v1.7, status=0)
3.  HU ↔ Phone:  SSL/TLS handshake (3 round-trips)
4.  HU → Phone:  AUTH_COMPLETE (plaintext)
    --- encrypted from here ---
5.  Phone → HU:  ServiceDiscoveryRequest
6.  HU → Phone:  ServiceDiscoveryResponse (7 channels)
7.  Phone → HU:  ChannelOpenRequest (ch 1-7, sequential)
8.  HU → Phone:  ChannelOpenResponse (SUCCESS, per channel)
9.  HU → Phone:  VideoFocusNotification (PROJECTED, on ch 1)
10. HU → Phone:  DrivingStatus (UNRESTRICTED, on ch 6)
11. Phone → HU:  AudioFocusRequest (RELEASE)
12. HU → Phone:  AudioFocusNotification (LOSS)
13. Phone → HU:  MediaSetup (ch 2, audio media, codec=PCM)
14. HU → Phone:  MediaConfig (READY, max_unacked=5)
15. Phone → HU:  MediaSetup (ch 1, video, codec=H264_BP)
16. HU → Phone:  MediaConfig (READY, max_unacked=5)
    ... (ch 3-4 audio guidance/system same pattern)
17. Phone → HU:  SensorStartRequest (type=DRIVING_STATUS)
18. HU → Phone:  DrivingStatus (UNRESTRICTED)
19. Phone → HU:  MediaStart (ch 1, video, session_id=0)
20. Phone → HU:  VideoFocusRequest
21. Phone → HU:  CodecConfig (H.264 SPS/PPS, 29 bytes)
22. Phone → HU:  MediaData (H.264 frames, continuous)
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
- HU must send ACK after `max_unacked` frames or phone stalls
- CodecConfig (SPS/PPS) arrives as message type 0x0001 — same as VERSION_REQ
  on channel 0, but on video channel it means CODEC_CONFIG

## 7. Frame Flags

**Verified**: Samsung SM-N981N (2026-04-21)

Flags byte (byte 1 of AAP header):
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
- ServiceDiscoveryRequest includes large icon data (~800 bytes)
