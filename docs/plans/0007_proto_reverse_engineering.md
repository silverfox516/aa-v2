# 0007 — AAP proto reverse engineering (Android Auto APK)

> Created: 2026-04-29
> Status: **EXECUTED + ON_HOLD** — gearhead/gmscore static analysis 완료,
> 원래 목적(PLAYBACK_INPUT 추가 필드 발견) 달성 불가로 확인. 부수 학습
> 산출물 다수 (아래 "Findings" 섹션).
> Related: F.20 (KEYCODE 결정 — 본 plan 결과로 PLAYBACK_INPUT 직접 사용
> 가능성 재평가), G.1 (MediaBrowser ON_HOLD), 0006 (Cluster ON_HOLD)

## Findings (2026-04-29)

User local에서 APK pull + jadx decompile + collaborative analysis 진행한
결과:

### 추출 자산

| APK | 버전 | 크기 | Decompiled | Java 파일 수 |
|-----|------|------|------------|-------------|
| com.google.android.projection.gearhead | 16.6.661444 | 33.6 MB | 26 GB | 25,602 |
| com.google.android.gms (base.apk only) | (logged) | ~199 MB | (large) | 139,066+ |
| splits | 10개 (Maps/Cronet/Ads/Measurement/Dynamite A,C 등) | (미추출) | (skipped) | - |

### 검색 결과 (모두 negative)

1. `\b32770\b` in gearhead/sources: 10 hit, 모두 다른 채널 컨텍스트
   (Input KeyBinding, Sensor, CarControl, CarLocalMedia)
2. `MediaPlayback` string in gearhead: 1 hit (`yhj.java:395`) — gms.car
   API surface ID 상수만, proto 정의 아님
3. `.k(32769,` / `.k(32771,` (outbound STATUS/METADATA send): 0 hit —
   gearhead가 phone→HU MediaPlayback를 송신하는 코드 없음
4. gmscore base.apk `com/google/android/gms/`: car/gal/gearhead/auto
   디렉토리 자체가 없음
5. gmscore `MediaPlayback` / `aap_protobuf` / `32770` 검색: 0 hit

### inx 서브클래스 전체 매핑 (AAP 채널 핸들러 base class)

| 클래스 | service type | 정체 | 비고 |
|--------|-------------|------|------|
| imh.java | 1 | (제어, msg 65535) | InputControl으로 추정 |
| ino.java | 10 | **NavigationStatus** | INSTRUMENT_CLUSTER_START/STOP/STATE 등 처리. **별도 작업 시 재참조 가치 큼** |
| hyi.java | 16 | (미상) | 32769~32772 케이스 없음 |
| ilb.java | 19 | CarControl | CAR_CONTROL_GET/SET_PROPERTY (vehicle HVAC, etc) |
| ilg.java | 20 | CarLocalMedia | HU의 로컬 미디어 (radio/USB), AAP MediaPlayback 아님 |

**MediaPlayback 핸들러는 gearhead.apk inx 서브클래스 중에 존재하지 않는다.**

### 가능한 원인 (미검증)

1. **Dynamic loaded module**: AAP 코드가 런타임에 Google CDN에서 다운로드되어
   메모리에만 존재. 정적 디스크 분석으로 추출 불가.
2. **MediaSession bridge**: 모던 Android Auto는 phone의 currently-playing
   미디어를 MediaSession으로 노출 → AAP 레이어가 그걸 wire format으로
   bridge만 하고, 별도 inx 핸들러 없음. PlaybackInput receive도 MediaSession
   TransportControl 호출로 직결.
3. **Module B 미설치**: 폰의 `pm path com.google.android.gms` 결과에
   `split_DynamiteModulesB.apk` 부재. AAP 모듈이 별도 split이라면 그 안에.

### 원래 목표 평가

**(A) "PLAYBACK_INPUT 추가 필드 발견" 달성 불가.**

- gearhead.apk + gmscore base.apk static analysis 양쪽 모두 negative
- 추가 작업 비용 (split B 추적, 또는 wire 캡처 + protobuf-inspector
  역방향 분석)이 본 학습 프로젝트 budget 대비 큼
- milek7의 `MediaPlaybackInput { InstrumentClusterInput input }`이 사실상
  공개 자료로 얻을 수 있는 최대치로 보임 — Android Auto가 실제 wire에서
  보내는 PlaybackInput은 MediaSession의 directional input이 주류일 가능성
- 만약 Google이 seek_to_position 같은 추가 필드를 내부적으로 가지고
  있더라도 wire에서 관찰되지 않는 한 우리가 사용 못 함 (의미 매핑이
  HU/phone 양측 합의 필요)

### 부수 학습 산출물

이 시도에서 *목표와 별개로* 얻은 것:

1. **AAP 코드 packaging 구조**: gearhead.apk는 UI/UX 레이어, 실제 wire는
   gmscore의 dynamic module. 분석 비용을 미리 알고 들어가면 효율적.
2. **inx 패턴**: AAP 채널 핸들러의 abstract base class 구조 + 5개 실제
   서브클래스 매핑 → 향후 다른 채널(Navigation, CarControl) 학습 시 직접
   참조 가능.
3. **`ino.java` (NavigationStatus)**: turn-by-turn 메시지 wire 형태와
   handler 구조의 직접 reference. 우리 NavigationStatusService 구현 시
   activator로 사용 가능.
4. **F.20 결정 추가 정당화**: KEYCODE_MEDIA_* 경로는 reverse engineering
   불가능 영역에 의존하지 않음 — 본 시도가 실패함으로써 결정의 robustness
   재확인.
5. **공개 reverse engineering의 한계 정량적 이해**: ProGuard + dynamic
   loading + split APK = static analysis로 도달 불가 영역 존재. 미래
   보다 정교한 reverse engineering 시도(wire 캡처 등) 시 사전 정보로
   유용.

### 미래 trigger

본 plan 재오픈 조건:
- Wire 캡처 환경 구축 가능 시 (transport-level sniffing tool)
- 다른 폰 모델에서 PlaybackInput 파싱 실패 등 이상 동작 관찰 시
- F.20에서 KEYCODE 경로로 해결 안 되는 미디어 제어 케이스 발견 시
- Module B 또는 다른 split APK에 AAP 코드가 packaged된 폰 모델 발견 시

## Goal

milek7의 공개 reverse-engineered .proto에 누락된 필드들을 Google Android
Auto APK에서 직접 추출. 학습 가치:

- Wire format level의 protocol 분석 능력 — AAP 포팅 시 진짜 필요한
  스킬
- ID 32770 (MediaPlaybackInput)의 실제 필드 구조 확보 → seek 등의
  dedicated 명령 가능 여부 판단
- ID 외 다른 미스터리 필드들 (예: cluster sink 거부 원인 — HeadunitInfo
  의 필수 필드?) 조사
- protobuf wire format은 field name이 wire에 안 들어감 — field number
  + type만 알면 폰이 알아들음. 우리가 만든 .proto는 field name이 임의
  여도 무방. 실제로는 number/type/semantic 매핑이 핵심

## Approach

### Step 1 — APK 확보 (user local)

옵션 둘:

**(a) 폰에서 직접 추출** (가장 정확 — 우리가 테스트 중인 폰의 실제
버전):
```bash
# 1. USB 디버깅 활성화된 폰 연결
adb shell pm list packages -f | grep com.google.android.projection.gearhead
# 결과 예: package:/data/app/com.google.android.projection.gearhead-XX/base.apk=com.google.android.projection.gearhead

# 2. APK pull
adb pull /data/app/com.google.android.projection.gearhead-XX/base.apk gearhead.apk
```

**(b) APKMirror 같은 미러 사이트** — 최신 stable 다운로드. 실제 폰
버전과 다를 수 있음 (검증 폰 SM-N981N의 Android Auto 버전 우선).

### Step 2 — Decompile (user local)

```bash
# jadx 설치 (없으면)
# Linux/Mac: 패키지 매니저 또는 https://github.com/skylot/jadx/releases
# Windows: scoop install jadx 또는 binary 다운로드

# 명령행 변환 — Java source로 추출
jadx --output-dir gearhead-decompiled gearhead.apk

# 결과: gearhead-decompiled/sources/com/google/...
```

대안: GUI 버전 `jadx-gui gearhead.apk` 후 검색이 더 편할 수 있음.

### Step 3 — MediaPlaybackInput 코드 위치 찾기 (협업)

Android Auto APK는 Java 코드 + protobuf nano가 흔함. 검색 패턴:

```bash
cd gearhead-decompiled

# 32770 상수 검색 (10진수 또는 0x8002 16진수)
grep -rn "32770\|0x8002" sources/ | head -20

# protobuf nano 패턴 — 보통 nano runtime이 generated code에서 다음 형태:
#   public static final int FIELD_NAME_FIELD_NUMBER = N;
#   public T fieldName_;
# Or new abstract messageNano subclasses
grep -rn "extends.*MessageNano\|extends.*GeneratedMessage" sources/ | head -20

# tag computation 패턴: (field_number << 3) | wire_type
# field 1 varint = 0x08 (1<<3 | 0)
# field 2 varint = 0x10
# field 3 varint = 0x18
# field 4 varint = 0x20
# field 5 varint = 0x28
# field 1 length-delimited (string/bytes/embedded msg) = 0x0a
grep -rn "writeUInt32\|writeInt64\|writeMessage" sources/ | grep -i "playback\|media" | head
```

### Step 4 — 결과 해석 (협업)

User가 위 grep 결과 또는 jadx-gui에서 찾은 클래스 파일을 paste하면
같이 분석. 일반적으로 이런 형태가 나옴:

```java
// jadx output 예시 (실제 클래스 이름은 obfuscated일 수 있음)
public final class MediaPlaybackInputProto {
    public static final class MediaPlaybackInput extends MessageNano {
        public InstrumentClusterInput input;       // field 1
        public Long seekToPositionMs;              // field 2 — 가설
        public Integer audioFocusState;            // field 3 — 가설

        public int getSerializedSize() {
            int size = 0;
            if (this.input != null) {
                size += CodedOutputByteBufferNano.computeMessageSize(1, this.input);
            }
            if (this.seekToPositionMs != null) {
                size += CodedOutputByteBufferNano.computeInt64Size(2, this.seekToPositionMs.longValue());
            }
            // ...
        }
    }
}
```

핵심 정보 추출:
- field number (computeXxxSize의 첫 인자)
- wire type (computeInt64 → varint(0), computeMessage → LEN(2), 등)
- semantic (변수 이름이 아니라 호출 site에서 추론 — 예: seekTo() 함수가 이 setter 호출하면 seek 의도)

obfuscated된 경우 (`a`, `b`, `c` 같은 이름) — 더 어려움. setter 호출
site의 컨텍스트 (UI handler, Intent action 처리 등) 따라가야 함.

### Step 5 — 결과 docs에 반영

조사 완료된 필드들을 `docs/aap_messages.md` 에 기록:

```markdown
## Media Playback Input (ch10, msg 32770) — 추출됨 (2026-04-XX)

| Field# | Type | Semantic | 출처 |
|--------|------|----------|------|
| 1 | InstrumentClusterInput | directional input | milek7 + APK 검증 |
| 2 | int64 | seek_to_position_ms | APK 추출 |
| 3 | (TBD) | (TBD) | APK 추출 |
```

생성된 .proto는 `protobuf/aap_protobuf/service/mediaplayback/message/MediaPlaybackInput.proto`로
신규 생성:

```proto
syntax = "proto2";
import "aap_protobuf/shared/InstrumentClusterInput.proto";
package aap_protobuf.service.mediaplayback.message;

message MediaPlaybackInput {
    optional shared.InstrumentClusterInput input = 1;
    optional int64 seek_to_position_ms = 2;
    // ... 발견된 추가 필드들
}
```

field name은 우리 임의 — wire에는 number만 들어감. 그러나 미래 reader가
편하도록 추출 출처 주석 명시.

## 추가 조사 후보 (optional, 시간 여유 시)

같은 기법으로 다른 미스터리 풀기:

1. **HeadunitInfo 모든 필드** — cluster sink 거부 원인 후보
   (`com.google.android.projection.gearhead`가 SDR validation할 때 어떤
   필드를 검사하는지)
2. **MediaSinkService.display_type=CLUSTER 추가 필수 필드** — 단일
   sink Cluster도 거부됐던 원인
3. **AudioFocusRequest의 모든 enum 값** — 우리가 unsolicited GAIN을
   non-standard로 보내는 게 정확한 패턴인지
4. **MediaBrowserService — 폰이 ch12 안 여는 원인**

## 작업 흐름 (협업)

1. User: APK 추출 + decompile 후 `docs/research/0007/` 디렉토리에 결과
   드롭 (또는 chat에 paste)
2. Claude: smali/Java 코드 분석, field number/type/semantic 매핑 추출
3. User: 필요시 추가 grep/검색 결과 제공
4. Claude: 추출 결과를 .proto 파일 + docs/aap_messages.md에 정리,
   docs/protocol.md에 wire format 노트
5. F.20 등 결정 재검토 — 직접 PLAYBACK_INPUT 사용 가능성 평가

## 작업량 추정

| 단계 | 시간 | 누가 |
|------|------|------|
| Step 1 (APK pull) | 5~10min | User |
| Step 2 (jadx decompile) | 10~30min | User |
| Step 3 (search & extract) | 1~2h | 협업 |
| Step 4 (interpret + map) | 1~3h | 협업 (코드 양 따라) |
| Step 5 (docs 정리) | 30min~1h | Claude |
| **총** | **~3~6h** | |

obfuscation이 심하면 시간 더 들 수 있음. 보통 google nano가 setter 패턴
유지해서 추적은 가능.

## Risks / 주의점

1. **APK 라이선스** — Android Auto는 Google 사유 코드. 분석은 학습
   목적이라 일반적으로 합법이지만 reverse-engineered 코드를 그대로 공개
   레포에 commit하면 안 됨. 우리 .proto는 *재구성된* 정의라 OK (interface
   reverse engineering은 미국 fair use 인정). 본 프로젝트가 비공개라
   문제 없을 듯하지만 push 정책에 유의.
2. **버전 디펜던시** — APK 버전마다 필드 추가/제거 가능. 추출 시 APK
   version 명시.
3. **micro-protobuf 메타데이터 stripping** — Google이 빌드 시 일부
   metadata를 제거. 그래도 nano runtime의 read/write 로직 자체는
   일반 Java 코드라 분석 가능.

## Verification

추출된 필드의 정확성 검증:
- (선택) wire 캡처 시도 — 폰이 실제로 우리 추출 결과와 같은 형태로
  메시지를 송신하는지 확인 (어렵지만 가장 강한 증거)
- 일관성 — 같은 필드를 다른 setter 호출 site에서 같은 의미로 쓰는지
- 인접 필드 (예: position 받는 setter 옆에 set_action(SEEK_TO) 같은
  enum) 같이 묶여 호출되는지
