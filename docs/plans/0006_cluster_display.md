# 0006 — Cluster display (PIP overlay)

> Created: 2026-04-28
> Status: PLANNING
> Related decisions: F.20 (재생 명령 채널 분리), G.0 (passive handler rule),
> 0004 (multi-session pattern 참고)

## Goal

폰의 cluster display sink를 받아서 메인 video 위에 작은 반투명 PIP로
띄운다. 학습 가치:

- AAP 멀티 디스플레이 패턴 학습 (단일 sink만 다루는 데모 수준 → 진짜
  자동차 H/U 패턴)
- 같은 service 클래스(VideoService)를 다른 channel + 다른 display_type
  으로 두 번 인스턴스화 — service 추상화의 재사용성 확인
- TCC803x SoC 듀얼 H.264 디코더 동시성 검증 — 실제 포팅 시 hw 한계
  체크 자료
- 폰이 cluster sink에 어떤 컨텐츠를 보내는지 raw 관찰 (앱별 다름이 예상됨)

## Current state (2026-04-28)

| 자산 | 상태 |
|------|------|
| `VideoService` | 단일 인스턴스, ch1 = MAIN. `display_type` 미설정 (proto2 default = MAIN) |
| `MediaSinkService` proto | `display_type` field 정의 있음 (MAIN/CLUSTER/AUXILIARY) |
| `IEngineCallback.on_video_data` | `(session_id, data, size, ts, is_config)` — channel 정보 없음 |
| `AaService.onVideoData` AIDL | 같음 — channel 정보 없음 |
| `VideoDecoder` Java | 단일 인스턴스, `AaDisplayActivity`의 SurfaceView로 출력 |
| `AaDisplayActivity` | SurfaceView 1개 (메인 video), FrameLayout 아님 |

## Architecture

채널 ch15 = cluster video. 본 plan에서는 MAIN(ch1) + CLUSTER(ch15) 두
sink advertise.

```
HU SERVICE_DISCOVERY_RESPONSE
├── id=1  media_sink_service { codec=H264_BP, video_configs=[1280x720@30], display_type=MAIN }
└── id=15 media_sink_service { codec=H264_BP, video_configs=[480x270@20],  display_type=CLUSTER }
                                                                            ↑ 작은 해상도/저fps

Phone → HU
├── ch1  MEDIA_DATA frames (메인: Maps/Spotify 풀 UI)
└── ch15 MEDIA_DATA frames (cluster: turn-by-turn / 곡명 미니 카드)
```

HU 내부:

```
Native (per-channel routing):
  VideoService(ch=1, display=MAIN)    → IEngineCallback.onVideoData(sid, ch=1, ...)
  VideoService(ch=15, display=CLUSTER) → IEngineCallback.onVideoData(sid, ch=15, ...)

AIDL callback 시그니처 확장:
  onVideoData(int sessionId, int channel, byte[] data, long ts, boolean isConfig)

Java AaService:
  if (channel == MAIN_VIDEO_CH)    videoDecoderMain.feedData(...)
  if (channel == CLUSTER_VIDEO_CH) videoDecoderCluster.feedData(...)

AaDisplayActivity:
  FrameLayout
  ├── SurfaceView   (메인, 풀스크린, opaque)         → videoDecoderMain
  └── TextureView   (320x180, top-left, alpha 0.7)  → videoDecoderCluster
       - 첫 프레임 dequeue 시 fade-in
       - MEDIA_STOP / 3초 inactivity 시 fade-out
```

## Files to touch (대략)

| 파일 | 변경 |
|------|------|
| `core/include/aauto/service/VideoService.hpp` | 생성자에 `channel_id`, `display_type`, `VideoConfig` 추가 |
| `core/src/service/VideoService.cpp` | `fill_config()`에서 `set_display_type()` 호출 |
| `core/include/aauto/engine/IEngineController.hpp` | `IEngineCallback::on_video_data`에 `channel` 인자 |
| `core/src/engine/Engine.cpp` | callback 호출부 channel 전달 |
| `core/src/service/VideoService.cpp` | `on_data_received()` callback이 channel 같이 |
| `impl/android/main/main.cpp` | `services[15]` 등록, 콜백 lambda channel 전달 |
| `impl/android/aidl/AidlEngineController.{hpp,cpp}` | `on_video_data` channel 인자 |
| `aidl/IAAEngineCallback.aidl` | `oneway void onVideoData(int sessionId, int channel, ...)` |
| `app/android/.../AaService.java` | callback에 channel 받아 라우팅, dual decoder 관리 |
| `app/android/.../VideoDecoder.java` | (확인 후) 채널/Surface/config 인스턴스화 가능하면 변경 없음 |
| `app/android/.../AaDisplayActivity.java` | FrameLayout으로 변경, TextureView 추가, fade 애니메이션 |

## Risks

1. **TCC803x 듀얼 H.264 디코더 동시성** — 가장 큰 위험. SoC vendor spec
   확인 필요. 안 되면 학습 가치는 같으나 실제 시연 안 됨. **Day 0 spike**로
   판가름.
2. **폰이 ch15 sink를 안 열 가능성** — MediaBrowser ch12와 같은 패턴
   가능성. 모든 폰이 cluster를 advertise한 모든 H/U에 보내는 건 아님.
   Spotify/YT Music이 보통 어떻게 다루는지 미확인.
3. **TextureView alpha 합성 비용** — 320x180 정도면 무시할 수준. 더
   크게 하면 GPU 부담 증가.
4. **inactivity detector vs frame buffering** — 디코더에 frame이 흘러
   가지만 alpha 0인 동안엔 화면에 안 보임. data flow와 visibility를
   별도 state machine으로 분리.
5. **AIDL 시그니처 변경 파급** — `onVideoData`에 channel 추가하면 모든
   호출부 update 필요. 기존 동작은 channel=1 기본값으로 호환.

## Day breakdown

### Day 0 — Spike: TCC803x 듀얼 디코더 동시성 검증 (skipped)

원래 계획은 임시 Activity로 dummy MediaCodec 두 인스턴스를 띄워 미리
검증하는 spike였으나 skip. 이유:
- Day 1/2는 native side만 변경 (Java 디코더 1개 그대로) → spike 결과와
  무관하게 의미 있는 학습 산출
- Day 3가 자연스럽게 듀얼 디코더 시도 — 그때 실패해도 본 plan의 Day
  1/2/(Day 2 결과) 자체가 학습 자료로 commit됨
- 공이 적게 들고, 진짜 사용 컨텍스트에서 검증되는 게 spike보다 신호
  강함

대신 Day 3 진입 시 첫 30분에 듀얼 `MediaCodec.createDecoderByType` 호출
가능 여부 확인 — 실패 시 plan 종료, 학습은 Day 1/2까지로 확정.

### Day 1 — VideoService 인자화 + display_type=MAIN 명시 (3~4h)

목표: cluster sink 추가 전에 코드 일반화. 동작 변화 없음.

- VideoService 생성자가 `channel_id`, `display_type`, `VideoConfig` 받음
- main.cpp는 `services[1] = VideoService(1, MAIN, 1280x720@30)` — 동작
  동일
- `IEngineCallback::on_video_data`에 channel 인자 추가
- AIDL `onVideoData`에 channel 추가, 모든 caller channel=1 또는 channel
  하드코드
- **검증**: 빌드 + 실기에서 메인 video 정상 흐름 (regression 없음)
- 별도 commit

### Day 2 — Cluster sink 등록 + native 측 routing (3~4h)

목표: ch15에 cluster VideoService 등록, native단에서 데이터 흐름 관찰.

- main.cpp `services[15] = VideoService(15, CLUSTER, 480x270@20)`
- 콜백 lambda가 channel 같이 흘림
- 일단 Java측에서는 channel==15면 drop (UI 안 만듦)
- **검증**: 실기에서 폰이 ch15 CHANNEL_OPEN_REQ 보내는지, MEDIA_DATA가
  실제 흐르는지 logcat 확인. 흐르면 → Day 3, 안 흐르면 → MediaBrowser와
  같은 ON_HOLD 분기 (학습 산출물은 챙김)
- 별도 commit (성공/실패 둘 다 의미 있음)

### Day 3 — Java 듀얼 디코더 + cluster TextureView (5~7h)

목표: 두 번째 디코더 + cluster surface로 실제 PIP 표시.

- AaService가 channel별 VideoDecoder 인스턴스 관리
- AaDisplayActivity FrameLayout으로 재구성, TextureView 추가 (320x180,
  top-left margin 24px, alpha 0)
- TextureView SurfaceTextureListener에서 cluster decoder에 Surface 연결
- 첫 frame dequeue → `view.animate().alpha(0.7f).setDuration(300)`
- **검증**: cluster sub-window가 메인 video 위에 fade-in되어 표시됨

### Day 4 — fade-out + inactivity detector (2~3h)

- MEDIA_STOP 수신 시 fade-out
- 3초간 frame 미도착 시 fade-out (timer reset on each frame)
- frame 다시 들어오면 fade-in
- **검증**: 폰에서 cluster 컨텐츠 트리거(예: 내비 시작) ↔ 정지 토글 시
  자연스럽게 나타났다 사라짐

### Day 5 (선택) — 학습 정리 (1h)

- F.21 결정 등록 (멀티 sink 패턴, channel-aware callback 패턴)
- aap_messages.md ch15 row 추가
- troubleshooting.md에 SoC duplex decode 결과 기록
- 폰별 cluster 컨텐츠 차이 quirk 카탈로그 (있으면)

## Verification (per Day)

각 Day 별 별도 commit + 별도 디바이스 검증.

- Day 0: 듀얼 디코더 PASS/FAIL 명확. 본 plan에 결과 기록 후 commit
- Day 1: 메인 video regression 없음 (cluster 추가 전)
- Day 2: ch15 데이터 흐름 logcat dump 보존. 폰 동작 다르면 plan 수정
- Day 3: cluster sub-window 시각 확인 (스크린샷이나 영상)
- Day 4: 나타남↔사라짐 자연스러움 시연

## 작업량 합계

| Day | 시간 |
|-----|------|
| 0 (spike) | 1~2h |
| 1 | 3~4h |
| 2 | 3~4h |
| 3 | 5~7h |
| 4 | 2~3h |
| 5 (선택) | 1h |
| **총** | **15~21h** |

각 Day는 독립 commit. Day 0 결과에 따라 plan 자체가 변경될 수 있음.
