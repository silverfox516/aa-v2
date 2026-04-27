# AAP Protocol Messages Reference

> 서비스별 메시지 목록, 송수신 방향, 단계(Phase), 선행 트리거, 실 동작
> 검증 폰, 구현 상태. Phone = 폰 (Android Auto app), HU = Head Unit (aa-v2).
>
> **Phase 라벨**:
> - `handshake` — VERSION 교환, SSL 핸드셰이크, AUTH
> - `discovery` — ServiceDiscovery 요청/응답
> - `channel-open` — 각 채널 열기 요청/응답
> - `setup` — 채널 열린 후 codec/config 협상 (MEDIA_SETUP, MEDIA_CONFIG)
> - `streaming` — 미디어 데이터 (MEDIA_DATA, MEDIA_ACK)
> - `focus` — Audio/Video/Nav focus 요청/응답
> - `heartbeat` — Ping 주기 메시지
> - `runtime` — 세션 진행 중 발생하는 상태/알림
> - `shutdown` — ByeBye 등 정상 종료
>
> **Verified**: 실기기에서 실제로 관찰된 메시지인지 + 어떤 폰에서 확인.
> 비어 있으면 코드는 있으나 실 송수신 미관찰 또는 미구현.
>
> 검증 폰: Samsung SM-N981N (Galaxy Note20 5G) — 본 문서의 기본 검증 모델.

---

## Control Channel (ch 0)

| ID | Message | Direction | Phase | Trigger | Role | Status | Verified |
|----|---------|-----------|-------|---------|------|--------|----------|
| 1 | VERSION_REQUEST | HU → Phone | handshake | session start (transport open) | AAP 버전 협상 요청 (v1.1) | ✅ 구현 | SM-N981N |
| 2 | VERSION_RESPONSE | Phone → HU | handshake | after VERSION_REQUEST | 버전 응답 + 호환성 상태 (관찰된 폰 v1.7) | ✅ 구현 | SM-N981N |
| 3 | ENCAPSULATED_SSL | 양방향 | handshake | after VERSION exchange | SSL/TLS 핸드셰이크 데이터 (multi-fragment 가능) | ✅ 구현 | SM-N981N |
| 4 | AUTH_COMPLETE | HU → Phone | handshake | SSL handshake done | 인증 완료 (마지막 평문 메시지) | ✅ 구현 | SM-N981N |
| 5 | SERVICE_DISCOVERY_REQ | Phone → HU | discovery | after AUTH_COMPLETE | 폰 정보(name, label) + 지원 서비스 요청 | ✅ 구현 | SM-N981N |
| 6 | SERVICE_DISCOVERY_RESP | HU → Phone | discovery | after SERVICE_DISCOVERY_REQ | HU 지원 서비스/채널 응답 (14개 service capability) | ✅ 구현 | SM-N981N |
| 7 | CHANNEL_OPEN_REQ | Phone → HU | channel-open | after ServiceDiscovery, per channel | 서비스 채널 열기 요청 | ✅ 구현 | SM-N981N |
| 8 | CHANNEL_OPEN_RESP | HU → Phone | channel-open | after CHANNEL_OPEN_REQ | 채널 열기 응답 (status=SUCCESS) | ✅ 구현 | SM-N981N |
| 9 | CHANNEL_CLOSE_NOTIF | 양방향 | shutdown | session teardown | 채널 닫기 알림 | ⬜ 미구현 | |
| 11 | PING_REQUEST | 양방향 | heartbeat | periodic (PingConfiguration) | Heartbeat 요청 (timestamp echo) | ✅ 구현 | SM-N981N |
| 12 | PING_RESPONSE | 양방향 | heartbeat | after PING_REQUEST | Heartbeat 응답 | ✅ 구현 | SM-N981N |
| 13 | NAV_FOCUS_REQ | Phone → HU | focus | navigation app interaction | 내비게이션 포커스 요청 | ✅ 구현 | SM-N981N |
| 14 | NAV_FOCUS_NOTIF | HU → Phone | focus | after NAV_FOCUS_REQ | 내비게이션 포커스 응답 (PROJECTED) | ✅ 구현 | SM-N981N |
| 15 | BYEBYE_REQUEST | 양방향 | shutdown | user disconnect / phone exit | 정상 종료 요청 | ✅ 구현 | SM-N981N |
| 16 | BYEBYE_RESPONSE | 양방향 | shutdown | after BYEBYE_REQUEST | 종료 응답 | ✅ 구현 | SM-N981N |
| 17 | VOICE_SESSION_NOTIF | Phone → HU | runtime | voice assistant invoked | 음성 인식 세션 상태 | ⬜ 미구현 | |
| 18 | AUDIO_FOCUS_REQ | Phone → HU | focus | after ServiceDiscovery + media start | 오디오 포커스 요청 (GAIN/RELEASE) | ✅ 구현 | SM-N981N |
| 19 | AUDIO_FOCUS_NOTIF | HU → Phone | focus | after AUDIO_FOCUS_REQ | 오디오 포커스 응답 (LOSS / GAIN) | ✅ 구현 | SM-N981N |
| 20 | CAR_CONNECTED_DEVICES_REQ | Phone → HU | runtime | (rare) | 차량 연결 디바이스 목록 요청 | ⬜ 미구현 | |
| 21 | CAR_CONNECTED_DEVICES_RESP | HU → Phone | runtime | after CAR_CONNECTED_DEVICES_REQ | 연결 디바이스 목록 응답 | ⬜ 미구현 | |
| 22 | USER_SWITCH_REQ | Phone → HU | runtime | (rare) | 사용자 전환 요청 | ⬜ 미구현 | |
| 23 | BATTERY_STATUS_NOTIF | Phone → HU | runtime | battery state change | 폰 배터리 상태 알림 | ⬜ 미구현 | |
| 24 | CALL_AVAILABILITY_STATUS | Phone → HU | runtime | call state change | 전화 가용 상태 | ⬜ 미구현 | |
| 25 | USER_SWITCH_RESP | HU → Phone | runtime | after USER_SWITCH_REQ | 사용자 전환 응답 | ⬜ 미구현 | |
| 26 | SERVICE_DISCOVERY_UPDATE | 양방향 | runtime | dynamic service capability change | 서비스 검색 업데이트 | ⬜ 미구현 | |

### Sequence (검증된 순서, SM-N981N 기준)

```
USB transport open
  → HU sends VERSION_REQUEST (4B, v1.1)
  ← Phone VERSION_RESPONSE (6B, v1.7 status=0)
  ↔ ENCAPSULATED_SSL (multiple fragments, ~135B → ~2347B → ~1186B → ~51B)
  → AUTH_COMPLETE (plaintext, last unencrypted)
  -- session state: SslHandshake → Running --
  ← SERVICE_DISCOVERY_REQ (839B, contains phone PhoneInfo)
  → SERVICE_DISCOVERY_RESP (302B, 14 service capabilities)
  ← AUDIO_FOCUS_REQ (RELEASE, 2B)
  → AUDIO_FOCUS_NOTIF (LOSS, 4B)
  ← CHANNEL_OPEN_REQ for each channel (1=video, 2~4=audio, 5=input, 6=sensor,
                                       7=mic, 8=nav, 9=phone, 10=playback,
                                       11=notification, 12=mediabrowser,
                                       13=bluetooth, 14=vendor.ext)
  → CHANNEL_OPEN_RESP for each (status=0)
  -- streaming begins per channel --
  ↔ PING_REQUEST/PING_RESPONSE every N seconds (PingConfiguration)
```

---

## Media Sink — Video (ch 1)

| ID | Message | Direction | Phase | Trigger | Role | Status | Verified |
|----|---------|-----------|-------|---------|------|--------|----------|
| 0 | MEDIA_DATA | Phone → HU | streaming | continuous after MEDIA_START | H.264 프레임 데이터 (8B timestamp + NAL) | ✅ 구현 | SM-N981N |
| 1 | CODEC_CONFIG | Phone → HU | setup | first frame after MEDIA_START | SPS/PPS 코덱 초기화 데이터 | ✅ 구현 | SM-N981N |
| 32768 | MEDIA_SETUP | Phone → HU | setup | after CHANNEL_OPEN_RESP | 코덱 타입 알림 (H264_BP) | ✅ 구현 | SM-N981N |
| 32769 | MEDIA_START | Phone → HU | streaming | after MEDIA_CONFIG | 미디어 스트림 시작 | ✅ 구현 | SM-N981N |
| 32770 | MEDIA_STOP | Phone → HU | streaming | focus 변경 / disconnect | 미디어 스트림 중지 | ✅ 로깅 | |
| 32771 | MEDIA_CONFIG | HU → Phone | setup | after MEDIA_SETUP | 수신 준비 응답 (max_unacked) | ✅ 구현 | SM-N981N |
| 32772 | MEDIA_ACK | HU → Phone | streaming | per frame received | 프레임 수신 확인 (credit 반환) | ✅ 구현 | SM-N981N |
| 32775 | VIDEO_FOCUS_REQ | Phone → HU | focus | (rare; usually HU pushes) | 비디오 포커스 변경 요청 (NATIVE/PROJECTED) | ✅ 구현 | |
| 32776 | VIDEO_FOCUS_NOTIF | HU → Phone | focus | Surface ready / destroyed | 비디오 포커스 응답 | ✅ 구현 | SM-N981N |
| 32777 | UPDATE_UI_CONFIG_REQ | Phone → HU | runtime | UI config change (e.g., night mode) | UI 설정 업데이트 요청 | ⬜ 미구현 | |
| 32778 | UPDATE_UI_CONFIG_REPLY | HU → Phone | runtime | after UPDATE_UI_CONFIG_REQ | UI 설정 업데이트 응답 | ⬜ 미구현 | |
| 32779 | AUDIO_UNDERFLOW_NOTIF | HU → Phone | runtime | playback buffer underrun | 오디오 언더플로우 알림 | ⬜ 미구현 | |

### Sequence (Video, SM-N981N)

```
CHANNEL_OPEN: ch=1 (video)
  ← MEDIA_SETUP (codec=H264_BP, 2B)
  → MEDIA_CONFIG (max_unacked=N, 6B)
  → VIDEO_FOCUS_NOTIF(PROJECTED) — when Surface ready (F.14)
  ← CODEC_CONFIG (SPS/PPS NALU)
  ← MEDIA_DATA (per frame: 8B timestamp + IDR NALU first, then P-frames)
  → MEDIA_ACK (every frame, credit return — see troubleshooting #2)
  ↻ continuous; large frames split via multi-fragment with 4B total_size
    (troubleshooting #4; F.17 round-trip verified)
```

### Input Channel (ch 5) — KEY_BINDING은 Input 채널에서 처리

| ID | Message | Direction | Phase | Trigger | Role | Status | Verified |
|----|---------|-----------|-------|---------|------|--------|----------|
| 32770 | KEY_BINDING_REQ | Phone → HU | setup | after CHANNEL_OPEN | HU 지원 키 바인딩 요청 | ✅ 구현 (SUCCESS 응답) | SM-N981N |
| 32771 | KEY_BINDING_RESP | HU → Phone | setup | after KEY_BINDING_REQ | 키 바인딩 응답 | ✅ 구현 | SM-N981N |

---

## Media Sink — Audio (ch 2: media, ch 3: guidance, ch 4: system)

| ID | Message | Direction | Phase | Trigger | Role | Status | Verified |
|----|---------|-----------|-------|---------|------|--------|----------|
| 0 | MEDIA_DATA | Phone → HU | streaming | continuous after MEDIA_START | PCM 오디오 데이터 (8B timestamp + samples) | ✅ 구현 | SM-N981N |
| 32768 | MEDIA_SETUP | Phone → HU | setup | after CHANNEL_OPEN_RESP | 코덱 타입 알림 (PCM 48kHz/16/2 — media; 16kHz/16/1 — guidance/system) | ✅ 구현 | SM-N981N |
| 32769 | MEDIA_START | Phone → HU | streaming | after MEDIA_CONFIG | 오디오 스트림 시작 | ✅ 구현 | SM-N981N |
| 32770 | MEDIA_STOP | Phone → HU | streaming | playback end / focus change | 오디오 스트림 중지 | ✅ 구현 | SM-N981N |
| 32771 | MEDIA_CONFIG | HU → Phone | setup | after MEDIA_SETUP | 수신 준비 응답 | ✅ 구현 | SM-N981N |
| 32772 | MEDIA_ACK | HU → Phone | streaming | per audio buffer | 수신 확인 | ✅ 구현 | SM-N981N |

세 채널 동일 메시지 셋. 차이는 sample rate / role:
- ch 2 (media): 48kHz / 16-bit / stereo — 음악, 미디어 앱
- ch 3 (guidance): 16kHz / 16-bit / mono — 내비 음성 안내
- ch 4 (system): 16kHz / 16-bit / mono — 시스템 효과음

---

## Input Source (ch 5)

| ID | Message | Direction | Phase | Trigger | Role | Status | Verified |
|----|---------|-----------|-------|---------|------|--------|----------|
| 32769 | INPUT_REPORT | HU → Phone | streaming | per touch / key event | 터치/키 입력 전달 (TouchEvent, KeyEvent) | ✅ 구현 (터치만) | SM-N981N |
| 32770 | KEY_BINDING_REQ | Phone → HU | setup | after CHANNEL_OPEN | 지원 키코드 확인 | ✅ 구현 | SM-N981N |
| 32771 | KEY_BINDING_RESP | HU → Phone | setup | after KEY_BINDING_REQ | 키코드 응답 | ✅ 구현 | SM-N981N |
| 32772 | INPUT_FEEDBACK | Phone → HU | runtime | input acknowledgement | 입력 피드백 (햅틱 등) | ⬜ 미구현 | |

### InputReport 내부 필드
- `touch_event`: 터치 좌표 + action (DOWN/UP/MOVE) — ✅ 구현
- `key_event`: 키코드 + down/up — ⬜ 미구현
- `absolute_event`: 절대좌표 입력 — ⬜ 미구현
- `relative_event`: 상대 입력 (로터리 등) — ⬜ 미구현
- `touchpad_event`: 터치패드 입력 — ⬜ 미구현

---

## Sensor Source (ch 6)

| ID | Message | Direction | Phase | Trigger | Role | Status | Verified |
|----|---------|-----------|-------|---------|------|--------|----------|
| 32769 | SENSOR_REQUEST | Phone → HU | setup | after CHANNEL_OPEN | 센서 시작 요청 (type 지정) | ✅ 구현 | SM-N981N |
| 32770 | SENSOR_RESPONSE | HU → Phone | setup | after SENSOR_REQUEST | 센서 시작 응답 (SUCCESS) | ✅ 구현 | SM-N981N |
| 32771 | SENSOR_BATCH | HU → Phone | streaming | sensor data available | 센서 데이터 배치 전송 | ✅ 구현 (DrivingStatus만) | SM-N981N |
| 32772 | SENSOR_ERROR | HU → Phone | runtime | sensor failure | 센서 에러 | ⬜ 미구현 | |

### 센서 타입 (관찰)
- DRIVING_STATUS (13): 운전 상태 (UNRESTRICTED) — ✅ SM-N981N에서 SENSOR_START 수신
- NIGHT_MODE (10): 야간 모드 — ✅ SM-N981N에서 SENSOR_START 수신 (응답만, 데이터 미전송)
- GPS (8): 위치 데이터 — ⬜ 미구현 (G.4 참고: ISensorSource 미설정)
- COMPASS (1): 나침반 — ⬜ 미구현
- GYROSCOPE (2): 자이로스코프 — ⬜ 미구현
- ACCELEROMETER (3): 가속도계 — ⬜ 미구현
- RPM, ODOMETER, FUEL, SPEED 등 차량 센서 — ⬜ 미구현

### 중요 (troubleshooting #3)
폰은 SENSOR_REQUEST 후 SENSOR_RESPONSE(SUCCESS)를 기다린 다음에야 비디오
스트리밍을 시작한다. 응답 누락/지연 시 비디오 시작이 3-5초 지연된다.

---

## Microphone Source (ch 7)

| ID | Message | Direction | Phase | Trigger | Role | Status | Verified |
|----|---------|-----------|-------|---------|------|--------|----------|
| 32773 | MIC_REQUEST | Phone → HU | runtime | voice assistant active | 마이크 녹음 시작/중지 요청 | ⬜ 미구현 (stub) | |
| 32774 | MIC_RESPONSE | HU → Phone | runtime | after MIC_REQUEST | 마이크 응답 | ⬜ 미구현 (stub) | |

서비스 광고만 함 (troubleshooting #1: Samsung 폰들이 microphone capability를
요구). 캡처 구현 없음.

---

## Navigation Status (ch 8)

| ID | Message | Direction | Phase | Trigger | Role | Status | Verified |
|----|---------|-----------|-------|---------|------|--------|----------|
| 32769 | NAV_START | Phone → HU | runtime | navigation begin | 내비게이션 클러스터 시작 | ⬜ 미구현 | |
| 32770 | NAV_STOP | Phone → HU | runtime | navigation end | 내비게이션 클러스터 중지 | ⬜ 미구현 | |
| 32771 | NAV_STATUS | Phone → HU | runtime | route progress update | 내비게이션 상태 (경로 안내 중/종료) | ⬜ 미구현 | |
| 32774 | NAV_STATE | Phone → HU | runtime | turn-by-turn update | 턴바이턴 상태 (단계별 안내) | ⬜ 미구현 | |
| 32775 | NAV_CURRENT_POSITION | Phone → HU | runtime | position update | 현재 위치 좌표 | ⬜ 미구현 | |

### 관련 proto 메시지
- NavigationStatusStart: 클러스터 표시 시작
- NavigationState: 턴 방향, 거리, 도로명
- NavigationCue: 텍스트 안내 큐
- NavigationStep: 경로 단계
- NavigationManeuver: 회전 방향 (LEFT, RIGHT, U_TURN 등)
- NavigationDistance: 남은 거리
- NavigationCurrentPosition: GPS 좌표

---

## Phone Status (ch 9)

| ID | Message | Direction | Phase | Trigger | Role | Status | Verified |
|----|---------|-----------|-------|---------|------|--------|----------|
| 32769 | PHONE_STATUS | Phone → HU | runtime | call/battery/signal change | 폰 상태 (배터리, 신호, 통화 등) | ⬜ 미구현 (unhandled 로그) | SM-N981N (수신 관찰됨, 미처리) |
| 32770 | PHONE_STATUS_INPUT | HU → Phone | runtime | call accept/reject button | 폰 상태 관련 입력 (전화 수락/거절) | ⬜ 미구현 | |

### PhoneStatus 필드
- signal_strength: 신호 세기 (0-4)
- battery_level: 배터리 잔량 (%)
- call_state: 통화 상태 (incoming/active/ended)
- caller_name, caller_number: 발신자 정보

---

## Media Playback Status (ch 10)

| ID | Message | Direction | Phase | Trigger | Role | Status | Verified |
|----|---------|-----------|-------|---------|------|--------|----------|
| 32769 | PLAYBACK_STATUS | Phone → HU | runtime | track change / play / pause | 재생 상태 (곡 정보, 위치, 앨범아트) | ⬜ 미구현 (unhandled 로그) | SM-N981N (수신, 76KB MEDIA_CONFIG 포함) |
| 32770 | PLAYBACK_INPUT | HU → Phone | runtime | media key / steering wheel | 재생 제어 (play/pause/skip/seek) | ⬜ 미구현 | |
| 32771 | PLAYBACK_METADATA | Phone → HU | runtime | metadata available | 메타데이터 (아티스트, 앨범, 장르 등) | ⬜ 미구현 | |

### MediaPlaybackStatus 필드
- state: PLAYING/PAUSED/STOPPED
- source: 미디어 소스 (Spotify, YouTube Music 등)
- song_title, artist, album: 곡 정보
- position_ms, duration_ms: 재생 위치/전체 길이
- album_art: 앨범아트 이미지 (PNG/JPEG, 50-90KB — multi-fragment 검증 트래픽)

---

## Generic Notification (ch 11)

| ID | Message | Direction | Phase | Trigger | Role | Status | Verified |
|----|---------|-----------|-------|---------|------|--------|----------|
| 32769 | SUBSCRIBE | HU → Phone | runtime | HU UI ready | 알림 구독 요청 | ⬜ 미구현 | |
| 32770 | UNSUBSCRIBE | HU → Phone | runtime | HU UI gone | 알림 구독 해제 | ⬜ 미구현 | |
| 32771 | NOTIFICATION | Phone → HU | runtime | app notification arrives | 앱 알림 (제목, 내용, 아이콘) | ⬜ 미구현 | |
| 32772 | NOTIFICATION_ACK | HU → Phone | runtime | after NOTIFICATION | 알림 수신 확인 | ⬜ 미구현 | |

---

## Bluetooth (ch 13 — stub advertise)

| ID | Message | Direction | Phase | Trigger | Role | Status | Verified |
|----|---------|-----------|-------|---------|------|--------|----------|
| 32769 | PAIRING_REQUEST | Phone → HU | runtime | post-CHANNEL_OPEN | BT 페어링 요청 | ⬜ 미구현 (stub, unhandled) | SM-N981N (MEDIA_START 21B 수신, unhandled) |
| 32770 | PAIRING_RESPONSE | HU → Phone | runtime | after PAIRING_REQUEST | 페어링 응답 | ⬜ 미구현 | |
| 32771 | AUTH_DATA | 양방향 | runtime | pairing in progress | 인증 데이터 교환 | ⬜ 미구현 | |
| 32772 | AUTH_RESULT | 양방향 | runtime | pairing complete | 인증 결과 | ⬜ 미구현 | |

채널은 advertise 됨 (BluetoothService stub, G.2). 폰이 메시지를 보내지만
HU는 unhandled 로그만.

---

## Media Browser (ch 12 — stub advertise)

| ID | Message | Direction | Phase | Trigger | Role | Status | Verified |
|----|---------|-----------|-------|---------|------|--------|----------|
| 32769 | ROOT_NODE | Phone → HU | runtime | after GET_NODE(root) | 미디어 브라우저 루트 노드 | ⬜ 미구현 | |
| 32770 | SOURCE_NODE | Phone → HU | runtime | after GET_NODE(source) | 미디어 소스 목록 | ⬜ 미구현 | |
| 32771 | LIST_NODE | Phone → HU | runtime | after GET_NODE(list) | 폴더/재생목록 | ⬜ 미구현 | |
| 32772 | SONG_NODE | Phone → HU | runtime | after GET_NODE(song) | 개별 곡 정보 | ⬜ 미구현 | |
| 32773 | GET_NODE | HU → Phone | runtime | user browses | 노드 탐색 요청 | ⬜ 미구현 | |
| 32774 | BROWSE_INPUT | HU → Phone | runtime | search input | 브라우징 입력 | ⬜ 미구현 | |

채널은 advertise 됨 (G.1). 폰이 우리가 GET_NODE를 안 보내므로 응답
메시지 미관찰.

---

## Vendor Extension (ch 14 — stub advertise)

| ID | Message | Direction | Phase | Trigger | Role | Status | Verified |
|----|---------|-----------|-------|---------|------|--------|----------|
| (vendor-defined) | (vendor-defined) | 양방향 | runtime | vendor-defined | 자유 채널, 표준 메시지 정의 없음 | ⬜ 미구현 (stub) | |

채널은 advertise 됨 (G.3). 표준 동작 없음.

---

## Radio (미광고)

차량 라디오 통합 — FM/AM/HD Radio/DAB 제어. 총 25개 메시지.
미구현. 차량에 라디오 HW + HAL 필요.

---

## WiFi Projection (미광고)

| ID | Message | Direction | Phase | Trigger | Role | Status | Verified |
|----|---------|-----------|-------|---------|------|--------|----------|
| 32769 | CREDENTIALS_REQ | Phone → HU | (ch 13 의 AAW 대체로 사용 안 함) | — | WiFi 자격증명 요청 | ⬜ 미구현 (RFCOMM으로 처리) | |
| 32770 | CREDENTIALS_RESP | HU → Phone | — | — | WiFi 자격증명 응답 | ⬜ 미구현 | |

무선 AA WiFi 자격증명은 AAP 채널이 아닌 BT RFCOMM(AAW protocol, 아래)으로
교환됨.

---

## AAW (Android Auto Wireless) — RFCOMM 프로토콜

AAP 채널이 아닌 BT RFCOMM 위에서 동작하는 별도 프로토콜. 무선 AA 시작
때만 사용되며, 이후 TCP/AAP로 전환된다.

| ID | Message | Direction | Phase | Trigger | Role | Status | Verified |
|----|---------|-----------|-------|---------|------|--------|----------|
| 1 | WIFI_START_REQ | HU → Phone | handshake (AAW) | RFCOMM accepted | WiFi AP 정보 전달 (IP, port) | ✅ 구현 | SM-N981N |
| 2 | WIFI_INFO_REQ | Phone → HU | handshake (AAW) | after WIFI_START_REQ | WiFi 자격증명 요청 | ✅ 구현 | SM-N981N |
| 3 | WIFI_INFO_RESP | HU → Phone | handshake (AAW) | after WIFI_INFO_REQ | SSID, PSK, BSSID 전달 | ✅ 구현 | SM-N981N |
| 4 | WIFI_VERSION_REQ | HU → Phone | handshake (AAW) | RFCOMM accepted | AAW 버전 확인 | ✅ 구현 | SM-N981N |
| 5 | WIFI_VERSION_RESP | Phone → HU | handshake (AAW) | after WIFI_VERSION_REQ | AAW 버전 응답 | ✅ 구현 | SM-N981N |
| 6 | WIFI_CONNECTION_STATUS | Phone → HU | handshake (AAW) | WiFi connected on phone | WiFi 연결 상태 알림 | ✅ 구현 | SM-N981N |
| 7 | WIFI_START_RESP | Phone → HU | handshake (AAW) | end of AAW handshake | WiFi 시작 결과 (status=0 성공) | ✅ 구현 | SM-N981N |

### Sequence (Wireless 시작, SM-N981N 검증)

```
RFCOMM accepted on AAW UUID
  → WIFI_VERSION_REQ
  → WIFI_START_REQ (HU의 IP + port 5277)
  ← WIFI_INFO_REQ  (msgId=2 len=0)
  → WIFI_INFO_RESP (SSID, PSK, BSSID)
  ← WIFI_CONNECTION_STATUS (msgId=7) — 폰이 H/U의 hotspot에 접속됨
  ← WIFI_START_RESP (status=0)
  -- 이후 TCP 5277로 전환, AAP handshake가 USB와 동일 시퀀스로 진행 --
```

---

## 구현 현황 요약

| 서비스 | 채널 | SD등록 | 메시지 수 | 구현 | 미구현 | 검증 |
|--------|------|--------|----------|------|--------|------|
| Control | 0 | 암시적(ch0) | 26 | 15 | 11 | SM-N981N |
| Video Sink | 1 | ✅ | 14 | 11 | 3 | SM-N981N |
| Audio Sink (×3) | 2-4 | ✅ | 6 | 6 | 0 | SM-N981N |
| Input Source | 5 | ✅ | 4 | 3 | 1 | SM-N981N |
| Sensor Source | 6 | ✅ | 4 | 3 | 1 | SM-N981N |
| Microphone | 7 | ✅ | 2 | 0 | 2 (stub) | (advertise만) |
| Navigation Status | 8 | ⬜ unregistered (G.3a) | 5 | 0 | 5 | — |
| Phone Status | 9 | ⬜ unregistered (G.3a) | 2 | 0 | 2 | (이전: 수신, unhandled) |
| Media Playback | 10 | ⬜ unregistered (G.3a) | 3 | 0 | 3 | (이전: 수신, unhandled — lag 원인) |
| Generic Notification | 11 | ⬜ unregistered (G.3a) | 4 | 0 | 4 | — |
| Media Browser | 12 | ⬜ unregistered (G.1) | 6 | 0 | 6 | — |
| Bluetooth | 13 | ⬜ unregistered (G.2) | 4 | 0 | 4 | (이전: 수신, unhandled) |
| Vendor Extension | 14 | ⬜ unregistered (G.3) | — | 0 | — | — |
| Radio | - | ⬜ | 25 | 0 | 25 | |
| WiFi Projection | - | ⬜ | 2 | 0 | 2 | |
| AAW (RFCOMM) | - | N/A | 7 | 7 | 0 | SM-N981N |
| **합계** | | | **114+** | **45** | **69+** | |

> **검증 컬럼 의미**: "SM-N981N"은 해당 서비스의 메시지가 실 동작에서
> 관찰되었음 (정상 처리 또는 unhandled 로그). "(advertise만)"은
> ServiceDiscoveryResponse에는 포함되지만 폰이 채널을 열지 않거나
> 메시지를 안 보냄. 빈 셀은 아직 미관찰/미구현.

---

## 사용 권장

- AAP 메시지를 처음 학습할 때는 **Phase** 컬럼으로 묶어 보기 (handshake →
  discovery → channel-open → setup → streaming → focus → runtime →
  shutdown 순).
- 새 폰 모델로 검증 시 **Verified** 컬럼에 모델명 추가. 모델별
  관찰 차이는 troubleshooting.md에 기록.
- 미관찰/미구현 상태인 메시지를 풀 구현할지 stub으로 남길지는
  architecture_review.md Part G의 학습 가치 판단 기준 참고.
