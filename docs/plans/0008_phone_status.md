# 0008 — PhoneStatus channel (통화 시그널링)

> Created: 2026-04-29
> Status: Day 1 PASSED (2026-04-30) — ch9 살아있음 확인. PHONE_STATUS
> 메시지 idle 상태에서 calls=0/signal=0 송신 관찰. 통화 트리거 시
> 추가 메시지 흐름 검증은 Day 2 (UI 배너 + AIDL chain)에서 진행.
> Related decisions: G.0 (passive handler rule), F.20 (KEYCODE-only
> 미디어 제어와 같은 패턴 — non-AAP 라우팅), G.1 (MediaBrowser
> deprecation 패턴 — 본 plan에서도 같은 운명일 위험)

## Goal

폰의 통화 상태(수신/발신/통화중/대기 등)를 HU에서 *시그널링 레벨*에서
관찰. 학습 가치:

- AAP의 핵심 차량 기능 중 하나(통화) wire 동작 학습
- PhoneStatus 메시지 구조 + 송신 트리거(통화 시점 vs idle 등) 관찰
- modern Android Auto가 ch9도 ch12처럼 deprecated인지 검증
- Bluetooth HFP(통화 오디오)와 AAP의 명확한 책임 분리 학습 (proto 주석에
  "HFP handles call audio routing separately"로 명시됨)

이번 plan은 **시그널링까지만 다룸** — 통화 오디오는 BT HFP 별도 스택,
본 학습 범위 밖.

## Scope (스코프 명확화)

### IN
- ch9 PhoneStatusService passive handler 구현 (parse + log + callback)
- main.cpp services[9] 등록
- 폰의 통화 (수신/발신/끊기) 시점에 PHONE_STATUS 메시지 흐름 관찰
- HU UI에서 incoming-call 배너 표시 (선택, Day 2)
- D-pad 수락/거절 송신 (선택, Day 3)

### OUT
- **통화 오디오 라우팅** — BT HFP 별도 스택, AAP 무관. proto 주석 명시.
  TCC803x BT 칩 + Bluedroid HFP 통합은 별도 단위 프로젝트.
- HU 마이크/스피커 통화 통합 — 같은 이유.
- 연락처 동기화 (별도 PhoneBook 채널, 우리 proto에 없음).
- 발신(다이얼) UI — incoming만 우선 다룸.

## Current state (2026-04-29)

| 자산 | 상태 |
|------|------|
| `protobuf/aap_protobuf/service/phonestatus/` | ✅ 모두 import (`PhoneStatusMessageId`, `PhoneStatus`, `PhoneStatusInput`, `PhoneStatusService`) |
| `core/include/aauto/service/PhoneStatusService.hpp` | (확인 필요) |
| `core/src/service/PhoneStatusService.cpp` | (확인 필요 — G.0 stub일 가능성) |
| main.cpp 채널 9 등록 | ❌ 미등록 |
| AaService onPhoneStatus 콜백 | 없음 |
| HU UI 통화 배너 | 없음 |
| BT HFP 통합 | ❌ 본 plan 범위 밖 |

## Protocol (proto 분석)

### Inbound (Phone → HU)

**`PHONE_STATUS` (32769)**:
```proto
PhoneStatus {
    repeated Call calls = 1;          // 동시 통화 (3-way 등) 가능
    Call {
        State phone_state = 1;        // INCOMING/IN_CALL/ON_HOLD/INACTIVE/MUTED/...
        uint32 call_duration_seconds; // 통화 경과시간
        string caller_number;         // 전화번호 (+82-10-... 등)
        string caller_id;             // 연락처 이름
        string caller_number_type;    // mobile/work/home
        bytes caller_thumbnail;       // 연락처 사진
    }
    optional uint32 signal_strength;  // 0~4 bar (status bar 표시용)
}
```

State enum:
- `UNKNOWN = 0`
- `IN_CALL = 1` — 통화중
- `ON_HOLD = 2` — 보류
- `INACTIVE = 3` — 통화 종료
- `INCOMING = 4` — 수신중
- `CONFERENCED = 5` — 컨퍼런스 콜
- `MUTED = 6` — 음소거

### Outbound (HU → Phone)

**`PHONE_STATUS_INPUT` (32770)**:
```proto
PhoneStatusInput {
    InstrumentClusterInput input = 1;  // ENTER(수락)/BACK(거절)/UP/DOWN(컨택트 navigate)
    optional string caller_number;     // 통화 식별
    optional string caller_id;
}
```

ENTER on incoming = 수락, BACK on incoming = 거절. milek7 reverse
engineering의 InstrumentClusterInput 패턴 그대로 (PLAYBACK_INPUT과 동일
generic input).

## Risks

1. **MediaBrowser와 같은 deprecation 운명 가능성** ★ (가장 큰 리스크)
   - Modern Android Auto가 통화 UI도 polled 렌더링한다면 (Spotify처럼
     androidx.car.app으로) ch9도 phone-side가 무시할 수 있음
   - 검증 방법: passive handler 구현 + 등록 후 실 통화 트리거 → 메시지
     흐름 관찰. 안 흐르면 ch12와 같은 deprecation으로 결론.

2. **G.0 throttle**: PhoneStatusService를 advertise만 하고 핸들러
   미등록이면 phone-side throttle 가능 (우리 #22 사례). passive
   handler 등록 필수.

3. **HU identity 게이팅** (가능성 낮음): 모든 다른 advertise 채널
   정상 open되는 상황이라 channel-specific 거부면 deprecation일
   가능성이 더 큼.

4. **Active call이 없으면 메시지 안 흐를 가능성**: PhoneStatus는
   *event-driven*. Idle 상태에선 한두 번 송신 후 quiet하게 머무름.
   테스트는 반드시 실 통화 트리거(수신 또는 발신) 필요.

5. **caller_thumbnail 데이터량**: 폰의 contacts 사진 포함 시 큰 byte
   전송. AAP proto는 chunked 분할 지원하므로 transport 부담 자체는 없
   지만 logcat에서 raw dump는 truncate.

## Day-by-Day breakdown

### Day 0 — 사전 검증 (15min)

- 현재 `core/include/aauto/service/PhoneStatusService.hpp` /
  `.cpp` 상태 확인 (있는지, 형태)
- `protobuf` 트리에 모든 .proto 정상 있는지 확인
- Day 1 진행 전제 조건: 위 두 가지 OK

### Day 1 — passive handler + 등록 (3~4h)

- `core/include/aauto/service/PhoneStatusService.hpp`:
  - 콜백 typedef (CallStateCallback, SignalStrengthCallback)
  - `set_*_callback` setter
- `core/src/service/PhoneStatusService.cpp`:
  - PHONE_STATUS handler (32769) — parse + log + invoke callback
  - signal_strength callback
  - 통화 array 핸들러 (각 Call.state, caller_number, caller_id 로깅)
  - logcat 친화적 포맷 (PLAYBACK_STATUS 패턴 그대로)
- `impl/android/main/main.cpp`: `services[9] = ...PhoneStatusService(send_fn)`
- 빌드 + 실기 테스트:
  1. AA 연결 (USB 또는 wireless)
  2. 폰으로 *수신* 통화 시도 (다른 폰에서 걸기 또는 본인 휴대폰으로
     걸기)
  3. logcat 관찰:
     - `PHONE_STATUS state=INCOMING caller_number=... caller_id=...`
     - 응답 안 받고 끊으면 `state=INACTIVE`
     - 통화 받으면 `state=IN_CALL` + duration_seconds 카운트

**Day 1 종료 기준**:
- ✅ PHONE_STATUS 메시지 흐름 관찰 → ch9 살아있음, Day 2 진행
- ❌ 메시지 0개 (ch12와 같은 패턴) → DEPRECATED 결론, plan ON_HOLD,
  Day 1 자체로 학습 산출물 commit

### Day 2 — UI display (선택, 4~5h)

- AIDL `IAAEngineCallback.onPhoneStatus(int sessionId, int state,
  String callerNumber, String callerId, byte[] thumbnail, int duration)`
- AaService `PhoneCallInfo` 캐시
- DeviceListActivity overlay 또는 신규 `IncomingCallActivity`:
  - INCOMING 상태일 때 풀스크린 배너 (caller_id + thumbnail)
  - IN_CALL 상태일 때 작은 status bar 표시 (caller_id + duration)
  - INACTIVE 상태일 때 배너 dismiss

### Day 3 — D-pad input (선택, 2~3h)

- `PhoneStatusService::send_input(action, caller_number)` outbound API
- `IAAEngine.acceptCall()` / `rejectCall()` AIDL — 내부적으로 ENTER /
  BACK 송신
- HU UI에 수락/거절 버튼 — 누르면 AIDL 호출

### Day 4 (별도 프로젝트) — BT HFP 통화 오디오 통합

- 본 학습 plan 범위 밖. AAP 무관, Bluedroid 통합 작업.
- 별도 plan 파일로 다룸 (필요시).

## Files to touch (Day 1만)

| 파일 | 변경 |
|------|------|
| `core/include/aauto/service/PhoneStatusService.hpp` | 콜백 타입 + setter (없으면 신규) |
| `core/src/service/PhoneStatusService.cpp` | PHONE_STATUS handler 구현 |
| `impl/android/main/main.cpp` | `services[9]` 등록 + 콜백 lambda (Day 2 전엔 nullptr OK) |

Day 2 추가 시:
- `aidl/com/aauto/engine/IAAEngineCallback.aidl` (onPhoneStatus 추가)
- `core/include/aauto/engine/IEngineController.hpp` (`on_phone_status` 추가)
- `impl/android/aidl/AidlEngineController.{hpp,cpp}`
- `app/android/src/com/aauto/app/AaService.java`
- `app/android/src/com/aauto/app/IncomingCallActivity.java` (or overlay)
- AndroidManifest.xml versionName bump

## Verification (Day 1)

- 실기기 SM-N981N USB 연결 → 다른 폰에서 본인 폰으로 전화 걸기
- 기대 logcat:
  ```
  [AAP RX] phone.status      CHANNEL_OPEN_REQ
  [AAP TX] phone.status      CHANNEL_OPEN_RESP
  [AAP RX] phone.status      PHONE_STATUS
  AA.PhoneStatusService: phone.status PHONE_STATUS
                         calls=1 state=INCOMING caller="+82-..."
                         caller_id="..." duration=0s thumbnail=NB
  ```
- 통화 수락 시:
  ```
  AA.PhoneStatusService: phone.status PHONE_STATUS
                         calls=1 state=IN_CALL ... duration=Ns
  ```
- 끊기 시:
  ```
  AA.PhoneStatusService: phone.status PHONE_STATUS
                         calls=0 (or state=INACTIVE)
  ```

logcat에서 위 패턴 관찰되면 Day 1 PASS.

## 작업량 합계

| Day | 작업 | 추정 |
|-----|------|------|
| 0 | 사전 검증 | 15min |
| 1 | core handler + 실기 검증 | 3~4h |
| 2 (선택) | AIDL chain + UI 배너 | 4~5h |
| 3 (선택) | D-pad input | 2~3h |
| **총 (Day 1만)** | | **~4h** |
| **총 (Day 1~3)** | | **~10h** |

각 Day별 별도 commit 권장. Day 1만으로도 학습 산출 1차 회수 (PHONE_STATUS
wire format 직접 관찰 vs 생기지 않으면 deprecation 확정).

## 미래 trigger

- Day 1 결과가 deprecation이면 plan ON_HOLD하고 다른 학습 영역 이동
- BT HFP 통합이 학습 대상이 되면 별도 plan 0009 또는 0010
- 발신(다이얼) UI / 연락처 동기화는 별도 plan
