# Platform Constraints — Android Auto Headunit Porting Checklist

> 작성: 2026-04-27. aa-v2 개발 중 부딪힌 플랫폼 제약을 "포팅 체크리스트"로
> 재정리. troubleshooting.md는 디버깅 로그(증상→근본→수정), 본 문서는
> 다른 H/U(다른 SoC, 다른 Android 버전)로 옮길 때 미리 알아둘 카테고리별 제약.
>
> 각 항목은 다음 형식:
> - **제약**: 한 줄 요약
> - **포팅 시 의미**: 왜 이게 다른 플랫폼에서도 문제가 되는지
> - **필요 설정/코드**: 우리가 채택한 해결책
> - **출처**: troubleshooting.md 번호

---

## 1. 프로세스 / 권한 / SELinux

### 1.1 시스템 UID로 실행

**제약**: Bluetooth/WiFi 관리 API는 일반 앱 UID(10xxx)에서 호출 시
거부된다. `BluetoothManagerService.checkPackage()`, `WifiManager.startSoftAp()`
등이 호출자가 system UID(1000)인지 또는 system 권한 서명인지 확인한다.

**포팅 시 의미**: AOSP 기반 Android 차량 플랫폼이면 동일하게 적용된다.
앱이 BT/WiFi AP 토글을 직접 제어해야 하는 한 시스템 권한이 필수.

**필요 설정/코드**:
- `AndroidManifest.xml`에 `android:sharedUserId="android.uid.system"` 선언
- `Android.bp`에 `certificate: "platform"`
- privapp permissions whitelist에 `NETWORK_STACK` 등록

**출처**: troubleshooting.md #15

### 1.2 Native daemon SELinux 도메인

**제약**: native daemon이 binder service로 등록되려면 SELinux 도메인에
`add_service`/`find_service` 권한이 필요. 잘못된 seclabel은 SELinux가
프로세스를 정지시키므로 `ServiceManager.getService()`가 영원히 null.

**포팅 시 의미**: SELinux는 모든 AOSP 플랫폼 공통. 신규 native daemon마다
도메인 정의가 필요.

**필요 설정/코드**:
- 개발: `seclabel u:r:su:s0` (검증 단계만; 절대 production 금지)
- Production: dedicated 도메인 (`aa_engine.te`) + `service.te`로 service 등록 권한 부여
- 진단: `dmesg | grep avc` 로 denial 확인

**출처**: troubleshooting.md #9

---

## 2. USB / AOA 호스트 스택

### 2.1 Automotive USB handler 가로채기

**제약**: Android Automotive의 `UsbProfileGroupSettingsManager`가 모든 USB
attach 이벤트를 `android.car.usb.handler`로 라우팅한다. 앱의 intent-filter는
이 시스템 핸들러보다 우선순위가 낮다.

**포팅 시 의미**: Automotive 빌드(차량 IVI)면 모든 H/U 공통 문제. 비-Automotive
Android 빌드(태블릿 등)면 해당 없음.

**필요 설정/코드**:
- 임시: `pm disable android.car.usb.handler` (간단하지만 다른 USB 기기 영향)
- 채택: 우리 앱의 `onDeviceAvailable`에서 `FLAG_ACTIVITY_REORDER_TO_FRONT`로
  포어그라운드 재탈환
- 장기: 플랫폼 USB 라우팅 config에 우리 앱을 AOA 전담으로 등록, 또는
  `android.car.usb.handler`를 대체 구현

**출처**: troubleshooting.md #13

### 2.2 USB attach 중복 이벤트

**제약**: 앱 시작 시 `getDeviceList()` 스캔과 `BroadcastReceiver`가 모두
이미 연결된 USB 기기를 알리므로 동일 기기에 대해 attach 이벤트가 중복.
중복 AOA 전환 시도는 두 번째가 실패하거나 phone을 혼란에 빠뜨림.

**포팅 시 의미**: Android USB 호스트 스택 공통. 일반 Android 앱도 동일.

**필요 설정/코드**: AOA 전환 진행 중 플래그(`aoaSwitchInProgress`)로 두 번째
이벤트 무시.

**출처**: troubleshooting.md #14

### 2.3 USB bulk write 동기 호출이 이벤트 루프 차단

**제약**: `ioctl(USBDEVFS_BULK)` write는 timeout 동안 호출자 스레드를 블록.
asio strand 위에서 직접 호출 시 strand가 멈추고 protocol 처리 전체가 정지.

**포팅 시 의미**: Linux USB host(libusb 포함) 공통. asio 같은 단일 스레드
이벤트 루프 모델이면 어떤 transport든 비슷한 함정.

**필요 설정/코드**: USB write를 dedicated 스레드로 옮기고, strand는 완료
콜백만 처리.

**출처**: troubleshooting.md #8

### 2.4 같은 폰의 transport 전환 — write timeout

**제약**: 폰이 무선↔USB 전환 시 짧은 stall 구간이 있음. 짧은 USB write
timeout(500ms 이하)은 이 구간을 못 넘김.

**포팅 시 의미**: USB 전송 timing은 칩셋이 아닌 폰 측 동작. 모든 H/U 공통.

**필요 설정/코드**:
- USB bulk timeout 2000ms
- `onPhoneIdentified`에서 같은 폰의 다른 transport 세션이 있으면 즉시 종료
  (timeout 기다리지 않음, F.16)

**출처**: troubleshooting.md #21

---

## 3. WiFi / Hotspot

### 3.1 시스템 hotspot API 권한

**제약**: `WifiManager.startSoftAp()`는 `NETWORK_STACK` 서명 권한 필요.
일반 앱은 호출 자체가 안 됨.

**포팅 시 의미**: 다른 무선 AA 구현을 만들든 동일하게 시스템 권한 필요.

**필요 설정/코드**:
- 매니페스트에 `<uses-permission android:name="android.permission.NETWORK_STACK"/>`
- privapp-permissions whitelist에 추가
- API: `startSoftAp(null)` / `stopSoftAp()` (deprecated `setWifiApEnabled` 아님)

**출처**: troubleshooting.md #15

### 3.2 5GHz AP는 국가 코드 필요

**제약**: `SoftApManager`는 valid 국가 코드 없이 5GHz 채널을 거부. 런타임에
`iw reg set`, `settings put`, `setprop persist.vendor.wifi.country` 모두
무효 — 부팅 시 적용된 값이 아니면 안 먹음.

**포팅 시 의미**: WiFi 칩이 5GHz 지원해도 플랫폼 빌드 단에서 국가 코드를
설정해야 5GHz 무선 AA가 가능. 2.4GHz는 해당 없음.

**필요 설정/코드**:
- 보드 overlay나 `ro.boot.wificountrycode=KR` 같은 부팅 시 prop
- TCC803x에서는 현재 미해결 — 2.4GHz로만 동작

**출처**: troubleshooting.md #18 (말미)

---

## 4. Bluetooth

### 4.1 A2DP_SINK 프로필 충돌

**제약**: 무선 AA(AAW)와 A2DP_SINK가 같은 BT 링크를 공유하려 들면
AVRCP PAUSE/PLAY가 충돌. 폰이 미디어 키 누른 것처럼 인식해서 재생이 멈춤.

**포팅 시 의미**: Bluedroid 기반 모든 H/U 공통. 무선 AA 세션 동안에는
A2DP_SINK 비활성화 필요.

**필요 설정/코드**: `BtProfileGate`가 무선 세션 시작 시 A2DP_SINK
priority를 0으로 떨어뜨리고 disconnect, 종료 시 복원.

**출처**: 코드 `BtProfileGate.java`, F.10 인접 결정

### 4.2 RFCOMM SDP UUID

**제약**: 폰이 H/U를 무선 AA 호환으로 발견하려면 정해진 AAW UUID
(`4DE17A00-52CB-11E6-BDF4-0800200C9A66`)로 SDP service record를 등록해야 함.
임의 UUID 사용 시 폰이 검색 결과에서 무시.

**포팅 시 의미**: Google Android Auto Wireless 사양. 변경 불가.

**필요 설정/코드**: `BluetoothServerSocket` 생성 시 정확한 UUID 사용.

**출처**: docs/plans/0004_phase6_wireless_aa.md "Key Facts"

---

## 5. MediaCodec / 비디오 디코딩

### 5.1 TCC803x libstagefright ubsan

**제약**: TCC803x의 libstagefright에서 `releaseOutputBuffer(idx, true)`가
timestamp multiplication에서 undefined behavior(`mul-overflow`) 발생,
ubsan 빌드면 abort.

**포팅 시 의미**: SoC 별 stagefright 패치 차이. 다른 SoC에서는 발생 안 할
수 있음. 검증 시 ubsan 빌드 필수.

**필요 설정/코드**: `releaseOutputBuffer(idx, System.nanoTime())` — 명시적
render timestamp 전달.

**출처**: troubleshooting.md #6

### 5.2 IDR frame drop on reconfigure

**제약**: 비디오 codec을 release/reconfigure 하는 동안 input thread가
block되면 그 사이 도착한 IDR(I-frame)이 손실됨. 이후엔 P-frame만 받아
디코딩 불가능 — H.264는 keyframe 없이 시작 못 함.

**포팅 시 의미**: HW 디코더 reconfigure timing 차이는 SoC마다 있지만,
"reconfigure 중 IDR drop" 패턴은 모든 MediaCodec 사용 코드 공통.

**필요 설정/코드**: 단일 스레드 모델 채택 (F.15) — `feedData()`가 input
queue + output drain 모두 한 호출로 처리. 별도 output drain thread 없음.
codec은 세션당 한 번만 configure하고, reconfigure 안 함.

**출처**: troubleshooting.md #19

### 5.3 VideoFocus는 Surface lifecycle이 드라이버

**제약**: `VideoFocus(PROJECTED)`를 Surface가 준비되기 전에 보내면 폰이
codec config + 첫 IDR을 보내는데 디코더가 받을 준비가 안 되어 IDR 손실.

**포팅 시 의미**: 모든 H/U 공통. Surface 라이프사이클을 protocol focus와
동기화 필요.

**필요 설정/코드** (F.14):
- `surfaceCreated` → `VideoFocus(PROJECTED)`
- `surfaceDestroyed` → `VideoFocus(NATIVE)`
- `activateSession`은 sink만 attach, focus는 안 보냄

**출처**: troubleshooting.md #20, F.14

### 5.4 Cross-process Surface 전달 불가 (Android 10)

**제약**: Android 10에서 Surface의 `IGraphicBufferProducer` IBinder는 AIDL
parcel을 통해 다른 프로세스로 안전하게 전달 불가. `gui/view/Surface.h`의
parcelable이 의도적으로 lightweight stub.

**포팅 시 의미**: Android 11+ 에서는 일부 개선이 있었으나 보장 안 됨.
출시 제품이라면 SharedMemory + native 디코딩이 필요. aa-v2는 학습 목적상
F.12 결정으로 회피 — daemon이 H.264 NALU를 AIDL로 전달, app이 디코딩.

**필요 설정/코드**: 디코더를 native daemon이 아닌 app 프로세스에 두는
아키텍처. 비용은 IPC 오버헤드(#5.5).

**출처**: troubleshooting.md #7

### 5.5 IPC 오버헤드 (~200-300ms 입력 지연)

**제약**: F.12 아키텍처는 모든 비디오 프레임이 AIDL `oneway` Binder로
이동. 프레임당 copy + Binder 직렬화. native AMediaCodec(JNI)에 비해
지연이 누적.

**포팅 시 의미**: 모든 IPC-기반 디코딩 아키텍처 공통의 trade-off.
출시 제품이라면 SharedMemory 또는 JNI 단일 프로세스로 가야 함.

**완화 코드**:
- `TCP_NODELAY` (Nagle 끄기)
- transport에 dedicated read/write 스레드
- direct `oneway` Binder (큐 hop 제거)
- MediaCodec `KEY_LOW_LATENCY` + 1ms timeout

**출처**: troubleshooting.md #18, F.12

---

## 6. Activity / 백 스택

### 6.1 `singleTask`는 자기 task의 root

**제약**: `launchMode="singleTask"` activity는 자기 task의 root가 됨.
finish 시 시스템 launcher로 돌아감 (이전 activity가 같은 task 아님).

**포팅 시 의미**: Android 모든 버전 공통.

**필요 설정/코드**: 앱의 root(예: `DeviceListActivity`)만 `singleTask`로,
child activity(예: `AaDisplayActivity`)는 default `standard` 모드.
필요 시 `TaskStackBuilder`로 back stack 명시 구성.

**출처**: troubleshooting.md #11

---

## 7. 브로드캐스트 / IPC 전달

### 7.1 시스템 프로세스 브로드캐스트는 protected만 허용

**제약**: `persistent="true"` 등으로 시스템-유사 프로세스에서 동작하는
앱은 사용자 정의 action 문자열의 브로드캐스트가 거부될 수 있음.
"non-protected broadcast" 로그.

**포팅 시 의미**: Automotive 등 시스템 권한 앱 공통.

**필요 설정/코드**: 같은 프로세스 내 직접 callback 인터페이스
(`DeviceStateListener`)로 브로드캐스트 대체. 같은 프로세스 내
브로드캐스트는 시스템 검사 우회.

**출처**: troubleshooting.md #12

---

## 8. AAP 프로토콜 동작 (폰 측 요구)

이 섹션은 H/U 코드의 의무 사항. 빠뜨리면 폰이 protocol을 진행 안 함.

### 8.1 MicrophoneService 필수 advertise

**제약**: Samsung 폰들은 `media_source_service`(microphone)가
ServiceDiscoveryResponse에 없으면 ChannelOpen을 보내지 않음. 캡처 구현은
없어도 advertise는 필수.

**검증 모델**: Samsung SM-N981N

**필요 설정/코드**: ch 7에 PCM 16kHz mono 설정으로 stub 등록.

**출처**: troubleshooting.md #1

### 8.2 ACK는 frame마다, batching 금지

**제약**: AAP는 credit-based flow control. `max_unacked` 윈도우 소진 후
ACK가 와야 다음 프레임을 보냄. 10프레임마다 ACK 같은 batching은 폰의
전송률을 분당 몇 프레임 수준으로 떨어뜨림.

**필요 설정/코드**: 모든 frame에 `ack=1` 즉시 응답.

**출처**: troubleshooting.md #2

### 8.3 SensorStartResponse 즉시 응답

**제약**: 폰이 `SensorStartRequest`를 보내고 `SensorStartResponse(SUCCESS)`를
기다림. 응답 늦으면 비디오 시작이 3-5초 지연됨.

**필요 설정/코드**: SensorStartRequest 수신 즉시 SUCCESS 응답.

**출처**: troubleshooting.md #3

### 8.4 AudioFocus 매핑

**제약**: AudioFocusRequest 타입을 잘못 매핑하면 폰이 contradictory
state로 인식해서 stall.

**필요 매핑**:
- GAIN → STATE_GAIN
- GAIN_TRANSIENT → STATE_GAIN_TRANSIENT
- GAIN_TRANSIENT_MAY_DUCK → STATE_GAIN_TRANSIENT_GUIDANCE_ONLY
- **RELEASE → STATE_LOSS** (헷갈리지만 LOSS가 정답)

**출처**: troubleshooting.md #5

### 8.5 멀티-fragment first에 4-byte total_size 필드

**제약**: 단편화된 메시지의 first fragment는 wire header와 payload
사이에 4-byte big-endian `total_size` 필드를 가짐. `payload_length`에는
포함되지 않음. 이 4바이트 누락은 framer를 desync로 만들고 모든 후속
frame이 garbage가 됨.

**필요 설정/코드**:
- 디코드: First fragment 감지 시 `total_frame_size = HEADER + 4 + payload_length`
- 인코드: First fragment에 4바이트 total_size 작성

**출처**: troubleshooting.md #4

### 8.6 Per-fragment decryption (TLS record boundaries)

**제약**: 암호화된 multi-fragment 메시지를 ciphertext로 reassemble한 뒤
한 번에 SSL_read 하면 fail — TLS record 경계가 AAP 메시지 경계와 안 맞음.
SSL_read가 인접 메시지의 데이터를 잘못 소비.

**필요 설정/코드**: fragment마다 개별 decrypt → plaintext를 채널별로 누적
(aasdk 모델).

**출처**: troubleshooting.md #4

---

## 9. asio / async I/O 패턴

### 9.1 `async_read` vs `async_read_some`

**제약**: `asio::async_read(socket, buffer)`는 buffer가 가득 찰 때까지
대기. 16KB buffer면 16KB가 도착해야 콜백. AAP는 작은 메시지가 다수라서
이 패턴 쓰면 영원히 안 끝남.

**포팅 시 의미**: 모든 asio 기반 transport 공통.

**필요 설정/코드**: `socket.async_read_some(buffer)` — 일부라도 도착하면
즉시 콜백.

**출처**: troubleshooting.md #16

### 9.2 TCP accept를 io_context에서 하지 말 것

**제약**: TCP accept는 WiFi 연결 지연(3-4초)동안 블록. io_context
스레드에서 호출하면 그동안 다른 모든 핸들러가 정지.

**필요 설정/코드**: accept를 별도 스레드(우리는 Binder 스레드)에서 수행,
accepted fd를 transport descriptor로 전달.

**출처**: troubleshooting.md #16

---

## 10. 로깅

### 10.1 native daemon stderr → logcat 자동 안 됨

**제약**: `fprintf(stderr, ...)`는 init.rc로 시작된 daemon에서 logcat에
안 잡힘. 수동 실행 시에는 보임 — 일관성 없음.

**필요 설정/코드**: `main()`에서 `set_log_function(android_log_function)`
등록. 내부적으로 `__android_log_vprint` 호출.

**출처**: troubleshooting.md #17

---

## 부록 — 카테고리 ↔ troubleshooting 인덱스

| 카테고리 | troubleshooting.md 항목 |
|---|---|
| 프로세스/권한 | #9, #15 |
| USB/AOA | #1*, #8, #13, #14, #21 |
| WiFi/Hotspot | #15, #18 |
| Bluetooth | (코드 BtProfileGate / 0004) |
| MediaCodec | #6, #7, #19, #20 |
| Activity 백 스택 | #11 |
| 브로드캐스트 | #12 |
| AAP 프로토콜 | #1, #2, #3, #4, #5 |
| asio/async I/O | #16 |
| 로깅 | #17 |
| 성능/지연 | #18 |

\* #1은 AAP 의무 (8.1) 분류

---

## 사용 권장

새 H/U 플랫폼으로 포팅 시:
1. 본 문서를 카테고리 순서대로 훑고 항목별로 "우리 플랫폼은 어떻게 다른가" 점검
2. 각 항목의 "출처" troubleshooting.md를 보면 증상/근본/수정의 구체적
   사례가 나옴
3. 새로 발견한 플랫폼 제약은 troubleshooting.md에 먼저 기록(증상 중심),
   안정되면 본 문서에 카테고리화해서 추가
