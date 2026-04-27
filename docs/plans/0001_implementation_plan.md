# 0001 — Android Auto v2 Implementation Plan

> Created: 2026-04-16
> Status: APPROVED
> Approach: Vertical Slice (feature-first, not layer-first)
>
> **Implementation status (2026-04-27)**:
> - Phase 0a / 0b: DONE (see 0002, protocol.md)
> - Phase 1 (Walking Skeleton): DONE — sinks relocated per F.12 (see Phase 1 note)
> - Phase 2 (Audio): DONE — sinks relocated per F.12
> - Phase 3 (Input): DONE — input source relocated to Java app
> - Phase 4 (Remaining services): only Sensor + Microphone-stub are registered; the other 6 service classes exist but are NOT registered (advertise-without-response caused phone to throttle video cadence — see G.0 / troubleshooting #22). Plus 2 extras (Control, Microphone). See Phase 4 status table.
> - Phase 5 (AIDL daemon + App): DONE (see 0003)
> - Phase 6 (Hardening): see per-item status table — only 6.5 (multi-session) fully done; 6.1 / 6.4 partial; 6.2 / 6.3 / 6.7 not started; 6.6 / 6.8 not verified
> - Wireless AA (added later as a separate track, also originally labelled "Phase 6"): DONE (see 0004)

---

## Overview

Greenfield Android Auto headunit implementation on TCC803x Android 10 IVI.
Architecture decisions F.1~F.11 confirmed in `docs/architecture_review.md`.

**Core principle**: Each phase delivers a working, testable vertical slice.
Interfaces are defined alongside their first implementation, not upfront.

---

## Phase 0a: Core Architecture Design (Reference-Free)

**Goal**: Design the complete C++17 core architecture from first principles,
using only proto definitions and F.1~F.11 decisions. No reference to existing
implementations (openauto/aasdk/`../aa`) to avoid inheriting their structural
patterns.

| Step | Description | Output |
|------|-------------|--------|
| 0a.1 | Design all hexagonal port interfaces (C++ header-level) | docs/plans/0002_core_architecture_design.md |
| 0a.2 | Design Session state machine (8 states, transitions, timeouts) | Same doc |
| 0a.3 | Design Framer (binary wire protocol, fragmentation) | Same doc |
| 0a.4 | Design async data flow (asio handler chains, strand model) | Same doc |
| 0a.5 | Design Service pattern and error propagation model | Same doc |
| 0a.6 | Design Engine and factory injection model | Same doc |

**Exit criteria**: Architecture design reviewed and approved. All port
interfaces, state machines, and data flows documented with actual C++ code.

---

## Phase 0b: Protocol Fact Verification

**Goal**: After architecture is locked, consult reference code ONLY for
protocol facts (timing, byte sequences, edge cases). Structure is not
revisited.

| Step | Description | Output |
|------|-------------|--------|
| 0b.1 | Verify SSL handshake initiation sequence (who sends first) | Annotate 0002 doc |
| 0b.2 | Verify VERSION_REQUEST/RESPONSE exact fields and order | Annotate 0002 doc |
| 0b.3 | Verify media channel setup sequence (SETUP/CONFIG/START) | Annotate 0002 doc |
| 0b.4 | Identify timing constraints and known quirks | Annotate 0002 doc |

**Exit criteria**: Protocol facts verified. No structural changes to architecture.

---

## Phase 1: Walking Skeleton

**Goal**: USB plug-in -> SSL handshake -> video frame decoded on screen.
The absolute minimum vertical slice proving the architecture works end-to-end.

### What gets built

| Component | Layer | Notes |
|-----------|-------|-------|
| `ITransport` | core port | async_read / async_write / close |
| `AndroidUsbTransport` | impl/android | USB accessory via NDK, asio integration |
| `ICryptoStrategy` | core port | encrypt / decrypt / handshake |
| `OpenSslCryptoStrategy` | impl/common | OpenSSL SSL_CTX, HU cert (F.9 build-time inject) |
| `Framer` | core | AAP frame encode/decode (header + payload + encryption) |
| `Handshaker` | core | SSL handshake + version exchange state machine |
| `Session` | core | transport <-> services, lifecycle states |
| `Engine` | core | owns asio io_context, creates Session |
| `VideoService` | core/service | video channel message handling |
| `IVideoSink` | core port | onVideoFrame(H264 NAL unit) |
| `CallbackVideoSink` | core/sink | forwards H.264 NAL units to a callback (used by AIDL bridge per F.12) |
| Logger | core/utils | AA_LOG_* macros |
| Build system | root | Android.bp + CMakeLists.txt for all above |

### What is NOT built yet

- Audio, input, sensor, navigation, phone services
- AIDL daemon separation (runs as monolith initially)
- App layer (MainActivity, discovery monitors)
- Multi-session support
- IEngineController IPC

### F.12 update (2026-04-21)

Video decoding moved out of native into the app process. The native side now uses
`CallbackVideoSink`, which forwards encoded H.264 NAL units via AIDL
(`onVideoData`) to Java `VideoDecoder` (MediaCodec → Surface).
The originally planned native `AMediaCodecVideoSink` is intentionally not built.

### Temporary shortcuts (to be removed later)

- Engine runs in-process (no daemon split)
- USB device opened directly by engine (no UsbMonitor / app discovery)
- Hardcoded Surface for video output (no AaDisplayActivity)
- Single-session only

### Test plan

| Test | Type | Description |
|------|------|-------------|
| Framer round-trip | Unit (gtest) | encode -> decode identity for various payload sizes |
| Handshaker state transitions | Unit (gtest) | mock transport feeding recorded handshake bytes |
| Session lifecycle | Unit (gtest) | create -> handshake -> running -> disconnect states |
| USB -> video on screen | Manual | plug phone, verify AA screen renders on TCC803x |

**Exit criteria**: Phone plugged in via USB -> AA projection visible on display.

---

## Phase 2: Audio Path

**Goal**: Add audio output. Phone audio plays through vehicle speakers.

| Component | Layer | Notes |
|-----------|-------|-------|
| `AudioService` | core/service | audio channel message handling, multi-stream (media/guidance/call) |
| `IAudioSink` | core port | onAudioData(PCM), configure(sample rate, channels) |
| `CallbackAudioSink` | core/sink | forwards PCM via AIDL (`onAudioData`) to Java `AudioPlayer` per F.12 |
| Audio focus | impl/android | Android AudioManager integration for focus arbitration |

### Test plan

| Test | Type | Description |
|------|------|-------------|
| AudioService message dispatch | Unit | mock sink receives correct PCM data |
| Audio focus transitions | Unit | simulate focus gain/loss/duck |
| Phone audio playback | Manual | music + navigation voice plays on speakers |

**Exit criteria**: Video + audio both working. Navigation voice guidance audible.

---

## Phase 3: Input Path

**Goal**: Touch screen input reaches the phone. User can interact with AA UI.

| Component | Layer | Notes |
|-----------|-------|-------|
| `InputService` | core/service | input channel message handling |
| `IInputSource` | core port | onTouchEvent, onKey |
| Touch dispatch | app/android (Java) | `AaDisplayActivity.onTouchEvent` forwards touch events to engine via AIDL — replaces the originally planned native `TouchInputSource` |

### Test plan

| Test | Type | Description |
|------|------|-------------|
| Touch coordinate mapping | Unit | screen coords -> AAP coords correct |
| InputService message encode | Unit | touch events produce valid AAP messages |
| Touch interaction | Manual | tap, scroll, swipe on AA projection |

**Exit criteria**: Full interactive AA session — video + audio + touch.

---

## Phase 4: Remaining Channel Services

**Goal**: Complete all AAP service channels.

| Service | Channel | Priority | Status |
|---------|---------|----------|--------|
| NavigationStatusService | navigation status | HIGH — turn-by-turn display | CLASS EXISTS, NOT REGISTERED — fill_config only, no message handlers. Disabled 2026-04-27 (G.0 / troubleshooting #22) |
| PhoneStatusService | phone status | MEDIUM — battery, signal display | CLASS EXISTS, NOT REGISTERED — same reason |
| SensorService | sensor data | HIGH — GPS for navigation | DONE (service handler only); `ISensorSource` platform implementation deferred — no platform sensor source is currently wired in |
| MediaBrowserService | media browsing | LOW — music app browsing | CLASS EXISTS, NOT REGISTERED — same reason |
| MediaPlaybackService | media playback | MEDIUM — steering wheel controls | CLASS EXISTS, NOT REGISTERED — was the largest source of stub-induced cadence throttle (76KB MEDIA_CONFIG + 1s MEDIA_START) |
| BluetoothService | bluetooth | MEDIUM — BT pairing flow | CLASS EXISTS, NOT REGISTERED — same reason. `HeadunitConfig::bluetooth_mac` placeholder retained for future re-enable |
| GenericNotificationService | notifications | LOW | CLASS EXISTS, NOT REGISTERED — same reason |
| VendorExtensionService | vendor | LOW | CLASS EXISTS, NOT REGISTERED — same reason |

Additional services implemented during Phase 4 that were not in the original plan:

| Service | Channel | Notes |
|---------|---------|-------|
| ControlService | control | session-level control messages (heartbeat, channel open/close, focus) |
| MicrophoneService | microphone | required by some phones (e.g. Samsung SM-N981N) for handshake |

### Test plan

- Unit tests for each service's message encode/decode
- Integration test: recorded multi-channel AAP session replay
- Manual: navigation works, phone status displays, steering wheel media controls

**Exit criteria**: All primary channels functional. Secondary channels stubbed with logging.

---

## Phase 5: AIDL Daemon Separation + App Layer

**Goal**: Split monolith into aa-engine daemon + Android app (F.5 architecture).

### 5a: Daemon separation

| Component | Notes |
|-----------|-------|
| `IEngineController` | core driving port: start/stop session, status callbacks |
| `AidlEngineController` | impl/android/aidl: AIDL server in aa-engine |
| aa-engine binary | /system/bin/aa-engine, init.rc service registration |
| SELinux policy | sepolicy for daemon process |

### 5b: App layer

| Component | Notes |
|-----------|-------|
| `AaService` | Android Service, AIDL client to aa-engine |
| `UsbMonitor` | USB device attach/detach -> notify engine |
| `BtMonitor` | Bluetooth discovery (for wireless AA) |
| `SessionManager` | multi-session tracking, active session policy |
| `DeviceRegistry` | persistent phone registry (instance_id -> alias) |
| `MainActivity` | launcher entry |
| `AaDisplayActivity` | Surface provider for video sink |

### 5c: Surface handoff

- AaDisplayActivity creates Surface and passes to aa-engine (via AIDL fd or shared memory)
- Video sink receives Surface reference at session start

### Test plan

| Test | Type | Description |
|------|------|-------------|
| AIDL round-trip | Integration | app -> aidl -> engine -> callback -> app |
| Engine crash recovery | Integration | kill aa-engine, verify app detects and restarts |
| USB hot-plug | Manual | plug/unplug during session |
| Full lifecycle | Manual | boot -> USB plug -> AA session -> unplug -> clean shutdown |

**Exit criteria**: Daemon architecture working. App discovers phone, engine handles
session, clean separation. Same functionality as Phase 3 end but properly architected.

---

## Phase 6: Hardening

**Goal**: Production quality — stability, error recovery, testing depth.

| Step | Description | Status |
|------|-------------|--------|
| 6.1 | Core unit test coverage >= 80% (gtest + CTest) | PARTIAL — 42 tests passing in core; coverage % not measured yet |
| 6.2 | ASan/UBSan enabled for test builds, clean run | NOT STARTED — no sanitizer flags in build |
| 6.3 | Framer + crypto envelope fuzzing (libFuzzer or AFL) | NOT STARTED — no fuzz harnesses present |
| 6.4 | Connection error recovery: USB disconnect/reconnect, SSL failure retry | PARTIAL — same-phone USB↔wireless transport switch covered (F.16); broader recovery unverified |
| 6.5 | Multi-session support: 2 phones simultaneously (F.3) | DONE — `SessionManager` (F.13) |
| 6.6 | Background audio: video sink detach when AA not foreground | NOT VERIFIED |
| 6.7 | QuirksProfile framework (F.10): extensible but empty initially | NOT STARTED |
| 6.8 | Performance profiling: latency (USB read -> frame display), CPU usage | NOT VERIFIED |

**Exit criteria**: Stable daily-driver quality on TCC803x hardware.

---

## Dependency Graph

```
Phase 0 (Reference Study)
  |
  v
Phase 1 (Walking Skeleton: USB -> Handshake -> Video)
  |
  +---> Phase 2 (Audio) ---> Phase 3 (Input)
  |                              |
  |                              v
  |                         Phase 4 (Remaining Services)
  |                              |
  |                              v
  +-----------------------> Phase 5 (AIDL Daemon + App)
                                 |
                                 v
                            Phase 6 (Hardening)
```

Note: Testing is continuous from Phase 1, not a separate final phase.
Each phase includes its own unit tests.

---

## Risks

| Level | Risk | Phase | Mitigation |
|-------|------|-------|------------|
| **CRITICAL** | AAP handshake fails with real phone (cert/protocol mismatch) | 1 | Phase 0 deep study; test with multiple phone models early |
| **HIGH** | TCC803x AMediaCodec/AAudio API limitations (Android 10 NDK) | 1, 2 | Verify API availability in Phase 1 before building abstractions |
| **HIGH** | Reference code misinterpretation (aasdk protocol understanding) | 1 | Phase 0 thoroughness; compare with Wireshark USB captures |
| **MEDIUM** | OpenSSL version mismatch (system vs bundled) | 1 | Decide in Phase 1: use system libssl or vendor our own |
| **MEDIUM** | SELinux policy complexity for native daemon | 5 | Allocate dedicated time; study existing HAL daemon policies |
| **MEDIUM** | Surface sharing between app process and native daemon | 5 | Research AIDL ParcelFileDescriptor / HardwareBuffer early |
| **LOW** | standalone asio + NDK build compatibility | 1 | Verify immediately in Phase 1 scaffolding |

---

## Conventions

- All source in English (CLAUDE.md policy)
- C++17, 4-space indent, same-line braces
- Log tags: `#define LOG_TAG "ClassName"`
- Logging: `AA_LOG_I()`, `AA_LOG_D()`, `AA_LOG_W()`, `AA_LOG_E()`
- Interfaces defined alongside first implementation, refined as needed
- Each phase starts with a detailed sub-plan in docs/plans/
