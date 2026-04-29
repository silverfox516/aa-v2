# 0005 — MediaBrowser channel

> Created: 2026-04-28
> Status: **DEPRECATED-IN-MODERN-AA (2026-04-29)** — re-investigation
> with Spotify installed confirmed the legacy AAP MediaBrowser channel
> (ch12) is effectively unused by modern Android Auto. The phone
> never opens the channel regardless of installed media app, because
> apps now use the androidx.car.app library: the phone renders the
> browse UI itself and projects it via the video sink (ch1). HU does
> not receive track-list data on a separate channel anymore. Service
> code retained as a fallback path / learning artifact.
> Related decisions: G.1 (refresh — phone-side refusal observation),
> F.20 (재생 명령은 KEYCODE via Input ch — MediaBrowser는 곡 *선택*만
> 담당, 재생 토글과 분리)

## Day 1 attempt outcome (2026-04-28)

Code shipped:
- `core/.../MediaBrowserService.{hpp,cpp}`: 4 inbound handlers (parse +
  log + invoke callback) + 2 outbound primitives (`request_node`,
  `browse_input`) + temporary auto-request-on-channel-open hook
- `main.cpp`: services[12] registered

Observation on real device (Nothing A001, SM-N981N):
- ✅ `mutable_media_browser_service()`만으로 SERVICE_DISCOVERY_RESPONSE
  은 정상 전송됨 (NACK 없음)
- ✅ G.0 throttle 영향 없음 — PLAYBACK_STATUS 1Hz 유지, 비디오/오디오
  정상
- ❌ **폰이 ch12 (media.browser) CHANNEL_OPEN_REQ를 보내지 않음**
- 우리가 advertise한 모든 채널 중 ch12만 폰이 열지 않음 (ch1~7, ch10
  은 모두 정상 open)
- 따라서 우리 `on_channel_open` 트리거되지 않음 → auto-request도
  나가지 않음 → 어떤 inbound message도 관찰 못 함
- 다른 미디어 앱들(YT Music, 사용자가 추가 시도한 앱들)에서도 동일
  결과

Cleanup after on-hold decision:
- main.cpp services[12] 등록 해제 (다른 unregistered stub들과 동일
  취급)
- MediaBrowserService::on_channel_open() Day 1 hook 제거 (재시도 시
  다시 추가)
- 코드 자체는 유지 — 재시도 시 proto wiring + handler skeleton 재
  유도 비용 없음
- 채널-aware msg_type_name 작업은 별도 가치 있어 분리 commit으로 유지

Hypotheses for the refusal (검증 미진행):
1. Phone-side capability gating — `MediaBrowserService { }`가 빈
   message지만 폰이 추가 capability 필드(예: supported source types,
   protocol version)를 기대해서 silent reject. **가장 가능성 높음** —
   Service.proto의 다른 service들은 capability 필드가 있는데
   MediaBrowserService만 비어있는 게 어색함.
2. Headunit identity gating — 폰이 HeadunitInfo의 manufacturer/model
   string으로 화이트리스트 기반 결정. 우리 HU 식별자가 unknown이라
   거부.
3. Phone-side AAP 버전 차이 — 신/구 버전 간 protocol 차이로 빈 sink
   advertise를 silent reject.

재시도 트리거 조건:
- (a) reference openauto/aasdk MediaBrowserService config에 추가
  필드가 있는지 확인 — 차이 발견 시 재시도
- (b) 다른 폰(특히 Pixel, Galaxy 다른 모델)에서 같은 결과인지 확인
- (c) Spotify 정식 차량 인증 ↔ 일반 사이드로드 차이가 있는지 (Spotify
  AA browse는 Google Assistant Driving Mode 등록 필요한 사례 보고됨)
- (d) cluster display 같은 다른 멀티 디스플레이 시나리오 학습 우선
  순위 도래 시 함께 재검토

---

## Goal

폰의 미디어 라이브러리 트리(Spotify 플레이리스트, 로컬 음악 등)를
HU에서 탐색해서 곡을 선택할 수 있게 한다. 학습 가치:

- 폰이 source/list/song 단위로 뭘 expose하는지 raw 관찰
- 페이지네이션(start/total) + album_art opt-out 패턴 학습
- 두 개의 폰-제어 채널(Input ch KEYCODE = 재생 토글, MediaBrowser
  BROWSE_INPUT = 곡 선택)이 어떻게 분리되어 동작하는지 학습
- G.0 passive-handler rule 두 번째 검증 사례 (PlaybackMetadata는 push,
  MediaBrowser는 request/response)

학습 산출물 우선. 100% 완성된 browser UI는 목표가 아님.

## Current state (2026-04-28)

| 자산 | 상태 |
|------|------|
| `protobuf/aap_protobuf/service/mediabrowser/` 9개 .proto | ✅ 모두 import됨 |
| `core/include/aauto/service/MediaBrowserService.hpp` | empty stub |
| `core/src/service/MediaBrowserService.cpp` | `fill_config`만 |
| `main.cpp` 채널 12 등록 | ❌ 미등록 (G.1) |
| `IAAEngineCallback` MediaBrowser 콜백 | 없음 |
| `AaService` MediaBrowser 캐시 | 없음 |
| `DeviceListActivity` 우측 패널 | "Track List (coming soon)" placeholder |

## Protocol (proto에서 추론)

채널 12 (mediabrowser).

Inbound (Phone → HU):
- `MEDIA_ROOT_NODE` (32769) — `{ path, sources: [MediaSource] }`
- `MEDIA_SOURCE_NODE` (32770) — `{ source, start, total, lists: [MediaList] }`
- `MEDIA_LIST_NODE` (32771) — `{ list, start, total, songs: [MediaSong] }`
- `MEDIA_SONG_NODE` (32772) — `{ song, album_art, duration_seconds }`

Outbound (HU → Phone):
- `MEDIA_GET_NODE` (32773) — `{ path, start?, get_album_art? = true }`
- `MEDIA_BROWSE_INPUT` (32774) — `{ input: InstrumentClusterInput, path }`

플로우:
```
HU --MEDIA_GET_NODE { path: "" }-------------------> Phone
HU <--MEDIA_ROOT_NODE { sources: [...] }------------ Phone

HU --MEDIA_GET_NODE { path: "<source-path>", start: 0 }--> Phone
HU <--MEDIA_SOURCE_NODE { lists: [...], start, total }---- Phone

HU --MEDIA_GET_NODE { path: "<list-path>", start: 0 }----> Phone
HU <--MEDIA_LIST_NODE { songs: [...], start, total }----- Phone

HU --MEDIA_BROWSE_INPUT { input: ENTER, path: "<song-path>" }--> Phone
HU <--PLAYBACK_STATUS / METADATA (자동 재생 시작)----------------- Phone
```

## Day 1 — passive learning (이번 라운드)

목표: 폰이 어떻게 응답하는지 raw 관찰. UI는 손대지 않음.

### Files to touch

- `core/include/aauto/service/MediaBrowserService.hpp` — 콜백 typedef
  4개 + `set_*_callback` 4개 + `request_node(path, start, get_art)` +
  `browse_input(path, input_type)` public API
- `core/src/service/MediaBrowserService.cpp` — 4개 inbound 핸들러 등록,
  parse + AA_LOG_I + invoke callback. 두 개 outbound public method —
  proto 빌드 + send_message로 송신.
- `impl/android/main/main.cpp` — `services[12] = std::make_shared<MediaBrowserService>(send_fn);`
  콜백은 일단 nullptr (Day 2에서 wire). G.0 rule 충족(handler 등록만으로
  passive 가치 확보).

### Learning checklist (Day 1 끝나고 기록)

- [ ] 채널 open되는가 (ServiceDiscoveryResponse에 advertise만으로 열림?
      혹은 추가 capability 필드 필요?)
- [ ] root GET_NODE 보내면 폰이 응답하는가
- [ ] root에 어떤 source가 보이는가 (Spotify, YT Music, 폰 로컬 뮤직, ...)
- [ ] source 진입 → list 응답 오는가
- [ ] list 진입 → song 응답 오는가
- [ ] album_art는 단계마다 따라오는가 (root에서 source의 art? source
      단계에서 list의 art?)
- [ ] start/total 페이지네이션은 어디부터 발생하는가
- [ ] G.0 throttle 영향 (스크롤 lag) — passive handler만으로 ok인가

### Day 1 종료 기준

- 실기기 SM-N981N에서 root → source → list → song까지 4단계 모두 응답
  관찰. logcat에 명확한 트리 dump.
- 위 checklist 항목 답이 docs/troubleshooting.md 또는 docs/aap_messages.md에
  기록됨.

## Day 2 — AIDL → Java chain (다음 라운드)

목표: 관찰된 데이터를 Java에 노출. PlaybackMetadata 패턴 그대로.

- `IAAEngineCallback.aidl` — `oneway void onMediaBrowserNode(int sessionId,
  int nodeType, byte[] protoBytes)` (단일 콜백 + type discriminator로
  AIDL surface 4중복 방지). 또는 4개 분리. Day 1 결과 보고 결정.
- `IAAEngine.aidl` — `oneway void requestMediaBrowserNode(int sessionId,
  String path, int start, boolean getAlbumArt)` + `oneway void browseInput(...)`
- `AidlEngineController` — proxy
- `AaService` — `MediaBrowserState` per-session HashMap. cache root +
  현재 navigated path stack
- `getMediaBrowser(sid)` public method

### Day 2 종료 기준

- AaService에 root node 캐시됨, deep navigation API 동작.
- 단위 테스트(가능하면) 또는 logcat dump로 데이터 chain 검증.

## Day 3 — UI (다음 라운드)

목표: 우측 placeholder를 navigable list로 교체. 곡 클릭 → 재생.

- `DeviceListActivity` 우측 패널 재작성 — `ListView` (RecyclerView는
  현재 코드베이스가 plain Activity API 위주라 ListView가 일관됨)
- breadcrumb 또는 back button으로 트리 navigation
- 각 row: optional album_art thumbnail + name + chevron
- Source/List 클릭 → `aaService.requestMediaBrowserNode(path)` 후 응답
  대기 → 다음 레벨 표시
- Song 클릭 → `aaService.browseInputEnter(path)` (BROWSE_INPUT) → 폰이
  PLAYBACK_STATUS 보내면 미디어 카드가 자동 갱신

### UI 구성 (right panel)

```
┌─ Track List ──────────────┐
│ ◀ Spotify > Liked Songs   │  ← breadcrumb
├───────────────────────────┤
│ [art] Song A              │
│ [art] Song B              │
│ [art] Song C              │  ← scrollable
│ ...                       │
└───────────────────────────┘
```

### Day 3 종료 기준

- 곡 선택 → 폰이 그 곡 재생 시작 → 미디어 카드가 새 곡으로 갱신.
  시연 가능.

## Day 4 (선택) — quirk 비교

- Spotify ↔ YT Music ↔ 폰 로컬 뮤직 비교
- 각각 어떤 source 단계에서 무엇을 expose하지 않는지 / album_art 정책
- F.10 QuirksProfile 카탈로그의 첫 항목으로 기록

## Risks

1. **G.0 throttle 재발** — register_handler 0개로 advertise만 하면
   troubleshooting #22 재발. **Day 1에서 반드시 4개 핸들러 모두 등록한
   채로 advertise**.
2. **`mutable_media_browser_service()`만으로 부족할 가능성** — Service.proto의
   MediaBrowserService 메시지가 빈 message{}여서 옵션 필드 없음. 폰이 추가
   capability 필드를 기대해서 NACK할 수 있음. Day 1 첫 1시간 내에 판가름.
3. **album_art 트래픽** — root만 받아도 source 5~10개 × 5~50KB. `get_album_art=false`
   기본으로 시작하고 화면에 보일 때만 재요청. Day 3에서 처리.
4. **YT Music browse 차단** — 2024년 즈음 차량용 browse API 깐깐해진 사례.
   Spotify 우선 검증, YT Music은 quirk로 기록.
5. **페이지네이션 누락** — list 안에 곡 1000개 같은 경우. start/total
   기록하고 lazy-load. Day 2에서 chunked load 패턴 확립.

## Verification

- Day 1: 실기기에서 4단계 트리 응답 logcat 확인. checklist 답 채워짐.
- Day 2: getMediaBrowser API에서 캐시 dump 동작 확인.
- Day 3: 곡 클릭 → 재생 시연.
- Day 4 (선택): Spotify/YT Music/Local 비교 표 작성.

## 작업량 추정

| Day | 작업 | 추정 |
|-----|------|------|
| 1 | core service handler + main.cpp 등록 + 실기기 관찰 | 4~6h |
| 2 | AIDL → Java 데이터 chain | 4~6h |
| 3 | UI navigable list + 곡 선택 → 재생 | 6~8h |
| 4 (선택) | quirk 비교 | 2~4h |
| **총** | | **14~20h** |

각 Day별 별도 commit 권장. Day 1만으로도 학습 산출물 1차 회수.

---

## 2026-04-29 재투자 — 결론: legacy 채널 deprecated

User local에 gearhead.apk + gmscore base.apk decompile (plan 0007 작업의
부산물)이 준비된 상태에서 ch12 거부 root cause 재조사. 동시에 폰에
Spotify 설치하고 재테스트.

### 가설 검증 흐름

**가설 A — phone에 car-compatible MediaBrowser app 없음**:

- 이전 테스트 환경: YT Music만 설치
- YT Music dumpsys 검사: `com.google.android.gms.car.application` 메타데이터
  grep hit 없음 (단 dumpsys 출력 한계)
- 다른 음악 앱 (Spotify/Pocket Casts/Audible/TuneIn/Pandora) 0개 설치 확인
  (`adb shell pm list packages | grep ...`)
- → "car-compat 앱 부재가 ch12 안 여는 직접 원인" 가설로 진입

**검증 — Spotify 설치 후 재테스트**:

Spotify 설치 후 dumpsys 확인:
```
android.media.browse.MediaBrowserService: SpotifyMediaBrowserService,
                                          SpotifyMediaLibraryService
androidx.car.app.media.MEDIA_SHOW_PLAYBACK_VIEW: ...
com.spotify.carapplibrary.androidauto.AndroidAutoService
```

Spotify는 다음 둘 다 가짐:
- legacy `android.media.browse.MediaBrowserService` action
- modern `androidx.car.app` library (Car App Library)

main.cpp에 `services[12] = MediaBrowserService` 다시 등록 + Day 1 hook
(auto-request root on channel_open) 재추가 + 빌드 + 실기 테스트.

**관찰된 logcat (요약)**:

```
[AAP TX] control SERVICE_DISCOVERY_RESP 229 bytes
  ← MediaBrowser advertise 정상 ("media browser service configured" log)

[AAP RX] CHANNEL_OPEN_REQ 8개:
  video, audio.media, audio.guidance, audio.system,
  input, sensor, microphone, media.playback
  ← media.browser CHANNEL_OPEN_REQ **여전히 안 옴**

이후 PLAYBACK_STATUS source가 "Samsung Music" → "Spotify"로 전환
  ← Spotify가 phone-side에서 실제 동작 중인데도 ch12 안 열림
```

**가설 A 폐기**: Spotify가 깔려있고 active이지만 phone은 ch12 안 엶.

### 새 가설 — legacy AAP MediaBrowser 채널 deprecated

**관찰**:
- 다른 모든 advertise 채널은 정상 open → HU identity 게이팅 아님
- Spotify의 `androidx.car.app` (Car App Library) action 존재
- AAP wire layer는 dynamic-loaded module에 있음 (plan 0007 결론)

**가설**: Modern Android Auto는 미디어 brows를 다음 모델로 바꿈:
1. 앱이 `androidx.car.app.CarAppService`(Car App Library) 구현
2. 폰이 그 앱의 browse UI 자체를 렌더링
3. 폰이 그 렌더된 UI를 video sink (ch1)로 HU에 projecting
4. HU는 그냥 비디오로 표시 → 사용자가 그 안에서 browse
5. **별도 ch12 MediaBrowser 채널 사용 안 함**

웹 검증 (Android developer docs):
> "If a user has an older version of Android Auto installed or cannot
> use the Car App Library version, the host will fall back to the
> MediaBrowserService implementation."

→ 우리 가설 confirmed. 모던 AA는 Car App Library 우선, legacy
MediaBrowser는 fallback only. 폰이 Car App Library 경로로 가면 HU
측 ch12 advertise는 무시.

**최종 결론**:

- 학습 목표 "ch12로 곡 리스트 받기"는 **modern Android Auto 환경에서
  achievable하지 않음**.
- 곡 리스트는 phone-rendered UI 일부로 video sink (ch1)에 픽셀로
  projecting되는 게 표준. HU 측에서 *데이터로* 추출 불가.
- legacy ch12는 deprecated path — older AA 버전이나 Car App Library
  미사용 앱에서만 fallback으로 활성. Spotify/YT Music 등 주요 앱은
  이미 Car App Library로 마이그레이션.

### 코드/문서 정리

- `services[12]` 등록 제거 (다시 disabled)
- `MediaBrowserService::on_channel_open` Day 1 hook 제거
- `MediaBrowserService.hpp` header 상태 update — DEPRECATED-IN-MODERN-AA
- service 코드 자체는 tree에 유지 (older AA / 미마이그레이션 앱
  fallback 시 재활성 가능)

### 학습 산출물

목표 미달성이지만 학습 가치 있는 사실 확인:

1. AAP의 legacy 채널이 modern Android Auto에서 deprecated됨을 정량적
   확인 — 다른 채널 모두 열리는데 ch12만 안 열린다는 결정적 증거
2. Modern Android Auto의 미디어 모델 = phone-rendered UI projection
   via video sink. HU는 "dumb display" 역할
3. F.20 (KEYCODE) 결정의 전략적 의미 추가 정당화 — legacy AAP는
   변동성 높은 영역, KEYCODE는 OS 표준이라 안정적
4. AAP 포팅 학습 측면 — modern AAP를 단순히 "channels open + protocol"
   로 보면 안 되고, 어느 채널이 deprecated인지 인지 필수
5. `androidx.car.app.CarAppService` 패턴이 다음 학습 후보 — phone-side
   wire 분석 시 video 채널 안의 input/event 흐름 (touch, key) 관찰
   가치 큼

### 미래 trigger (재오픈 조건)

- Older Android Auto 버전(예: 5.x 이하)에서 동작 검증 필요 시
- Car App Library 미사용 + MediaBrowserService 단독 앱(예: 일부 podcast
  앱)에서 ch12가 활성화되는지 직접 확인 시
- Google이 fallback 자체를 제거하는 발표가 나오면 본 결론 closed
