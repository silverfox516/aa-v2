# Android Auto Headunit — Architecture Review

> 작성 시점: 2026-04-16. 최종 갱신: 2026-04-16 (F.1~F.11 결정 반영).
> 본 문서는 aa-v2 greenfield 재작성의 설계 기반 문서.
> Part 0 = 프로젝트 목적, Part A = 채택된 구조,
> Part E = 가정 재검토, Part F = 확정된 결정 누적.

---

## Part 0. 목적

H/U에서 동작하는 AA 어플리케이션 작성을 통해 AA 분석하여 AA 포팅 작업 시
필요한 능력 확보.

- AA 동작에 필요한 플랫폼 사이드의 요구, 제한 사항, 기술들 파악, 확보
- AAP 메시지들의 역할과 실 동작에서의 송수신
- 이를 위한 어플리케이션의 이상적 구조 설계, 확보

### 이 목적이 설계 결정에 미치는 영향

본 프로젝트는 출시용 제품이 아니라 **학습/분석/포팅 능력 확보**가 목표다.
이 사실이 Part A의 설계 원칙과 Part F의 결정들 전반의 근거로 작용한다.

- **이상적 구조 우선**: 시간 절약을 위한 단축 구조는 채택하지 않는다.
  옳은 구조를 위해 더 많은 시간을 쓰는 결정이 정당화된다 (F.1, F.5, F.11).
- **풀 구현 vs stub은 학습 가치로 판단**: 모든 서비스를 풀 구현해야 할
  필요는 없다. 채널 등록만으로 폰의 동작을 관찰할 수 있다면 그 자체로
  분석 가치가 있다 (Phase 4 stub 서비스들).
- **문서가 산출물**: 동작에 대한 이해(`protocol.md`, `aap_messages.md`,
  `troubleshooting.md`)와 결정의 근거(본 문서 Part F)는 코드와 동등한
  결과물이다. 알게 된 것은 반드시 기록한다.
- **레퍼런스 코드는 사실 검증용으로만 사용**: 구조를 베끼면 학습이 안
  된다. Phase 0a에서 reference-free 설계를 먼저 하고, Phase 0b에서만
  프로토콜 사실 확인용으로 참고한다 (0001 plan).

---

## Part A. 채택된 구조 (F.1~F.11 결정 반영)

### A.1 설계 원칙

1. **Hexagonal Architecture (Ports & Adapters)**: core가 ports(추상 인터페이스)를
   정의하고, 플랫폼 adapter가 양쪽에 붙는다. inbound port(ITransport —
   데이터 유입), outbound port(IAudioSink/IVideoSink — 데이터 유출),
   driving port(IEngineController — 제어 명령 유입). (F.11)
2. **엄격한 단방향 의존**: app → impl → core. core는 platform 무지(無知).
   `#ifdef __ANDROID__` 는 core 어디에도 없어야 한다.
3. **인터페이스 분리**: sink, source, transport, crypto, engine controller 모두
   추상 인터페이스로 분리. impl 계층이 구현체를 제공.
4. **의존성 주입**: Service는 sink/source를 직접 만들지 않는다. 외부에서 attach.
   Session은 transport를 직접 만들지 않는다. 외부에서 주입.
5. **Composition over hierarchy**: HU 제품마다 서비스 구성이 다르다. 이를
   `ServiceComposition` 같은 데이터로 표현하고, 하드코딩하지 않는다.
6. **세션 1:1 폰**: 폰 1대 = Session 1개 = transport 1개 = crypto context 1개.
   다대다 매핑 없음. 멀티 폰은 Session N개로 표현. (F.3: 단일 프로세스)
7. **Async I/O**: standalone asio io_context 기반 이벤트 루프. 세션별 thread가
   아니라 async handler로 다중 세션 처리. (F.4)
8. **명시적 lifecycle**: Session/Service/Transport 모두 상태가 enum으로 명시되고,
   전이 trigger와 callback이 코드에 드러나야 한다. "암묵적 상태" 금지.
9. **Test seam**: core는 mock transport/sink/crypto 만으로 단위 테스트 가능해야
   한다. impl/app 없이도 protocol 로직을 검증할 수 있어야 한다.
10. **Native daemon — JNI 없음**: engine은 독립 native 프로세스. Android app과는
    AIDL, Yocto UI와는 D-Bus/Unix socket으로 통신. JNI 0 LOC. (F.5)
11. **명명과 위치의 일관성**: `core/`에 인터페이스(ports), `impl/<platform>/`에
    구현(adapters), `app/<platform>/`에 UI/lifecycle/discovery. 같은 종류 코드가
    두 곳에 있으면 안 됨.

### A.2 아키텍처 다이어그램

```
                    Driving Adapters                    Engine Core                    Driven Adapters
              ┌───────────────────────┐                                        ┌───────────────────────┐
              │ App (per platform)     │                                        │ Platform Sinks        │
              │  • Discovery           │                                        │  • AMediaCodecVideo   │
              │    (USB/BT/WiFi)       │    IEngineController                   │  • AAudioSink         │
              │  • UI (Surface 제공)   │───(driving port)──►┌────────────────┐  │  • GStreamerVideo     │
              │  • 사용자 입력         │                    │                │  │  • AlsaAudioSink      │
              │                        │                    │   Engine       │  └──────▲────────────────┘
              │  Android: AIDL         │                    │     │          │         │
              │  Yocto: D-Bus/socket   │                    │     ▼          │    IAudioSink / IVideoSink
              └───────────────────────┘                    │   Session(s)   │    (outbound ports)
                                                           │     │          │
              ┌───────────────────────┐                    │     ▼          │
              │ Transport Adapters     │                    │   Services     │
              │  • UsbTransport        │    ITransport      │   (per channel)│
              │  • TcpTransport        │◄──(inbound port)───│     │          │
              │  • CompositeWireless   │                    │     ▼          │
              └───────────────────────┘                    │   Framer       │
                                                           │   Crypto       │
              ┌───────────────────────┐                    │                │
              │ Input/Sensor Adapters  │    IInputSource    │                │
              │  • TouchInput          │───(inbound port)──►│                │
              │  • GpsSensor           │    ISensorSource    │                │
              └───────────────────────┘                    └────────────────┘
```

**프로세스 경계** (F.5):
```
┌─────────────────────────┐          IPC           ┌──────────────────────────────┐
│ App Process              │  ◄──AIDL/D-Bus──►     │ aa-engine Process              │
│  • Java app (Android)    │    (제어 명령만)       │  • Engine + Sessions           │
│  • Qt app (Yocto)        │                        │  • asio io_context             │
│  • Discovery monitors    │                        │  • AMediaCodec / GStreamer     │
│  • Surface / UI          │                        │  • AAudio / ALSA               │
└─────────────────────────┘                        └──────────────────────────────┘
  미디어 데이터는 IPC 안 탐 — engine이 NDK/native API로 직접 출력
```

### A.3 디렉토리 레이아웃

```
aa-v2/
├── core/                          # platform-free C++17, standalone asio (header-only)
│   ├── include/aauto/
│   │   ├── transport/             # ITransport (async_read/async_write)
│   │   ├── crypto/                # ICryptoStrategy, CryptoConfig (빌드시 주입)
│   │   ├── session/               # Session, Handshaker, Framer, PhoneInfo
│   │   ├── service/               # IService + 각 서비스 헤더
│   │   ├── sink/                  # IVideoSink, IAudioSink (multi-cast: vector<sink>)
│   │   ├── source/                # IInputSource, ISensorSource
│   │   ├── engine/                # Engine, IEngineController, HeadunitConfig, Composition
│   │   └── utils/                 # Logger, ProtocolUtil
│   └── src/                       # 위 헤더 구현
│
├── third_party/
│   └── asio/                      # standalone asio headers (vendored, boost-free)
│
├── impl/
│   ├── common/                    # 모든 OS 공통 (libusb 기반 USB transport 등)
│   ├── android/
│   │   ├── transport/             # AndroidUsbTransport, TcpTransport
│   │   ├── sink/                  # AMediaCodecVideoSink, AAudioSink
│   │   ├── aidl/                  # AidlEngineController (IEngineController 구현)
│   │   └── source/                # TouchInputSource, SensorSource
│   └── linux/
│       ├── transport/             # UsbTransport (libusb), TcpTransport
│       ├── sink/                  # GStreamerVideoSink, AlsaAudioSink
│       ├── dbus/                  # DbusEngineController (IEngineController 구현)
│       └── source/                # EvdevInputSource, GpsSensorSource
│
├── app/
│   ├── android/
│   │   ├── ui/                    # MainActivity, AaDisplayActivity
│   │   ├── discovery/             # UsbMonitor, BtMonitor, WifiMonitor
│   │   ├── manager/               # SessionManager, DeviceRegistry
│   │   └── service/               # AaService (AIDL client, lifecycle bridge)
│   └── linux/
│       ├── ui/                    # Qt main + UI
│       └── discovery/             # UdevMonitor, BluezMonitor
│
├── protobuf/                      # AAP .proto 정의 + 빌드 산출물
├── docs/
│   ├── architecture_review.md     # 본 문서
│   └── plans/                     # 작업 계획 (CLAUDE.md 정책)
├── tests/
│   ├── core/                      # mock-based unit tests (gtest)
│   └── integration/               # recorded AAP byte stream 시나리오
├── Android.bp                     # Soong 빌드 정의
└── CMakeLists.txt                 # CMake 빌드 정의 (Yocto + 호스트 테스트)
```

### A.4 핵심 컴포넌트 책임

| Component | 책임 | 책임 아닌 것 |
|---|---|---|
| `Engine` | Session 생성/삭제, asio io_context 소유, IEngineController 콜백 수신 | UI, discovery, 활성화 정책 |
| `Session` | transport ↔ services 중개, encryption, framing, lifecycle state machine | UI 알림, 다른 Session 인지 |
| `Service` | AAP 채널 메시지 처리, sink vector로 multi-cast (F.8) | sink 생성, transport 직접 호출 |
| `Sink/Source` | 플랫폼 출력/입력 어댑터 (outbound/inbound port 구현) | protocol 파싱 |
| `Transport` | async 바이트 스트림 (async_read/async_write via asio) | 메시지 framing, encryption |
| `IEngineController` | engine ↔ app IPC 경계 (driving port) | 비즈니스 결정 |
| `AidlEngineController` | Android AIDL 구현 | Yocto 지원 |
| `DbusEngineController` | Yocto D-Bus/socket 구현 | Android 지원 |
| `SessionManager` (app) | 여러 Session 트래킹, 활성 1개 정책, sink 라우팅 | protocol 처리 |
| `DeviceRegistry` (app) | instance_id → 사용자 별명 / 자동연결 정책 영속화 | 세션 lifecycle |
| `QuirksProfile` (확장점) | 폰 모델별 quirk 데이터 (F.10, 구현은 필요 시) | — |

### A.5 멀티-디바이스 모델

- **Session per phone**: 폰 N대 = Session N개. 각자 transport, crypto, async handler chain.
  단일 프로세스, 단일 io_context에서 모든 세션 스케줄링. (F.3, F.4)
- **Active session 한 개**: 차량 스피커/디스플레이는 하나뿐이므로 한 번에 한
  Session만 sink 부착. 나머지는 dormant (handshake/keepalive는 유지해도 됨,
  정책 결정). 전환은 sink vector 교체로 즉시 (zero-copy).
- **DeviceRegistry**: 폰 instance_id를 영속 저장. "이전에 본 폰은 자동 활성"
  같은 정책 가능.
- **Background audio**: 화면을 떠도 active session의 audio sink는 유지. video sink만 detach.

### A.6 Test 전략

- **core 단위 테스트**: `MockTransport`(async scriptable), `MockCryptoStrategy`(passthrough),
  `RecordingVideoSink/AudioSink` 로 protocol 시나리오 재현. 폰 없이 검증.
- **integration 테스트**: 실제 OpenSSL + recorded AAP byte stream(.bin) 으로
  end-to-end. CI에서도 실행 가능.
- **빌드 타깃**: gtest + CTest (CMake). Android.bp는 `cc_test` 모듈.
  호스트 Linux CMake 빌드로 빠른 개발 사이클, Android `cc_test`로 타깃 검증. (F.2)
- **안전성 보완**: ASan/UBSan을 테스트 빌드에 기본 활성화. AAP framer/crypto
  envelope 코드에 fuzzing 적용 (C++ 메모리 안전성 보완, F.1 trade-off).

---

## Part E. What we didn't question (가정 재검토)

Part A의 "이상적 구조"는 사실 현재 코드의 폴리시드 버전에 가깝다 — survey
결과를 먼저 보고 그 위에서 사고했기 때문(anchoring bias). 이 섹션은 우리가
무비판적으로 받아들인 가정들을 명시적으로 뒤집어 본다. 채택 권고가 아니라
**사고의 옵션 공간**을 넓히기 위한 도구.

### E.1 가정: "core는 C++로 짠다"

**왜 의심해야 하나**: openauto/aasdk가 C++라서 이어받았을 뿐, first-principle
근거가 약하다. 우리가 직접 짜는 부분(protocol 파서, 세션 상태머신, AAP 메시지
디스패치)은 **메모리 안전성 + 패턴 매칭 + 가벼운 동시성**이 더 가치 있다.

**대안들**:
- **Rust core**: 프로토콜 파서/framer는 unsafe 없이 작성 가능. lifetime이
  buffer ownership을 컴파일 타임에 검증 → "이중 free", "use-after-free" 류
  버그를 원천 차단. tokio 한 thread pool로 N 세션 async 처리.
- **Kotlin Multiplatform**: app과 core가 같은 언어. JNI 자체가 사라짐.
  AAOS(Android Automotive)면 native가 굳이 필요 없을 수도.
- **Zig**: C ABI 호환 + 빌드 단순. 임베디드 친화적.

**Trade-off**: Rust/Kotlin은 OpenSSL/MediaCodec 같은 기존 native 의존성을
FFI로 다리 놓아야 함. 학습 비용. 그러나 "AAP 프로토콜 버그가 메모리 안전성
때문에 디버깅 어려움" 같은 실제 비용과 비교해야.

### E.2 가정: "단일 프로세스에서 모든 세션을 돌린다"

**왜 의심**: 폰 한 대가 SSL 핸드셰이크 무한 retry loop에 빠지거나 디코더가
SIGSEGV를 내면 **다른 폰 세션도 같이 죽는다**. 자동차 환경에서 isolation은
보안·신뢰성 양쪽으로 가치가 큼.

**대안: process-per-session**
```
aa-supervisor (Android Service)
  ├─ fork → aa-session-worker (phone A)  ← BT MAC + USB FD pass
  ├─ fork → aa-session-worker (phone B)
  └─ Binder/Unix socket으로 audio/video PCM 데이터만 IPC
```

각 worker가 sandbox(SELinux domain) 내에서만 동작 → A 폰 SSL 버그가 B 영향 X.
Crash 시 supervisor가 자동 재기동.

**Trade-off**: IPC overhead (PCM/H.264 byte 전송 자체는 audio 1Mbps, video
10Mbps 정도라 modern Linux IPC로 충분). 코드 분리 비용. AAOS 아키텍처와
잘 맞음.

### E.3 가정: "세션마다 3개 worker thread (recv/proc/send)"

**왜 의심**: TCC803x 같은 임베디드에서 폰 4대 = 12 thread + heartbeat 4 +
sensor 4 + ... = OS thread 폭발. context switch 오버헤드.

**대안: async runtime**
- **C++**: boost::asio strand 기반 — io_context 하나로 N 세션 (single thread
  scheduling, lock-free intra-session)
- **Rust**: tokio multi-threaded runtime
- **Kotlin**: coroutines + Dispatchers.IO

각 세션은 thread가 아니라 **co-routine**이고, 동시성은 future/await로 표현.
`receive→process→send` 파이프라인이 그대로 read/decrypt/dispatch chain으로 표현.

**Trade-off**: 코드 가독성(코루틴/async에 익숙해야), 디버깅(stack trace가
runtime을 거침). 그러나 자원 효율은 한 자릿수 ms/MB 단위로 향상.

### E.4 가정: "JNI bridge가 필요하다"

**왜 의심**: JNI는 결합이 강하고 디버깅이 어렵고 ABI를 깨면 통째로 죽는다.
JNI 호출 자체가 ART에서 비싸다(VM stub 거침).

**대안 1: native daemon + Binder/AIDL**
- C++/Rust core를 별도 native 프로세스(`/system/bin/aa-engine`)로 실행
- Java app은 AIDL 인터페이스로 통신
- JNI 0 LOC. 양 쪽이 독립적으로 재시작/디버그 가능
- AAOS HAL 모델과 동일한 패턴

**대안 2: 전부 Java/Kotlin**
- AAP는 결국 byte 파싱 + OpenSSL + MediaCodec/AudioTrack 호출
- 모두 Java API 존재 (JCE, MediaCodec, ...)
- 성능 측정 필요하지만 modern hardware는 충분할 수도
- core/impl/jni 디렉토리 통째로 사라짐. 빌드/배포 단순

**Trade-off**: 1번은 IPC. 2번은 GC pause(audio jitter risk), 그리고 다른
플랫폼(Linux/macOS) 지원 포기. 우리 제품이 자동차 한정이면 후자가 합리적.

### E.5 가정: "Service는 채널당 클래스 (singleton)"

**왜 의심**: AAP 메시지는 결국 (channel, type, payload) tuple. 이걸 처리하는
함수들의 모음이 왜 클래스여야 하나? 상태(`session_id_`, `media_data_count_`)도
결국 메시지 처리 중간 결과일 뿐.

**대안: Reactive stream pipeline**
```
TransportStream (Flow<Bytes>)
  → DecryptStage (Flow<DecryptedFrame>)
  → DemuxStage (Flow<AapMessage>)
  → fanOut by channel
       ├─ Audio  (Flow<AudioFrame>)  → AudioSink.collect
       ├─ Video  (Flow<VideoFrame>)  → VideoSink.collect
       └─ Control (Flow<ControlMsg>) → handleControl
```

각 stage는 pure function (state는 `scan`/`fold`로 명시). 단위 테스트는
input flow → expected output flow 비교. mock 필요 없음.

**Trade-off**: 학습 곡선. 디버깅(어디서 뭐 잘못됐는지 stack 끊김). C++에서는
RxCpp나 직접 구현 필요 — 무게 큼. Rust(`futures`/`async-stream`), Kotlin
(`Flow`)에서는 자연.

### E.6 가정: "Sink/Source는 직접 호출 인터페이스 (push 모델)"

**왜 의심**: 현재 `IVideoSink::OnVideoFrame()`은 service가 sink를 직접 호출.
멀티 sink, 녹화, 디버그 tap 같은 케이스가 어려워진다.

**대안: pub/sub message bus**
- 모든 미디어 데이터는 in-process pub/sub(LCM, ZeroMQ inproc, 또는 단순
  `EventBus`) 토픽에 publish
- 토픽: `/session/{id}/video/h264`, `/session/{id}/audio/{channel}/pcm`
- consumer는 누구든 subscribe 가능: 디스플레이, 녹화기, 미리보기, 디버그 dump

**Trade-off**: byte buffer 복사 비용 (현 모델은 zero-copy shared_ptr). 진단/
확장성 가치와의 trade.

### E.7 가정: "Transport API는 동기 블로킹 (Send/Receive)"

**왜 의심**: `transport_->Receive()`가 블로킹이라 thread를 점유. cancellation은
`Disconnect()`로 우회 — 어색함.

**대안: 명시적 async**
```cpp
awaitable<vector<uint8_t>> Receive(stop_token);
awaitable<bool>             Send(span<const uint8_t>, stop_token);
```
또는 callback-based (`OnDataAvailable(callback)`).

C++20 coroutines 또는 Rust async에서 자연. cancellation 명시적, 타임아웃 명시적.

### E.8 가정: "HU 인증서/키는 빌드 타임에 박혀 있다"

**왜 의심**: AAP는 폰이 HU 인증서를 신뢰해야 동작. 리버스엔지된 reference 키를
영구히 쓰면 Google이 revoke할 위험. 또한 HU 제품마다 다른 키는 차량 fleet
관리에 유용.

**대안**:
- 빌드 시 `--cert-pem`/`--key-pem` argument로 주입
- TEE/HSM에 키 보관, 부팅 시 로드 (자동차급 보안)
- 인증서 회전(rotation) 메커니즘 — 원격 업데이트로 새 키 받기

### E.9 가정: "Discovery는 OS-level watcher가 한다"

**왜 의심**: USB는 udev/UsbManager, BT는 BluetoothBroadcastReceiver, WiFi는
WifiManager — 발견 메커니즘이 OS API에 결합. 멀티 플랫폼 코드를 똑같이 짜야.

**대안: unified Discovery service**
- 추상 `IDeviceDiscovery` 인터페이스 (`Stream<DiscoveryEvent>`)
- 플랫폼별 구현이 OS event를 표준 이벤트로 변환
- core가 transport 종류와 무관하게 "device appeared"를 받아 transport factory에 위임

이건 부분적으로 현재도 되어 있지만, 무선(BT+WiFi 짝)은 Java가 choreography해서
core가 무지함.

### E.10 가정: "모든 세션을 동일하게 다룬다"

**왜 의심**: 폰마다 capability 다름 (Android Auto vs CarPlay vs custom). 폰
모델별 quirk(예: 삼성은 SD_REQ를 두 번 보냄, 픽셀은 audio focus를 빨리 lose)는
현재 코드 어디에도 모델링 안 됨 — 발견되면 ad-hoc 분기.

**대안: PhoneCapabilities + QuirksProfile**
- `instance_id` 또는 `device_name` → `QuirksProfile` 매핑
- 각 quirk는 명시적 데이터 (timeout 값, retry 정책, 비활성 메시지 목록)
- 새 폰 모델 발견 시 데이터만 추가, 코드 안 건드림

### E.11 가정: "core/impl/app 3-tier 레이어가 자연스럽다"

**왜 의심**: 이 구조는 "생성자가 인터페이스를 알고, 런타임에 구현체를 주입"이라는
OOP 패러다임의 산물. Hexagonal architecture/Ports & Adapters는 다르게 본다:
**core가 ports(인터페이스)를 정의하고, adapter들이 양쪽에 붙는다 — 양방향**.

```
            ┌─────────────────────┐
 Driving →  │                     │ ← Driven
 (Discovery)│   Application Core  │ (Sinks)
            │   (Use cases)       │
            │                     │
            └─────────────────────┘
              ↑                 ↑
        Inbound port       Outbound port
        (e.g. ConnectPhone) (e.g. PlayAudio)
```

이렇게 보면 "ITransport는 inbound (data가 안으로 흐름), IAudioSink는 outbound
(data가 밖으로 흐름)" 이라는 비대칭이 명확해지고, 양쪽 모두 mock으로 교체 가능.
Test seam이 디자인의 부산물이 아니라 핵심.

---

## Part E 정리

위 11개 가정 중 **현실적으로 즉시 채택 가능한 것**은 별로 없다 — 대부분 언어
교체나 프로세스 모델 변경 같은 큰 결정이 필요. 그러나 다음 두 개는 우리 코드에
점진적으로 도입 가능:

- **E.10 QuirksProfile** — 폰 모델별 quirk를 데이터화. Phase 단위 plan으로
  실행 가능. P1 후보.
- **E.6 부분 적용** — VideoSink를 list로 받아 multi-cast (녹화 sink, 미리보기
  sink). 인터페이스 작은 변경만으로 가능. P3 후보.

나머지(언어 교체, 프로세스 분리, 전면 async, AIDL daemon화)는 **장기 비전 문서**
영역. 신규 product line이나 차세대 HU 설계 시 진지하게 검토할 것.

---

## Part F. Decisions

이 섹션은 Part E의 가정 재검토 결과 확정된 결정을 누적 기록한다. 결정마다
일자, 대안, 선택 근거를 짧게 남긴다.

### F.1 Core 언어: C++17 (2026-04-16)

- **대안**: C++17, Rust (E.1 참고)
- **선택**: C++17
- **근거**:
  - openauto/aasdk를 레퍼런스로 직접 활용 가능
  - Android NDK + Yocto recipe 모두 1급 시민, cross-compile 마찰 최소
  - 팀 친숙도
  - AAP 손파싱 영역이 작아(protobuf가 페이로드 처리) Rust의 메모리 안전성 ROI가 제한적
- **C++20 기능**: coroutine, concepts 등은 케이스별 도입 검토 (전면 강제 X)

### F.2 빌드 시스템: Android.bp + CMake (2026-04-16)

- **Android**: Android.bp (Soong) — AOSP 트리에 통합되는 표준 방식
- **Yocto**: CMake — meta-layer recipe(`inherit cmake`)와 자연스럽게 호환
- **공통 소스 트리**: 동일한 `core/` `impl/common/` 소스를 두 빌드 시스템이 각각 참조.
  플랫폼별 impl(`impl/android/`, `impl/linux/`)은 해당 빌드 시스템에서만 컴파일.
- **빌드 정의**: 이중 유지(Android.bp + CMakeLists.txt). 소스는 단일.
- **단위 테스트**: 둘 다 지원 — 호스트 Linux는 CMake + gtest (빠른 개발 사이클),
  Android는 `cc_test` 모듈 (target/host 양쪽 가능).

### F.3 프로세스 모델: 단일 프로세스 다중 세션 (2026-04-16)

- **대안**: 단일 프로세스 / process-per-session (E.2)
- **선택**: 단일 프로세스 (Part A 기본값 유지)
- **근거**:
  - 동시 폰 1~2대 시나리오에서 격리 가치가 supervisor + IPC 설계 비용을 정당화 못함
  - 미디어 데이터 zero-copy(shared_ptr) 유지
  - Yocto/Android 양쪽 동일 모델 (process-per-session은 두 OS에서 supervisor 비대칭)
  - openauto/aasdk 레퍼런스 그대로 활용
- **재검토 트리거**: 차량 안전 인증(ASIL-B 이상) 요건 발생 시, 또는 cross-session crash가 실제로 관측될 때
- **확장 경로**: 추후 isolation 필요 시 ITransport 위에 RemoteTransport(IPC)를 추가하는 형태로 점진 도입 가능

### F.4 동시성 모델: standalone asio + C++17 callback (2026-04-16)

- **대안**: thread-per-session (Part A 원안) / standalone asio + callback / boost::asio (E.3)
- **선택**: standalone asio (header-only, boost 의존 없음) + C++17 callback handler
- **근거**:
  - I/O bound 코드에 async가 이상적 모델 — aasdk 원본도 asio 기반
  - header-only라 통합 비용 최소 (Android.bp 3줄, CMake 1줄, 헤더 vendor)
  - C++20 coroutine 불가 환경이므로 callback handler 방식 (aasdk 스타일)
  - thread-per-session은 1~2폰에서 실용적이었으나, 확장성·코드 일관성에서 asio 우위
- **Part A 영향**: A.4 "3 worker threads" → "io_context single-threaded + async handlers" 로 재정의 필요
- **E.7 (transport async)**: 이 결정에 의해 자연 해소 — ITransport 인터페이스가 async_read/async_write 기반으로

### F.5 JNI 정책: 제거, native daemon + IPC (2026-04-16)

- **대안**: JNI bridge (Part A 원안) / native daemon + AIDL (E.4) / 전부 Java (E.4)
- **선택**: native daemon + 플랫폼별 IPC (Android=AIDL, Yocto=D-Bus/Unix socket)
- **근거**:
  - JNI 0 LOC — God JNI 문제 원천 제거
  - engine 바이너리가 양 플랫폼에서 동일 구조 (Android: /system/bin, Yocto: /usr/bin)
  - 미디어 데이터(H.264, PCM)는 IPC 안 탐 — NDK AMediaCodec / AAudio로 native에서 직접 처리
  - IPC 트래픽은 제어 명령(세션 시작/중지, 상태 알림)만 — 가벼움
  - crash 격리: engine crash 시 app 생존, 재시작 가능
- **engine 책임**: 세션 전체 lifecycle, AAP 프로토콜, 암호화, 미디어 디코딩/출력
- **app 책임**: discovery 이벤트 수신(USB/BT/WiFi), Surface/UI 제공, 사용자 입력
- **IPC 경계**: `IEngineController` 추상 인터페이스 (core). 플랫폼별 구현 분리:
  - `AidlEngineController` (impl/android/)
  - `DbusEngineController` (impl/linux/)
- **Part A 영향**:
  - A.3 `impl/android/jni/` → 삭제. `impl/android/aidl/` 로 대체
  - A.4 "JNI bridge" 행 → "IEngineController: 플랫폼 IPC 어댑터" 로 재정의
  - A.2 App↔impl 경계가 "JNI" 대신 "AIDL/D-Bus"
- **배포 작업 (일회성)**: Android SELinux 정책, init.rc 서비스 등록

### F.6 타깃 우선순위: Android 먼저 (2026-04-16)

- **선택**: Android (TCC803x Android 10 IVI) 를 1차 타깃, Yocto는 후속
- **근거**: 사용자 결정. 기존 `../aa`가 Android 기반이라 검증 환경 확보 용이
- **영향**: 초기 impl은 `impl/android/` 위주. core는 platform-free로 유지하되
  Yocto impl은 Android 동작 확인 후 추가

### F.7 AAP 코드 출처: 새로 작성, openauto/aasdk + ../aa 참고 (2026-04-16)

- **대안**: openauto/aasdk fork / 새로 작성 + 참고 / 완전 백지
- **선택**: 새로 작성, openauto/aasdk 및 `../aa` 코드를 참고
- **근거**:
  - F.1~F.5에서 구조가 원본과 크게 달라짐 (asio callback, AIDL daemon, 레이어 분리)
  - fork하면 기존 구조를 뜯어고치는 비용이 새로 짜는 것보다 클 수 있음
  - 프로토콜 동작 이해, 메시지 포맷, 엣지 케이스 처리는 기존 코드에서 참고
- **참고 대상**: openauto/aasdk (프로토콜 원본), `../aa` (우리 환경에 맞춘 구현)

### F.8 Sink 모델: push + multi-cast (2026-04-16)

- **대안**: push 단일 sink (Part A) / push + multi-cast / pub/sub bus (E.6)
- **선택**: push + multi-cast (sink vector)
- **근거**:
  - full pub/sub는 buffer 복사 오버헤드 + 추상화 비용이 큼
  - 그러나 단일 sink만 허용하면 녹화/디버그 dump 등 확장이 어려움
  - 절충: `vector<shared_ptr<ISink>>`로 multi-cast. 인터페이스 변경 최소.

### F.9 HU 인증서/키: 빌드시 주입 + 런타임 로드 옵션 (2026-04-16)

- **대안**: 하드코딩 (Part A) / 빌드시 주입 / TEE 보관 (E.8)
- **선택**: 빌드시 주입 (CMake -DCERT_PEM=..., Android.bp cflags) + 런타임 파일 경로 지정 옵션
- **근거**: 하드코딩은 보안/배포 모든 면에서 열등. 빌드시 주입은 최소 비용으로 큰 개선.
  런타임 로드는 개발/디버깅 편의.

### F.10 QuirksProfile: 원칙 채택 (2026-04-16)

- **선택**: 설계 여지만 확보 (Session 생성 시 QuirksProfile 주입 가능), 구현은 실제 quirk 발견 시
- **근거**: 폰 모델별 quirk는 실제 사례 발견 후 데이터화하는 게 효율적.
  지금은 인터페이스에 확장점만 남김.

### F.11 레이어 모델: Hexagonal 인지, 디렉토리 유지 (2026-04-16)

- **대안**: 3-tier (Part A) / hexagonal ports & adapters (E.11)
- **선택**: hexagonal 패턴을 명시적으로 인지. 디렉토리(core/impl/app)는 유지.
- **근거**: F.5 (AIDL daemon) 결정으로 이미 hexagonal 구조가 자연 형성됨:
  - ITransport = inbound port
  - IAudioSink/IVideoSink = outbound port
  - IEngineController = driving port (AIDL/D-Bus adapter)
- Part A 설명에 ports/adapters 관점을 명시하면 설계 의도가 더 명확해짐.

### F.12 미디어 디코딩 위치: app 프로세스 (2026-04-21)

- **F.5 수정**: "미디어 데이터 IPC 안 탐" → "압축 스트림은 IPC 허용, 디코딩은 app 담당"
- **대안**: daemon 직접 디코딩 (F.5 원안) / 압축 스트림 IPC (app 디코딩) / JNI Surface 전달
- **선택**: 압축 스트림 IPC. daemon은 프로토콜+암호화까지, app이 디코딩+렌더링.
- **근거**:
  - Android Surface는 app 프로세스 소유 — daemon에서 Surface 전달이 플랫폼 한계로 불가
    (AIDL Surface 타입 빌드 오류, IBinder 추출 API 없음, SurfaceComposerClient 비표준)
  - H.264 압축 스트림은 FHD@60fps에서도 ~4 MB/s (메모리 대역폭의 0.1%) — IPC 비용 무시 가능
  - 책임 분리 개선: daemon=프로토콜 전문, app=미디어+UI 전문
- **플랫폼별 차이**:
  - Android: AIDL 콜백으로 압축 스트림 전달 → app이 Java MediaCodec → SurfaceView
  - Yocto: daemon이 GStreamer로 직접 디코딩 (IPC 불필요, 단일 프로세스 가능)
- **Part A 영향**:
  - A.4 engine 책임: "미디어 디코딩/출력" 삭제 → "미디어 스트림 추출"
  - A.4 app 책임: "Surface/UI 제공" → "미디어 디코딩 + Surface/UI"
  - IPC 경계에 미디어 콜백 추가 (onVideoData, onAudioData)

### F.13 SessionManager 기반 다중 세션 관리 (2026-04-24)

- **대안**: SessionLifecycleController (단일 세션) / SessionManager (다중 세션)
- **선택**: SessionManager — session_id 기반 Map으로 다중 세션 독립 관리
- **근거**:
  - 단일 세션 모델(SessionLifecycleController)은 USB/무선 동시 연결 시 상태 혼재
  - USB detach 이벤트가 무선 세션에 영향을 주는 버그 발생
  - transport 타입으로 구분하는 것은 임시방편 — session_id로 추적이 근본
- **SessionLifecycleController 삭제**: SessionManager로 완전 대체

### F.14 VideoFocus는 Surface 라이프사이클이 드라이버 (2026-04-24)

- **대안**: activateSession에서 즉시 PROJECTED / Surface 준비 후 PROJECTED
- **선택**: surfaceCreated → PROJECTED, surfaceDestroyed → NATIVE
- **근거**:
  - PROJECTED 전에 Surface가 없으면 codec config/IDR 드롭 → 비디오 안 나옴
  - Surface 라이프사이클이 실제 렌더링 가능 시점을 정확히 반영

### F.15 VideoDecoder 단일 스레드 모델 (2026-04-24)

- **대안**: 별도 output thread (이중 스레드) / pushData 내 동기 처리 (단일 스레드)
- **선택**: 단일 스레드 (레퍼런스 패턴)
- **근거**:
  - 이중 스레드는 join 블로킹 → IDR 드롭 → 비디오 안 나옴
  - HW 디코더가 충분히 빨라서 feedData 한 번에 input+output 처리 가능
  - 스레드 동기화 복잡도 제거 (codecLock, configured 플래그 불필요)

### F.16 같은 폰 transport 전환 처리 (2026-04-24)

- **선택**: USB timeout 2초 + onPhoneIdentified 같은 폰 감지 → 기존 세션 종료
- **근거**:
  - 같은 폰은 한 번에 하나의 AA 연결만 유지 (폰 동작)
  - 무선→유선: AOA 전환 시 폰이 무선 끊음 → USB timeout으로 대기
  - 유선→무선: onPhoneIdentified에서 같은 폰 감지 → USB 즉시 종료 (timeout 대기 없음)
