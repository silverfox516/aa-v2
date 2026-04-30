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

### F.17 Active session sink swap을 Engine이 책임 (2026-04-27)

- **배경**: `Engine::set_active_session()`은 원래 `active_session_id_`만
  업데이트하고 sink 재배치는 `// TODO: detach sinks from old active, attach
  to new active` 주석으로 남아 있었음. 실제 sink swap은 app 측
  `AaService.activateSession()`이 `attachAllSinks/detachAllSinks` AIDL을
  순서대로 호출해 처리했다. 두 API가 같은 일을 따로 한다는 점에서
  CLAUDE.md "no band-aid" 원칙 위반.

- **대안**:
  - (a) `Engine::set_active_session()`이 sink swap까지 책임지도록 완성
  - (b) `Engine::set_active_session()`을 제거하고 swap 책임을 app에 일임
  - (c) 현 상태 유지(API 두 벌, 한쪽은 stub)

- **선택**: (a). `set_active_session(sid)`이 단일 트랜잭션으로
  이전 active의 sink detach → `active_session_id_` 갱신 → 새 active의
  sink attach를 모두 수행한다.

- **근거**:
  - "active session"은 core가 가진 명시적 상태(`active_session_id_`)이므로,
    그 상태 전환의 부수 효과(sink 재배치)도 core가 갖는 게 자연스러움.
  - app 측이 detach + attach를 따로 호출하는 패턴은 두 호출 사이의 race나
    부분 실패가 가능. 단일 entry point가 atomic 트랜잭션을 보장.
  - multi-session 시나리오(USB+Wireless 동시) 학습 가치가 본 프로젝트 목표
    (Part 0)의 "이상적 구조 설계"와 직결.
  - core가 platform-free인 점은 유지됨 (sink detach/attach는 이미 IService
    포트로 추상화되어 있고, 본 작업은 core 안의 호출 그래프만 바꿈).

- **영향**:
  - app 측 `AaService.activateSession()`은 향후 `setActiveSession()` 단일
    호출로 단순화 가능 (별도 작업 — F.17 결정의 직접 후속).
  - 그 작업이 완료되면 AIDL의 `attachAllSinks`/`detachAllSinks`는 deprecated
    가능. 단, 그 전까지는 두 경로가 공존(중복은 의식하고 받아들이는 임시
    상태).
  - 결정에 맞춰 두 곳의 암묵적 active 변경 경로를 함께 제거했다 —
    "암묵적 magic 없음" 원칙:
    - `do_start_session`의 첫 세션 자동 활성화 (`activate_session()` 함수
      자체 삭제). 첫 세션이라도 app이 명시적으로 `set_active_session()`을
      호출해야 sink가 붙는다.
    - `cleanup_session`에서 active session 제거 시 임의의 다른 세션을
      자동 승격하던 로직 (이제 단순히 `active_session_id_ = 0`). 어느
      세션을 다음 active로 할지는 app 정책이며, 명시적 `set_active_session()`
      호출이 필요하다.

### F.18 AaService 책임 분리 — transport-coordinator 패턴 (2026-04-27)

- **배경**: `AaService`가 494줄에 `UsbMonitor.Listener` /
  `WirelessSessionCoordinator.Callback` / `EngineConnectionManager.Callback`
  세 콜백을 동시에 implement 하고, `usbSessionId` / `wirelessSessionId` /
  `availableUsbDevice` 등 transport-aware한 사이드 상태를 직접 관리했다.
  `SessionManager`가 transport-agnostic을 표방하면서도 transport별
  세션 추적은 `AaService` 측 사이드 필드에 의존하는 모순이 있었다.

- **대안**:
  - (a) USB만 별도 코디네이터로 추출, wireless는 그대로
  - (b) USB / wireless 둘 다 동일 패턴 코디네이터로 분리 (대칭)
  - (c) 현 상태 유지 + `AaService` 안에서 의도 주석으로만 정리

- **선택**: (b). USB와 wireless 모두 자기 transport의 세션 lifecycle을
  스스로 소유하는 동일 패턴 코디네이터로 만들고, 둘이 호스트에 통보하는
  창구는 단일 `SessionLifecycleListener`로 통합한다. transport→session_id
  매핑은 `SessionManager.getSessionsByTransport()`로 호출자가 묻는다.

- **근거**:
  - 비대칭 설계는 학습 자료로서 가치가 떨어진다 — 두 transport가 같은
    역할을 한다면 같은 모양이어야 한다 (Part 0 "이상적 구조").
  - 사이드 상태는 cleanup 누수의 단골이다. `usbSessionId` / `wirelessSessionId`
    필드처럼 "지운 자리에서만 -1로 되돌려놓는" 코드는 단일 source of
    truth(`SessionManager`)로 합쳐야 누수가 원천 차단된다.
  - 코디네이터가 자기 lifecycle을 끝까지 소유해야 (`engine.startSession` /
    `startTcpSession`까지), 호스트가 transport-aware한 분기 if문을
    누적할 자리가 사라진다.

- **영향**:
  - `AaService` 줄 수 494 → 413 (-81). implements 3 → 2.
  - `EngineProxyProvider` 역할은 `Supplier<IAAEngine>`(`() -> engineProxy`)
    람다로 충분하므로 별도 인터페이스는 만들지 않았다.
  - **후속 작업 완료 (2026-04-27)**: `EngineConnectionManager.Callback`
    인터페이스를 제거하고 생성자에서 `Consumer<IAAEngine>` + `Runnable`
    람다 두 개를 받도록 단순화했다. 결과 `AaService`는
    `SessionLifecycleListener` 하나만 implements하는 상태가 됐다 (≤1 목표
    달성). 줄 수도 413 → 408로 추가 감소. 콜백 인터페이스가 단 두 개의
    함수만 갖는 경우엔 인터페이스보다 람다가 더 가벼우면서 문서적
    가치도 동등하다는 패턴 사례.
  - 두 코디네이터는 이제 패턴이 동일하므로, 추가 transport(예: TCP-only
    custom 케이블 등)를 붙일 때 동일 모양으로 새 코디네이터만 만들면
    호스트 수정이 거의 없다 — 학습 결과의 직접적 활용 형태.

### F.19 BufferedTransport — transport read를 윗 레이어와 분리 (2026-04-27)

- **배경**: `Session::start_read()` → `on_read_complete()` →
  `InboundAssembler.feed()` → `start_read()` 패턴이라, 윗 레이어 처리
  (decrypt, framer reassemble, dispatch, sink callback)가 끝나야 다음
  underlying read가 발행된다. 사용자 요구는 명확했다: **"transport에서
  읽을 게 있으면 항상 바로 읽어야 하고, 쓸 게 있으면 항상 바로 써야
  한다"**. 현재 RX 구조는 이 요구를 어긴다.

- **대안**:
  - (a) 각 platform-specific transport (`AndroidUsbTransport`,
    `AndroidTcpTransport` 등)에 자체 internal read queue를 둠
  - (b) core에 `BufferedTransport` decorator를 두고 모든 transport를
    wrap
  - (c) `Session`이 multi-buffer로 outstanding read를 여러 개 발행

- **선택**: (b). `core/transport/BufferedTransport`는 `ITransport`를
  구현하면서 다른 `ITransport`를 wrap. 내부 `rx_queue`(deque) + 자동
  read 루프. underlying.async_read는 윗 레이어 처리와 무관하게 항상
  진행되고, 받은 chunk는 queue에 쌓였다가 `Session::async_read` 호출
  시 즉시 deliver된다.

- **근거**:
  - (a)는 platform 추가할 때마다 같은 로직 반복. Hexagonal 원칙
    위반(횡단 관심사가 adapter에 분산).
  - (b)는 정확히 decorator 패턴 — port 인터페이스 그대로 두고 core가
    cross-cutting 동작을 추상화. 향후 어떤 transport (USB / TCP / 가상
    transport / 또 다른 platform)든 무료로 적용된다.
  - (c)는 ITransport 계약 변경(여러 outstanding read 허용)이 필요해
    파급 큼. 또 ITransport 구현체마다 동시 read 처리 로직 추가 필요.

- **영향**:
  - `impl/android/main/main.cpp`의 `AndroidTransportFactory`가
    underlying transport를 만든 직후 `BufferedTransport`로 wrap +
    `start()` 호출. 단 한 곳.
  - `Session`은 변경 없음 — `ITransport`로 받아 그대로 사용.
  - 검증: 실기기 USB scroll 테스트에서 input-to-display lag이 ~400ms
    → ~300ms로 약 100ms 감소. 차이의 출처는 strand가 더 이상 underlying
    read와 직렬되지 않아 큰 RX 메시지 처리 중에도 다음 read가 진행되는
    효과.
  - 잔여 ~300ms는 troubleshooting.md #18에 기록된 F.12 IPC 비용
    baseline (200-300ms) 범위. 추가 단축은 SharedMemory / JNI 단일
    프로세스 같은 큰 구조 변경 필요.
  - `BufferedTransport`의 RX queue는 max 32 chunk (≈ 512KB) — 윗 레이어가
    크게 밀릴 때 oldest chunk drop + 경고 로그. 무한 메모리 증가 방지.
  - TX는 forward만(underlying에 위임). underlying transport(USB/TCP)가
    이미 자체 dedicated write thread + queue를 가지고 있어 추가 buffering
    불필요.

### F.20 미디어 재생 제어 송신 경로: Input ch + KEYCODE (2026-04-28)

- **배경**: HU UI에서 ◀◀/▶ ⏸/▶▶ 버튼을 눌렀을 때 폰의 미디어 앱
  (Spotify, YT Music 등)을 제어해야 한다. AAP에는 두 경로가 있다:
  - PLAYBACK_INPUT (ch10 mediaplayback service)
  - InputReport.key_event (ch6 input service) with KEYCODE_MEDIA_*

- **선택**: KEYCODE 경로(ch6 input service)로 통일. PLAYBACK_INPUT은
  미구현으로 둔다.

- **근거**:
  - KEYCODE_MEDIA_*는 Android 표준 키 이벤트로, 폰 측 호환성이 가장
    넓다 (스티어링휠 미디어 키와 동일한 처리 경로).
  - PLAYBACK_INPUT은 mediaplayback proto 정의가 폰 모델/AAP 버전에 따라
    필드 해석이 미묘하게 다른 사례가 보고되어 있다. KEYCODE는 단일
    바이트로 의미가 고정.
  - 한 경로로 통일하면 AaService의 mediaSessionId/audio focus 동기화
    로직이 단순해짐.

- **세부 결정**:
  - HU 사용자의 명시적 play/pause 토글: `KEYCODE_MEDIA_PLAY_PAUSE (85)`
  - 내부 focus-swap 복구(BG → 다시 FG로 long-press 등)에서 강제 재생:
    `KEYCODE_MEDIA_PLAY (126)` — PLAY_PAUSE는 이미 재생 중인 소스를
    멈춰버리는 부작용이 있음.
  - 다음 곡 / 이전 곡: `KEYCODE_MEDIA_NEXT (87)` / `MEDIA_PREVIOUS (88)`
  - InputService는 down + up 페어로 송신 (`InputService::send_media_key`).

- **영향**:
  - PLAYBACK_INPUT은 ID enum만 트리에 있고(`MediaPlaybackStatusMessageId.proto`)
    message 정의 `.proto`는 import한 적이 없음. 본 결정에 따라 그대로 유지.
    필드 해석을 학습 대상으로 삼게 되면 그때 .proto를 가져와 catalog만
    채우면 된다 (송신 자체는 여전히 KEYCODE 경로로 갈 수 있음).
  - troubleshooting.md #23 — focus GAIN만으로는 폰이 재생을 시작하지
    않는 현상의 fix가 본 결정과 직접 연결됨.

### F.21 Cluster sink advertise는 MAIN sink와 *동시에만* 가능 (2026-04-28)

- **배경**: plan 0006 Day 2에서 secondary VideoService(channel 15,
  display_type=CLUSTER) advertise 시도. 메인 sink에 추가하는 시나리오와
  cluster 단독 시나리오 모두 검증.

- **관찰**:
  - **MAIN + CLUSTER 동시 advertise**: 폰이 SDR 받고도 어떤 채널도
    CHANNEL_OPEN_REQ를 보내지 않고 ~1초 후 connection close. distinct
    `display_id` (main=0, cluster=1) 설정해도 동일.
  - **CLUSTER 단독 (MAIN을 CLUSTER로 변경)**: 같은 결과. 폰 거부.
  - 다른 sink/service 없이 CLUSTER 단독 SDR 또한 폰이 거부.

- **결론**: AAP host(폰) 측 implementation은 **최소 1개 MAIN display
  sink가 advertise되어야 정상 진행**. CLUSTER는 항상 MAIN에 *additive*
  로만 가능. CLUSTER 단독 / 대체는 **invalid SDR**로 silent reject.

- **추가 학습 산출**: 본 결정은 일반 폰/앱 조합(Spotify/YT Music + 비
  인증 HU)에서는 cluster sink advertise 자체를 phone-side가 거부함도
  관찰. Cluster를 활용하려면 (a) 인증된 OEM HU identity, 또는
  (b) modern AA의 androidx.car.app cluster 모델 사용 필요. 본 학습
  프로젝트의 현재 budget으로는 도달 불가.

- **영향**:
  - VideoService에 `display_type` / `display_id` 필드 + 조건부 emit
    로직 유지 (default값일 때만 wire에 안 넣음). 재시도 시 인프라
    재구축 비용 zero.
  - main.cpp services[15] 등록 미실행. 재오픈 시 MAIN과 함께 advertise
    필요.
  - 자세한 시도 + 시나리오: docs/plans/0006_cluster_display.md.

### F.22 Channel-specific outbound dispatch — IService 통한 broadcast 금지 (2026-04-30)

- **배경**: plan 0009 Day 2에서 BluetoothService outbound API
  (`send_pairing_response`, `send_auth_result`)를 추가하면서, 기존 패턴 —
  `IService`에 default-empty virtual 추가 + `Session::complete_*`이 모든
  service에 broadcast — 그대로 따라갔다. 코드 리뷰 (2026-04-30) 결과 본
  패턴이 abstraction leakage임을 확인.

- **문제**:
  - `IService`가 모든 channel의 outbound API union으로 비대화. 현재
    누적된 default-empty: `send_media_key` (ch5만), `release/gain_audio_focus`
    (ch0만), `set_video_focus` (video만), `attach/detach_sinks` (sink
    가진 service만), 그리고 새 `send_pairing_response/send_auth_result`
    (ch13만).
  - 신규 channel 추가 시마다 IService 확장 → 모든 18개 service가
    자동으로 의미 없는 default 보유. fragile.
  - `Session::complete_pairing` 같은 outbound가 모든 service에 broadcast
    fire하지만 실제로는 한 service만 처리. 비효율 + 의도 불명확.

- **결정 (2026-04-30)**:
  - **신규 channel-specific outbound API는 IService에 추가하지 않는다**.
  - 대신 `Session`이 `ServiceType` 기반으로 instance를 lookup해서
    static_cast로 specific service에 직접 호출.
  - 예: `Session::complete_pairing` → `find_bluetooth_service()` →
    `BluetoothService::send_pairing_response()`.
  - 기존 IService outbound (`send_media_key`, audio focus, video focus,
    attach/detach sinks)는 **legacy**로 그대로 유지. 향후 시간 여유
    되면 같은 패턴으로 점진적 refactor 가능.

- **장점**:
  - IService 인터페이스가 generic service contract만 유지 (channel
    open/close/message handling).
  - Session이 channel별 specific 동작을 알지만 그건 Session의 책임 (우리
    Hexagonal에서 Session = port-level orchestrator).
  - dispatch 비용도 낮음 (services_ map size 작음, type() 호출 가벼움).

- **trade-off**:
  - Session이 BluetoothService 헤더에 의존 — forward declaration으로
    헤더 의존성 최소화.
  - dynamic_cast 대신 static_cast + ServiceType 일치 검사 — 타입 안전성
    런타임 보장은 같음.

- **반례 (적용 안 하는 경우)**:
  - 같은 outbound가 정말로 여러 service 종류에 의미 있을 때 (예:
    attach/detach_sinks는 video + audio 둘 다 의미 있음). 이런 경우는
    legacy broadcast 패턴 그대로.

- **영향**:
  - 본 결정의 첫 적용: BluetoothService.send_pairing_response /
    send_auth_result는 IService override 아님, public method.
  - Session에 `find_bluetooth_service()` private helper.
  - 새 channel-specific outbound 추가 시 본 결정 참조.

---

## Part G. Scoping Decisions (학습 우선순위 적용)

> Part 0의 목표 중 "Stub vs 풀 구현은 학습 가치로 판단"의 적용 사례.
> 어떤 부분을 stub/미구현으로 두기로 했는지, 그 결정의 근거가
> 무엇인지 추적한다. 이 카탈로그가 있어야 "왜 이건 stub인가?"라는
> 질문에 매번 답하지 않아도 된다.
>
> 형식: 항목 / 현재 상태 / 학습 가치 판단 / 풀 구현 시 얻는 추가 가치
>      / 트리거 조건.

### G.0 (2026-04-27 갱신) "advertise만 하는 stub은 free 아님"

**갱신 전제**: 본 카탈로그 작성 직후 lag 진단(troubleshooting.md #22)
에서 발견한 사실이 G.1~G.5의 "stub으로 두는 결정" 자체를 흔든다.
advertise만 하고 응답 안 하는 채널이 ServiceDiscoveryResponse에 들어
있으면 폰이 그 채널을 열고 메시지를 보내며, 우리 silence가 폰의 전체
video cadence를 throttle한다. 비용이 작지 않다.

이에 따라 본 시점에 다음 결정을 내림:
- **응답 핸들러 없는 stub은 ServiceDiscoveryResponse에서도 빼기**
  (단순히 클래스 등록만 안 함 → 그러면 fill_config가 안 불려서 advertise
  도 안 됨).
- **service 클래스 자체는 in-tree에 보존** — 각 클래스의 `fill_config`
  코드는 proto 구조 documentation 가치가 있음. 향후 풀 구현 시 그대로
  활용.
- 결과적으로 G.1~G.4의 "stub" 항목들의 실제 상태는 "service class
  exists but not registered". advertise = false.

이 발견 자체가 학습 가치 큰 산출물 — "stub vs full = 학습 가치로 판단"
원칙이 "stub의 hidden cost"라는 새 측면을 가지게 됐다.

**갱신 (2026-04-27)**: MediaPlaybackService를 fill_config-only stub에서
PLAYBACK_STATUS / PLAYBACK_METADATA를 proto 파싱 + 로그하는 "수신
핸들러만" 가진 형태로 격상하고 ch 10에 다시 등록. **lag 재발 없음** —
폰의 video cadence는 30fps 유지. 즉 폰이 보는 신호는 "HU가 응답
메시지를 보냈는가"가 아니라 "메시지가 실제로 처리되고 있는가"에 가깝다.
handler가 등록만 되어 있으면(=`ServiceBase`의 unhandled 경로가 아니라
명시적 handler 경로로 빠지면) 통과. 따라서 G.0 룰의 실용적 표현은:

- **silent stub** (unhandled-only): 비용 큼 (cadence throttle)
- **passive handler stub** (parse + log + 응답 없음): 안전
- **full impl** (응답까지): 추가 가치는 application-level (UI 상태 반영 등)

이 정밀화 덕에 G.1~G.3a 항목들의 "재등록 트리거" 비용이 낮아졌다 —
응답까지 구현 안 해도 핸들러만 추가하면 advertise 가능.

### G.1 MediaBrowserService (channel 12) — DEPRECATED-IN-MODERN-AA (2026-04-29)

- **현재**: 클래스 존재 + 4개 inbound 핸들러 + 2개 outbound primitive
  완비. **registration 제거됨** — 두 차례 시도 후 modern AA에서
  비활성 채널로 결론.
- **2026-04-28 Day 1 시도**: 핸들러 등록 + advertise → 폰
  (Nothing A001 / SM-N981N)이 SDR 받고도 CHANNEL_OPEN_REQ 미전송
  (YT Music + 다른 미디어 앱).
- **2026-04-29 재투자**: Spotify(car-compat 인증된 앱) 설치 후 재테스트
  → 여전히 ch12 안 열림. PLAYBACK_STATUS source는 "Spotify"로 전환
  관찰됨에도 불구하고. 다른 모든 advertise 채널은 정상 open → HU
  identity 게이팅 아님.
- **결론**: legacy AAP MediaBrowser 채널은 modern Android Auto에서
  사실상 deprecated. 모던 미디어 앱들은 `androidx.car.app` (Car App
  Library) 사용 → 폰이 brows UI를 *직접 렌더링*해서 video sink (ch1)로
  projecting → HU는 dumb display 역할. ch12는 Car App Library 미지원
  앱(드물게 남은 podcast 앱 등)에 대한 fallback path로만 존재.
- **자산 유지 이유**: PhoneStatusService와 같은 디자인 패턴(passive
  parse handler + 콜백)을 그대로 재활용 가능한 reference 구현. 또한
  older AA 버전이나 fallback 시나리오가 학습 대상이 되면 재활성화 비용
  zero.
- **자세한 시도 + 가설 검증 흐름**: docs/plans/0005_media_browser.md.

### G.2 BluetoothService (channel 13)

- **현재**: 클래스 존재. **registration 제거됨** (2026-04-27, lag 원인).
  advertise 안 함. `HeadunitConfig.bluetooth_mac` placeholder
  ("02:00:00:00:00:00")는 클래스가 살아있는 동안 보존.
- **왜 등록 안 하는가**: G.0 — advertise하면 폰이 PAIRING_REQUEST를 보내고
  무응답이면 throttle. 진짜 페어링은 Bluedroid 통합 큰 작업이고 본 학습
  목표 범위 밖.
- **풀 구현 시 가치**: AAP가 BT 스택과 어떻게 협력하는지 (HFP 핸즈프리,
  A2DP 미디어 라우팅 협상). 단, A2DP_SINK 충돌 처리가 추가로 필요
  (platform_constraints.md 4.1).
- **트리거**: AAP-driven BT 페어링 워크플로우를 학습 대상으로 명시할 때.

### G.3 VendorExtensionService (channel 14)

- **현재**: 클래스 존재. **registration 제거됨** (2026-04-27, lag 원인).
- **왜 등록 안 하는가**: G.0 — advertise만 하고 응답 없으면 lag 비용.
  vendor extension은 표준 동작이 없으므로 학습 가치도 일반화 안 됨.
- **풀 구현 시 가치**: 거의 없음 (학습 목표상). vendor message 정의/송수신
  자체는 다른 서비스에서 이미 다 학습됨.
- **트리거**: 특정 OEM(예: TCC) 고유 기능을 AA로 노출해야 할 사업 요구가
  생기면. 학습 프로젝트로서는 close.

### G.3a Phase 4 응답-없는 서비스들 (NavigationStatus, PhoneStatus, GenericNotification)

- **현재**: 클래스 존재. registration 제거됨 (2026-04-27, lag 원인).
  Phase 4에서 처음 추가됐을 땐 advertise + 정식 구현 의도였으나 응답
  핸들러까지는 못 갔음.
- **갱신 (2026-04-27)**: G.0 갱신 룰 적용 가능 — passive handler
  (parse + log)만 추가하면 advertise 안전. 응답까지 만들지 않아도 됨.
- **풀 구현 시 가치 / 트리거**:
  - `NavigationStatus`: 클러스터 표시 UI가 있어야 의미. passive handler로
    먼저 등록해 메시지 구조 학습 → 그 다음 cluster HW 통합 시 풀 구현.
  - `PhoneStatus`: passive handler로 배터리/신호/통화 메시지 관찰 가치.
    표시 UI 시 풀 구현.
  - `GenericNotification`: HU가 SUBSCRIBE 보내야 폰이 NOTIFICATION 보냄
    — passive handler로는 관찰 못 함. SUBSCRIBE outbound 구현이 진입 조건.

### G.3b MediaPlaybackService — passive handler 등록됨 (2026-04-27)

- **현재**: passive handler 등록 + UI binding 완료 (2026-04-28).
  PLAYBACK_STATUS / PLAYBACK_METADATA proto 파싱 → AaService PlaybackInfo
  → DeviceListActivity media card. PLAYBACK_INPUT proto 자체는 미구현
  (재생 제어는 F.20에 따라 Input ch + KEYCODE 경로로 송신).
- **얻은 학습**:
  - 폰이 1초 간격으로 PLAYBACK_STATUS 보냄 (state, source app 이름,
    재생 위치 초 단위, shuffle/repeat/repeat_one)
  - METADATA는 곡 변경 시 또는 재생 시작 시 한 번 보냄 (song / artist /
    album / album_art bytes / playlist / duration / rating)
  - album_art 크기는 song마다 천차만별 (3KB ~ 90KB 관찰됨)
  - playlist 필드는 source app이 제공할 때만 채워짐 — Spotify는 보통
    채우고, YT Music은 빈 문자열인 경우가 많음
  - 한국어 string은 UTF-8 그대로 통과
  - lag 영향 없음 — passive handler가 G.0의 "처리되고 있는가" 신호
    충족
- **남은 학습 (트리거 시 추가)**:
  - PLAYBACK_INPUT message 정의 자체가 트리에 없음 (ID enum만). 비교
    실험을 하려면 먼저 `.proto`를 가져와야 함. 현재는 KEYCODE만
    사용하므로 PLAYBACK_INPUT의 실제 응답 패턴은 미관찰.
  - rating 필드의 의미 (단순 별점인지 좋아요 toggle인지) — 소스 앱별
    다른 듯

### G.4 ISensorSource 플랫폼 구현

- **현재**: 미구현. core `SensorService`는 service 핸들러로서 폰의 sensor
  request에 응답(예: SensorStartResponse)은 하지만, 실제 GPS/IMU 데이터
  공급원(`ISensorSource`) impl은 wire 안 됨.
- **왜 stub인가**: GPS는 차량 GNSS HAL, IMU는 SoC 가속도계 등 H/W
  의존성이 큼. 학습 가치는 "AAP sensor channel 동작 + 폰이 sensor
  데이터를 어떻게 요청/소비"까지로 충분히 큼 (현재 응답 logic만으로
  비디오 스트리밍 정상 동작 확인됨).
- **풀 구현 시 가치**: 진짜 GPS 좌표 공급으로 navigation 채널과 결합한
  실 동작 시나리오 학습. 차량 GNSS HAL 통합 패턴 학습.
- **트리거**: navigation 채널 깊이 분석이 우선순위가 될 때, 또는 실제
  주행 시나리오 검증이 필요할 때.

### G.5 Hardening (Phase 6 — 0001 plan 항목별)

#### G.5.1 6.1 Core unit test coverage ≥ 80%

- **현재**: PARTIAL — core 테스트 43개 통과, 커버리지 % 측정 안 됨.
- **판단**: 테스트가 학습 자료로서의 protocol 동작 captures 역할을 수행
  중 (e.g., session_handshake_test, EncodeDecodeMultiFragmentRoundTrip,
  SetActiveSessionSwapsSinks). 단순 % 채우기 보다 "어떤 동작을 코드로
  documents하는지"가 더 중요한 단계.
- **트리거**: 회귀가 잡히지 않는 영역 발견 시 그 영역 커버리지 추가.

#### G.5.2 6.2 ASan/UBSan in test builds

- **현재**: **DONE (2026-04-27)**. `core/CMakeLists.txt`에
  `ENABLE_SANITIZERS` 옵션 추가. `cmake -DENABLE_SANITIZERS=ON ..` 후
  `ctest`로 48개 단위 테스트가 AddressSanitizer + UndefinedBehaviorSanitizer
  켜진 상태에서 모두 clean pass. host CMake 빌드 전용 (Android.bp 디바이스
  빌드는 영향 없음).
- **얻은 것**: BufferedTransport / Framer / Session / encrypt 경로 등
  byte buffer를 다루는 새 코드들에 대한 memory safety + UB-free 증거
  layer 한 단계 추가. 향후 의심 시 sanitizer 빌드를 즉시 디버깅 무기로
  활용 가능.
- **앞으로**: 새 native 코드 추가할 때마다 sanitizer 빌드로 한 번 더
  검증 권장. CI에 sanitizer job 추가는 자연스러운 다음 단계.

#### G.5.3 6.3 Framer + crypto envelope fuzzing

- **현재**: NOT STARTED.
- **판단**: 학습 가치 △ — protocol 입력 도메인이 좁아서 (header 4바이트
  + flag bits + payload bytes) random fuzz 가치가 제한적. 그러나
  framer는 보안 경계이므로 정당한 투자.
- **트리거**: ASan/UBSan(6.2) 정착 후. fuzzer는 sanitizer 없이는 가치 ↓.

#### G.5.4 6.4 Connection error recovery

- **현재**: PARTIAL. F.16의 same-phone transport switch 처리 + USB write
  timeout 2초까지 됨. SSL handshake 중간 실패 후 재시도, transport
  순간 끊김 후 graceful 복구 등은 미검증.
- **판단**: 학습 가치 ↑ — 현실 차량 환경의 USB 케이블 흔들림, BT 일시
  끊김 등은 모두 이 영역. 시나리오별 디버그 = 플랫폼 한계 학습.
- **트리거**: P3-G(시나리오 러너) 도입 시 자연스럽게 함께.

#### G.5.5 6.5 Multi-session (2 phones simultaneously)

- **현재**: DONE. F.13 SessionManager로 multi-session OK. F.17 + F.18로
  active session sink swap도 정리됨.
- **추가 가치 없음** — close.

#### G.5.6 6.6 Background audio (sink detach when AA not foreground)

- **현재**: NOT VERIFIED. F.18로 sink detach API는 정리됐지만 "AA가
  background 갔을 때 자동 detach" 트리거는 app 측에 wire 안 됨.
- **판단**: 학습 가치 ○ — Android Activity lifecycle ↔ AAP focus 동기화
  패턴 학습 가치 있음. 단, 실차에서 어떤 시나리오가 있는지 명확하지
  않으므로 우선순위는 다음 라운드.
- **트리거**: 차량 내 다른 앱과의 audio focus 충돌 시나리오가 학습 대상이
  될 때.

#### G.5.7 6.7 QuirksProfile framework (F.10)

- **현재**: NOT STARTED. F.10에서 "원칙 채택"으로 결정됐지만 framework
  코드 자체는 없음.
- **판단**: 학습 가치 △ — 현재 검증된 폰이 1~2종이라 quirks가 아직
  카탈로그화 가치가 적음. 폰 모델 5+종 검증 후 quirks가 누적되면 그때
  framework화하는 게 자연스러움.
- **트리거**: 검증 폰 5종 누적 또는 동일 phenomenon이 polymorphic
  처리가 필요해질 때.

#### G.5.8 6.8 Performance profiling (latency, CPU)

- **현재**: NOT VERIFIED. troubleshooting.md #18에서 input latency
  ~200-300ms 잔여 기록은 있으나 정량 측정/대시보드 없음.
- **판단**: 학습 가치 ↑ — 어디서 시간이 걸리는지(transport / decode /
  display) 분리 측정은 IPC 아키텍처(F.12) trade-off의 학습에 직결.
- **트리거**: F.12 vs JNI 단일 프로세스 비교를 학습 대상으로 둘 때.

### G.6 사용 권장

- 새 stub/미구현 결정을 할 때마다 본 카탈로그에 G.x로 추가하라.
  결정의 근거가 휘발되지 않게.
- "왜 X는 풀 구현이 아닌가?" 질문이 두 번 이상 나오면 그 답이 본
  카탈로그에 있는지 확인. 없으면 추가.
- 카탈로그 항목이 "트리거" 조건에 도달하면 풀 구현 작업을 plans/
  하위에 새 plan 파일(0005, 0006...)로 추가.
