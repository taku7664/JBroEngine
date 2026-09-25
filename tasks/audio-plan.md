# 오디오 계획 (기존 엔진 재검토 뒤 재설계)

> 계약은 `docs/ProjectRule.md`, 결정은 `tasks/todo.md` Decisions 다. 이 문서는 그 둘을 향해 가는 순서와
> 상태를 적는다. 상태는 항목마다 `[완료]` `[진행]` `[제안]` `[가정]` `[열림]` 으로 붙인다.
> `[제안]` 은 **사용자 확인 전**이다. 2026-09-25 에 §0 의 방향(직접 믹서·공용 소스와 차원별 리스너·새 Tier S 모듈)이
> 확인돼 D-197 이 됐다. 같은 날 남은 셋(임포트 옵션은 `Mode` 만, 모듈 이름은 관례대로, 보이스 64)도 정해졌고
> 기존 자료의 이식은 하지 않기로 했다(이 엔진으로 만든 콘텐츠가 없다). 같은 날 devil 검증 세 회차(§1.5)에서 "직접 믹서" 의
> 근거 넷 중 셋이 틀린 것으로 드러나, 믹서는 **`ma_engine` 을 안에 둔 `AudioMixer`** 로 바뀌었다(D-198 이 D-197 (1)·(3) 을 고침).
> 아직 코드는 없다.

## 0. 확정된 방향 (2026-09-25, D-197·D-198)

1. **믹싱은 `JBroAudio` 안의 `ma_engine` 이 한다. `ma_engine` 은 `AudioMixer` 밖으로 나가지 않는다.** 리소스 매니저는 끄고
   (`MA_NO_RESOURCE_MANAGER`), 장치는 열지 않으며(`noDevice`), 데이터 소스는 우리가 넘긴다(§2.3). 페이드·예약 재생·도플러·원뿔
   감쇠를 miniaudio 에서 그대로 얻는다. 기존 엔진이 겪은 수명 문제는 `ma_engine` 이 아니라 **래퍼가 `OwnerPtr<IAudioPlayer>` 를
   호출자에게 넘긴 설계**에서 나왔다(§1.3·§1.5). (D-198, D-197 의 "직접 믹서" 를 고침)
2. **보이스와 버스는 믹서가 소유하고, 밖으로는 번호(index+generation 핸들)만 나간다.** 호출자가 쥐는 `OwnerPtr<IAudioPlayer>` 는
   없다. 죽은 핸들은 무시된다(§2.4).
3. **믹서 API 는 메인 스레드 전용이고, 오디오 스레드는 `ma_engine_read_pcm_frames` 하나만 부른다.** 재생 중 바꾸는 값(볼륨·피치·
   위치·방향·속도·시작/정지)은 miniaudio 가 원자 변수로 들고 있어 메인 스레드가 바로 쓴다. 원자가 아닌 값(거리·감쇠·원뿔)은
   보이스가 멈춰 있을 때만 쓴다(§2.5). 명령 링은 두지 않는다. (D-198, D-197 의 "POD 명령 링" 을 고침)
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
| 종료 크래시, 자식 객체 UAF (단계 1·2, `45d10ca6`·`1fc0e0b9`) | Player·Effect·Bus 가 디바이스의 `ma_engine` 과 노드 그래프를 빌리는데 수명은 호출자가 쥠. 고친 방법이 "디바이스가 자식을 추적하는 공유 상태 + 역순 무효화" 라 복잡했다 | 객체를 넘기지 않는다. 핸들만 나가고, 보이스·버스·클립의 파괴 순서는 믹서 한 곳이 정한다 (§2.4) |
| Freeverb 파라미터 데이터 레이스 (단계 5 "대기") | 게임 스레드가 쓰고 오디오 콜백이 동기화 없이 읽음. 사용자 노드의 파라미터가 평범한 `float` 였다 | 사용자 이펙트 노드의 파라미터는 원자 변수다 (§2.5, 6 단계) |
| 버스를 바꾸면 보이스를 다시 만들어야 한다고 봄 | 기존 주석은 "`ma_sound` 는 출력 그룹을 못 바꾼다" 고 했으나 **틀렸다**: `ma_node_attach_output_bus` 는 재생 중에도 스레드 안전하다 (§1.5) | 버스 변경은 `ma_node_attach_output_bus` 한 번이다 |
| 에디터 미리 듣기가 디바이스를 하나 더 만듦 | 종료 경로가 둘이 되어 크래시가 났던 자리(단계 1) | 장치는 프로세스에 하나. 미리 듣기는 같은 믹서의 전용 버스 (§2.9) |
| non-loop `PlayOnStart` 가 끝난 뒤 다시 만들어져 반복 재생 (단계 3) | 재생 상태를 시스템의 해시맵에 따로 두고, 끝남과 로드 실패를 구분하지 않음 | 상태는 컴포넌트의 시스템 전용 필드. 끝남·실패가 다른 값 (§2.7) |
| 매 프레임 할당·조회 (단계 6 "대기") | `unordered_map`·`seen` 집합·매 프레임 효과 `LoadAsset`·`map<string,float>` | 고정 풀·컴포넌트 순회·종류별 POD 파라미터 |
| 스크립트가 `IAudioDevice` 를 그대로 받음 (`Script.Audio`) | — | 새 규칙 §5 위반(ServiceContext 에 하드웨어 금지). `Service::AudioService` 값 서비스만 (§2.8) |
| 컴포넌트에 `String Bus` | — | 새 규칙 §10.4 위반. 버스는 `NameId` (§2.6) |
| 자산 기본값과 컴포넌트 오버라이드의 우선순위가 정해지지 않음 (단계 4) | 같은 이름의 필드가 둘에 있음 | 재생 파라미터는 컴포넌트에만 둔다 (§2.2) |

### 1.4 없었던 것 (백로그로만 있던 것)

PlayOneShot·페이드·크로스페이드, 스크립트 재생 API, 믹서 창·스냅숏·덕킹, 사용자 감쇠 곡선·도플러, 동시 재생 상한과 보이스
훔치기(`MaxPolyphony` 는 필드만 있었다), 오디오 프로파일러, 장치 선택·핫 언플러그, Web autoplay unlock, 오프라인 렌더 골든 테스트.

### 1.5 devil 검증 (2026-09-25, 세 회차)

근거는 기존 엔진에 있는 miniaudio v0.11.25 원문(`Engine/ThirdParty/miniaudio/miniaudio.h`)이다. 줄 번호는 그 파일 기준이다.

1. **"직접 믹서여야 한다" - needs revision (strong).** D-197 이 든 근거 넷 중 셋이 틀렸다.
   - 오프라인 테스트: `ma_engine` 도 `noDevice` 로 띄워 `ma_engine_read_pcm_frames` 로 당길 수 있다(11305·11346).
   - 재생 중 버스 변경: `ma_node_attach_output_bus`(10803)는 스핀락과 원자 변수로 스레드 안전하다(10739-10757). 기존 엔진 주석의
     "못 바꾼다" 를 확인 없이 옮긴 것이었다.
   - 수명 UAF: 원인은 호출자에게 `OwnerPtr` 를 넘긴 래퍼 설계이지 `ma_engine` 이 아니다. 핸들 계층은 어느 쪽에서도 같다.
   - 반대로 직접 짜면 페이드(`ma_sound_set_fade_in_milliseconds`, 1446)·예약 재생(`ma_sound_set_start_time_in_pcm_frames`, 389 - 기존의
     PlayAt)·도플러(1442)·원뿔(1385)을 다시 만들어야 한다. 할당은 `ma_sound_init` 이 할당 콜백을 거친다(77362) - 우리 할당기로 받는다.
   - 메인 스레드 호출이 안전한가: `ma_node_uninit` 은 **오디오 스레드가 그 노드를 다 읽을 때까지 기다린 뒤 돌아온다**(7.2 절, 2417-2460).
     그래서 `ma_sound_uninit` 이 돌아오면 PCM 을 바로 풀어도 된다 - D-197 의 퇴역 큐가 필요 없다. 대가는 메인 스레드가 그 노드 하나를
     처리하는 시간만큼 멈출 수 있다는 것이고, 소리 노드는 그래프의 잎이라 가장 싸다(같은 절). 볼륨·피치는 원자(11178-11179),
     spatializer 의 위치·방향·속도도 원자다(5266-5268). **거리·감쇠·원뿔·도플러 계수는 평범한 `float` 다(5255-5264)** - 재생 중 쓰면
     경쟁이다. 그래서 §2.5 의 규칙을 둔다.
   - 작용한 편향: 기존 주석을 근거로 삼은 anchoring, "기존이 아팠다 → 기존 부품 탓" 이라는 narrative bias.
   - 결정: `ma_engine` 을 `AudioMixer` 안에 둔다(D-198). 수명 추적은 없어지는 것이 아니라 믹서 한 곳으로 모인다 - 표현도 고쳤다.
2. **"공용 `AudioSource` 의 자료에는 차원 의미가 없다" - holds (weak).** 지금 필드는 맞다. 3D 의 원뿔(소스 방향)은 2D 에서 뜻이 없어
   공용에 넣으면 §10.2 와 부딪힌다 → 3D 전용 필드는 공용 소스에 넣지 않는다(§2.7). 도플러는 2D 에서도 뜻이 있다.
3. **"임포트 옵션은 `Mode` 만" - holds, 표현을 고침 (moderate).** 빼는 다섯 필드는 맞다. 다만 라우드니스 정규화 게인과 루프 시작·끝
   지점은 재생마다 다른 값이 아니라 **파일의 속성**이라 에셋 몫이다 → 원칙으로 적는다(§2.2).

## 2. 설계

### 2.1 계층 (새 엔진)

```
ThirdParty/miniaudio     자기 vcxproj(정적 라이브러리, 구현 번역 단위 하나). OGG 용 stb_vorbis 를 같이 넣는다
JBroPlatform             IAudioOutput + IPlatform::CreateAudioOutput()   기본 null. Windows 는 ma_device(WASAPI)
JBroAudioTypes (Tier S)  Component::AudioSource, Service::AudioService, AudioBusId 같은 값 타입,
                         Internal/ 확장 블록(AudioServiceContext·AudioSystemContext)
JBroAudio (Tier E)       AudioMixer(안에 ma_engine: 보이스 풀·버스 그룹·리스너·이펙트 노드), 클립 등록, 디코드, 고정 할당기
JBroAsset                Asset::AudioAsset (CPU 자료만) 로더
JBroFramework2D (Tier S) Component::AudioListener2D
JBroFramework2DSystem    System::Audio2DSystem  AudioSource + Transform2D, AudioListener2D 를 읽어 믹서를 부른다
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
    디스크에서 조금씩 읽는 스트리밍은 `StreamFromDisk` 다(7 단계, D-203): `IPlatform::OpenFileStream` 으로 열고 스트리머 스레드가 흘려 읽는다.
- 임포트 옵션은 `.jmeta` 의 `Audio.ImportOptions` 블록이다(D-120 왕복).
  **옵션에는 디코드 방식(`Mode`: `Decompressed`|`Streaming`)만 둔다**(2026-09-25 확인). 기존의 DefaultVolume·Loop·Is3D·
  Min/MaxDistance·DefaultBus 는 컴포넌트와 같은 이름이 둘에 있어 우선순위가 끝내 정해지지 않았다(기존 단계 4). 재생 파라미터는
  컴포넌트가 유일한 원천이다. 기존 메타를 읽는 호환은 두지 않는다 - 이 엔진으로 만든 오디오 에셋이 없다.
  **원칙: 에셋은 파일의 속성, 컴포넌트는 인스턴스의 재생 파라미터다**(devil 3 회차, §1.5). 지금 파일의 속성은 `Mode` 하나이고,
  라우드니스 정규화 게인·루프 시작/끝 지점이 들어온다면 에셋 쪽이다.
- **PCM·압축 바이트의 수명**: 로드한 자료는 `AudioMixer::RegisterClip` 으로 **믹서의 클립**이 된다(`AudioClipHandle`). 보이스는
  슬롯마다 자기 데이터 소스(Decompressed 는 공유 PCM 을 가리키는 `ma_audio_buffer_ref`, Streaming 은 압축 바이트 위의 `ma_decoder`)를
  든다 - 데이터 소스는 커서를 가지므로 보이스끼리 나누지 않는다. `UnregisterClip` 은 그 클립을 쓰는 보이스를 `ma_sound_uninit` 하고
  나서 메모리를 푼다. `ma_sound_uninit` 은 오디오 스레드가 그 노드를 다 읽을 때까지 기다리므로(§1.5) 그 뒤에 푸는 것이 안전하다.
  오디오 스레드는 아무것도 풀지 않는다.
  `[가정]` in-place 재로드는 그 클립을 쓰는 보이스를 멈춘다. 핸들은 보존한다(D-111).
- 이펙트 에셋(`.jfx`)은 6 단계다. 파라미터는 종류별 POD 구조체다(`map<string,float>` 가 아니다).

### 2.3 miniaudio 에서 가져다 쓰는 것

| 부품 | 쓰임 |
|---|---|
| `ma_device` | Windows·Web 출력 (JBroPlatform 안). 콜백이 믹서의 `Render` 를 부른다 |
| `ma_engine` (`noDevice`) | 노드 그래프·리샘플·공간화·리스너. `Render` 가 `ma_engine_read_pcm_frames` 를 부른다 |
| `ma_sound` (`ma_sound_init_ex`, 데이터 소스 지정) | 보이스. 볼륨·피치·루프·페이드·예약 시작·위치·도플러·원뿔 |
| `ma_sound_group` | 버스. Master·EditorPreview·프로젝트 버스 |
| `ma_audio_buffer_ref` / `ma_decoder` (+ stb_vorbis) | 데이터 소스. Decompressed 는 공유 PCM 참조, Streaming 은 메모리 위 디코더 |
| 사용자 노드 (`ma_node_vtable`) | 이펙트 (6 단계). LPF·HPF·Echo 는 miniaudio DSP 를 감싼 노드, Reverb 는 기존 Freeverb 를 옮긴다 |

`ma_resource_manager` 는 쓰지 않고 `MA_NO_RESOURCE_MANAGER` 로 끈다 - 파일을 스스로 열고(§2 위반) 작업 스레드를 띄우기 때문이다.
`ma_engine_play_sound`(엔진 내부에서 소리를 할당하는 한 번 재생)도 쓰지 않는다. 서드파티 규칙대로 miniaudio 를 고치지 않는다(D-60).
`ma_engine` 은 `AudioMixer` 의 private 멤버이고 miniaudio 헤더는 `JBroAudio` 의 `.cpp` 만 include 한다 - 공개 헤더가 miniaudio 를
끌고 오지 않는다. 이것은 D-60 이 막는 "라이브러리 헤더를 감싼 두 번째 표면" 이 아니라 RHI 가 D3D12 를 가리는 것과 같은 엔진 경계다.

### 2.4 믹서와 핸들

```
AudioMixer (프로세스 수명, 초기화 때 전부 할당)
  engine                   ma_engine (noDevice, 할당 콜백 = 믹서의 고정 할당기)
  voices[MaxVoices]        고정 풀. 슬롯 = ma_sound + 데이터 소스 자리 + 세대. AudioVoiceHandle { uint32 index; uint32 generation; }  (POD 8B)
  buses[MaxBuses]          ma_sound_group. 0 = Master, 1 = EditorPreview(에디터만, 엔드포인트에 바로), 나머지 = 프로젝트 목록. AudioBusId = uint8
  clips                    등록된 클립. AudioClipHandle { index; generation; }
  Render(float* out, frameCount)   = ma_engine_read_pcm_frames. 출력 콜백이 부르고, 테스트는 직접 부른다
```

- `Play(desc) → AudioVoiceHandle` 은 메인 스레드에서 빈 슬롯을 잡고 `ma_sound_init_ex`·값 설정·`ma_sound_start` 를 한 번에 한다.
  핸들이 즉시 돌아오고 같은 프레임에 `SetVolume` 을 이어 쓸 수 있다. 끝남은 메인 스레드가 `ma_sound_at_end` 를 읽어 안다(원자).
  끝난 보이스의 슬롯은 믹서의 프레임 시작 처리(`Update`)가 거둔다.
- 슬롯이 없으면 **보이스 훔치기**: 우선순위가 낮은 것 → 들리는 크기가 작은 것 → 오래된 것. 결정적이어야 한다(테스트).
  MaxVoices 기본값은 **64** 다(기존 `AudioDeviceDesc::MaxPolyphony` 와 같다, 2026-09-25 확인). 고정 풀이라 믹서 초기화 때 정하고,
  바꿀 자리는 `EngineConfig` 다 `[가정]`.
- 버스는 부모(목록의 앞 버스)를 가질 수 있고 센드로 다른 버스에 보낼 수 있다. 솔로는 믹싱 상태다(7 단계, D-203).
- **할당**: `ma_sound_init_ex` 는 엔진 노드의 힙을 할당 콜백으로 잡고(기존 엔진 `miniaudio.h:77362`), `ma_decoder_init_memory` 도
  그렇다. 그대로 두면 정상 프레임의 `Play` 가 힙을 건드린다(§9 위반). → 엔진과 디코더에 **믹서의 고정 할당기**(보이스 슬롯마다
  미리 잡은 블록)를 할당 콜백으로 넘긴다. 블록 크기는 1 단계에서 `ma_engine_node_get_heap_size`·디코더 실측으로 정하고, 넘치면
  `Play` 가 실패하고 경고를 남긴다(조용히 힙으로 떨어지지 않는다). 정상 프레임 할당 0 회를 카운팅 할당기로 단언한다.
- 버스 변경은 `ma_node_attach_output_bus` 로 재생 중에 한다(§1.5). 보이스를 다시 만들지 않는다.

### 2.5 스레드

- 오디오 스레드가 하는 일은 `Render`(`ma_engine_read_pcm_frames`) 하나다. miniaudio 는 이 경로를 잠금 없이 돈다(7.2 절).
  우리 코드가 오디오 스레드에서 도는 것은 6 단계의 사용자 이펙트 노드뿐이고, 거기서도 **할당·잠금·파일 I/O·로그·엔진 객체 접근이 없다.**
- 믹서 API 는 메인 스레드 전용이다(`SafePtr` 와 같은 규약). 워커가 부르지 않는다.
- **재생 중 쓰는 값은 miniaudio 가 원자로 든 것만이다**: 볼륨·피치(`miniaudio.h:11178-11179`), 위치·방향·속도(5266-5268),
  시작·정지·페이드. **거리·감쇠 모델·rolloff·원뿔·도플러 계수는 평범한 `float` 라(5255-5264) 보이스가 멈춰 있을 때만 쓴다** -
  `Play` 안에서 `ma_sound_start` 전에 쓰고, 재생 중에 컴포넌트 값이 바뀌면 다음 재생부터 적용한다. 믹서 API 가 이것을 타입으로
  나눈다: 재생 중 바꿀 수 있는 것은 `Set*` 이고, 나머지는 `AudioPlayDesc` 에만 있다.
- 사용자 이펙트 노드의 파라미터는 원자 변수이고 오디오 스레드가 처리 앞에 한 번 읽는다(기존 Freeverb 경쟁의 해법, 6 단계).
  miniaudio 필터의 `reinit` 은 스레드 안전하지 않으므로 노드 안에서 파라미터가 바뀐 것을 보고 오디오 스레드가 한다 `[가정]`.
- `ma_sound_uninit` 은 오디오 스레드가 그 노드를 다 읽기를 기다린다(§1.5) - 메인 스레드가 잠깐 멈출 수 있다. 소리 노드는 그래프의
  잎이라 가장 싸다. 1 단계에서 그 시간을 잰다.
- Web 은 콜백이 메인 스레드에서 돌 수 있다. 그때는 기다림이 생기지 않는다 `[가정]`.

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
  `ForEach<AudioSource>` 로 소스를 돌며 상태 기계를 진행하고, 바뀐 값만 믹서에 쓴다(마지막으로 쓴 값 캐시). 재생 중에는 원자 값만
  쓴다 - 거리·감쇠가 바뀌면 다음 재생부터다(§2.5).
  위치는 오너의 `Transform2D` 월드에서 `(x, y, 0)` 이다. 리스너가 여럿이면 첫 번째를 쓰고 경고를 한 번 남긴다.
- 상태 기계는 기존 단계 3 의 정책을 잇는다: 끄면 보이스를 즉시 멈추고 켜면 한 번 다시 무장, 클립이 바뀌면 교체, 컴포넌트가
  떼이면 그 보이스만 멈춤, 플레이 중지는 전부 멈춤. 자연 종료(`Finished`)와 로드 실패(`LoadFailed`)는 다른 값이라
  non-loop `playOnStart` 가 반복되지 않는다.
- 3D 는 `AudioListener3D` 와 `Audio3DSystem` 을 같은 모양으로 둔다(방향은 `Transform3D` 에서). D-116 에 따라 2D 뒤다.
- **3D 에서만 뜻이 있는 소스 필드(원뿔 감쇠 - 소스의 방향이 필요하다)는 공용 `AudioSource` 에 넣지 않는다.** 3D 쪽 별도 컴포넌트로
  둔다(§10.2, devil 2 회차 §1.5). 도플러는 2D 에서도 뜻이 있으므로 공용에 둘 수 있다.
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

1. `[완료]` (2026-09-25) **믹서 뼈대와 오프라인 렌더.** 실측과 결과:
   - `ThirdParty/miniaudio`(v0.11.25, 자기 vcxproj, 구현 번역 단위 `miniaudio.cpp` 하나 + stb_vorbis v1.22). 설정 매크로는
     `JBro.Common.props` 의 `JBroMiniaudioDefines` 한 곳이다 - `ma_engine` 의 모양이 매크로에 따라 달라지므로 구현과 쓰는 쪽이 같은 값을
     봐야 한다(`MA_NO_RESOURCE_MANAGER`·`MA_NO_ENCODING`·`MA_NO_GENERATION`, 백엔드는 WASAPI·Web Audio·null 만).
   - `AudioMixer`(`JBroAudio`): 보이스 풀·버스 그룹(0 Master, 1 EditorPreview, 2.. 프로젝트)·클립 등록·우선순위 → 들리는 크기 →
     나이 순의 결정적 훔치기·페이드·예약 시작·리스너. 공개 헤더는 miniaudio 를 보지 않는다(상태는 `OwnerPtr<State>`).
   - **고정 할당기**: miniaudio 의 할당을 2 의 거듭제곱 칸 빈 목록으로 받고, 초기화 때 보이스 수 × {모노·스테레오} × {공간화 켬·끔}
     으로 소리를 만들었다 지워 예열한다. 300 프레임(재생·위치·멈춤·버스·피치 섞음) 동안 **CRT 할당 0 회, 할당기 증가 0 회**.
   - 렌더 스레드가 계속 당기는 동안 클립을 200 번 등록·재생·해제·메모리 반납해도 풀린 메모리(디버그 힙 0xDD)가 나오지 않는다.
     ASan 대신 이것으로 봤다 - 이 저장소에 ASan 구성이 없다. **`UnregisterClip` 의 최악 멈춤은 21 µs**(보이스 하나, Debug).
   - 피치 1 에서 0.2 초 동안 영점 교차 175, 피치 2 에서 352.
   - 뮤테이션 넷(예열 제거·재생 중 버스 변경 무효·해제가 보이스를 남김·훔치기가 가장 새것)이 모두 해당 테스트에서 죽는다.
   - 음성 테스트: `msbuild JBroEngine.slnx /p:JBroTierProbe=Audio` 가 `JBro/Audio/AudioMixer.h` 에서 C1083 이다.
   - 남긴 것: Vorbis 스트리밍 시작은 stb_vorbis 가 CRT 에서 할당한다(miniaudio 가 할당기를 넘기지 않음, `miniaudio.h:65636`).
   원래의 완료 조건: miniaudio(+ stb_vorbis) 서드파티 빌드 단위, `JBroAudioTypes`·`JBroAudio` 모듈,
   `AudioMixer`(내부 `ma_engine` noDevice·보이스 풀·버스 그룹·고정 할당기)·`Render`. 장치 없음.
   완료 조건: 사인파 클립 PCM 을 등록해 `Render` 로 당긴 결과가 기대값과 같다(볼륨·버스 볼륨·음소거·재생 중 버스 변경·피치 2 배에서
   주파수·루프 경계·끝남·훔치기 순서). `Play`·`Stop`·`Render` 가 도는 정상 프레임에서 힙 할당 0 회(카운팅 할당기, §9) - 고정 할당기
   블록 크기는 여기서 잰다. 다른 스레드가 `Render` 를 계속 당기는 동안 `UnregisterClip` 을 거듭해도 해제된 메모리를 읽지 않는다
   (ASan). `ma_sound_uninit` 이 메인 스레드를 멈추는 시간을 재서 적는다. 스크립트 타깃이 `JBroAudio` 를 include 하면 컴파일 실패
   (음성 테스트). miniaudio 헤더가 `JBroAudio` 공개 헤더로 새어 나가지 않는다.
2. `[완료]` (2026-09-25) **출력과 에셋.** 결과:
   - `IAudioOutput`·`AudioOutputDesc`·`AudioRenderCallback`(함수 포인터 + 사용자 자료)이 `Platform.h` 에 있고, Windows 는
     `WindowsAudio.cpp` → `Internal::CreateMiniaudioOutput`(`ma_device`, WASAPI) 이다. 웹은 Emscripten 빌드에서만 같은 것을 준다.
     장치를 못 열면 null 과 경고 한 줄이고 엔진은 소리 없이 돈다.
   - `EngineConfig::audioEnabled`(기본 참, 믹서)·`audioDeviceEnabled`(기본 거짓, 게임 호스트와 에디터만 참)·`audioMaxVoices`(64).
     `EngineInstance` 가 장치를 먼저 열어 그 형식으로 믹서를 만들고, 내릴 때 **장치 정지 → 믹서** 순서다.
   - `AudioData`(CPU 자료)와 `AssetSystem::GetAudio`. Decompressed 는 f32 PCM 전체, Streaming 은 파일 바이트 + 길이 탐침
     (`AudioDecoder.h` 의 `ProbeAudio`·`DecodeAudio`·`ComputeAudioPeaks`). 파일은 `IPlatform::ReadWholeFile` 로만 읽는다.
   - **해제 알림**: `SetAudioReleaseListener` 가 in-place 재로드·`CollectUnused`·`Unbind` 에서 자료를 풀기 **직전에** 부른다.
     재로드는 새 자료를 다 읽은 뒤에 알린다 - 읽기가 실패하면 재생 중인 소리를 끊지 않는다.
   - 임포트 옵션 `Audio.ImportOptions.mode` 는 `JBroAssetTypes` 의 `AudioImportOptions`(리플렉션)이고 메타 파일이 왕복한다.
   - 사람 확인(실제 스피커)은 5 단계의 에디터 확인과 함께 한다.
   원래의 계획: `IAudioOutput`·`IPlatform::CreateAudioOutput`(Windows `ma_device`), `Asset::AudioAsset` 로더(두 모드),
   `EngineInstance` 소유와 종료 순서.
   완료 조건: WAV·MP3·FLAC·OGG 각각 두 모드로 디코드한 앞부분이 참조값과 같다(오프라인). 한글·공백 경로. 초기화/종료 100 회 반복과
   재생 중 종료에서 크래시·잔존 스레드 없음. 실제 게임 호스트에서 들린다(사람 확인).
3. `[완료]` (2026-09-25) **2D 컴포넌트와 시스템**(3D 리스너와 시스템도 같이 섰다). 결과:
   - `Component::AudioSource`(JBroAudioTypes, 차원 무관)·`AudioListener2D`(`panDistance`)·`AudioListener3D`, `System::Audio2DSystem`·
     `Audio3DSystem`(실행 순서 450). 상태 기계는 차원 무관한 `System::AudioSystem::UpdateSource` 한 곳이다.
   - 리스너가 없으면 게임 카메라(`primary` 먼저) 자리에서 듣는다. 여럿이면 첫 것 + 경고 한 번.
   - 2D 의 가까운 소리가 한쪽 귀로 꺾이는 문제: 소스를 리스너 앞 `panDistance` 깊이에 두고 최소·최대 거리를 같은 깊이만큼 넓힌다
     (pan 은 miniaudio 에서 원자가 아니라 재생 중에 쓸 수 없다). 0.5 단위 옆 소리도 양쪽에서 들린다(테스트).
   - 도플러: 위치 차 / 프레임 시간으로 소스·리스너 속도를 잰다(`doppler > 0` 인 소스만).
   - 버스는 `AudioBusName { NameId }` 이고 파일과 인스펙터에는 이름 글자다(코덱). 재생 중 버스를 바꾸면 곧바로 옮긴다.
     목록에 없는 이름은 Master + 경고 한 번.
   - `.jproject` `AudioBuses` 읽고 쓰기. 부동소수는 **가장 짧게 왕복하는 글자**로 적는다 - `%.9g` 면 `0.8` 이 `0.800000012` 가
     되어 버스 블록이 저장만으로 바뀐다(D-189, 뮤테이션으로 확인). 새 프로젝트는 `Music`·`SFX` 로 시작한다.
     설정을 저장하면(`SetProjectFile`) 곧바로 믹서 버스를 다시 세운다.
   - 게임을 멈추면(`SetSimulationEnabled(false)`) 소스를 전부 풀어 처음으로 되돌리고, 다시 켜면 `playOnStart` 가 한 번 운다.
     오브젝트를 지우면 `AudioSource::OnDetached` 가 제 보이스를 멈춘다.
   - 8 소스(공간화·도플러·두 버스 섞음) 120 프레임에 **CRT 할당 0 회**.
   - 뮤테이션 여섯(재로드 알림 빠짐·playOnStart 재사용·정지 때 소스 남김·떼기에 보이스 남김·9 자리 부동소수·깊이 0)이 모두
     해당 테스트에서 죽는다.
   원래의 계획: `AudioSource`·`AudioListener2D`·`Audio2DSystem`, `.jproject` `AudioBuses` 읽고 쓰기, 캔버스 직렬화,
   해석 패스.
   완료 조건: non-loop `playOnStart` 정확히 1 회, 루프는 멈출 때까지, 끄고 켜기·클립 교체·떼기·플레이 중지 정책(§2.7), 여러 소스가
   독립, 리스너 거리에 따른 감쇠가 오프라인 렌더에서 보인다. 프로젝트 파일 거듭 저장 바이트 비교(D-189). 정상 프레임 할당 0 회.
4. `[완료]` (2026-09-25) **스크립트 서비스.** 결과: `Service::AudioService`(PlayOneShot·PlayOneShotAt·Play/Stop/Pause/Resume·
   IsPlaying·GetTime·버스 볼륨/음소거·StopAll). 버스를 글자로 주면 해시만 계산한다(할당 없음). `AudioServiceContext`·
   `AudioSystemContext` 는 D-37 확장 블록이고 **호스트가** 낸다(네트워크와 같다). 두 프렐류드가 `AudioSource` 와 서비스를 보인다.
   `JBroScriptIncludes` 에 `JBroAudioTypes` 가 들었고 스크립트 프로브가 그 라이브러리를 링크한다. 미리 듣기는 `StopAll` 의 대상이
   아니다.
   원래의 계획: `Service::AudioService`, 확장 블록, 프렐류드·`JBro.Script.props`.
   완료 조건: 스크립트에서 `PlayOneShot`·버스 볼륨이 믹서에 닿는다. 핫 리로드 뒤 재바인딩. 음성 테스트.
5. `[완료]` (2026-09-25) **에디터.** 결과:
   - 인스펙터: `clipId` 는 오디오 에셋 드롭다운(`clip` → Audio 별명), `bus` 는 프로젝트 버스 드롭다운(`JBro.AudioBusName`)이다.
     목록에 없는 이름은 지우지 않고 회색 항목으로 남기며 "Master 로 재생됩니다" 를 알린다. 둘 다 이름을 치고 Enter 로 고르고 커맨드
     하나다(테스트가 실제 인스펙터를 눌러 잰다).
   - 오디오 에셋: 형식(Hz·채널)·길이, 파형(`Widget::Waveform`, 512 칸, 누르면 그 자리부터 듣기), 재생/정지, 반복 재생, 임포트 옵션
     블록(`Audio.ImportOptions`). 미리 듣기는 믹서의 EditorPreview 버스이고 게임의 `StopAll` 이 건드리지 않는다. 다른 것을 고르면
     다음 프레임에 멈춘다. 에셋을 붙잡지 않는다 - 내려가면 해제 알림이 미리 듣기를 끊는다.
   - `AudioMixer::Seek`(miniaudio 의 원자 `seekTarget`)와 `AudioSystem::SeekPreview`.
   - 프로젝트 설정: 오디오 갈래(버스 이름·음량 슬라이더·삭제·버스 추가, 겹친 이름과 `Master` 알림). 저장하면 곧바로 믹서 버스가 다시 선다.
   - 실제 에디터: `EditorApplicationConfig::audioDevice` 를 에디터 호스트만 켠다.
   - 옮기지 않은 것: 기존의 스펙트럼 시각화(`ImSpectrumVisualizer`)와 믹서 창(버스 미터). 미터 자리는 `AudioMixer::Stats::lastPeak`
     (Master 만)다 `[열림]`. 오디오 임포터 창은 두지 않는다 - 스프라이트처럼 가져오기 뒤 인스펙터에서 옵션을 고친다.
   원래의 계획: 인스펙터(버스는 프로젝트 목록 콤보), 오디오 임포트 옵션, 미리 듣기(EditorPreview 버스, 파형), 프로젝트 설정의
   버스 목록 편집(커맨드). 기존 `ImAudioBusField`·`ImAudioVisualizer`·`EditorAudioPreview`·`AudioImporterWindow` 를 먼저 읽고 옮긴다
   (§11.0). 화면 글자는 로컬라이징 키(§11.2).
   완료 조건: 실제 `JBroEditorHost` 에서 조작해 본다(검증 규약). 편집은 커맨드이고 되돌린다.
6. `[완료]` (2026-09-25) **이펙트와 3D.** 3D 리스너와 시스템은 3 단계에서 함께 섰다. 이펙트는 계획을 바꿨다(D-202):
   `.jfx` 에셋 대신 **버스마다 고정 사슬**(고역 차단 → 저역 차단 → 메아리 → 잔향, 0 이면 끔). 결과:
   - `AudioBusEffects`(값 타입)·`AudioMixer::SetBusEffects`/`GetBusEffects`·`BusEffectNode`(miniaudio 사용자 노드, 그룹과 부모 사이).
   - 5 kHz 톤이 300 Hz 저역 통과에서 0.5 → 0.0017, 100 Hz 톤이 3 kHz 고역 통과에서 0.05 아래, 0.05 초 소리가 0.2 초 뒤 0.25 로
     되울리고(사이 0), 잔향 꼬리가 소리 뒤 0.43.
   - 모든 칸을 켜 둔 채 매 프레임 컷오프를 바꾸는 200 프레임에 CRT 할당 0 회. 다른 스레드가 당기는 동안 2000 번 켜고 꺼도
     유한하지 않은 샘플이 없다.
   - `.jproject` 는 기본값과 다른 칸만 적는다. 스크립트 `SetBusEffects`·`SetBusLowPass`, 프로젝트 설정의 "이펙트" 마디.
   - 뮤테이션 넷(저역 통과 건너뜀·메아리 지연 0·잔향 버퍼를 넘기지 않음·기본값도 적음)이 모두 죽는다.
   - 실제 장치: `스피커(Realtek High Definition Audio)` 가 약 0.1 초에 7680 프레임을 당겼고 `Stop` 뒤에는 당기지 않는다(테스트,
     소리 없음). 실제 에디터(`--frames 240`)가 장치를 열고 깨끗이 닫는다. 통계 창에 장치·보이스·최대 음량·빼앗긴 수가 선다.

7. `[완료]` (2026-09-26) **라우팅·스트리밍·장치**(D-203). 결과:
   - 버스 중첩(곱해진 음량 0.125 = 0.5 × 0.5 × 소리 0.5), 버스마다 미터, 센드가 원음 0 의 잔향 버스를 먹이고(0.43) 끊으면 멎는다.
     되돌아오는 센드·자기 자신·Master 의 센드는 거절된다. 솔로는 자식의 조상을 열어 두고 풀면 전과 같다.
   - 보이스 필터: 5 kHz 보이스가 제 300 Hz 저역 통과에서 0.0017, 같은 버스의 다른 보이스는 그대로. 켜고 끄기 200 번에 CRT 할당 0 회.
     소스의 `lowPass` 100 Hz 가 재생 중 0.25 → 0.013.
   - 출력 이득 0.1 초 페이드: 중간 0.29, 끝 0, 샘플 사이 가장 큰 뜀 0.08 아래(클릭 없음). 스펙트럼: 1 kHz 사인이 848~1046 Hz 칸에
     0.91(-6 dB), 먼 칸은 0.3 이상 낮다.
   - 디스크 스트리밍(경로에 한글): 1 초짜리를 3 초 되풀이해 가장 조용한 0.1 초 창이 0.5, 끊김 0 회. 0.5 초로 옮기면 커서 0.5.
     자리 둘에 셋째는 거절, 되풀이가 아닌 것은 끝나면 거둬진다. 에셋은 경로만 들고 파형은 파일을 한 번 흘려 읽어 그린다.
   - 장치: 이 기계의 출력 6 개, 목록의 이름으로 연 장치가 그 이름을 말하고 없는 이름은 열리지 않는다. 가짜 플랫폼으로 호스트를
     재어: 고르면 옛 장치를 닫고 새로 열고, 없는 이름은 기본으로, 뽑히면 다음 프레임에 다시 열고, 아무것도 안 열리면 2 초 뒤에
     다시 시도해 되살아난다. 포커스 정책은 켜 둔 동안만 출력을 0 으로 했다가 되돌린다. 스크립트가 장치를 나열하고 고른다.
   - 프로젝트 설정: 출력 장치 목록(시스템 기본 포함, 새로 고침)·`AudioMuteWhenUnfocused`·버스마다 "라우팅" 마디(Parent·Send·
     SendLevel)·"이펙트" 의 `Dry`. 통계 창: 버스마다 미터와 솔로, 스펙트럼. 마디를 연 채 그리는 UI 시험이 표의 ID 쌓기를 지킨다.
   - 뮤테이션: 열 개(센드 되돌림 검사 없앰·솔로가 조상을 닫음·Dry 무시·Play 의 보이스 필터 무시·출력 이득이 뜀·스트림 위치 옮기기 무시·스트림 되풀이가 끝남·뽑힌 장치를 그대로 둠·포커스 무시·Parent 를 적지 않음)가 모두 죽는다.

8. `[완료]` (2026-09-26) **페이드·더킹·트림**(D-205). 결과:
   - 버스 음량 0.1 초 페이드가 중간에 0.29, 끝에 0, 샘플 사이 뜀 0.05 아래. 음소거는 10 ms 에 걸쳐 멎는다(클릭 없음).
   - 더킹: 대사 버스가 울리는 동안 배경음 버스가 0.5 → 0.25, 끝나면 0.5 로 돌아온다. 자기 자신 아래로는 걸리지 않는다.
   - 트림 0.5 의 클립이 0.25 로, 보이스 음량 0.5 를 더하면 0.125. 메타의 `gain` 이 에셋 경로(디스크 스트리밍)로 믹서에 닿는다.
   - `.jproject` 더킹 키와 스크립트 `FadeBusVolume`, 프로젝트 설정의 "라우팅" 에 DuckBy·DuckAmount·DuckRelease.
   - 뮤테이션: 여섯(더킹이 물러서지 않음·페이드 시간 무시·버스 음량이 뜀·Play 의 트림 무시·에셋 트림을 넘기지 않음·더킹을 적지 않음)이 모두 죽는다.

9. `[완료]` (2026-09-26) **되돌아가기·사용자 처리기·웹 실측**(D-206). 결과:
   - 가짜 플랫폼으로 호스트를 재어: 알림이 없으면 고른 장치를 찾지 않고, 알림과 함께 그 장치가 돌아오면 그리로 옮기며 기본
     장치를 닫는다. 실제 Windows 감시는 켜고 내릴 때 멈추지 않는다(플랫폼을 내리면 스레드가 끝난다).
   - 버스 처리기가 버스를 절반으로 줄이고 떼면 돌아온다. 오디오 스레드가 당기는 동안 300 번 걸고 떼며(300 번 모두 처리기가
     돈 뒤에 뗌) 뗀 뒤에 불리거나 부르는 중으로 남은 일이 없다.
   - 웹: `MiniaudioAudioOutput` 이 Emscripten 으로 빌드되고 Web Audio 로 열린다(48000 Hz, 2 채널). 자동 재생을 허락하는 창에서
     첫 입력 없이 당겼고 "기다림" 은 두 틱 만에 내려간다(고치기 전에는 끝내 참). 누름 한 번에 `unlocked` 가 온다.
   - 뮤테이션 셋(처리기를 부르지 않음·떼기가 기다리지 않음·다시 꽂힌 장치를 무시)이 모두 죽는다. 떼기가 기다리지 않는 것은
     처음 시험(빠른 처리기)에서 살아남아, 한 번에 0.3 ms 걸리는 처리기와 "돌아온 순간 부르는 중인가" 검사로 바꾼 뒤 죽었다.
   - 실제 스피커 시험은 `JBRO_AUDIO_DEVICE_TEST=1` 일 때만 돈다(켜고 돌려 통과: 7680 프레임, 장치 6 개).
   - 자동 재생을 막는 모양(입력 전 `AudioContext` 멈춤·`resume` 거절)의 무음 실측: 입력 전 당긴 프레임 0 이고 "기다림" 참, 누름
     한 번에 풀려 당긴다. 오디오 파일 전부가 Emscripten 으로 컴파일된다 - 64 비트에서 잰 크기 단언과 MSVC 전용 필드 이름 뽑기를
     고쳤다(D-206 (6), MSVC 결과는 그대로).

10. `[완료]` (2026-09-26) **남은 이펙트**(D-210). 결과:
   - EQ: 60 Hz 사인 0.1 이 +12 dB 낮은 선반에서 0.39, 12 kHz 0.4 가 -12 dB 높은 선반에서 0.096, 1 kHz 0.2 가 +6 dB 가운데에서 0.40.
   - 디스토션을 다 걸면 봉우리/실효값 비가 1.41(사인) → 1.05(네모에 가깝다). 코러스는 0.1 초 창의 크기가 0.12~0.40 로 오르내린다.
   - 피치 시프트: 440 Hz 가 +12 반음에서 초당 영점 교차 1760, -12 반음에서 440(원래 880) - 정확히 두 배와 절반이다.
   - 컴프레서: -6 dB 사인이 문턱 -20·비율 4 에서 0.16, 줄인 양 9.8 dB. 메이크업이 되돌리고 비율 1 이면 꺼진다.
   - 리미터: 0.8 사인 둘(합 1.6)이 0.98 로 눌리고 미터는 1.6 을 보인다. 끄면 1 에서 잘린다.
   - 새 칸을 모두 켜고 매 프레임 바꾸는 200 프레임에 CRT 할당 0 회, 다른 스레드가 당기는 동안 2000 번 바꿔도 찢긴 샘플이 없다.
   - 뮤테이션 여섯(선반 뒤집기·디스토션 빼기·코러스 흔들기 빼기·피치 방향 뒤집기·비율 무시·리미터 끄기)이 모두 죽는다.
     믹서는 Emscripten 으로도 컴파일된다.

## 4. 규칙과 부딪히는 지점

- **§2 파일 IO**: 스트리밍이 파일을 직접 열면 위반이다 → 메모리 스트리밍은 압축 바이트를 메모리에 두고(§2.2), 디스크 스트리밍은
  `IPlatform::OpenFileStream` 으로만 연다(D-203).
- **§5 ServiceContext 에 하드웨어 금지**: 기존 `Script.Audio` 는 위반이었다 → 값 서비스와 확장 블록(§2.8).
- **§9 매 프레임 할당·문자열 비교 금지**: 기존 시스템의 해시맵·`seen` 집합·문자열 버스 비교 → 고정 풀과 `NameId`(§2.4·§2.6).
- **§10.4 컴포넌트 공개 필드의 String 금지**: 버스 이름 → `NameId`.
- **§6 SafePtr 는 메인 스레드 전용**: 오디오 스레드는 `SafePtr` 도 에셋도 보지 않는다. 클립은 믹서가 들고, 그 클립의 보이스를 `ma_sound_uninit`(오디오 스레드를 기다림)한 뒤에 푼다(§2.2).
- **D-60 서드파티를 감싸지 않는다**: `ma_engine` 은 `AudioMixer` 의 private 이고 miniaudio 헤더는 `.cpp` 만 본다. 라이브러리
  헤더를 감싼 두 번째 표면이 아니라 엔진 경계다(§2.3).
- **ThirdParty README "각 라이브러리는 자기 빌드 단위"**: miniaudio 는 자기 vcxproj 이고 JBroPlatform·JBroAudio 둘이 링크한다.
  구현 번역 단위가 하나라 중복 정의가 없다.

## 5. 열린 것과 가정 모음

- 2026-09-25 에 정해진 것: 임포트 옵션은 파일의 속성만(지금은 `Mode`, §2.2), 모듈 이름은 `JBroAudioTypes`·`JBroAudio`(§2.1),
  MaxVoices 64(§2.4), 기존 캔버스·메타의 이식은 하지 않음(§2.2·§2.7), 믹서는 내부 `ma_engine`(D-198, §1.5), 3D 전용 소스 필드는
  공용 소스 밖(§2.7).
- 7 단계(D-203)로 닫힌 것: 버스 중첩·솔로·센드, 디스크 스트리밍, 보이스 필터, 장치 고르기·핫 언플러그·포커스 정책·웹 자동 재생
  알림, 스펙트럼과 버스 미터.
- 9 단계(D-206)로 닫힌 것: 다시 꽂힌 장치로 되돌아가기, 사용자 이펙트(엔진 쪽 버스 처리기), 웹 자동 재생 실측(한 브라우저).
- `[열림]` 웹 호스트 전체 빌드 - 오디오 쪽은 컴파일된다(D-206 (6)). 남은 것은 렌더러의 웹 백엔드와 `WebPlatform` 의 창·입력으로,
  엔진의 웹 이식이다. 두지 않기로 한 것: 스크립트가 거는 처리기(D-206), 보이스 단위 메아리·잔향(D-202).
- `[가정]` 재로드는 쓰는 보이스를 멈춘다(§2.2), 엔진·디코더 할당은 슬롯별 고정 블록이고 넘치면 `Play` 실패(§2.4), 필터 `reinit` 은
  노드 안에서 오디오 스레드가(§2.5), Web 콜백(§2.5), `audioEnabled`·MaxVoices 설정 자리(§2.9·§2.4), 미리 듣기는 Master 음소거와
  무관(§2.9).
- 기존 백로그의 자리(D-205): 페이드(보이스 페이드 1 단계, 버스 페이드 8 단계), PlayAt(예약 시작), 믹서 창(통계 창의 미터·솔로·
  스펙트럼), 덕킹·트림(8 단계), 감쇠 곡선·도플러(3 단계), occlusion(보이스 필터, 7 단계), 프로파일러(통계 창). 두지 않기로 한 것:
  PlayOneShot 반환 핸들, 재생 마커 콜백, 스냅숏 에셋, 자동 라우드니스 정규화(까닭은 D-205).
