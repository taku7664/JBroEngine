# Audio System Review

## 목적

JBroEngine 사운드 계층의 전체 구조를 정리하고, 코드 검토(5회 정독) 중 발견한 버그·최적화 지점을 기록합니다.

검토 범위:

- `Engine/Core/Audio/*` — 백엔드 추상화 / miniaudio 구현 / Empty 폴백
- `Engine/Core/Asset/AudioAsset.*` — 오디오 자산 로딩·디코딩
- `Engine/GameFramework/Audio/*` + `Component/AudioComponents.h` — 런타임 재생 시스템
- `Application/Editor/Main/Inspector/AssetInspectorPreview.cpp` + `EditorAudioPreview.*` — 에디터 미리듣기
- `Engine/Editor/ImItem/ImAudioVisualizer.*` + `ImSpectrumVisualizer.*` — 파형/스펙트럼 애널라이저
- `Engine/ThirdParty/miniaudio/miniaudio.h` + `stb/stb_vorbis.c` — 디코더 내부 동작

검토 방식: 한 번 통독 후 동일 코드를 총 5회 재독하며 가설 검증.

---

## 사운드 계층 구조

RHI(`IRHIDevice`)와 동형의 백엔드 추상화 패턴.

### 백엔드

- `IAudioDevice` — 백엔드 추상 인터페이스 (생성/Tick/listener/글로벌볼륨/정밀싱크)
  - `CMiniAudioDevice` — miniaudio 실제 구현 (`JBRO_HAS_MINIAUDIO=1` 일 때만 활성)
  - `CEmptyAudioDevice` — no-op 폴백 (헤드리스/CI/디바이스 없음)
- `IAudioPlayer` — 재생 1 인스턴스 (트랜스포트/싱크/볼륨/공간음향)
  - `CMiniAudioFilePlayer` — `ma_sound` 기반, 파일 경로에서 직접 생성 (현재 유일한 실재 player)
  - `CMiniAudioPlayerStub` / `CEmptyAudioPlayer` — no-op
- `IAudioBus` / `IAudioEffect` / `IAudioListener` — 인터페이스만 존재, 구현은 골격(단계 G 예정)

### 자산

- `CAudioAsset` — `.wav/.mp3/.flac/.ogg` 를 디코딩.
  - **Decompressed**: 전체 PCM(F32/2ch/48kHz 정규화)을 메모리에 보관.
  - **Streaming**: 파일 경로만 보관, 재생 시 디코딩.
- `CAudioAssetLoader` — miniaudio 의 `ma_decoder` 직접 호출. 헤더엔 백엔드 비노출.
- 임포트 옵션은 YAML 직렬화 (`CAudioImportOptions`), `.Jmeta` 안에 저장.

### 런타임 재생

- `CAudioSystem` — 씬 단위. `AudioListener`/`AudioPlayer` 컴포넌트를 매 프레임 갱신.
  - 인스턴스 생성/자산변경 재생성/disable 시 즉시 해제/non-loop ended GC/파괴 엔티티 청소.
  - **편집 모드에서 비동작** (`ShouldUpdateInEditMode()=false`).
- `Engine.Audio` (런타임 디바이스) 1개.

### 에디터 미리듣기

- `EditorAudioPreview` — 런타임과 **분리된 독립 `ma_engine`**. lazy-init 싱글톤, 한 번에 한 자산.
- `AssetInspectorPreview` — Enter/Stay/Exit 핸들러. PCM 으로 파형/스펙트럼 애널라이저 구동.

---

## 검토 결과 — 메모리 "잔여" (사용자 보고) → 누수 확정, 수정 완료

> Audio 미리듣기하고 해제하면 사운드 자체 메모리는 내려가는데, 내부 잔여 메모리가 안 내려감.

### 초기 가설 (반증됨)

처음엔 "miniaudio 엔진 인프라 1회 lazy 할당, 비버그" 로 봤다. **사용자가 3개 미리듣기 메모리 그래프(뚜렷한 3계단, 해제 후 베이스라인 유지) 로 반증.** 1회 인프라면 평평해야 하는데 계단식 → 누수 맞음. 가설 폐기.

### 진짜 원인 (코드 근거 확정)

`AssetManager` 의 자산 캐시가 프리뷰 후 안 비워진다:

1. `CAssetManager::LoadAsset`(`AssetManager.cpp:295-314`) 은 로드한 자산을 `m_loadedAssetTable` 에 **영구 보관**. 자동 GC 없음.
2. 해제는 **오직 `UnloadAsset(guid)` 명시 호출** 시 + use-count==0 일 때만(`AssetManager.cpp:394-421`).
3. `CAudioPreviewHandler::OnEnter` 는 `LoadAsset(guid)` 로 자산을 디코딩(F32/2ch/48kHz PCM, 큰 용량)하지만, **어디서도 `UnloadAsset` 을 부르지 않았다.** 지역 `AssetRef` 소멸로 use-count 만 0 → 자산 PCM 은 테이블에 그대로 잔류.

따라서:

- **사운드 메모리(`ma_sound`) = 내려감** ✓ — `EditorAudioPreview::Stop` 이 `ma_sound_uninit` (동기 해제).
- **잔여(자산 PCM) = 안 내려감** ✓ — AssetManager 캐시 잔류. 미리듣기 N개 = N계단.

### 적용된 수정 (`AssetInspectorPreview.cpp` — CAudioPreviewHandler)

- `OnEnter` 에서 로드한 guid 를 멤버 `m_loadedGuid` 에 저장.
- `OnExit` 에서 `m_spectrum.Unbind()` (자산 PCM 포인터 참조 해제) **후** `am->UnloadAsset(m_loadedGuid)` 호출.
  - 순서 중요: 스펙트럼은 자산 PCM 을 포인터로 직접 참조(복사 아님) → Unbind 가 UnloadAsset 보다 먼저여야 dangling 회피.
  - 안전장치: 인스펙터/씬 등 다른 사용자가 같은 자산을 쓰면 use-count>0 → `UnloadAsset` 가드가 해제 거부(`AssetManager.cpp:406`). 프리뷰만의 일회성 로드만 정리됨.

### 미검증

빌드는 통과. 실제 메모리 그래프가 평평해지는지 사용자 측 재현 필요(VS 진단 스냅샷 또는 동일 3회 미리듣기 반복).

---

## 검토 결과 — OGG 애널라이저 일부 파일 동작 안 함 (사용자 보고)

> OGG 미리보기 애널라이저가 원래 다 됐는데 갑자기 몇몇 파일만 안 됨.

### 결론: stb_vorbis 길이 추정 실패 + 엔진 로더의 length 의존 → 수정 가능

#### 메커니즘

1. `AssetInspectorPreview::OnEnter` 는 `CAudioAsset::GetPcmData()` 가 비면 파형/스펙트럼 둘 다 안 띄움.
2. `CAudioAssetLoader::Load`(`AudioAsset.cpp:236-262`) 흐름:
   ```cpp
   ma_decoder_get_length_in_pcm_frames(&decoder, &totalFrames);   // OGG에서 0 가능
   std::vector<uint8_t> pcm(totalFrames * bytesPerFrame);         // 0 → 빈 벡터
   ma_decoder_read_pcm_frames(&decoder, pcm.data(), totalFrames, &framesRead);  // 0개 요청
   ```
3. `totalFrames=0` 이면 버퍼 0, 읽기 0 → PCM 비어 애널라이저 죽음.

#### length 가 0 이 되는 경로 (검증 완료)

- 로더는 `ma_decoder_init_file`/`_init_memory` 사용 → stb_vorbis **pull mode** (push mode 아님).
  - push mode 였다면 `ma_stbvorbis_get_length_in_pcm_frames` 가 항상 0 (miniaudio.h:66031, 주석: push mode 에서 길이 신뢰 불가). 하지만 file/memory 는 pull mode 라 해당 없음.
- pull mode 길이 = `stb_vorbis_stream_length_in_samples` (stb_vorbis.c:4946).
  - 마지막 Ogg 페이지의 granule position 으로 역산.
  - `vorbis_find_page` 가 마지막 페이지 못 찾으면 `total_samples=SAMPLE_unknown` → **return 0** (stb_vorbis.c:4971-4976, 5018).
  - granule 이 0xffffffff/0xffffffff 여도 → 0 (stb_vorbis.c:5002-5006).
  - 끝 페이지가 64KB 초과로 떨어져 있거나, 컨테이너 끝에 trailing 데이터(앨범아트/메타 chunk 추가) 가 붙으면 발생.

#### "원래 됐는데 갑자기 몇몇만" 의 정합성

그 파일들이 **재인코딩되었거나 태그 에디터로 끝부분 구조가 바뀐** 것. 파일별 차이라 일부만 깨짐. **엔진 코드가 망가진 게 아님** — stb_vorbis 길이 추정의 알려진 취약점 + 입력 파일 차이.

#### 핵심: 디코딩 자체는 length 와 독립

`ma_decoder_read_pcm_frames` 는 실제 디코딩이라 length 추정이 실패해도 프레임은 끝까지 읽힌다. 현재 코드만 length 를 신뢰해 0개를 요청하는 게 문제. **청크 단위로 EOF 까지 읽으면 length=0 OGG 도 복구 가능.**

---

## 토론거리 (검토 후 정리 — 사용자 확인 대기)

### D1. 로더의 OGG 경로 — native codepage → 한글 경로 디코딩 실패 (확정, 수정 결정: 확인 후 wide 통일)

`AudioAsset.cpp:228` 의 `desc.ResolvedPath.string()` 은 Windows native 코드페이지 문자열을 `ma_decoder_init_file(char*)` 에 넘긴다. 미리듣기 player(`MiniAudioDevice.cpp:67-74`)는 UTF-8→wide 변환 후 `_w` 변종을 쓰는데, **에셋 로더는 변환을 안 한다.**

검증 (miniaudio 내부):

- `ma_fopen`(miniaudio.h:13283) → `fopen_s(char*)` = native codepage. UTF-8 아님.
- OGG 는 더 직접적: `ma_stbvorbis_init_file`(miniaudio.h:65593) → `stb_vorbis_open_filename(char*)` → 내부 `fopen` = native codepage.
- 따라서 한글 경로 OGG = native 해석 실패 → 파일 못 엶 → 디코딩 실패 → PCM 0 → 애널라이저 죽음.

영향: 한글 등 비-ASCII 경로 OGG. 메모리 페이로드(`HasMemoryPayload`)는 무관. "몇몇만" 과 부분 일치(비-ASCII 경로 파일만) — length 추정 실패와 **독립적인 2번째 원인**.

**중요 — D2 와 결합 필수**: `ma_decoder_init_file_w`(miniaudio.h:10042) 로 고치면 한글 경로는 열리지만, `_w` 경로는 VFS 콜백 기반 → stb_vorbis 가 **push mode** 로 init → `ma_stbvorbis_get_length_in_pcm_frames` 가 **항상 0**(miniaudio.h:66031). 즉 wide 변환만 하면 한글 OGG 가 열려도 length=0 으로 PCM 이 또 안 채워진다. **D1(wide) 과 D2(청크 폴백)는 함께 적용해야 완전.**

### D2. length 의존 디코딩 → 청크 읽기 폴백 (수정 결정: length=0 분기만 폴백)

위 OGG 결론과 직결. `ma_decoder_read_pcm_frames` 는 length 와 독립이라 청크 단위로 EOF 까지 읽으면 length 추정 취약 OGG / push mode OGG 모두 복구. 결정된 범위 = **`totalFrames==0` 분기에서만** 청크 루프 폴백(서지컬, 정상 파일 경로 불변). CLAUDE.md 최소영향 원칙 부합.

### D3. `CreatePlayerFromFile` 가 인터페이스 밖 (RTTI 의존)

`IAudioDevice` 에 없고 `CMiniAudioDevice` 전용 → `AudioSystem.cpp:114` 가 `dynamic_cast`. 백엔드 교체(웹 등 별 백엔드) 시 게임 시스템이 mini 를 직접 앎. `AudioPlayerDesc::StreamPathUtf8` 경로로 통일하거나 인터페이스로 승격이 깔끔. **현재 버그는 아님.**

### D4. 런타임 동일 자산 N 엔티티 — 중복 디코딩? → 반증됨 (현재 비버그)

초기 우려: `CAudioSystem` 의 모든 player 가 `CreatePlayerFromFile`(`MA_SOUND_FLAG_DECODE`) → 같은 자산 N 엔티티면 N번 디코딩 + N개 PCM.

**반증** (miniaudio 문서/구현):

- ma_engine 은 파일 경로 기반 **ref-count 캐시 내장**(miniaudio.h:1313-1316): 같은 경로를 N번 `ma_sound_init_from_file` 해도 메모리엔 **1회만 디코딩**, ref-count 증가. 마지막 uninit 시 해제.
- 조건 1 — 같은 경로 문자열(64bit 파일명 해시 비교): `AudioSystem.cpp:116` 가 `generic_u8string()` 으로 항상 `/` 정규화 → 일관 ✓.
- 조건 2 — non-stream(decode 모드): 현재 `MA_SOUND_FLAG_DECODE` ✓ (캐시는 stream 에 미적용).

**결론: 현재 버그/비효율 아님** — resource manager 가 자동 공유. 단 향후 (a) 한글경로 대응으로 `_w`/VFS 경로 전환 시 또는 (b) Streaming 모드 player 도입 시 캐시가 빠지므로 그때 재검토.

### D5. `CMiniAudioDevice::Tick` 비어있음

`IsEnded` 기반 GC 가 게임 `OnUpdate` 프레임에 의존. 프레임 멈추면(에디터 일시정지) ended player 안 치워짐. 프리뷰는 단일 player 라 무관, 런타임만 잠재 이슈. 단계 G 에서 marker dispatch 와 함께 채울 자리.

### D6. miniaudio 엔진 2개 (런타임 + 프리뷰)

각각 독립 `ma_engine`. 프리뷰 첫 사용 시 인프라 베이스라인이 한 번 더 오름(위 "메모리 잔여" 와 연관). 설계상 격리 의도는 명확하나, 디바이스 인프라 2배. 단일 엔진 공유 + bus 분리로도 격리 가능 — 트레이드오프 검토 대상.

---

## 적용된 수정 (D1 + D2, `AudioAsset.cpp::Load`)

결정된 범위대로 `CAudioAssetLoader::Load` 한 함수만 수정. 정상 파일 경로는 불변.

1. **D1 — 경로 wide 변환** (`_WIN32` 분기): `ma_decoder_init_file` → UTF-8 → wide → `ma_decoder_init_file_w`. 미리듣기 player 와 동일 패턴. 한글 등 비-ASCII 경로 OGG 디코딩 복구. 비-Windows(웹 등)는 기존 `ma_decoder_init_file` 유지 — 멀티플랫폼 동작 보존.
2. **D2 — length=0 청크 폴백**: `totalFrames==0` 일 때만 4096 프레임 청크 루프로 EOF 까지 디코딩. `_w`/VFS 경로의 push mode(length 항상 0) 와 끝 페이지 손상 OGG 둘 다 PCM 복구. `totalFrames>0` 일반 경로는 기존 1회 읽기 유지.

두 수정은 결합 필수 — D1 만 하면 한글 OGG 가 열려도 push mode length=0 으로 PCM 이 비므로 D2 가 받쳐줘야 완전.

## 검증

- `Engine.vcxproj` (Debug_Editor|x64 → Debug|x64) 빌드 성공, `AudioAsset.cpp` 재컴파일 통과.
- 전체 솔루션 빌드 성공 → `Build/Debug_Editor/Application.exe` 생성. (yaml-cpp LNK4099 PDB 경고는 기존 이슈, 본 변경 무관.)

## 곁다리 버그 — .jmat 가 Material 로 인식 안 됨 (수정 완료)

G-1 작업 중 발견. `CProjectManager::DetectAssetType`(`ProjectManager.cpp:1890`) 이 타입 결정 유일 경로인데 `.jscene`/`.jprefab` 은 있고 **`.jmat` 매핑이 없었다** → AssetBrowser 가 만든 `.jmat` 파일이 `EAssetType::Custom` 으로 import → `[MAT]` 라벨/`CMaterialAssetLoader` 안 걸림.

수정: `.jmat` → `EAssetType::Material` 매핑 추가(`.jfx` 옆). 빌드 통과. (런타임 재현은 사용자 측 — 새 Material 생성 시 `[MAT]` 로 뜨는지 확인.)

## Not Verified

- 실제 깨지는 OGG 파일로 런타임 재현 미수행 (리포 내 OGG 에셋 없음 — 사용자 프로젝트 외부).
  실제 깨지던 파일을 다시 미리듣기해 파형/스펙트럼이 뜨는지 사용자 측 확인 필요.
- 한글 경로 OGG 가 실제로 D1 수정으로 살아나는지 런타임 재현 미수행.
- 메모리 누수 수정은 사용자 측 실측 확인됨 — 미리듣기 해제 시 메모리 정상 하강.
- G-1/G-2 런타임 미검증: `.jfx` 생성·편집·재로드, EffectGuid 인스펙터 슬롯 드롭/직렬화 round-trip (에디터 실행 필요).
- `.jmat` 수정 후 Material 정상 인식 런타임 미검증.

---

# DSP 효과 (Reverb / Filter / ...) 설계 방향

## 핵심 질문 — raw 를 바꾸나? 읽기 방식이 바뀌나?

**둘 다 아니다.** 정답은 **노드 그래프(처리 체인) 삽입.**

- 자산의 raw PCM(`CAudioAsset::m_pcmData`) 은 **불변**. 효과는 디스크/메모리 원본을 절대 안 건드린다.
- "메모리 읽기 방식" 도 안 바뀐다. 디코딩·캐시·재생 위치 로직 그대로.
- 바뀌는 것은 **재생 시 샘플이 흐르는 경로**. miniaudio 의 `ma_engine` 은 이미 노드 그래프(`ma_node_graph`):

  ```
  ma_sound(소스 노드) → [DSP 노드: reverb/lpf/...] → ma_sound_group(bus) → endpoint(출력)
  ```

  DSP 노드가 매 오디오 콜백마다 흐르는 PCM 프레임을 실시간 변환(`process_pcm_frames`). 원본은 읽기 전용으로 통과만 한다. 같은 자산에 효과 A/B 를 다르게 걸어도 PCM 은 하나만 캐시(ref-count 공유) → 메모리 이득 유지.

### 비-실시간(파괴적) 편집은 별개 모드

만약 "원본 WAV 에 reverb 를 구워서 새 자산으로 저장(bake)" 이 목표라면 그건 raw 를 바꾸는 오프라인 처리 — 별도 기능. 게임 런타임/믹서 효과는 위의 실시간 노드 그래프가 맞다. 우선순위는 실시간. bake 는 필요 시 에디터 도구로 추가.

## miniaudio 토대 (이미 깔려 있음)

- 노드 그래프: `ma_sound`/`ma_sound_group` 자체가 노드(miniaudio.h:1030). `ma_node_attach_output_bus` 로 체인 구성.
- 내장 필터: `ma_lpf`/`ma_hpf`/`ma_peak2`/`ma_loshelf2`/`ma_hishelf2`/`ma_notch2`/`ma_delay`(에코) 등 2차 IIR 다수.
- reverb 는 miniaudio 내장 노드 없음 → `ma_delay` 조합 또는 Freeverb 류 직접 노드 구현 필요.
- 효과 노드: 커스텀 DSP 는 `ma_node` vtable(`onProcess`)로 구현해 그래프에 삽입.

## 확정 방향 — 효과는 "에셋" (Material 패턴)

결정: **Reverb 등 효과를 독립 에셋으로 만들고, AudioPlayer 가 옵션으로 GUID 참조.**
스프라이트의 Material 과 동형.

| Sprite 측 | Audio 측 (신규) |
|---|---|
| `CSpriteAsset` (픽셀 PCM) | `CAudioAsset` (PCM) — 기존 |
| `CMaterialAsset` (큐/파라미터) | **`CAudioEffectAsset`** (효과종류 + 파라미터) — 신규 |
| `SpriteRenderer2D.MaterialGuid` (직렬화 GUID) | `AudioPlayer.EffectGuid` (직렬화 GUID, 옵션) — 신규 필드 |
| `SpriteRenderer2D.Material` (런타임 캐시, 직렬화 X) | `AudioPlayer` 런타임 캐시 `AssetRef` (직렬화 X) |
| `CSpriteRenderSystem` 이 Material 적용 | `CAudioSystem` 이 효과 노드 배선 |

참조 패턴은 `SpriteRenderer2D.h:18-30` 그대로 따른다: 직렬화되는 건 `EffectGuid` 하나, 런타임 `AssetRef` 캐시는 사용 중 자산 unload 보호 + 직렬화 제외.

목표 명시(사용자): **"에디팅 가능하게만"** — 효과 에셋이 파라미터를 들고 직렬화·편집되면 1차 충족. 실제 DSP 노드 배선은 나중 reverb 에디터 만들 때 채워도 됨. 따라서 1차 범위는 **에셋 정의 + 참조 + 직렬화 + bus 라우팅 토대**, 실제 audible DSP 는 후속.

## 인터페이스 토대 (이미 있음, 단계 G 로 비어있음)

- `IAudioEffect` — `GetKind` / `SetParameter(name,value)` / `GetParameter(name)`. 문자열 키 dict.
- `IAudioBus::AttachEffect` / `DetachAllEffects` — bus 단위 효과 체인.
- `IAudioPlayer::AttachEffect` / `DetachAllEffects` — player 단위 효과 체인.
- `EAudioEffectKind`: Reverb / LowPass / HighPass / Echo / Distortion / Compressor / Limiter.

현재 `CMiniAudioEffect`/`CMiniAudioBus` 는 전부 no-op 스텁. 채울 자리.

## 구현 단계

### G-1. CAudioEffectAsset 정의 + 직렬화 (1차 핵심) — ✅ 완료 (tasks/AudioEffectAsset.todo.md)

`CMaterialAsset` 패턴. `Engine/Core/Asset/AudioEffectAsset.{h,cpp}` 신규.

- 필드: `EAudioEffectKind Kind` + 파라미터(현 `IAudioEffect` 의 문자열→float dict 와 동일 모델, 또는 효과별 struct).
- `CAudioEffectAssetLoader` — YAML 직렬화/로드(프로젝트 규칙: JSON 아님). Material 처럼 디스크 표현은 가벼운 메타.
- `EAssetType::AudioEffect` 추가 + `AssetManager.RegisterLoader`.
- 파라미터 키 표준화: lpf=`cutoff`,`q` / delay=`delay`,`decay`,`wet` / reverb=`roomSize`,`damping`,`wet`,`dry` 등.
- **이 단계만으로 "에디팅 가능" 목표 달성** — 에셋 생성/파라미터 편집/저장 가능.

### G-2. AudioPlayer 컴포넌트에 EffectGuid 참조 추가 — ✅ 완료

- `AudioComponents.h` 의 `AudioPlayer` 에 `AssetGuid EffectGuid` + 런타임 캐시 `AssetRef<IAsset> EffectAssetCache` / `CachedEffectGuid` (직렬화 X). `SpriteRenderer2D` 캐시 패턴 그대로.
- 리플렉션 등록(`BuiltinComponentRegistry.cpp`)에 `EffectGuid`(AssetGuid 타입) 한 줄 추가 → 직렬화·스냅샷 자동 포함, 캐시는 제외.
- 인스펙터 슬롯은 `EReflectPropertyType::AssetGuid` 자동 렌더(`InspectorTool.cpp:367`) — 추가 UI 코드 0. MaterialGuid 와 동일하게 드래그&드롭 수락.
- 빌드: GameScriptSample.dll 포함 전체 성공 → builtin 컴포넌트의 `AssetRef` 멤버가 DLL 경계 문제 없음 재확인(호스트 전용).
- 미연결: `CAudioSystem` 이 EffectGuid 의 에셋을 캐시 hold + 노드 배선하는 건 G-4(audible) 에서. 현재는 데이터 연결까지.

### G-3. Bus 그래프 실체화 (audible DSP 선행) — ✅ 완료

- `CMiniAudioBus` 가 `ma_sound_group` 노드 소유 (ctor: engine+kind+parent, dtor uninit). `SetVolume/SetMuted` → `ma_sound_group_set_volume` 실연결. mini 전용 `GetGroup()` 으로 player 라우팅 노드 노출.
- `CMiniAudioDevice` 가 `EAudioBusKind` 별 표준 버스 보유(`Impl.Buses[Count]`). Initialize 에서 Master(parent=null=endpoint 직결) → 나머지(parent=Master) 계층 구성. Finalize 는 버스를 엔진보다 먼저 uninit.
- `CreatePlayerFromFile(path, bus=Master)` 가 해당 버스 group 으로 라우팅(`ma_sound_init_from_file` 의 pGroup). 버스 미초기화 시 group=null 폴백(기존 endpoint 직결 동작 보존).
- `IAudioDevice::GetBus(kind)` 인터페이스 추가 — 카테고리 볼륨/뮤트 제어 진입점. Empty/Mini 양쪽 구현. `CreateBus` 는 Custom 용으로 Master 하위 신규 group.
- 빌드: 전체 성공(Engine+Application+DLL). AudioSystem/프리뷰는 기본인자로 Master 경유 자동.
- 범위 밖(후속): AudioPlayer 의 카테고리별 라우팅(자산 `DefaultBus` 반영)은 현재 전부 Master 고정. 인프라/GetBus 는 즉시 사용 가능, 카테고리 라우팅은 작은 확장점으로 남김.

### G-5(에디터) 가 G-4(audible) 보다 먼저 — 순서 정정

사용자 지적: 파라미터 편집 UI 없이 G-4(audible)를 먼저 하면 모든 효과가 기본값 고정 → 청취해도 의미 없고 검증 불가. 따라서 **에디터(G-5의 인스펙터)를 선행**, 그 다음 G-4.

### G-5(인스펙터). 효과 에셋 에디터 — ✅ 완료

- `InspectorTool.cpp` 에 `DrawEffectEditor` + `SaveEffectData` + Kind별 표준 파라미터 스펙(`EffectParamSpecs`).
- AudioEffect 에셋 선택 시 인스펙터: **Kind 콤보** + **Kind별 파라미터 슬라이더**(범위 clamp). Reverb=roomSize/damping/width/wet/dry, LowPass/HighPass=cutoff/q, Echo=delay/decay/wet, Distortion=drive/wet, Compressor/Limiter=threshold/ratio.
- Kind 변경 시 `NormalizeEffectParams` 로 파라미터 키 교체(없는 키는 기본값).
- 저장: `.jfx` 본문에 `CAudioEffectSerializer::ToYaml` 디스크 쓰기 → 로드된 자산 `ApplyImportOptions` in-place 갱신(외부 AssetRef 보존).
- 로컬라이징 `inspector.effect.title/kind/apply` ko/en.
- 빌드 성공. **이로써 "에디팅 가능" 목표 완성** — 효과 에셋 생성/Kind 선택/파라미터 편집/저장 동작.
- 남은 G-5: AudioPlayer 인스펙터 EffectGuid 슬롯은 G-2 에서 리플렉션 자동 렌더로 이미 됨. `EditorAudioPreview` 효과 체인 즉시 청취는 G-4 후.

### G-4. IAudioEffect → miniaudio 노드 매핑 (audible DSP) — ✅ 인프라 완료 (배선/미리듣기는 후속)

완료(노드 + 파라미터):
- **Freeverb 커스텀 `ma_node`** 구현 — Jezar Freeverb(채널당 comb 8 + allpass 4, stereo spread). `onProcess` 에서 wet/dry/width 믹스. `ma_node_base` 첫 멤버.
- `CMiniAudioEffect` 가 Kind 별 노드 소유:
  - LowPass→`ma_lpf_node`, HighPass→`ma_hpf_node`, Echo→`ma_delay_node`(내장), Reverb/기타→Freeverb.
- `SetParameter(key,v)` 실동작 — lpf/hpf=cutoff(`_reinit`), echo=decay/wet, reverb=roomSize/damping/wet/dry/width. 키는 G-5 `EffectParamSpecs` 와 일치.
- `CreateEffect` 가 engine 넘겨 노드를 엔진 노드 그래프에 init. dtor 에서 노드별 uninit.
- miniaudio API 시그니처 전부 검증(node_config/vtable/lpf·hpf·delay config). 빌드 성공.

완료(배선 + 미리듣기):
- `CMiniAudioFilePlayer::AttachEffect/DetachAllEffects` — 효과 노드를 sound↔endpoint 사이 삽입(`ma_node_attach_output_bus`). 단일 효과 교체식. effect 를 SafePtr 로 보관해 재생 중 유지.
- `EditorAudioPreview::PlayFileWithEffect(path, guid, kind, params)` — 효과 노드 생성+파라미터+부착 후 재생. Stop 시 효과도 해제.
- 효과 에디터 위젯 미리듣기 UI — 테스트 사운드 드롭 슬롯 + 재생/정지. 현재 편집 중인 Kind/파라미터를 그 사운드에 적용해 청취.

완료(런타임 적용):
- `CAudioSystem` 의 `PlayerInstance` 에 `Effect`/`EffectGuid` 추가. 매 프레임 `AudioPlayer.EffectGuid` 변경 감지 → `BuildAndAttachEffect`(효과 에셋 로드→Kind/params→CreateEffect→SetParameter→AttachEffect). null 이면 DetachAllEffects.

후속(다음 턴):
- 다중 효과 체인(현재 단일).
- 스레드 안전: 노드 생성/파괴 게임 스레드, 배선 변경 Tick 경계.

### 시뮬레이션 정지 시 오디오 미정리 버그 (수정)

- 증상: 시뮬레이션 정지해도 재생 중이던 사운드가 안 멈춤.
- 원인: `CSceneManager::StopSimulation` 이 스냅샷 복원만 하고 시스템 정리를 안 함. `CAudioSystem` 은 편집 모드에서 `OnUpdate` 가 안 돌아(`ShouldUpdateInEditMode()=false`) player GC 가 일어나지 않음 → `m_instances` 의 ma_sound 가 계속 재생.
- 수정: 시뮬 정지 lifecycle 훅 추가.
  - `CGameSystem::OnSimulationStop`(virtual) + `SimulationStop`(init 됐을 때만 호출, Finalize 와 달리 `m_isInitialized` 유지 → 다음 Play 에서 재초기화 없이 동작).
  - `CScene::NotifySimulationStop` → 모든 user 시스템에 전파.
  - `CSceneManager::StopSimulation` 이 스냅샷 복원 **전**에 `NotifySimulationStop` 호출.
  - `CAudioSystem::OnSimulationStop` → player 전부 Stop + clear.

### Focus — 에디터 창 포커스 (엔진 레벨 수정)

- 증상: 더블클릭/버튼으로 열어도 포커스 안 됨 + 깜빡.
- 원인: `CImWindow::Focus` = `SetWindowFocus(GetImGuiLabel())`. imgui `SetWindowFocus(name)`→`FindWindowByName`→`ImHashStr(전체 label)`, window ID 도 `ImHashStr(전체 name)`. **title 이 바뀌면 name 해시가 달라져 매칭 실패**(사용자 지적 정확).
- 수정(엔진 공통): `CImWindow::Focus()` 는 `m_bRequestFocus` 플래그만 set. `HandleBegin` 의 `ImGui::Begin` **직전**에 플래그면 `ImGui::SetNextWindowFocus()`(현재 Begin 윈도우 직접, name lookup 우회). 모든 윈도우의 Focus 가 안정 동작. 패널 전용 임시 코드(RequestFocusFrames/OnPreBegin)는 제거.

## 결정 사항

- **파라미터 저장 모델: A — 효과 공통 `map<string,float>` (확정).**
  현 `IAudioEffect::SetParameter(name,value)` 모델과 직결. 효과 종류 추가 시 코드 변경 없음, reverb 에디터에서 키 동적 추가 자유. 키 표준 문서화로 오타 위험 보완.
- **효과 부착 단위 1차**: player(인스턴스) 우선. bus 단위는 G-3 이후. 자산 임포트 단위는 후순위.
- **reverb 알고리즘: Freeverb (확정).** miniaudio 내장 reverb 없음. Freeverb(comb 8 + allpass 4)는 공간 잔향 본질을 내는 최소 알고리즘, 파라미터(roomSize/damping/wet/dry/width)가 모델 A 키에 직결, 순수 DSP라 멀티플랫폼 무관. lpf/hpf/echo 는 miniaudio 내장 노드 그대로 사용, reverb 만 Freeverb 직접 구현.

## Not Verified (DSP)

- 위는 설계 방향 — 미구현. miniaudio 노드 API 스레드 안전 보장 범위는 G-4 착수 시 재확인.
