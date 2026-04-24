# AAP Protocol Messages Reference

> 서비스별 메시지 목록, 송수신 방향, 역할, 구현 상태.
> Phone = 폰 (Android Auto app), HU = Head Unit (aa-v2).

---

## Control Channel (ch 0)

| ID | Message | Direction | Role | Status |
|----|---------|-----------|------|--------|
| 1 | VERSION_REQUEST | HU → Phone | AAP 버전 협상 요청 | ✅ 구현 |
| 2 | VERSION_RESPONSE | Phone → HU | 버전 응답 + 호환성 상태 | ✅ 구현 |
| 3 | ENCAPSULATED_SSL | 양방향 | SSL/TLS 핸드셰이크 데이터 | ✅ 구현 |
| 4 | AUTH_COMPLETE | HU → Phone | 인증 완료 (마지막 평문 메시지) | ✅ 구현 |
| 5 | SERVICE_DISCOVERY_REQ | Phone → HU | 폰 정보 + 지원 서비스 요청 | ✅ 구현 |
| 6 | SERVICE_DISCOVERY_RESP | HU → Phone | HU 지원 서비스/채널 응답 | ✅ 구현 |
| 7 | CHANNEL_OPEN_REQ | Phone → HU | 서비스 채널 열기 요청 | ✅ 구현 |
| 8 | CHANNEL_OPEN_RESP | HU → Phone | 채널 열기 응답 (SUCCESS) | ✅ 구현 |
| 9 | CHANNEL_CLOSE_NOTIF | 양방향 | 채널 닫기 알림 | ⬜ 미구현 |
| 11 | PING_REQUEST | 양방향 | Heartbeat 요청 (timestamp echo) | ✅ 구현 |
| 12 | PING_RESPONSE | 양방향 | Heartbeat 응답 | ✅ 구현 |
| 13 | NAV_FOCUS_REQ | Phone → HU | 내비게이션 포커스 요청 | ✅ 구현 |
| 14 | NAV_FOCUS_NOTIF | HU → Phone | 내비게이션 포커스 응답 (PROJECTED) | ✅ 구현 |
| 15 | BYEBYE_REQUEST | 양방향 | 정상 종료 요청 | ✅ 구현 |
| 16 | BYEBYE_RESPONSE | 양방향 | 종료 응답 | ✅ 구현 |
| 17 | VOICE_SESSION_NOTIF | Phone → HU | 음성 인식 세션 상태 | ⬜ 미구현 |
| 18 | AUDIO_FOCUS_REQ | Phone → HU | 오디오 포커스 요청 (GAIN/RELEASE) | ✅ 구현 |
| 19 | AUDIO_FOCUS_NOTIF | HU → Phone | 오디오 포커스 응답 | ✅ 구현 |
| 20 | CAR_CONNECTED_DEVICES_REQ | Phone → HU | 차량 연결 디바이스 목록 요청 | ⬜ 미구현 |
| 21 | CAR_CONNECTED_DEVICES_RESP | HU → Phone | 연결 디바이스 목록 응답 | ⬜ 미구현 |
| 22 | USER_SWITCH_REQ | Phone → HU | 사용자 전환 요청 | ⬜ 미구현 |
| 23 | BATTERY_STATUS_NOTIF | Phone → HU | 폰 배터리 상태 알림 | ⬜ 미구현 |
| 24 | CALL_AVAILABILITY_STATUS | Phone → HU | 전화 가용 상태 | ⬜ 미구현 |
| 25 | USER_SWITCH_RESP | HU → Phone | 사용자 전환 응답 | ⬜ 미구현 |
| 26 | SERVICE_DISCOVERY_UPDATE | 양방향 | 서비스 검색 업데이트 | ⬜ 미구현 |

---

## Media Sink — Video (ch 1)

| ID | Message | Direction | Role | Status |
|----|---------|-----------|------|--------|
| 0 | MEDIA_DATA | Phone → HU | H.264 프레임 데이터 (8B timestamp + NAL) | ✅ 구현 |
| 1 | CODEC_CONFIG | Phone → HU | SPS/PPS 코덱 초기화 데이터 | ✅ 구현 |
| 32768 | MEDIA_SETUP | Phone → HU | 코덱 타입 알림 (H264_BP 등) | ✅ 구현 |
| 32769 | MEDIA_START | Phone → HU | 미디어 스트림 시작 | ✅ 구현 |
| 32770 | MEDIA_STOP | Phone → HU | 미디어 스트림 중지 | ✅ 로깅 |
| 32771 | MEDIA_CONFIG | HU → Phone | 수신 준비 응답 (max_unacked) | ✅ 구현 |
| 32772 | MEDIA_ACK | HU → Phone | 프레임 수신 확인 (credit 반환) | ✅ 구현 |
| 32775 | VIDEO_FOCUS_REQ | Phone → HU | 비디오 포커스 변경 요청 (NATIVE/PROJECTED) | ✅ 구현 |
| 32776 | VIDEO_FOCUS_NOTIF | HU → Phone | 비디오 포커스 응답 | ✅ 구현 |
| 32777 | UPDATE_UI_CONFIG_REQ | Phone → HU | UI 설정 업데이트 요청 | ⬜ 미구현 |
| 32778 | UPDATE_UI_CONFIG_REPLY | HU → Phone | UI 설정 업데이트 응답 | ⬜ 미구현 |
| 32779 | AUDIO_UNDERFLOW_NOTIF | HU → Phone | 오디오 언더플로우 알림 | ⬜ 미구현 |

### Input Channel (ch 5) — KEY_BINDING은 Input 채널에서 처리

| ID | Message | Direction | Role | Status |
|----|---------|-----------|------|--------|
| 32770 | KEY_BINDING_REQ | Phone → HU | HU 지원 키 바인딩 요청 | ✅ 구현 (SUCCESS 응답) |
| 32771 | KEY_BINDING_RESP | HU → Phone | 키 바인딩 응답 | ✅ 구현 |

---

## Media Sink — Audio (ch 2: media, ch 3: guidance, ch 4: system)

| ID | Message | Direction | Role | Status |
|----|---------|-----------|------|--------|
| 0 | MEDIA_DATA | Phone → HU | PCM 오디오 데이터 (8B timestamp + samples) | ✅ 구현 |
| 32768 | MEDIA_SETUP | Phone → HU | 코덱 타입 알림 (PCM) | ✅ 구현 |
| 32769 | MEDIA_START | Phone → HU | 오디오 스트림 시작 | ✅ 구현 |
| 32770 | MEDIA_STOP | Phone → HU | 오디오 스트림 중지 | ✅ 구현 |
| 32771 | MEDIA_CONFIG | HU → Phone | 수신 준비 응답 | ✅ 구현 |
| 32772 | MEDIA_ACK | HU → Phone | 수신 확인 | ✅ 구현 |

---

## Input Source (ch 5)

| ID | Message | Direction | Role | Status |
|----|---------|-----------|------|--------|
| 32769 | INPUT_REPORT | HU → Phone | 터치/키 입력 전달 (TouchEvent, KeyEvent) | ✅ 구현 (터치만) |
| 32770 | KEY_BINDING_REQ | Phone → HU | 지원 키코드 확인 | ✅ 구현 |
| 32771 | KEY_BINDING_RESP | HU → Phone | 키코드 응답 | ✅ 구현 |
| 32772 | INPUT_FEEDBACK | Phone → HU | 입력 피드백 (햅틱 등) | ⬜ 미구현 |

### InputReport 내부 필드
- `touch_event`: 터치 좌표 + action (DOWN/UP/MOVE) — ✅ 구현
- `key_event`: 키코드 + down/up — ⬜ 미구현
- `absolute_event`: 절대좌표 입력 — ⬜ 미구현
- `relative_event`: 상대 입력 (로터리 등) — ⬜ 미구현
- `touchpad_event`: 터치패드 입력 — ⬜ 미구현

---

## Sensor Source (ch 6)

| ID | Message | Direction | Role | Status |
|----|---------|-----------|------|--------|
| 32769 | SENSOR_REQUEST | Phone → HU | 센서 시작 요청 (type 지정) | ✅ 구현 |
| 32770 | SENSOR_RESPONSE | HU → Phone | 센서 시작 응답 (SUCCESS) | ✅ 구현 |
| 32771 | SENSOR_BATCH | HU → Phone | 센서 데이터 배치 전송 | ✅ 구현 (DrivingStatus만) |
| 32772 | SENSOR_ERROR | HU → Phone | 센서 에러 | ⬜ 미구현 |

### 센서 타입
- DRIVING_STATUS (13): 운전 상태 (UNRESTRICTED) — ✅ 구현
- NIGHT_MODE (10): 야간 모드 — ✅ 구현 (응답만, 데이터 미전송)
- GPS (8): 위치 데이터 — ⬜ 미구현
- COMPASS (1): 나침반 — ⬜ 미구현
- GYROSCOPE (2): 자이로스코프 — ⬜ 미구현
- ACCELEROMETER (3): 가속도계 — ⬜ 미구현
- RPM, ODOMETER, FUEL, SPEED 등 차량 센서 — ⬜ 미구현

---

## Microphone Source (ch 7)

| ID | Message | Direction | Role | Status |
|----|---------|-----------|------|--------|
| 32773 | MIC_REQUEST | Phone → HU | 마이크 녹음 시작/중지 요청 | ⬜ 미구현 (stub) |
| 32774 | MIC_RESPONSE | HU → Phone | 마이크 응답 | ⬜ 미구현 (stub) |

서비스 광고만 함 (폰이 마이크 없으면 ChannelOpen 거부).

---

## Navigation Status (ch 8)

| ID | Message | Direction | Role | Status |
|----|---------|-----------|------|--------|
| 32769 | NAV_START | Phone → HU | 내비게이션 클러스터 시작 | ⬜ 미구현 |
| 32770 | NAV_STOP | Phone → HU | 내비게이션 클러스터 중지 | ⬜ 미구현 |
| 32771 | NAV_STATUS | Phone → HU | 내비게이션 상태 (경로 안내 중/종료) | ⬜ 미구현 |
| 32774 | NAV_STATE | Phone → HU | 턴바이턴 상태 (단계별 안내) | ⬜ 미구현 |
| 32775 | NAV_CURRENT_POSITION | Phone → HU | 현재 위치 좌표 | ⬜ 미구현 |

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

| ID | Message | Direction | Role | Status |
|----|---------|-----------|------|--------|
| 32769 | PHONE_STATUS | Phone → HU | 폰 상태 (배터리, 신호, 통화 등) | ⬜ 미구현 |
| 32770 | PHONE_STATUS_INPUT | HU → Phone | 폰 상태 관련 입력 (전화 수락/거절) | ⬜ 미구현 |

### PhoneStatus 필드
- signal_strength: 신호 세기 (0-4)
- battery_level: 배터리 잔량 (%)
- call_state: 통화 상태 (incoming/active/ended)
- caller_name, caller_number: 발신자 정보

---

## Media Playback Status (ch 10)

| ID | Message | Direction | Role | Status |
|----|---------|-----------|------|--------|
| 32769 | PLAYBACK_STATUS | Phone → HU | 재생 상태 (곡 정보, 위치, 앨범아트) | ⬜ 미구현 |
| 32770 | PLAYBACK_INPUT | HU → Phone | 재생 제어 (play/pause/skip/seek) | ⬜ 미구현 |
| 32771 | PLAYBACK_METADATA | Phone → HU | 메타데이터 (아티스트, 앨범, 장르 등) | ⬜ 미구현 |

### MediaPlaybackStatus 필드
- state: PLAYING/PAUSED/STOPPED
- source: 미디어 소스 (Spotify, YouTube Music 등)
- song_title, artist, album: 곡 정보
- position_ms, duration_ms: 재생 위치/전체 길이
- album_art: 앨범아트 이미지 (PNG/JPEG, ~50-90KB)

---

## Generic Notification (ch 11)

| ID | Message | Direction | Role | Status |
|----|---------|-----------|------|--------|
| 32769 | SUBSCRIBE | HU → Phone | 알림 구독 요청 | ⬜ 미구현 |
| 32770 | UNSUBSCRIBE | HU → Phone | 알림 구독 해제 | ⬜ 미구현 |
| 32771 | NOTIFICATION | Phone → HU | 앱 알림 (제목, 내용, 아이콘) | ⬜ 미구현 |
| 32772 | NOTIFICATION_ACK | HU → Phone | 알림 수신 확인 | ⬜ 미구현 |

---

## Bluetooth (별도 채널)

| ID | Message | Direction | Role | Status |
|----|---------|-----------|------|--------|
| 32769 | PAIRING_REQUEST | Phone → HU | BT 페어링 요청 | ⬜ 미구현 |
| 32770 | PAIRING_RESPONSE | HU → Phone | 페어링 응답 | ⬜ 미구현 |
| 32771 | AUTH_DATA | 양방향 | 인증 데이터 교환 | ⬜ 미구현 |
| 32772 | AUTH_RESULT | 양방향 | 인증 결과 | ⬜ 미구현 |

---

## Media Browser (미광고)

| ID | Message | Direction | Role | Status |
|----|---------|-----------|------|--------|
| 32769 | ROOT_NODE | Phone → HU | 미디어 브라우저 루트 노드 | ⬜ 미구현 |
| 32770 | SOURCE_NODE | Phone → HU | 미디어 소스 목록 | ⬜ 미구현 |
| 32771 | LIST_NODE | Phone → HU | 폴더/재생목록 | ⬜ 미구현 |
| 32772 | SONG_NODE | Phone → HU | 개별 곡 정보 | ⬜ 미구현 |
| 32773 | GET_NODE | HU → Phone | 노드 탐색 요청 | ⬜ 미구현 |
| 32774 | BROWSE_INPUT | HU → Phone | 브라우징 입력 | ⬜ 미구현 |

---

## Radio (미광고)

차량 라디오 통합 — FM/AM/HD Radio/DAB 제어. 총 25개 메시지.
미구현. 차량에 라디오 HW가 필요.

---

## WiFi Projection (미광고)

| ID | Message | Direction | Role | Status |
|----|---------|-----------|------|--------|
| 32769 | CREDENTIALS_REQ | Phone → HU | WiFi 자격증명 요청 | ⬜ 미구현 (RFCOMM으로 처리) |
| 32770 | CREDENTIALS_RESP | HU → Phone | WiFi 자격증명 응답 | ⬜ 미구현 |

무선 AA WiFi 자격증명은 AAP 채널이 아닌 BT RFCOMM으로 교환 (BluetoothWirelessManager).

---

## AAW (Android Auto Wireless) — RFCOMM 프로토콜

AAP 채널이 아닌 BT RFCOMM 위에서 동작하는 별도 프로토콜.

| ID | Message | Direction | Role | Status |
|----|---------|-----------|------|--------|
| 1 | WIFI_START_REQ | HU → Phone | WiFi AP 정보 전달 (IP, port) | ✅ 구현 |
| 2 | WIFI_INFO_REQ | Phone → HU | WiFi 자격증명 요청 | ✅ 구현 |
| 3 | WIFI_INFO_RESP | HU → Phone | SSID, PSK, BSSID 전달 | ✅ 구현 |
| 4 | WIFI_VERSION_REQ | HU → Phone | AAW 버전 확인 | ✅ 구현 |
| 5 | WIFI_VERSION_RESP | Phone → HU | AAW 버전 응답 | ✅ 구현 |
| 6 | WIFI_CONNECTION_STATUS | Phone → HU | WiFi 연결 상태 알림 | ✅ 구현 |
| 7 | WIFI_START_RESP | Phone → HU | WiFi 시작 결과 (status=0 성공) | ✅ 구현 |

---

## 구현 현황 요약

| 서비스 | 채널 | 광고 | 메시지 수 | 구현 | 미구현 |
|--------|------|------|----------|------|--------|
| Control | 0 | 암시적 | 26 | 15 | 11 |
| Video Sink | 1 | ✅ | 14 | 11 | 3 |
| Audio Sink (×3) | 2-4 | ✅ | 6 | 6 | 0 |
| Input Source | 5 | ✅ | 4 | 3 | 1 |
| Sensor Source | 6 | ✅ | 4 | 3 | 1 |
| Microphone | 7 | ✅ | 2 | 0 | 2 (stub) |
| Navigation Status | 8 | ✅ | 5 | 0 | 5 |
| Phone Status | 9 | ✅ | 2 | 0 | 2 |
| Media Playback | 10 | ✅ | 3 | 0 | 3 |
| Generic Notification | 11 | ✅ | 4 | 0 | 4 |
| Bluetooth | - | ⬜ | 4 | 0 | 4 |
| Media Browser | - | ⬜ | 6 | 0 | 6 |
| Radio | - | ⬜ | 25 | 0 | 25 |
| WiFi Projection | - | ⬜ | 2 | 0 | 2 |
| AAW (RFCOMM) | - | N/A | 7 | 7 | 0 |
| **합계** | | | **114** | **45** | **69** |
