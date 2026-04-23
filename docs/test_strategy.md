# Test Strategy

Date: 2026-04-23

## Goal

리팩토링으로 분리된 helper/controller들이 동작을 유지하는지 검증할 수 있는 최소한의 자동화 전략을 정의한다. 현재 저장소는 네트워크 제한 때문에 전체 CMake/Android 빌드를 즉시 실행 검증하기 어려우므로, 우선 "순수 로직"과 "의존성 경계"가 선명한 부분부터 테스트 가능 구조를 활용하는 방식으로 접근한다.

## Current Constraint

- C++ 쪽은 `FetchContent` 기반 외부 dependency 다운로드가 막혀 전체 configure/build 실행이 아직 불가능하다.
- Android 쪽도 현재 세션에서는 실제 앱 빌드/실행 검증이 수행되지 않았다.
- 따라서 테스트 전략은 "지금 바로 붙일 수 있는 테스트"와 "환경이 열리면 바로 실행할 테스트"로 나눠서 본다.

## Test Layers

### 1. Core unit tests

대상:
- `OutboundMessageEncoder`
- `InboundMessageAssembler`
- `HandshakeCoordinator`
- `ControlService`
- `Engine` helper 조립 경로

목표:
- 프로토콜 처리와 상태 전이를 가장 먼저 회귀 방지한다.

추천 방식:
- 기존 `tests/core` 구조와 mock transport/mock crypto를 계속 활용
- 새로 분리한 session helper는 가능한 한 독립 단위 테스트 추가

우선 추가 대상:
- `OutboundMessageEncoder`
  - plaintext frame 인코딩
  - encrypt 경로 호출 여부
  - `send_plaintext_control_message()`가 비암호화 control frame을 생성하는지
- `InboundMessageAssembler`
  - 단일 프레임 메시지 조립
  - fragment 연속 입력 시 완성 메시지 조립
  - 암호화 fragment 복호화 후 payload 재조립
  - 짧은 payload drop 처리
- `HandshakeCoordinator`
  - version refused 시 `VersionMismatch` error 전달
  - SSL step output이 있으면 `EncapsulatedSsl` 송신 요청
  - handshake complete 시 completion callback 호출

### 2. Android pure helper tests

대상:
- `WirelessStateTracker`
- `HotspotConfigProvider`의 일부 보조 로직
- `SessionLifecycleController`
- `PlaybackController`
- `EngineConnectionManager`
- `UiNavigationController`

목표:
- Android framework heavy class인 `AaService` 대신, 분리된 helper를 중심으로 JVM/unit test 가능한 구조를 넓힌다.

추천 방식:
- Android dependency가 적은 helper부터 plain JVM 또는 Robolectric 기반 테스트
- framework 직접 의존이 큰 경우 interface wrapper 또는 fake callback으로 검증

우선 추가 대상:
- `WirelessStateTracker`
  - connecting -> ready -> clear 상태 전이
- `SessionLifecycleController`
  - active session id 갱신
  - cleanup 시 broadcast 및 playback clear callback 경로
  - stopActiveSession() 호출 조건
- `EngineConnectionManager`
  - binder null일 때 retry scheduling
  - registerCallback 성공 시 connected callback
  - shutdown 시 stopAll 호출
- `UiNavigationController`
  - `DeviceListActivity` / display flow intent stack 생성 확인

### 3. Characterization tests for AaService

대상:
- `AaService`

목표:
- 현재 서비스가 조합한 여러 controller 간 wiring이 깨지지 않는지 확인

추천 방식:
- 이 단계는 Robolectric 또는 instrumentation test가 더 적합
- 실제 테스트는 helper/controller 테스트가 먼저 확보된 뒤 최소 개수만 추가

필수 시나리오:
- USB device ready -> session start -> pending surface attach
- wireless ready -> TCP session start -> display flow 시작
- session error -> cleanup -> session ended broadcast

## Priority

### P1

- `OutboundMessageEncoder` unit test
- `InboundMessageAssembler` unit test
- `HandshakeCoordinator` unit test
- `WirelessStateTracker` unit test

### P2

- `SessionLifecycleController` unit test
- `EngineConnectionManager` unit test
- `UiNavigationController` test

### P3

- `AaService` characterization test
- Android integration/instrumentation test

## Suggested File Additions

### Core

- `tests/core/outbound_message_encoder_test.cpp`
- `tests/core/inbound_message_assembler_test.cpp`
- `tests/core/handshake_coordinator_test.cpp`

### Android

- `app/android/test/com/aauto/app/WirelessStateTrackerTest.java`
- `app/android/test/com/aauto/app/SessionLifecycleControllerTest.java`
- `app/android/test/com/aauto/app/EngineConnectionManagerTest.java`
- `app/android/test/com/aauto/app/UiNavigationControllerTest.java`

## Execution Plan

1. C++ helper unit test부터 추가한다.
2. Android pure helper test를 붙인다.
3. build 환경이 열리면 기존 `tests/core`와 새 helper test를 먼저 실행한다.
4. 마지막에 `AaService` 수준 characterization test를 최소 개수로 추가한다.

## Exit Criteria

- Core session helper 3종에 회귀 테스트가 존재한다.
- Android helper/controller 핵심 3종 이상에 테스트가 존재한다.
- session start/stop/error cleanup 주요 시나리오가 최소 한 번씩 자동화된다.
