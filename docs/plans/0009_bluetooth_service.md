# 0009 — AAP BluetoothService channel (ch 13)

> Created: 2026-04-29
> Status: Day 1 PASSED (2026-04-30) — ch13 살아있음 확인.
> CHANNEL_OPEN_REQ 정상 + PAIRING_REQUEST 즉시 송신 관찰
> (phone="A8:2B:B9:95:E9:2B" method=PIN). MediaBrowser ch12와 달리
> 본 채널은 modern AA에서도 active. Day 2 (Bluedroid bridge —
> auto-pair on request) 진행 가치 확인.
> Related decisions: G.0 (passive handler rule), G.1 / G.2 (deprecation
> 패턴 위험 — 본 plan에도 적용 가능), 0008 (PhoneStatus — 본 plan 다음
> 단계로 reorder됨, 사용자 결정)

## Goal

폰↔HU의 Bluetooth 페어링을 **AAP 시그널링으로 협상**하는 채널 구현. 실
HFP 음성 라우팅과는 별개 (그건 Bluedroid + audio HAL 영역).

학습 가치:

- AAP의 BT 페어링 자동화 메커니즘 wire-level 관찰 (PairingRequest/Response,
  AuthData/Result)
- AAP가 어떻게 Bluedroid와 책임 분리되어 있는지 (시그널링 vs 음성)
- modern Android Auto가 ch13도 deprecated 처리했는지 검증 (ch12 같은
  운명일 위험)
- AAP-driven 페어링 vs 사용자 manual 페어링 두 경로 비교

이번 plan은 **시그널링까지만** — 통화 음성은 BT HFP 별도 스택, 본 학습
범위 밖 (Day 4 별도 단위 프로젝트로 명시).

## Scope

### IN
- ch13 BluetoothService passive handler (4개 메시지)
- main.cpp services[13] 등록
- HeadunitConfig.bluetooth_mac 사용 (현재 placeholder)
- supported_pairing_methods 광고
- 폰의 PairingRequest 수신 시 응답 (STATUS_SUCCESS 또는 BLUETOOTH_UNAVAILABLE)
- AuthData 수신 시 결과 응답

### OUT
- **실제 Bluedroid 페어링 호출** — Day 4 별도. 응답은 stub("이미 페어링됨"
  또는 "사용 불가")로만.
- HFP-AG profile 통합 — 별도. Android system이 manual pair 후 자동
  routing할 수도 있음(C 시나리오), 본 plan과 무관.
- HU UI에서 페어링 confirmation (PIN/passkey 표시) — Day 2 (선택)

## Current state (2026-04-29)

| 자산 | 상태 |
|------|------|
| `protobuf/aap_protobuf/service/bluetooth/` | ✅ 모두 import |
| `core/include/aauto/service/BluetoothService.hpp` | (확인 필요, G.2 stub일 가능성) |
| `core/src/service/BluetoothService.cpp` | (확인 필요) |
| HeadunitConfig.bluetooth_mac | placeholder "02:00:00:00:00:00" |
| main.cpp services[13] 등록 | ❌ 미등록 (G.2 ON_HOLD) |

## Protocol (proto 분석)

### SDR config

```proto
BluetoothService {
    required string car_address = 1;                            // HU의 BT MAC
    repeated BluetoothPairingMethod supported_pairing_methods = 2;
}

enum BluetoothPairingMethod {
    BLUETOOTH_PAIRING_UNAVAILABLE = -1;
    BLUETOOTH_PAIRING_OOB = 1;
    BLUETOOTH_PAIRING_NUMERIC_COMPARISON = 2;
    BLUETOOTH_PAIRING_PASSKEY_ENTRY = 3;
    BLUETOOTH_PAIRING_PIN = 4;
}
```

### Inbound (Phone → HU)

**`BLUETOOTH_MESSAGE_PAIRING_REQUEST` (32769)**:
```proto
BluetoothPairingRequest {
    required string phone_address = 1;            // 폰 BT MAC
    required BluetoothPairingMethod pairing_method = 2;
}
```

**`BLUETOOTH_MESSAGE_AUTHENTICATION_DATA` (32771)**:
```proto
BluetoothAuthenticationData {
    required string auth_data = 1;                // PIN code, passkey, etc.
    optional BluetoothPairingMethod pairing_method = 2;
}
```

### Outbound (HU → Phone)

**`BLUETOOTH_MESSAGE_PAIRING_RESPONSE` (32770)**:
```proto
BluetoothPairingResponse {
    required MessageStatus status = 1;            // SUCCESS / BLUETOOTH_UNAVAILABLE / ...
    required bool already_paired = 2;
}
```

**`BLUETOOTH_MESSAGE_AUTHENTICATION_RESULT` (32772)**:
```proto
BluetoothAuthenticationResult {
    required MessageStatus status = 1;
}
```

### 페어링 흐름 (proto 주석 기반 추정)

```
HU → SDR { ..., bluetooth_service { car_address, supported_methods } }
Phone receives, decides to initiate pairing
Phone → HU: PAIRING_REQUEST { phone_address, method=PIN }
HU triggers Bluedroid pairing (or claims already_paired)
HU → Phone: PAIRING_RESPONSE { status=SUCCESS, already_paired=true/false }

[If pairing requires auth data]:
Phone → HU: AUTHENTICATION_DATA { auth_data="1234", method=PIN }
HU validates / passes to Bluedroid
HU → Phone: AUTHENTICATION_RESULT { status=SUCCESS }

[After pairing complete]:
HFP-AG (별도 stack) automatically activates → call audio routing
PhoneStatusService (ch9) starts sending status — plan 0008 영역
```

## Risks

1. **MediaBrowser 같은 deprecation** ★ — modern Android Auto는 manual
   페어링을 사용자에게 요구할 가능성. AAP가 페어링 자동화를 안 쓰면
   ch13도 폰이 안 엶. **검증 방법**: passive handler 등록 후 폰 연결
   → CHANNEL_OPEN_REQ 오는지 logcat 확인.

2. **응답 stubbing 어려움**: Bluedroid 통합 없이 PAIRING_REQUEST에
   "STATUS_SUCCESS, already_paired=true"라 응답하면 폰은 페어링 됐다고
   믿지만 실제 Bluedroid에는 페어링 없음. HFP 시도 시 폰은 BT 못 찾고
   실패. Day 1은 응답을 안전한 default로 — `BLUETOOTH_UNAVAILABLE` 또는
   `already_paired=true` (이미 manual pair됨 가정).

3. **car_address가 잘못된 placeholder인 경우**: 현재 "02:00:00:00:00:00"는
   real BT MAC 아님. 폰이 SDR validate하다가 거부할 수 있음. HU 시스템에서
   실제 BT MAC 가져오는 path 추가 필요 (Day 1 범위 안).

4. **HU manual 페어링과의 상호작용**: 사용자가 폰 Settings에서 미리 BT
   페어링한 상태라면 phone이 already_paired=true 응답 기대. AAP는
   페어링 *재확인* 용도일 수도.

## Day-by-Day breakdown

### Day 0 — 사전 검증 (15min)

- 현재 BluetoothService.{hpp,cpp} 확인 (G.2 stub 형태 검증)
- HU의 실제 BT MAC 얻는 방법 확인 (`adb shell settings get secure bluetooth_address`
  또는 `BluetoothAdapter.getDefaultAdapter().getAddress()`)

### Day 1 — passive handler + 등록 (4~5h)

- `core/include/aauto/service/BluetoothService.hpp`:
  - PairingRequestCallback / AuthDataCallback typedef
  - set_*_callback setter
  - send_pairing_response(status, already_paired) outbound API
  - send_auth_result(status) outbound API
- `core/src/service/BluetoothService.cpp`:
  - PAIRING_REQUEST handler (parse + log + callback + 기본 응답
    SUCCESS+already_paired=true 또는 BLUETOOTH_UNAVAILABLE — 안전 선택)
  - AUTH_DATA handler (parse + log + callback)
  - fill_config: car_address + supported_pairing_methods 광고
- `core/include/aauto/engine/HeadunitConfig.hpp`: bluetooth_mac 필드
  현재 있음 확인. supported_pairing_methods 추가 필드 (default = JUST_WORKS만)
- `impl/android/main/main.cpp`:
  - HU의 실제 BT MAC을 system에서 읽어 hu_config.bluetooth_mac 설정 (가능시)
  - `services[13]` 등록 (callback nullptr — Day 1)

검증 시나리오:
1. 빌드 + 실기 USB 연결
2. logcat 관찰:
   - `[AAP TX] control SERVICE_DISCOVERY_RESP NNN bytes` — 사이즈 약간 증가
   - **case A**: `[AAP RX] bluetooth CHANNEL_OPEN_REQ` 옴 → ch13 살아있음
   - **case B**: ch13 CHANNEL_OPEN_REQ 안 옴 → ch12 같은 deprecation
3. case A 시나리오 시 추가:
   - 폰을 미리 manual pair 한 경우: `PAIRING_REQUEST already_paired=true`
     응답 후 폰이 어떻게 행동하는지
   - Manual pair 안 된 경우: 폰이 PIN/Passkey 요청하는지

**Day 1 종료 기준**:
- case A → Day 2 진행 가치 있음
- case B → DEPRECATED 결론, plan ON_HOLD, 학습 산출 commit + plan 0008로 이동

### Day 2 — Bluedroid bridge (auto-pair-on-request, 5~7h)

User 질문(2026-04-29)으로 Day 2 정수 명확화: **PAIRING_REQUEST를 받으면
BT 라디오를 자동 켜고 실제 페어링까지 진행**. 이게 ch13의 진짜 가치.

흐름:
```
Native: PAIRING_REQUEST 수신
  → callback (phone_address, method)
  → AIDL onPairingRequest(int sessionId, String phoneAddr, int method)
    ↓
Java AaService.onPairingRequest():
  if (!btAdapter.isEnabled()) btAdapter.enable()         // BT 자동 ON
  device = btAdapter.getRemoteDevice(phoneAddr)
  if (device.bondState == BOND_BONDED) {
    // 이미 페어링됨 — 바로 응답
    aaEngine.completePairing(sessionId, SUCCESS, /*already_paired=*/true)
    return
  }
  device.createBond()                                    // 실제 페어링 trigger
  // BroadcastReceiver(ACTION_BOND_STATE_CHANGED) 대기
  on bond_state == BOND_BONDED:
    aaEngine.completePairing(sessionId, SUCCESS, false)
  on bond_state == BOND_NONE (실패):
    aaEngine.completePairing(sessionId, BLUETOOTH_PAIRING_FAILED, false)
    ↓
Native: send_pairing_response(status, already_paired)
```

또한 method가 PIN_ENTRY / PASSKEY인 경우 추가 작업:
- Phone이 AUTHENTICATION_DATA로 PIN/passkey 송신
- HU가 받아서 BluetoothDevice.setPin() 또는 setPasskey() 호출
- Bluedroid가 이를 사용해 페어링 완료
- 결과 다시 AUTH_RESULT로 응답

UI 부수 효과:
- BT 토글 버튼 자동 ON으로 갱신 (BluetoothAdapter.STATE_CHANGED broadcast로 이미 처리되고 있을 듯)
- "Pairing with phone..." progress UI (선택)
- AUTH_DATA의 PIN을 HU에 표시 (PIN_ENTRY method일 때 사용자 확인용)

수정 파일 (Day 2):
- `aidl/com/aauto/engine/IAAEngineCallback.aidl`: `oneway void onPairingRequest(int sessionId, String phoneAddress, int method)` + `oneway void onAuthData(int sessionId, String authData, int method)`
- `aidl/com/aauto/engine/IAAEngine.aidl`: `oneway void completePairing(int sessionId, int status, boolean alreadyPaired)` + `oneway void completeAuth(int sessionId, int status)`
- `core/include/aauto/engine/IEngineController.hpp`: `on_pairing_request` / `on_auth_data` virtual
- `impl/android/aidl/AidlEngineController.{hpp,cpp}`: bridge 호출
- `impl/android/main/main.cpp`: Service callback → AIDL forward
- `app/android/.../AaService.java`: BluetoothAdapter / BluetoothDevice 호출 + bond state receiver

부수적: Day 2 끝나면 wireless AA 사용성 향상 — user가 wired 한 번 연결하면
폰이 자동 페어링되고, 다음번 wireless AA 진입 시 manual pair 단계 skip.

### Day 3 — Bluedroid 통합 (별도 프로젝트)

- BluetoothAdapter, BluetoothDevice API로 실제 페어링 trigger
- HFP-AG profile activate
- 자동차 audio HAL routing 검증

이건 **AAP 학습 범위 밖**. 별도 plan으로 분리.

### Day 4 — PhoneStatus + 통화 통합 (plan 0008로 복귀)

- ch9 PhoneStatusService 활성화
- 페어링 + HFP 정상 동작 시 통화 시그널링까지 자연스럽게 흐름

## Files to touch (Day 1)

| 파일 | 변경 |
|------|------|
| `core/include/aauto/service/BluetoothService.hpp` | callback + send API + handler 멤버 |
| `core/src/service/BluetoothService.cpp` | 2개 inbound handler + 2개 outbound 송신 |
| `core/include/aauto/engine/HeadunitConfig.hpp` | (필요시) supported_methods 필드 |
| `impl/android/main/main.cpp` | services[13] 등록 + BT MAC 동적 설정 |

## Verification (Day 1)

기대 logcat 시나리오 A (ch13 살아있음 + HU 미페어링):

```
[AAP TX] control       SERVICE_DISCOVERY_RESP   NNN bytes
[AAP RX] bluetooth     CHANNEL_OPEN_REQ         4 bytes
[AAP TX] bluetooth     CHANNEL_OPEN_RESP        2 bytes
AA.BluetoothService: bluetooth CHANNEL_OPEN ch=13

[AAP RX] bluetooth     PAIRING_REQUEST          NN bytes
AA.BluetoothService: bluetooth PAIRING_REQUEST  phone="AA:BB..." method=PIN
AA.BluetoothService: bluetooth PAIRING_RESPONSE -> SUCCESS already_paired=false
                     (Day 1: stub response, no actual Bluedroid pair)
[AAP TX] bluetooth     PAIRING_RESPONSE         4 bytes

[AAP RX] bluetooth     AUTHENTICATION_DATA      NN bytes
AA.BluetoothService: bluetooth AUTH_DATA       data="1234" method=PIN
AA.BluetoothService: bluetooth AUTH_RESULT     -> SUCCESS (stub)
[AAP TX] bluetooth     AUTHENTICATION_RESULT    2 bytes
```

기대 시나리오 B (deprecation):

```
[AAP TX] control       SERVICE_DISCOVERY_RESP   NNN bytes
... ch13 CHANNEL_OPEN_REQ never arrives ...
```

## 작업량 합계

| Day | 작업 | 추정 |
|-----|------|------|
| 0 | 사전 검증 | 15min |
| 1 | core handler + 실기 검증 | 4~5h |
| 2 (선택) | UI confirmation | 3~4h |
| 3 (별도) | Bluedroid 통합 | 별도 프로젝트 |
| 4 | plan 0008 복귀 (PhoneStatus) | plan 0008 참고 |
| **총 (Day 1만)** | | **~5h** |

## 미래 trigger

- Day 1 결과가 deprecation이면 plan ON_HOLD하고 plan 0008으로 이동
- Bluedroid 통합 작업 자체는 Android Automotive 학습 라운드의 큰 챕터로
  분리. 본 학습 프로젝트의 budget 내에서는 의미 있는 진전이 어려움.
