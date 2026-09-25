# 오디오 계획 (기존 엔진 재검토 뒤 재설계)

> 계약은 `docs/ProjectRule.md`, 결정은 `tasks/todo.md` Decisions 다. 이 문서는 그 둘을 향해 가는 순서와
> 상태를 적는다. 상태는 항목마다 `[완료]` `[진행]` `[제안]` `[가정]` `[열림]` 으로 붙인다.
> `[제안]` 은 **사용자 확인 전**이다. 2026-09-25 에 §0 의 방향(직접 믹서·공용 소스와 차원별 리스너·새 Tier S 모듈)이
> 확인돼 D-197 이 됐다. 같은 날 남은 셋(임포트 옵션은 `Mode` 만, 모듈 이름은 관례대로, 보이스 64)도 정해졌고
> 기존 자료의 이식은 하지 않기로 했다(이 엔진으로 만든 콘텐츠가 없다). 아직 코드는 없다.

## 0. 확정된 방향 (2026-09-25, D-197)

1. **믹싱은 우리 코드가 한다.** miniaudio 의 `ma_engine`·노드 그래프를 쓰지 않는다. 디코더·리샘플러·spatializer·필터 같은
   **부품만** miniaudio 에서 가져다 쓴다(§2.3). 기존 엔진이 겪은 수명·경쟁·재배선 문제(§1.3)는 `ma_engine` 이 객체와 스레드를
   쥔 데서 나왔다.
2. **보이스와 버스는 믹서가 소유하고, 밖으로는 번호(index+generation 핸들)만 나간다.** 호출자가 쥐는 `OwnerPtr<IAudioPlayer>` 는
   없다. 죽은 핸들은 무시된다(§2.4).
3. **스레드 사이에는 POD 명령만 오간다.** 메인 → 오디오는 명령 링, 오디오 → 메인은 상태 링이다. 오디오 스레드는 할당·잠금·
   파일 I/O·엔진 객체 접근을 하지 않는다(§2.5).
4. **재생 컴포넌트는 차원과 무관한 `Component::AudioSource` 하나이고, 리스너는 차원별(`Component::AudioListener2D`, 뒤에
   `AudioListener3D`)이다.** 소스의 자료(클립·버스·볼륨·피치·루프·거리)에는 차원 의미가 없고, 리스너는 방향의 뜻이 차원마다
   다르다(§2.7).
5. **공용 소스와 오디오 값 타입은 새 Tier S 모듈 `JBroAudioTypes` 에 두고, 엔진 쪽 믹서는 새 Tier E 모듈 `JBroAudio` 다**(§2.1).
   이름은 같은 모양의 선례 `JBroAssetTypes`(Tier S, 스크립트가 보는 값 타입) ↔ `JBroAsset`(Tier E, 엔진 본체)을 따른다.

## 1. 기존 엔진의 오디오 - 무엇이 있었고 무엇이 아팠나

위치는 `C:\Users\박주형\source\repos\JBroEngine` 의 `Engine/Core/Audio`·`Engine/Core/Asset/Audio*`·`Engine/GameFramework/Audio`·
`Engine/GameFramework/Component/AudioComponents.h` 와 에디터 `Application/Editor/ImItem/ImAudio*`·`Main/Importer/AudioImporterWindow`·
`Main/Inspector/EditorAudioPreview` 다. 안정화 기록은 `.codex/audio-system.todo.md`(단계 1~8)에 있다.

### 1.1 계층

```
IAudioDevice       ma_engine 래퍼(CMiniAudioDevice) / 빈 구현(EmptyAudioDevice). 엔진이 하나 소유
  ├ CreatePlayer   OwnerPtr<IAudioPlayer> 를 호출자에게 준다  (Play/Pause/Stop/PlayAt/Seek/Volume/Pitch/Loop/Position/Spatial/효과 체인)
  ├ CreateBus      OwnerPtr<IAudioBus>  (Volume/Mute/효과)   GetBus(name) 은 디바이스 소유 버스
  ├ CreateEffect   OwnerPtr<IAudioEffect> (Kind + SetParameter(const char*, float))
  └ GetPrimaryListener → IAudioListener (Position/Forward/MasterVolume)
CAudioAsset        Decompressed(PCM 벡터) 또는 Streaming(파일 경로). 임포트 옵션: Mode·DefaultVolume·Loop·Is3D·Min/MaxDistance·DefaultBus
CAudioEffectAsset  .jfx(YAML) — Kind + map<string,float>
AudioListener      컴포넌트. MasterVolume
AudioPlayer        컴포넌트. AudioGuid·String Bus("SFX")·EffectGuids·Volume·Pitch·Loop·Is3D·Min/MaxDistance·감쇠 모델·Rolloff·PlayOnStart
CAudioSystem       unordered_map<컴포넌트 Guid, PlayerInstance> 에 재생 상태. 편집 모드에서는 돌지 않는다
```

버스는 **이름**으로 가리키고, 목록은 `.jproject` 의 `AudioBuses`(이름·볼륨의 맵 시퀀스, 최대 16 개)가 정한다. `Master` 는 예약
이름이고 나머지는 전부 그 자식이다. 모르는 이름은 Master 로 떨어지고 경고가 한 번 남는다. 새 엔진의 `ProjectFile.cpp` 는 이 키를
지금 읽지 않고 지나간다.

### 1.2 실제로 소리가 난 경로

**"파일 경로 → `ma_sound_init_from_file` + `MA_SOUND_FLAG_DECODE`" 하나뿐이었다.** PCM 기반 Player, 스트리밍, PlayAt·마커,
occlusion, 버스 이펙트는 스텁이거나 아무 일도 하지 않았다. `CAudioSystem` 은 `CMiniAudioDevice` 로 다운캐스트해 경로로 재생했고,
임포트 모드와 자산 기본값은 재생에 닿지 않았다(단계 4 "대기"). 이펙트는 LPF·HPF·Echo·Freeverb 만 실체가 있고 Distortion·
Compressor·Limiter 는 Reverb 로 떨어졌다.

### 1.3 그쪽이 아팠던 것

| 겪은 것 | 원인 | 새 설계에서 |
|---|---|---|
| 종료 크래시, 자식 객체 UAF (단계 1·2, `45d10ca6`·`1fc0e0b9`) | Player·Effect·Bus 가 디바이스의 `ma_engine` 과 노드 그래프를 빌리는데 수명은 호출자가 쥠. 고친 방법이 "디바이스가 자식을 추적하는 공유 상태 + 역순 무효화" 라 복잡했다 | 객체를 넘기지 않는다. 핸들만 나가므로 추적할 자식이 없다 (§2.4) |
| Freeverb 파라미터 데이터 레이스 (단계 5 "대기") | 게임 스레드가 쓰고 오디오 콜백이 동기화 없이 읽음 | 파라미터는 명령으로만 바뀐다 (§2.5) |
| 버스를 바꾸면 보이스를 다시 만들어야 함 | `ma_sound` 는 초기화 뒤 출력 그룹을 못 바꾼다 | 버스는 보이스의 번호 필드 하나다. 명령 하나로 바뀐다 |
| 에디터 미리 듣기가 디바이스를 하나 더 만듦 | 종료 경로가 둘이 되어 크래시가 났던 자리(단계 1) | 장치는 프로세스에 하나. 미리 듣기는 같은 믹서의 전용 버스 (§2.9) |
| non-loop `PlayOnStart` 가 끝난 뒤 다시 만들어져 반복 재생 (단계 3) | 재생 상태를 시스템의 해시맵에 따로 두고, 끝남과 로드 실패를 구분하지 않음 | 상태는 컴포넌트의 시스템 전용 필드. 끝남·실패가 다른 값 (§2.7) |
| 매 프레임 할당·조회 (단계 6 "대기") | `unordered_map`·`seen` 집합·매 프레임 효과 `LoadAsset`·`map<string,float>` | 고정 풀·컴포넌트 순회·종류별 POD 파라미터 |
| 스크립트가 `IAudioDevice` 를 그대로 받음 (`Script.Audio`) | — | 새 규칙 §5 위반(ServiceContext 에 하드웨어 금지). `Service::AudioService` 값 서비스만 (§2.8) |
| 컴포넌트에 `String Bus` | — | 새 규칙 §10.4 위반. 버스는 `NameId` (§2.6) |
| 자산 기본값과 컴포넌트 오버라이드의 우선순위가 정해지지 않음 (단계 4) | 같은 이름의 필드가 둘에 있음 | 재생 파라미터는 컴포넌트에만 둔다 (§2.2) |

### 1.4 없었던 것 (백로그로만 있던 것)

PlayOneShot·페이드·크로스페이드, 스크립트 재생 API, 믹서 창·스냅숏·덕킹, 사용자 감쇠 곡선·도플러, 동시 재생 상한과 보이스
훔치기(`MaxPolyphony` 는 필드만 있었다), 오디오 프로파일러, 장치 선택·핫 언플러그, Web autoplay unlock, 오프라인 렌더 골든 테스트.

## 2. 설계

### 2.1 계층 (새 엔진)

```
ThirdParty/miniaudio     자기 vcxproj(정적 라이브러리, 구현 번역 단위 하나). OGG 용 stb_vorbis 를 같이 넣는다
JBroPlatform             IAudioOutput + IPlatform::CreateAudioOutput()   기본 null. Windows 는 ma_device(WASAPI)
JBroAudioTypes (Tier S)  Component::AudioSource, Service::AudioService, AudioBusId 같은 값 타입,
                         Internal/ 확장 블록(AudioServiceContext·AudioSystemContext)
JBroAudio (Tier E)       AudioMixer(보이스 풀·버스·이펙트·명령/상태 링), AudioClip 버퍼 등록과 퇴역, 디코드
JBroAsset                Asset::AudioAsset (CPU 자료만) 로더
JBroFramework2D (Tier S) Component::AudioListener2D
JBroFramework2DSystem    System::Audio2DSystem  AudioSource + Transform2D, AudioListener2D 를 읽어 명령을 쓴다
JBroHost                 EngineInstance 가 출력과 믹서를 소유하고 둘을 잇는다. 확장 블록 병합
JBroEditor               인스펙터·임포트 옵션·미리 듣기·버스 필드·버스 목록(프로젝트 설정)
```

- 의존: `JBroAudio` → `JBroCore`·miniaudio 만. **믹서는 플랫폼도 캔버스도 모른다** - 그래서 장치 없이 테스트된다.
  `JBroFramework2DSystem` → `JBroAudio`·`JBroAudioTypes`. `JBroFramework2D` → `JBroAudioTypes`(프렐류드가 소스를 보이게).
- 이름: 믹서는 업데이트하는 시스템도 스크립트 서비스도 아니므로 역할 이름 `AudioMixer` 다(§10.3). 공용 소스는 차원 마커가 없고
  (`AudioSource`), 리스너와 시스템은 있다(`AudioListener2D` ↔ `Audio2DSystem`).
- 모듈 이름 `JBroAudioTypes`·`JBroAudio` 는 `JBroAssetTypes`·`JBroAsset` 의 관례를 따른 것이다(2026-09-25 확인). 엔진 쪽 어댑터 모듈
  (`JBroNetworkSystem` 같은 것)은 따로 두지 않는다 - 캔버스를 읽는 `Audio2DSystem` 은 `JBroFramework2DSystem` 에 산다.

### 2.2 에셋

- `AssetType::Audio` 와 `.wav/.ogg/.mp3/.flac` 매핑은 이미 있다(`AssetTypeRules.cpp:26,51-54`). 타입 이름은 `Asset::AudioAsset` 이다.
- 에셋은 CPU 자료만 든다(D-111). 두 모드:
  - **Decompressed**: 로드 때 f32 인터리브 PCM 으로 전부 디코드한다. 짧은 효과음.
  - **Streaming**: **압축된 파일 바이트를 메모리에 두고, 재생하면서 디코드한다**(`ma_decoder_init_memory`). 긴 배경음.
    파일은 `IPlatform::ReadWholeFile` 로만 열어야 하고(§2), 패키지(`.jpak`)와 Web 에서도 같은 길이어야 하기 때문이다.
    디스크에서 조금씩 읽는 스트리밍은 메모리가 실제로 문제가 될 때 `IPlatform` 에 읽기 API 를 더해서 한다 `[열림]`.
- 임포트 옵션은 `.jmeta` 의 `Audio.ImportOptions` 블록이다(D-120 왕복).
  **옵션에는 디코드 방식(`Mode`: `Decompressed`|`Streaming`)만 둔다**(2026-09-25 확인). 기존의 DefaultVolume·Loop·Is3D·
  Min/MaxDistance·DefaultBus 는 컴포넌트와 같은 이름이 둘에 있어 우선순위가 끝내 정해지지 않았다(기존 단계 4). 재생 파라미터는
  컴포넌트가 유일한 원천이다. 기존 메타를 읽는 호환은 두지 않는다 - 이 엔진으로 만든 오디오 에셋이 없다.
- **PCM·압축 바이트의 수명**: 에셋이 언로드·재로드돼도 오디오 스레드가 읽고 있을 수 있다. 그래서 로드한 자료는
  `AudioMixer::RegisterClip` 로 **믹서의 클립 버퍼**로 넘기고, 해제는 `UnregisterClip` 명령 → 오디오 스레드가 그 클립을 쓰는 보이스를
  멈추고 "놓았다" 를 상태 링에 올림 → 메인 스레드가 프레임 밖에서 메모리를 푸는 순서다(퇴역 큐). 오디오 스레드는 절대 풀지 않는다.
  `[가정]` in-place 재로드는 그 클립을 쓰는 보이스를 멈춘다. 핸들은 보존한다(D-111).
- 이펙트 에셋(`.jfx`)은 6 단계다. 파라미터는 종류별 POD 구조체다(`map<string,float>` 가 아니다).

### 2.3 miniaudio 에서 가져다 쓰는 것

| 부품 | 쓰임 |
|---|---|
| `ma_device` | Windows·Web 출력 (JBroPlatform 안) |
| `ma_decoder` (+ stb_vorbis) | WAV·MP3·FLAC·OGG 디코드. Decompressed 는 로드 때, Streaming 은 보이스마다 |
| `ma_linear_resampler` | 클립 샘플 레이트 → 장치 레이트, 그리고 피치. 보이스마다 하나 |
| `ma_spatializer` / `ma_spatializer_listener` | 감쇠(None·Inverse·Linear·Exponential)·팬. 2D 는 z=0 과 고정 방향으로 같은 부품을 쓴다 |
| `ma_lpf`·`ma_hpf`·`ma_delay` | 이펙트 (6 단계). Reverb 는 기존 Freeverb 를 옮긴다 |

`ma_engine`·`ma_sound`·`ma_sound_group`·`ma_node_graph`·`ma_resource_manager` 는 쓰지 않는다. 구현 번역 단위에서 쓰지 않는 것은
`MA_NO_ENGINE`·`MA_NO_NODE_GRAPH`·`MA_NO_RESOURCE_MANAGER` 로 끈다. 서드파티 규칙대로 고치지 않고 래핑하지 않는다(D-60).

### 2.4 믹서와 핸들

```
AudioMixer (프로세스 수명, 초기화 때 전부 할당)
  voices[MaxVoices]        고정 풀. AudioVoiceHandle { uint32 index; uint32 generation; }  (POD 8B)
  buses[MaxBuses]          0 = Master, 1 = EditorPreview(에디터만), 나머지 = 프로젝트 목록. AudioBusId = uint8 번호
  clips                    등록된 클립 버퍼. AudioClipHandle { index; generation; }
  commandRing (SPSC)       메인 → 오디오
  statusRing  (SPSC)       오디오 → 메인
  Render(float* out, frameCount)   출력 콜백이 부른다. 테스트는 직접 부른다
```

- `Play(desc) → AudioVoiceHandle` 은 메인 스레드에서 **슬롯을 바로 예약**하고(메인이 쥔 빈 목록) 명령을 쓴다. 그래서 핸들이 즉시
  돌아오고 같은 프레임에 `SetVolume` 을 이어 쓸 수 있다.
- 슬롯이 없으면 **보이스 훔치기**: 우선순위가 낮은 것 → 들리는 크기가 작은 것 → 오래된 것. 결정적이어야 한다(테스트).
  MaxVoices 기본값은 **64** 다(기존 `AudioDeviceDesc::MaxPolyphony` 와 같다, 2026-09-25 확인). 고정 풀이라 믹서 초기화 때 정하고,
  바꿀 자리는 `EngineConfig` 다 `[가정]`.
- 버스는 우선 Master 아래 한 층이다(기존과 같다). 볼륨·음소거. 중첩·솔로·센드는 `[열림]`.
- 명령이 링을 넘치면 그 프레임의 나머지 명령은 버리고 경고를 한 번 남긴다 `[가정]`. 링 크기는 프레임당 명령 수의 측정으로 정한다.
- **miniaudio 부품의 `_init` 은 할당 콜백을 받는다**(`ma_spatializer_init`·`ma_linear_resampler_init`·`ma_decoder_init_memory`,
  기존 엔진 `miniaudio.h:5281,5372`). 보이스 슬롯의 리샘플러·spatializer 는 믹서 초기화 때 슬롯마다 만들어 두고 재생 때는 리셋만 한다.
  스트리밍 디코더는 재생할 때 메인 스레드에서 열어야 하는데 그대로 두면 정상 프레임에 할당이 생긴다(§9) → 슬롯마다 고정 아레나를
  할당 콜백으로 넘긴다 `[가정]`. 1 단계에서 디코더 하나가 실제로 얼마를 잡는지 재서 정한다.

### 2.5 스레드

- 오디오 스레드가 하는 일은 `Render` 하나다: 명령 적용 → 보이스마다 디코드/리샘플/공간화/볼륨 → 버스 합산 → Master → 클리핑 방지.
  **할당·잠금·파일 I/O·로그 문자열·엔진 객체 접근이 없다.** 로그가 필요하면 상태 링에 코드를 올린다.
- 메인 스레드 쪽 믹서 API 는 메인 스레드 전용이다(`SafePtr` 와 같은 규약). 워커가 명령을 쓰지 않는다 - 링이 SPSC 라서다.
- 상태 링: 끝남(핸들), 클립 놓음, 위치(요청한 보이스만), 버스 피크(에디터 미터). 메인이 프레임 시작에 비운다.
- Web 은 콜백이 메인 스레드에서 돌 수 있다. SPSC 는 그래도 맞다 `[가정]`.

### 2.6 버스와 프로젝트 파일

- `.jproject` 의 `AudioBuses`(기존 엔진과 같은 키, `Name`·`Volume` 맵 시퀀스)를 `ProjectFile` 이 읽고 쓴다. 두 번째 형식을 만들지
  않는다(§2). 저장은 D-189(바뀐 것이 없으면 바이트 하나도 건드리지 않음)를 지킨다.
- 컴포넌트는 버스를 **`NameId`** 로 든다(§10.4). 원문은 프로젝트 파일과 에디터가 든다. 시스템은 프로젝트를 열 때 만든
  `NameId → AudioBusId` 표로 한 번 풀어 컴포넌트의 시스템 전용 필드에 캐시한다 - 매 프레임 조회가 없다.
- 모르는 이름은 Master 로 가고 경고를 한 번 남긴다(기존 규약 그대로). 빈 이름은 Master 다.

### 2.7 컴포넌트와 시스템

```cpp
// JBroAudioTypes (Tier S). 필드는 사용자 편집 값, 아래 블록은 시스템 전용 (D-47 과 같은 방식)
Component::AudioSource
    AssetId  clipId;  AssetHandle clip;      // 해석 패스(xxxId → xxx)
    NameId   bus;                            // 기본값 "SFX"
    float    volume = 1, pitch = 1;
    bool     loop = false, playOnStart = true, spatial = false;
    float    minDistance = 1, maxDistance = 50, rolloff = 1;
    AudioAttenuation attenuation = Inverse;
    // 시스템 전용
    AudioVoiceHandle voice;  AudioBusId resolvedBus;  AudioSourceState state;  // Idle·Playing·Finished·LoadFailed
Component::AudioListener2D   (JBroFramework2D)
    float volume = 1;
```

- `Audio2DSystem` 은 **플레이 중에만 돈다**(기존과 같다). 매 프레임: 첫 활성 리스너(`IsActiveComponent`)의 월드 위치를 믹서에 쓰고,
  `ForEach<AudioSource>` 로 소스를 돌며 상태 기계를 진행하고, 바뀐 값만 명령으로 쓴다(마지막으로 보낸 값 캐시).
  위치는 오너의 `Transform2D` 월드에서 `(x, y, 0)` 이다. 리스너가 여럿이면 첫 번째를 쓰고 경고를 한 번 남긴다.
- 상태 기계는 기존 단계 3 의 정책을 잇는다: 끄면 보이스를 즉시 멈추고 켜면 한 번 다시 무장, 클립이 바뀌면 교체, 컴포넌트가
  떼이면 그 보이스만 멈춤, 플레이 중지는 전부 멈춤. 자연 종료(`Finished`)와 로드 실패(`LoadFailed`)는 다른 값이라
  non-loop `playOnStart` 가 반복되지 않는다.
- 3D 는 `AudioListener3D` 와 `Audio3DSystem` 을 같은 모양으로 둔다(방향은 `Transform3D` 에서). D-116 에 따라 2D 뒤다.
- 새 컴포넌트는 2D·3D 양쪽 내장 컴포넌트 등록과 리플렉션(`JBRO_FIELD`), 캔버스 직렬화, 인스펙터에 올린다.
- 기존 캔버스 파일의 `AudioPlayer`·`AudioListener` 는 새 컴포넌트로 옮겨 읽지 않는다(2026-09-25 확인) - 이 엔진으로 만든 캔버스에
  오디오가 없다. `CanvasFile.h:13` 이 지금처럼 모르는 타입으로 버린다.

### 2.8 스크립트 경계

- `Service::AudioService` (값 서비스, `JBroAudioTypes`): `PlayOneShot(clip, bus, volume)`, 버스 볼륨·음소거(`NameId` 로), 그리고
  `Ref<AudioSource>` 를 받아 Play·Stop·Pause 하는 정도로 시작한다. **장치·믹서·보이스 핸들은 스크립트에 나가지 않는다.**
- 전달은 D-37 확장 블록이다. 공통 `ServiceContext` 에는 넣지 않는다 - D-43·D-122 와 같은 이유(공통 계층이 오디오를 참조하게
  된다). 서비스 헤더는 시스템을 전방 선언만 하고 호출은 `.cpp` 에 둔다(§5).
- 스크립트 타깃 include 경로에 `JBroAudioTypes` 를 더한다(`JBro.Script.props`). 음성 테스트: 스크립트 타깃에서 `JBroAudio`
  헤더를 include 하면 실패해야 한다.

### 2.9 수명과 호스트

| 스코프 | 대상 |
|---|---|
| Process | 출력(`IAudioOutput`), `AudioMixer`, Master·EditorPreview 버스 |
| Project | 프로젝트 버스 목록과 볼륨, `NameId → AudioBusId` 표 |
| Canvas(플레이) | 소스가 만든 보이스. 플레이 중지·캔버스 전환에서 전부 멈춤 |

- `EngineInstance` 가 출력과 믹서를 소유하고 `output->Start(mixerRenderCallback, mixer)` 로 잇는다(함수 포인터 + 사용자 자료, POD).
  종료는 **출력 정지 → 믹서 파괴** 한 순서다. 출력이 없으면(null 플랫폼·테스트) 소리 없이 같은 API 가 돈다.
- `EngineConfig` 에 `audioEnabled` 를 두는 것은 네트워크(`networkEnabled`, D-125)와 같은 자리다 `[가정]`.
- 에디터 미리 듣기는 **같은 믹서의 EditorPreview 버스**로 한다. Master 음소거와 무관하게 들린다 `[가정]`. 장치 초기화는 처음
  재생을 누를 때다(기존 단계 7 의 교훈).

## 3. 단계

각 단계는 구현 → 그 단계의 검증 → diff 검토 → 커밋이다. 검증이 실패하면 다음 단계로 가지 않는다.

1. `[대기]` **믹서 뼈대와 오프라인 렌더.** miniaudio(+ stb_vorbis) 서드파티 빌드 단위, `JBroAudioTypes`·`JBroAudio` 모듈, 보이스 풀·
   버스·명령/상태 링·`Render`. 장치 없음.
   완료 조건: 사인파 클립 PCM 을 등록해 `Render` 로 당긴 결과가 기대값과 같다(볼륨·버스 볼륨·음소거·피치 2 배에서 주파수·루프 경계·
   끝남 통지·훔치기 순서). 정상 `Render` 와 정상 프레임의 명령 쓰기에서 할당 0 회(카운팅 할당기, §9). 스크립트 타깃이 `JBroAudio` 를
   include 하면 컴파일 실패(음성 테스트). 클립 퇴역 중에 보이스가 읽어도 해제가 오디오 스레드 확인 뒤에만 일어난다.
2. `[대기]` **출력과 에셋.** `IAudioOutput`·`IPlatform::CreateAudioOutput`(Windows `ma_device`), `Asset::AudioAsset` 로더(두 모드),
   `EngineInstance` 소유와 종료 순서.
   완료 조건: WAV·MP3·FLAC·OGG 각각 두 모드로 디코드한 앞부분이 참조값과 같다(오프라인). 한글·공백 경로. 초기화/종료 100 회 반복과
   재생 중 종료에서 크래시·잔존 스레드 없음. 실제 게임 호스트에서 들린다(사람 확인).
3. `[대기]` **2D 컴포넌트와 시스템.** `AudioSource`·`AudioListener2D`·`Audio2DSystem`, `.jproject` `AudioBuses` 읽고 쓰기, 캔버스 직렬화,
   해석 패스.
   완료 조건: non-loop `playOnStart` 정확히 1 회, 루프는 멈출 때까지, 끄고 켜기·클립 교체·떼기·플레이 중지 정책(§2.7), 여러 소스가
   독립, 리스너 거리에 따른 감쇠가 오프라인 렌더에서 보인다. 프로젝트 파일 거듭 저장 바이트 비교(D-189). 정상 프레임 할당 0 회.
4. `[대기]` **스크립트 서비스.** `Service::AudioService`, 확장 블록, 프렐류드·`JBro.Script.props`.
   완료 조건: 스크립트에서 `PlayOneShot`·버스 볼륨이 믹서 명령으로 닿는다. 핫 리로드 뒤 재바인딩. 음성 테스트.
5. `[대기]` **에디터.** 인스펙터(버스는 프로젝트 목록 콤보), 오디오 임포트 옵션, 미리 듣기(EditorPreview 버스, 파형), 프로젝트 설정의
   버스 목록 편집(커맨드). 기존 `ImAudioBusField`·`ImAudioVisualizer`·`EditorAudioPreview`·`AudioImporterWindow` 를 먼저 읽고 옮긴다
   (§11.0). 화면 글자는 로컬라이징 키(§11.2).
   완료 조건: 실제 `JBroEditorHost` 에서 조작해 본다(검증 규약). 편집은 커맨드이고 되돌린다.
6. `[대기]` **이펙트와 3D.** `.jfx` 이펙트 에셋과 보이스·버스 이펙트 체인(LPF·HPF·Echo·Reverb), 재생 중 파라미터 변경.
   그 뒤 `AudioListener3D`·`Audio3DSystem`(3D todo).

## 4. 규칙과 부딪히는 지점

- **§2 파일 IO**: 스트리밍이 파일을 직접 열면 위반이다 → 압축 바이트를 메모리에 둔다(§2.2).
- **§5 ServiceContext 에 하드웨어 금지**: 기존 `Script.Audio` 는 위반이었다 → 값 서비스와 확장 블록(§2.8).
- **§9 매 프레임 할당·문자열 비교 금지**: 기존 시스템의 해시맵·`seen` 집합·문자열 버스 비교 → 고정 풀과 `NameId`(§2.4·§2.6).
- **§10.4 컴포넌트 공개 필드의 String 금지**: 버스 이름 → `NameId`.
- **§6 SafePtr 는 메인 스레드 전용**: 오디오 스레드는 `SafePtr` 도 에셋도 보지 않는다. 클립 버퍼는 믹서가 들고 퇴역 큐로 푼다(§2.2).
- **ThirdParty README "각 라이브러리는 자기 빌드 단위"**: miniaudio 는 자기 vcxproj 이고 JBroPlatform·JBroAudio 둘이 링크한다.
  구현 번역 단위가 하나라 중복 정의가 없다.

## 5. 열린 것과 가정 모음

- 2026-09-25 에 정해진 것: 임포트 옵션은 `Mode` 만(§2.2), 모듈 이름은 `JBroAudioTypes`·`JBroAudio`(§2.1), MaxVoices 64(§2.4),
  기존 캔버스·메타의 이식은 하지 않음(§2.2·§2.7).
- `[열림]` 버스 중첩·솔로·센드(§2.4), 디스크 스트리밍(§2.2), Web autoplay unlock·장치 선택·핫 언플러그·포커스 잃었을 때 정책(기존 백로그).
- `[가정]` 재로드는 쓰는 보이스를 멈춘다(§2.2), 명령 넘침은 버리고 경고(§2.4), 스트리밍 디코더는 슬롯별 고정 아레나(§2.4), Web 콜백과 SPSC(§2.5), `audioEnabled`·MaxVoices 설정 자리(§2.9·§2.4),
  미리 듣기는 Master 음소거와 무관(§2.9).
- 기존 백로그(PlayOneShot 반환 핸들, 페이드, PlayAt·마커, 믹서 창·스냅숏·덕킹, 감쇠 곡선·도플러·occlusion, 라우드니스·트림,
  프로파일러)는 6 단계 뒤에 사용자 우선순위를 받아 단계로 올린다.
