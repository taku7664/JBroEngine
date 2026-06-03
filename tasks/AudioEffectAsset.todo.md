# TODO — Audio Effect Asset (G-1)

## Goal

Reverb 등 사운드 효과를 독립 에셋으로 만든다 (Material 패턴). 1차 범위: 에셋 정의 + 직렬화 + 타입/로더 등록. 실제 audible DSP(노드 배선)는 후속(G-3/G-4).

"에디팅 가능하게만" — 효과 에셋이 Kind + 파라미터를 들고 디스크에 저장/로드되면 충족.

## Assumptions

- 파라미터 모델 = A: 효과 공통 `std::map<std::string, float>` (사용자 확정).
- 디스크 표현 = 소스 파일 `.jfx` 에 YAML (Material 의 `.jmat` 패턴). AssetWatcher 가 감지→GUID import.
- 직렬화는 YAML (프로젝트 규칙: JSON 아님). 기존 yaml-cpp 사용.
- 멀티플랫폼: Engine 변경분은 SDK/Include 헤더에 미러링.

## Success Criteria

- `CAudioEffectAsset` (Kind + `map<string,float>`) + `CAudioEffectAssetLoader` 추가.
- `EAssetType::AudioEffect` enum 추가.
- `.jfx` → `EAssetType::AudioEffect` 확장자 매핑 (`DetectAssetType`).
- AssetManager 에 로더 등록.
- 효과 에셋 YAML 직렬화/역직렬화 round-trip 동작.
- AssetBrowser: 라벨/아이콘 + "New Effect" 생성.
- Debug_Editor|x64 빌드 성공.

## Plan

- [x] `EAssetType::AudioEffect` enum 추가 (AssetTypes.h + SDK 미러) — Custom 뒤에 추가(정수값 보존)
- [x] `AudioEffectAsset.{h,cpp}` 신규 — CAudioEffectAsset(Kind + map<string,float>) + Loader + YAML
- [x] `DetectAssetType` 에 `.jfx` → AudioEffect 매핑 + ImporterNameForType
- [x] Engine.cpp 에 RegisterLoader(AudioEffect)
- [x] AssetBrowser 라벨([FX])/아이콘(material 재사용) + "New Effect" (EMPTY_EFFECT_YAML)
- [x] vcxproj 파일 등록 (filters 는 AudioAsset 도 없어 생략 — 기존 패턴 일치)
- [x] SDK/Include 헤더 미러 — StageSDK 글롭(Core\Asset\**\*.h) 자동
- [x] 로컬라이징 키 asset_browser.add_asset.effect ko-KR/en-US 양쪽

## Verification

- [x] Debug_Editor|x64 전체 빌드 성공 (Engine.lib + Application.exe + GameScriptSample.dll, 에러 0)
- [ ] 효과 에셋 생성 → 파라미터 저장 → 재로드 round-trip (런타임 미검증 — 사용자 측)
- [x] git diff 줄끝 check — 신규 파일 문제 없음(autocrlf LF→CRLF 경고만)
- [x] Diff review (아래 Review)

## 알아둔 함정

- `.jmat` 가 `DetectAssetType` 에 없어 Material 이 Custom 으로 떨어지는 듯 — 별개 기존 이슈, 본 작업 아님. 효과 에셋은 `.jfx` 매핑 반드시 추가해 같은 함정 회피.
- `IAudioEffect::SetParameter(name,value)` 와 키 일치시켜 G-4 에서 그대로 연결.

## Review

### Changed

- 코드를 읽었고: Material 은 빈 에셋(.jmat YAML 본문) + AssetWatcher import 패턴, Audio 임포트옵션은 이미 yaml-cpp 직렬화. 에셋 생성은 소스 파일을 디스크에 쓰면 watcher 가 GUID import.
- 어떻게 생각했고: 효과 에셋도 같은 패턴 — `.jfx` 소스 파일에 `Kind + Parameters(map<string,float>)` YAML. 파라미터 모델 A(공통 map) 라 효과 종류 추가 시 코드 불변, IAudioEffect::SetParameter 와 키 직결.
- 어떤 반례를 찾았고: `DetectAssetType` 에 `.jmat` 가 없어 Material 이 Custom 으로 떨어지는 기존 함정 발견 → 효과는 `.jfx` 매핑을 반드시 추가해 회피. enum 을 중간 삽입하면 빌드스크립트(BuildGame.ps1 의 타입 정수값)/직렬화가 깨지는 반례 → Custom **뒤**에 추가해 기존 값 보존.
- 어떻게 고쳤다: CAudioEffectAsset(Kind+map) + Serializer(YAML round-trip) + Loader(MemoryPayload 우선, 없으면 ifstream, 실패해도 기본값 폴백) + ReloadInto(in-place 갱신, 외부 AssetRef 보존). enum/매핑/등록/AssetBrowser UI/로컬라이징 연결.

### Verified

- Debug_Editor|x64 전체 빌드 성공, 에러 0.
- 신규 파일 줄끝 일관(autocrlf 경고만).

### Not Verified

- 런타임에서 .jfx 생성→파라미터 편집→재로드 round-trip 실동작 미검증 (에디터 실행 필요, 사용자 측).
- audible DSP 는 범위 밖(G-3/G-4) — 효과는 아직 소리에 영향 없음. 1차 목표("에디팅 가능")만 충족.

### Risks

- 효과 전용 아이콘 없음 — material 아이콘 재사용. 시각 구분 약함(후속 에셋 추가 시 교체).
- ReloadInto 가 빈 .jfx 를 기본값으로 해석 — 의도된 동작이나, 파일 깨짐과 빈 파일 구분 안 함.
- AudioPlayer.EffectGuid 참조(G-2)는 아직 미연결 — 효과 에셋만 독립 존재. 다음 단계.
