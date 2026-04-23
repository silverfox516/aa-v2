# Refactoring Assessment

Date: 2026-04-23

## Conclusion

리팩토링은 필요한 상태다. 현재 코드베이스는 동작 경로는 비교적 선명하지만, 세션/서비스/Android 브리지 계층에 책임이 과도하게 집중되어 있어 기능 추가와 장애 대응 비용이 빠르게 커질 구조다. 특히 `Session`, `ControlService`, `AaService`, `BluetoothWirelessManager`는 이미 하나의 클래스가 여러 상태 전이, I/O, 플랫폼 연동, 자원 해제를 함께 맡고 있다.

이번 판단은 "즉시 전면 재작성"이 아니라, 프로토콜 안정성을 유지하면서 경계 분리와 테스트 보강을 우선하는 점진적 리팩토링이 적절하다는 결론이다.

## Scope Reviewed

- Core runtime: `core/src/session`, `core/src/engine`, `core/src/service`
- Android app bridge: `app/android/src/com/aauto/app`
- Wireless path: `app/android/src/com/aauto/app/wireless`
- Test coverage: `tests`

## Why Refactoring Is Needed

### 1. Session이 상태 기계, 프레이밍, 암복호화, 서비스 라우팅을 모두 직접 가진다

- `core/src/session/Session.cpp:92` 이후 송신 경로가 프레임 조립, 암호화 여부 판단, 큐잉까지 한 메서드에 들어가 있다.
- `core/src/session/Session.cpp:209` 이후 수신 경로가 조각 재조립, 복호화, 메시지 타입 파싱, 핸드셰이크 분기, 서비스 위임을 모두 처리한다.
- `core/src/session/Session.cpp:352` 이후 핸드셰이크 상태 전이까지 같은 클래스가 직접 구현한다.

영향:
- 버그가 생기면 원인을 전송/복호화/상태 전이 중 어디서 찾아야 하는지 분리가 잘 안 된다.
- 핸드셰이크 정책 변경이나 재전송 정책 추가가 `Session` 중심의 대형 수정으로 이어질 가능성이 높다.

권장 기술:
- `Session`을 `HandshakeCoordinator`, `MessagePipeline`, `ChannelDispatcher` 같은 협력 객체로 분리
- 암복호화 포함 송수신 경로를 별도 `OutboundMessageEncoder` / `InboundMessageAssembler`로 추출
- 상태 전이를 enum + switch 기반 단일 클래스에서 이벤트 기반 상태 처리 함수로 축소

### 2. Engine이 세션 생성, 서비스 wiring, active session 정책을 한 곳에서 직접 조립한다

- `core/src/engine/Engine.cpp:172` 이후 transport 생성, crypto 생성, session 생성, peer service 주입, control service 초기화가 한 메서드에 몰려 있다.
- `core/src/engine/Engine.cpp:64` 의 active session 처리에는 아직 TODO가 남아 있다.

영향:
- 세션 생성 정책과 서비스 구성 정책이 분리되지 않아 멀티세션 확장 시 변경 범위가 넓다.
- 테스트에서 엔진 전체를 올리지 않으면 조립 로직을 검증하기 어렵다.

권장 기술:
- `SessionFactory` 또는 `SessionAssembler` 도입
- active session 전환 로직을 별도 `SessionRegistry` 또는 `ActiveSessionManager`로 분리
- wiring 로직을 pure function 또는 builder로 이동해 단위 테스트 가능하게 구성

### 3. ControlService가 프로토콜 처리와 heartbeat thread 관리까지 동시에 수행한다

- `core/src/service/ControlService.cpp:45` 이후 대부분의 control message handler가 생성자에 직접 등록된다.
- `core/src/service/ControlService.cpp:193` 이후 서비스 생명주기와 별개로 heartbeat thread를 직접 띄우고 종료한다.
- `core/src/service/ControlService.cpp:213` 이후 ServiceDiscoveryResponse 조립 책임도 이 클래스에 있다.

영향:
- control message 추가 시 생성자 비대화가 계속된다.
- thread 기반 heartbeat는 `asio` 기반 나머지 코어와 실행 모델이 달라 종료/경합 문제를 만들기 쉽다.
- 프로토콜 메시지 해석과 keepalive 정책을 독립적으로 테스트하기 어렵다.

권장 기술:
- handler registration을 `ControlMessageRouter`로 추출
- heartbeat를 `asio::steady_timer` 기반으로 전환해 코어 실행 모델 통일
- `ServiceDiscoveryBuilder`를 분리해 protobuf 생성 로직을 독립 테스트 가능하게 구성

### 4. AudioService와 VideoService에 중복된 미디어 채널 처리 패턴이 반복된다

- `core/src/service/AudioService.cpp:96` 이하와 `core/src/service/VideoService.cpp:87` 이하가 `Setup -> Config`, `Start`, `CodecConfig`, `Data`, `Ack` 패턴을 유사하게 구현한다.
- stop/close 시 sink 정리 패턴도 `core/src/service/AudioService.cpp:72`, `core/src/service/VideoService.cpp:76`에서 반복된다.

영향:
- ACK 정책이나 시작/정지 정책을 바꿀 때 동일한 수정이 여러 서비스에 반복된다.
- 새 media service 추가 시 중복 복제가 유력하다.

권장 기술:
- `MediaSinkServiceBase<TConfig, TSink>` 같은 공통 베이스 추출
- timestamp stripping, ack sending, start/stop lifecycle을 템플릿 또는 조합 객체로 공통화
- protobuf config 빌더만 서비스별로 override

### 5. Android AaService가 너무 많은 플랫폼 책임을 가진다

- `app/android/src/com/aauto/app/AaService.java:35` 클래스 하나가 binder 연결, USB 상태, wireless 상태, decoder/audio lifecycle, activity launching, hotspot 조회, engine retry를 모두 맡는다.
- `app/android/src/com/aauto/app/AaService.java:335` 이후 wireless 준비 판단과 hotspot 정보 해석이 같은 서비스 안에 있다.
- `app/android/src/com/aauto/app/AaService.java:414` 의 cleanup도 UI/미디어/세션 상태를 한번에 정리한다.

영향:
- Android lifecycle 버그가 세션 제어 버그와 섞인다.
- UI 플로우 변경이나 wireless 정책 변경 시 `AaService`가 계속 커진다.
- instrumentation test 없이 수동 검증 의존도가 커진다.

권장 기술:
- `EngineConnectionManager`, `UsbSessionCoordinator`, `WirelessSessionCoordinator`, `PlaybackController`로 역할 분리
- 서비스 내부 상태를 명시적 `SessionUiState` 또는 reducer 형태로 정리
- activity launching은 별도 navigator/helper로 추출

### 6. BluetoothWirelessManager는 프로토콜 파싱과 스레드 제어가 강결합돼 있다

- `app/android/src/com/aauto/app/wireless/BluetoothWirelessManager.java:113` 이후 accept loop가 세션 스레드 lifecycle과 직접 얽혀 있다.
- `app/android/src/com/aauto/app/wireless/BluetoothWirelessManager.java:177` 이후 handshake state machine, protobuf-like 인코딩/디코딩, disconnect callback이 한 클래스에 있다.
- `app/android/src/com/aauto/app/wireless/BluetoothWirelessManager.java:242` 에서는 성공 여부와 무관하게 `onDeviceDisconnected`가 항상 호출된다.

영향:
- 연결 실패와 정상 종료 시그널이 섞여 상위 계층에서 중복 cleanup을 부를 수 있다.
- wire protocol 검증을 독립 테스트하기 어렵다.
- 스레드 인터럽트 기반 종료는 재현 어려운 race condition을 만들 수 있다.

권장 기술:
- `WirelessHandshakeCodec`와 `WirelessSessionRunner` 분리
- disconnect reason/state를 sealed enum 수준의 명시적 결과 객체로 모델링
- callback contract를 `connecting -> ready -> disconnected` 와 `connecting -> failed`로 분리해 중복 상태 전이를 방지

### 7. 테스트 범위가 핵심 위험 영역을 따라가지 못한다

- 현재 C++ 테스트 타깃은 `tests/CMakeLists.txt:18` 기준 `placeholder`, `framer`, `session_handshake` 정도만 포함한다.
- `ControlService`, `Engine` 조립, media service, Android wireless path에는 자동화 검증이 거의 없다.

영향:
- 리팩토링을 시작할 때 회귀 검출망이 약하다.
- 구조 개선보다 "건드리기 무서워서 유지" 상태가 이어질 가능성이 높다.

권장 기술:
- refactor 전에 characterization test 추가
- `ControlService`와 `Engine`은 C++ 단위 테스트 보강
- Android 쪽은 JVM unit test 가능한 codec/state reducer 우선 추출

## Priority

### P1

- `Session` 책임 분리
- `ControlService` heartbeat를 `asio` 타이머 기반으로 전환
- `Engine` 조립 로직 추출
- 핵심 characterization test 추가

### P2

- `AudioService` / `VideoService` 공통 베이스 도입
- `AaService` 역할 분리
- `BluetoothWirelessManager` wire protocol codec 추출

### P3

- UI/activity 단의 상태 표현 정리
- active session 정책 구현 및 정책 객체화

## Refactoring Plan

### Phase 1. 안전망 확보

- `Session` 현재 동작을 고정하는 테스트 추가
- `ControlService`에 ping/pong, ByeBye, ServiceDiscovery 응답 테스트 추가
- `Engine` 세션 생성 및 cleanup 동작 테스트 추가
- Android wireless 프로토콜은 순수 codec 테스트 가능 구조부터 분리

완료 기준:
- 구조 변경 전 핵심 시나리오가 자동화 테스트로 재현된다.

### Phase 2. Core 실행 모델 정리

- `ControlService` heartbeat thread를 제거하고 `asio::steady_timer` 기반 scheduler로 교체
- `Engine::do_start_session`에서 wiring 책임을 `SessionAssembler`로 이동
- session lifecycle callback을 명시적 인터페이스로 정리

완료 기준:
- 코어 계층에서 thread 모델이 `asio` 중심으로 정리된다.

### Phase 3. Session 분해

- `Session`에서 handshake 처리 객체 분리
- 프레임/메시지 인코딩 및 디코딩 파이프라인 분리
- 서비스 dispatch를 별도 채널 라우터로 이동

완료 기준:
- `Session.cpp`가 상태 orchestration 중심으로 축소되고, 각 책임이 독립 테스트 가능해진다.

### Phase 4. Service 공통화

- `AudioService` / `VideoService` 공통 lifecycle 추출
- ACK 정책과 timestamp stripping 공통 유틸 도입
- 각 서비스는 protobuf 차이와 sink 타입 차이만 유지

완료 기준:
- media 서비스 간 중복 코드가 실질적으로 제거된다.

### Phase 5. Android 브리지 정리

- `AaService`에서 engine 연결, USB 세션, wireless 세션, playback 제어를 분리
- `BluetoothWirelessManager`에서 codec과 session runner 분리
- cleanup 경로를 단일 상태 전이 함수로 통합

완료 기준:
- Android 서비스는 coordinator 역할만 수행하고, 상태 전이 중복이 줄어든다.

## Suggested Target Architecture

- Core
  - `Engine`
  - `SessionAssembler`
  - `Session`
  - `HandshakeCoordinator`
  - `InboundMessageAssembler`
  - `OutboundMessageEncoder`
  - `ChannelDispatcher`
  - `ControlMessageRouter`
  - `ServiceDiscoveryBuilder`
- Android
  - `AaService`
  - `EngineConnectionManager`
  - `UsbSessionCoordinator`
  - `WirelessSessionCoordinator`
  - `PlaybackController`
  - `WirelessHandshakeCodec`

## Recommended Order Of Work

1. 테스트 추가로 현재 프로토콜 동작을 고정한다.
2. `ControlService`의 heartbeat 실행 모델부터 정리한다.
3. `Engine` 조립 로직을 추출해 세션 생성 책임을 분리한다.
4. `Session`을 송신/수신/핸드셰이크 책임으로 분해한다.
5. media 서비스 공통화를 적용한다.
6. 마지막으로 Android 서비스 계층을 분리한다.

## Notes

- 지금 구조는 "동작하는 초기 구현"으로는 충분히 합리적이다.
- 다만 무선 경로와 멀티세션, 장애 복구를 계속 확장할 계획이라면 지금 시점에서의 점진적 리팩토링이 비용 대비 효과가 높다.
- 전면 재작성보다 테스트 선행 후 경계 분리를 반복하는 방식이 가장 안전하다.

## Progress Update

Date: 2026-04-23

### Completed In Phase 1

- `tests/core/control_service_test.cpp` 추가
  - `ServiceDiscoveryRequest -> ServiceDiscoveryResponse`
  - `PingRequest -> PingResponse`
  - `AudioFocusRequest -> AudioFocusNotification`
  - `ByeByeRequest` / `ByeByeResponse` close callback 동작 고정
- `tests/core/engine_test.cpp` 추가
  - transport 생성 실패 시 engine error callback 전달 고정
  - 정상 세션 시작 시 transport/service/crypto 생성과 `Handshaking` 진입 고정
- `tests/CMakeLists.txt` 수정
  - 새 characterization test를 `aauto_core_test` 타깃에 연결

### Files Added Or Changed

- `docs/refactoring_assessment.md`
- `tests/core/control_service_test.cpp`
- `tests/core/engine_test.cpp`
- `tests/CMakeLists.txt`

### Verification Attempt

- `cmake -S . -B build` 로 테스트 빌드 구성을 검증하려고 시도했다.
- 하지만 `protobuf/CMakeLists.txt` 경유로 `abseil`을 FetchContent로 내려받는 단계에서 네트워크 제한 때문에 configure가 중단됐다.
- 실패 원인은 코드 자체보다 외부 의존성 다운로드 불가다.

확인된 오류 요약:
- `https://github.com/abseil/abseil-cpp.git` 접근 실패
- `Could not resolve host: github.com`

### Current Limitations

- 네트워크 제한 때문에 새 테스트의 실제 compile/test 실행은 아직 확인하지 못했다.
- 따라서 현재 상태는 "테스트 코드 추가 및 빌드 타깃 연결 완료, 실행 검증 대기"다.

### Recommended Next Step

- 다음 우선순위 작업은 `ControlService`의 heartbeat 구현을 별도 thread에서 `asio::steady_timer` 기반으로 옮기는 것이다.
- 이 작업이 끝나면 Core 실행 모델이 더 일관돼지고 이후 `Session` 분해도 훨씬 안전해진다.

## Progress Update 2

Date: 2026-04-23

### Completed After Phase 1

- `ControlService` heartbeat를 별도 `std::thread` 기반 루프에서 `asio::steady_timer` 기반 스케줄링으로 전환했다.
- `ControlService` 생성자에 executor를 주입하도록 바꿨다.
- `Engine`이 `ControlService`를 만들 때 `io_context` executor를 넘기도록 수정했다.
- `Engine`에서 `set_log_tag()`를 먼저 호출하고 `on_channel_open()`을 호출하도록 순서를 정리했다.
- `tests/core/control_service_test.cpp`도 새 생성자 시그니처에 맞게 갱신했다.

### Technical Impact

- control plane의 heartbeat 실행 모델이 Core의 다른 비동기 흐름과 같은 executor 계층으로 정렬됐다.
- thread 생성/종료와 join 대기 로직이 제거되어 shutdown 경로가 단순해졌다.
- 기존 구조에서 가장 위험했던 "서비스는 `asio` 기반인데 heartbeat만 별도 thread에서 `send()` 호출" 문제를 줄였다.

### Current Verification Status

- 네트워크 제한 때문에 전체 CMake configure/build는 여전히 실행 검증하지 못했다.
- 따라서 이번 heartbeat 리팩토링도 로컬 정적 검토와 diff 검토 기준으로 반영된 상태다.

### Updated Next Step

- 다음 작업은 `Engine::do_start_session()` 내부 조립 로직을 별도 assembler/factory 계층으로 추출하는 것이다.
- 그 다음 순서로 `Session` 내부의 handshake / encode-decode / dispatch 책임을 분리하는 것이 적절하다.

## Progress Update 3

Date: 2026-04-23

### Completed After Progress Update 2

- `Engine::do_start_session()`의 조립 책임을 helper 메서드로 분리했다.
- 세션 시작 경로를 다음 단계로 나눴다.
  - `make_send_fn()`
  - `create_session()`
  - `create_control_service()`
  - `register_services()`
  - `activate_session()`
  - `report_start_session_failure()`

### Technical Impact

- `do_start_session()`이 transport 확인, session 생성, service wiring, 등록, 활성화라는 상위 orchestration 역할에 더 가깝게 정리됐다.
- 세션 생성 실패 경로와 정상 조립 경로가 분리되어 이후 `SessionAssembler` 또는 별도 builder로 이동시키기 쉬운 형태가 됐다.
- `Engine`의 구조 리팩토링이 "한 메서드 비대화" 수준을 넘어서 단계별 책임 경계로 전환되기 시작했다.

### Current Verification Status

- 이번 변경도 전체 빌드 실행 검증은 하지 못했다.
- 원인은 이전과 동일하게 외부 dependency fetch 단계의 네트워크 제한이다.

### Updated Next Step

- 다음 우선순위는 `Session`의 내부 책임을 분리하는 것이다.
- 구체적으로는 다음 순서가 적절하다.
  - outbound encode/encrypt 경로 추출
  - inbound fragment assemble/decrypt 경로 추출
  - handshake 전용 처리 객체 분리

## Progress Update 4

Date: 2026-04-23

### Completed After Progress Update 3

- `Session` 내부에서 송신 파이프라인 helper를 분리했다.
  - `build_message_payload()`
  - `queue_encoded_frame()`
  - `encrypt_and_queue_frame()`
- 수신 경로의 fragment 조립/완성 메시지 추출 helper를 분리했다.
  - `append_fragment_payload()`
  - `extract_complete_message()`
- 핸드셰이크 전용 분기와 결과 처리 helper를 분리했다.
  - `handle_handshake_message()`
  - `handle_handshake_step_result()`
  - `send_plaintext_control_message()`

### Technical Impact

- `send_message()`가 "payload 조립 + 암호화 여부 결정" 수준으로 축소됐다.
- `on_fragment()`는 fragment append, message extract, dispatch 단계가 분리되어 이후 별도 assembler 추출이 쉬워졌다.
- SSL handshake의 초기 step / peer input step이 같은 결과 처리 함수로 수렴되면서 중복 로직이 줄었다.
- `AUTH_COMPLETE` plaintext 전송 경로가 명시적 helper로 분리되어 control-plane 예외 처리가 선명해졌다.

### Current Verification Status

- 이번 변경도 외부 dependency fetch 문제 때문에 실제 빌드 실행 검증은 하지 못했다.
- 현재 상태는 구조 정리와 코드 경로 단순화까지 반영된 상태다.

### Updated Next Step

- 다음 리팩토링은 helper 수준 분리를 넘어서 실제 객체 추출 단계로 가는 것이 적절하다.
- 우선순위는 다음과 같다.
  - `InboundMessageAssembler` 추출
  - `OutboundMessageEncoder` 추출
  - `HandshakeCoordinator` 추출

## Progress Update 5

Date: 2026-04-23

### Completed After Progress Update 4

- `Session` 보조 객체를 실제 파일/클래스로 추출했다.
  - `OutboundMessageEncoder`
  - `InboundMessageAssembler`
  - `HandshakeCoordinator`
- `Session`은 위 세 객체를 조합하는 orchestration 역할로 축소했다.
- `core/CMakeLists.txt`에 새 session 구성 파일들을 추가했다.

### Files Added

- `core/include/aauto/session/OutboundMessageEncoder.hpp`
- `core/include/aauto/session/InboundMessageAssembler.hpp`
- `core/include/aauto/session/HandshakeCoordinator.hpp`
- `core/src/session/OutboundMessageEncoder.cpp`
- `core/src/session/InboundMessageAssembler.cpp`
- `core/src/session/HandshakeCoordinator.cpp`

### Technical Impact

- outbound encode/encrypt 로직이 `Session` 밖으로 이동했다.
- inbound fragment parse/decrypt/reassemble 로직이 `Session` 밖으로 이동했다.
- handshake step 결과 처리와 SSL encapsulation 송신이 별도 coordinator로 이동했다.
- `Session`은 이제 transport read/write queue, 상태 전이, 서비스 dispatch, lifecycle orchestration에 더 집중한다.

### Current Verification Status

- 외부 dependency fetch 제한 때문에 전체 빌드/테스트 실행 검증은 여전히 보류 상태다.
- 따라서 이번 단계도 구조 리팩토링 반영까지 완료된 상태로 기록한다.

### Updated Next Step

- 다음 단계에서는 `Session`에 남아 있는 상태 전이/타임아웃/handshake 상태 경계까지 더 분리할 수 있다.
- 우선순위는 다음과 같다.
  - handshake state transition 정책 정리
  - disconnect/error path 정리
  - service dispatch 정책 분리 여부 검토

## Progress Update 6

Date: 2026-04-23

### Completed After Progress Update 5

- `Session` 내부 상태 정책 helper를 추가해 전이 의미를 명시적으로 정리했다.
  - `is_handshake_state()`
  - `begin_disconnect()`
  - `close_transport_and_services()`
  - `complete_disconnect()`
- `stop()`에서 graceful disconnect 진입 로직을 직접 쓰지 않고 `begin_disconnect()`를 사용하도록 정리했다.
- handshake 상태 판정을 중복 비교식 대신 `is_handshake_state()`로 통일했다.
- error 종료 경로에서 transport close 및 service close 처리를 `close_transport_and_services()`로 묶었다.

### Technical Impact

- `Session`에서 상태 전이 정책이 분산된 분기보다 의미 있는 동작 단위로 보이기 시작했다.
- graceful disconnect, timeout disconnect, error termination의 경계가 이전보다 읽기 쉬워졌다.
- 이후 상태 전이 전용 객체를 도입할 때 옮겨야 할 책임이 더 선명해졌다.

### Current Verification Status

- 빌드/테스트 실행 검증은 여전히 외부 dependency fetch 제한으로 보류 상태다.

### Updated Next Step

- 다음 작업은 `Session` 상태 정책 자체를 별도 상태 전이 객체로 분리할지 판단하는 것이다.
- 동시에 Android 쪽 `AaService` 분리를 시작해도 병렬 가치가 있다.

## Progress Update 7

Date: 2026-04-23

### Completed After Progress Update 6

- Android `AaService`에서 wireless 상태 보관 책임을 `WirelessStateTracker`로 분리했다.
- hotspot SSID/password/IP/BSSID 해석 책임을 `HotspotConfigProvider`로 분리했다.
- `AaService`는 wireless 연결 상태 변경 시 직접 필드를 흩어지게 만지지 않고 helper를 통해 상태를 다루도록 정리했다.

### Files Added

- `app/android/src/com/aauto/app/WirelessStateTracker.java`
- `app/android/src/com/aauto/app/HotspotConfigProvider.java`

### Technical Impact

- `AaService`의 wireless 상태 필드 수가 줄었고 상태 접근 경로가 단순해졌다.
- hotspot/network interface 탐색 같은 플랫폼 의존 로직이 서비스 본체에서 빠졌다.
- 이후 `WirelessSessionCoordinator`를 도입할 때 `AaService`에서 바로 옮길 수 있는 책임 경계가 생겼다.

### Current Verification Status

- Android app 쪽도 별도 빌드 실행 검증은 아직 하지 못했다.
- 현재는 구조 리팩토링과 코드 경로 정리까지 반영된 상태다.

### Updated Next Step

- 다음 Android 단계는 `AaService`에서 wireless session 시작/종료 orchestration 자체를 `WirelessSessionCoordinator`로 빼는 것이다.
- 그 다음으로 engine 연결 재시도와 binder 등록 경로를 `EngineConnectionManager`로 분리하는 것이 적절하다.

## Progress Update 8

Date: 2026-04-23

### Completed After Progress Update 7

- `AaService`의 wireless session orchestration을 `WirelessSessionCoordinator`로 분리했다.
- coordinator가 다음 책임을 맡도록 정리했다.
  - Bluetooth/WiFi readiness 확인
  - `BluetoothWirelessManager` 생성/정지
  - wireless callback 처리
  - `WirelessStateTracker` 갱신
  - `BtProfileGate` block/restore 연동
- `AaService`는 coordinator callback을 받아 TCP session 시작/종료와 UI 후속 처리만 담당하도록 줄였다.

### Files Added

- `app/android/src/com/aauto/app/WirelessSessionCoordinator.java`

### Technical Impact

- `AaService`에서 wireless manager lifecycle 직접 제어 코드가 빠졌다.
- wireless 연결 이벤트와 BT profile 제어가 한곳에 모여 wireless 도메인 경계가 더 선명해졌다.
- 다음 단계에서 `EngineConnectionManager`를 추가해도 `AaService`가 다시 비대해질 가능성이 줄었다.

### Current Verification Status

- Android 빌드 실행 검증은 아직 하지 못했다.
- 현재는 구조 리팩토링과 책임 분리 반영 상태다.

### Updated Next Step

- 다음 Android 단계는 engine binder 연결/재시도/콜백 등록을 `EngineConnectionManager`로 분리하는 것이다.
- 이후에는 session cleanup과 playback lifecycle도 별도 controller로 정리할 수 있다.

## Progress Update 9

Date: 2026-04-23

### Completed After Progress Update 8

- `AaService`의 engine binder 연결/재시도/콜백 등록 책임을 `EngineConnectionManager`로 분리했다.
- `EngineConnectionManager`가 다음 역할을 담당하도록 정리했다.
  - `ServiceManager`를 통한 `aa-engine` 조회
  - 연결 재시도 카운트와 지연 관리
  - `IAAEngineCallback` 등록
  - shutdown 시 `stopAll()` 호출과 연결 해제
- `AaService`는 이제 engine 연결 결과를 callback으로 받아 `engineProxy`만 갱신한다.

### Files Added

- `app/android/src/com/aauto/app/EngineConnectionManager.java`

### Technical Impact

- `AaService`에서 binder 연결과 retry 정책이 제거되면서 서비스 본체가 transport/session orchestration 쪽에 더 집중하게 됐다.
- Android 쪽 큰 책임 축인 wireless orchestration과 engine connection orchestration이 각각 별도 helper로 분리됐다.
- 다음 단계에서 playback/session cleanup을 추가로 controller화하기 쉬운 구조가 됐다.

### Current Verification Status

- Android 빌드 실행 검증은 아직 하지 못했다.
- 현재는 구조 리팩토링과 책임 분리 반영 상태다.

### Updated Next Step

- 다음 Android 단계는 session cleanup과 video/audio lifecycle을 `PlaybackController` 또는 `SessionLifecycleController`로 분리하는 것이다.

## Progress Update 10

Date: 2026-04-23

### Completed After Progress Update 9

- `AaService`의 media/playback 책임을 `PlaybackController`로 분리했다.
- `PlaybackController`가 다음 역할을 담당하도록 정리했다.
  - session config 기반 video size 관리
  - `VideoDecoder` lifecycle 및 surface attach/detach
  - `AudioPlayer` feed/release
  - session cleanup 시 media release
- `AaService`는 media callback을 직접 처리하지 않고 controller에 위임하도록 정리했다.

### Files Added

- `app/android/src/com/aauto/app/PlaybackController.java`

### Technical Impact

- Android 쪽에서 큰 책임 축 3개가 분리되기 시작했다.
  - wireless orchestration
  - engine connection
  - playback lifecycle
- `AaService`는 이전보다 서비스 본체라기보다 coordinator aggregator에 가까운 형태가 됐다.
- 이후 `SessionLifecycleController`까지 추가하면 서비스 본체를 더 얇게 만들 수 있다.

### Current Verification Status

- Android 빌드 실행 검증은 아직 하지 못했다.
- 현재는 구조 리팩토링과 책임 분리 반영 상태다.

### Updated Next Step

- 다음 단계는 `currentSessionId`, session start/stop, cleanup/broadcast 흐름을 `SessionLifecycleController`로 분리하는 것이다.

## Progress Update 11

Date: 2026-04-23

### Completed After Progress Update 10

- `AaService`의 세션 수명주기 책임을 `SessionLifecycleController`로 분리했다.
- controller가 다음 역할을 맡도록 정리했다.
  - USB/TCP 세션 시작
  - active session id 관리
  - stopSession 호출
  - session cleanup
  - `ACTION_SESSION_ENDED` broadcast
- `AaService`는 이제 세션 lifecycle callback과 UI 후속 처리 위주로 남게 됐다.

### Files Added

- `app/android/src/com/aauto/app/SessionLifecycleController.java`

### Technical Impact

- Android 쪽 큰 책임 축이 대부분 helper/controller로 분리됐다.
  - wireless orchestration
  - engine connection
  - playback lifecycle
  - session lifecycle
- `AaService`는 이전보다 훨씬 얇아졌고, 사실상 상위 통합 coordinator에 가까운 구조가 됐다.
- 이후 남는 작업은 클래스 분리보다 검증과 naming/polish 성격이 강하다.

### Current Verification Status

- Android 빌드 실행 검증은 아직 하지 못했다.
- 현재는 구조 리팩토링과 책임 분리 반영 상태다.

### Updated Next Step

- 다음 단계는 남은 중복 로그/에러 메시지/명명 정리와 함께, 가능하면 Android/JVM 단위 테스트 가능한 순수 helper부터 테스트를 붙이는 것이다.

## Progress Update 12

Date: 2026-04-23

### Completed After Progress Update 11

- `AaService`의 화면 전환 책임을 `UiNavigationController`로 분리했다.
- 다음 흐름이 서비스 본체 밖으로 이동했다.
  - DeviceList 화면 표시
  - DisplayActivity 포함 display flow 시작

### Files Added

- `app/android/src/com/aauto/app/UiNavigationController.java`

### Technical Impact

- `AaService`는 이제 UI navigation까지 직접 다루지 않게 됐다.
- Android 앱 계층에서 남아 있던 주요 책임 대부분이 helper/controller로 이동했다.
- 현재 구조는 추가 대규모 분리보다 검증, 정리, naming polish가 더 중요한 단계에 도달했다.

### Current Verification Status

- Android 빌드 실행 검증은 아직 하지 못했다.
- 현재는 구조 리팩토링과 책임 분리 반영 상태다.

### Suggested Wrap-up

- 다음은 새로 분리한 helper/controller들에 대한 검증 전략을 문서화하거나, 가능한 범위의 테스트를 붙이는 것이 적절하다.

## Progress Update 13

Date: 2026-04-23

### Completed After Progress Update 12

- `Session`에서 분리한 core helper 객체들에 대한 단위 테스트 초안을 추가했다.
- 대상은 `OutboundMessageEncoder`, `InboundMessageAssembler`, `HandshakeCoordinator`다.
- `tests/CMakeLists.txt`에 새 테스트 파일을 연결했다.

### Files Added Or Changed

- `tests/core/outbound_message_encoder_test.cpp`
- `tests/core/inbound_message_assembler_test.cpp`
- `tests/core/handshake_coordinator_test.cpp`
- `tests/CMakeLists.txt`
- `docs/refactoring_assessment.md`

### Test Coverage Added

- `OutboundMessageEncoder`
  - plaintext control frame 인코딩
  - handshake 완료 후 encrypted frame 전송
  - plaintext control bypass 유지
  - encrypt 실패 시 error callback 전파
- `InboundMessageAssembler`
  - plaintext message decode
  - encrypted fragment 재조립
  - short payload drop
  - decrypt 실패 시 `DecryptionFailed` 전파
- `HandshakeCoordinator`
  - version refusal 시 `VersionMismatch`
  - initial SSL output encapsulation 전송
  - SSL 완료 시 completion callback 호출
  - handshake step 실패 시 `SslHandshakeFailed`

### Verification Status

- 전체 build/test 실행은 아직 하지 못했다.
- 기존과 동일하게 configure 단계에서 외부 dependency fetch가 막히는 환경 제약이 남아 있다.
- 현재는 테스트 소스와 타깃 연결, 그리고 helper API 기준의 정적 정합성 보강까지 반영한 상태다.

## Progress Update 14

Date: 2026-04-23

### Completed After Progress Update 13

- 최초 `cmake` configure를 실제로 통과시켜 빌드 가능한 상태를 확인했다.
- `aauto_core_test` 빌드 시 새 helper 테스트가 아니라 기존 빌드 설정/호환성 문제에서 실패하는 것을 확인했다.
- protobuf 직렬화 helper의 deprecated `ByteSize()` 호출을 `ByteSizeLong()`으로 교체했다.
- clang 최신 경고가 외부 헤더에서 `-Werror`로 승격되어 실패하던 항목을 core 타깃에서 예외 처리했다.

### Files Added Or Changed

- `core/CMakeLists.txt`
- `core/src/session/Session.cpp`
- `core/src/service/AudioService.cpp`
- `core/src/service/InputService.cpp`
- `core/src/service/VideoService.cpp`
- `core/src/service/SensorService.cpp`
- `core/src/service/ControlService.cpp`
- `docs/refactoring_assessment.md`

### Verification Findings

- configure 단계는 이제 성공했다.
- 첫 빌드 실패 원인은 새 테스트 코드가 아니라 다음 두 가지였다.
  - 외부 헤더(`asio`, `abseil`)의 clang 호환 경고가 `-Werror`로 승격됨
  - 기존 protobuf 직렬화 helper의 `ByteSize()` deprecated 사용

### Current Next Step

- 수정된 빌드 설정으로 `aauto_core_test`를 다시 빌드하고, 통과하면 실제 테스트 실행까지 이어간다.

## Progress Update 15

Date: 2026-04-23

### Completed After Progress Update 14

- `aauto_core_test` 빌드와 실행 검증을 실제로 완료했다.
- core helper 테스트 추가 이후 드러난 기존 빌드/테스트 문제를 함께 정리했다.
- 최종적으로 core 테스트 42개 전부 통과를 확인했다.

### Files Added Or Changed

- `core/CMakeLists.txt`
- `core/include/aauto/engine/Engine.hpp`
- `core/include/aauto/service/ServiceBase.hpp`
- `core/src/engine/Engine.cpp`
- `core/src/session/Framer.cpp`
- `core/src/session/Session.cpp`
- `core/src/service/AudioService.cpp`
- `core/src/service/ControlService.cpp`
- `core/src/service/InputService.cpp`
- `core/src/service/SensorService.cpp`
- `core/src/service/VideoService.cpp`
- `tests/core/control_service_test.cpp`
- `tests/core/handshake_coordinator_test.cpp`
- `tests/core/inbound_message_assembler_test.cpp`
- `tests/core/session_handshake_test.cpp`
- `tests/core/mock/MockTransport.hpp`
- `docs/refactoring_assessment.md`

### Build And Test Fixes Applied

- protobuf 직렬화 helper의 deprecated `ByteSize()` 호출을 `ByteSizeLong()`으로 교체했다.
- clang 최신 경고가 외부 헤더에서 `-Werror`로 승격되던 문제를 core 타깃에서 완화했다.
- `Engine` 헤더의 `ControlService` 전방 선언 누락을 보완했다.
- `ServiceBase.hpp`가 로컬 `LOG_TAG` 매크로에 의존하던 문제를 제거해 헤더 자급성을 확보했다.
- `Framer` decode가 자체 encode 형식과 어긋나던 조각 처리 불일치를 바로잡았다.
- `MockTransport`가 pending read가 없을 때 read error 주입을 잃어버리던 문제를 고쳤다.
- `Session`의 no-control-service disconnect fallback을 보강했다.
- `Engine::run()` / `shutdown()`의 스레드 정리 경합을 줄여 테스트 중 abort를 제거했다.
- session 테스트 harness의 `io_context` 재시작 누락을 보완했다.

### Verification Result

- 실행 명령:
  - `cmake --build build --target aauto_core_test -j4`
  - `ctest --test-dir build/tests --output-on-failure`
- 결과:
  - `100% tests passed, 0 tests failed out of 42`

### Current Status

- core 리팩토링 결과는 현재 자동화 테스트 기준으로 검증된 상태다.
- 다음 우선순위는 Android helper/controller 계층에 대한 테스트 추가 또는 Android 빌드 검증이다.
