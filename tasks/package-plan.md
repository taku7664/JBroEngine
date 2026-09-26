# 에셋 패키지 `.jpak` 와 게임 빌드 계획 (D-227)

> 상태: **진행** (2026-09-26 시작). 사용자 확인(2026-09-26): 포함 집합은 참조를 따라간다, 만드는 곳은 엔진 모듈 + 에디터 메뉴,
> 그림은 푼 픽셀·폰트는 원본 + 미리 뜬 아틀라스, 보호는 "이미지 같은 원본을 보호하기 위함 - 된다면 상관없다".
> 이 계획의 첫 동기는 text-plan §5 5 단계에서 남긴 "`.jpak` 에 미리 뜬 아틀라스" 다. 그 아래 층(패키지·게임 빌드)이 없어 함께 세운다.

## 1. 기존 엔진의 패키지 - 무엇이 있었고 무엇이 아팠나

읽은 것: `Engine/Core/Asset/AssetPackage.*`, `AssetManager.cpp`(`BuildAssetPackage`·`LoadPackedAssetManifest`·`LoadAssetInternal`),
`Application/Editor/Build/BuildAssetCollector.cpp`·`GameBuildManager.cpp`, `BuildScripts/BuildGame.ps1`, `Engine/Core/Build/BuildManifest.h`,
`Application/Application.cpp`, 그쪽 문서 `tasks/BuildPipelineDesign.md`·`EngineAuditFollowup.md`·`EngineCodeAudit.md`.

### 1.1 있었던 것

- 파일은 `.jbpack` 이다(`Content/game_assets.jbpack`). "jpak" 은 새 엔진 문서의 이름이다.
- 모양: 72 바이트 머리(`JBPACK1\0`, 판, 항목 수, 페이로드·색인의 자리와 크기, 색인·페이로드 해시) → 페이로드를 맞대어 → 색인이 끝에.
  정렬 없음. 항목은 GUID 글자 차례. 색인 레코드: GUID·위치 글자·타입·페이로드 종류·판·플래그·절대 오프셋·저장 크기·원래 크기·해시·팩 이름·
  **임포트 옵션 YAML**.
- 해시: 64 비트 FNV-1a(길이를 섞고 0 을 내지 않는다). 평문에 대해 잰다.
- 보호: xorshift XOR 난독화, **고정 키**(`0x9E3779B97F4A7C15`) 에 항목 해시·오프셋을 씨로. 압축 플래그는 있었지만 쓰지 않았다.
- 페이로드: 스프라이트만 빌드 때 RGBA8 로 풀었다(`JBSPR81`). 캔버스·프리팹·레이어는 YAML 원문, 오디오·폰트·패밀리는 원본 바이트.
  **글리프 아틀라스는 패키지에 든 적이 없다** - 런타임에 떴다.
- 포함 집합: 빌드 캔버스·시작 캔버스·항상 포함·기본 폰트 패밀리·폴백 패밀리에서 너비 우선으로 참조를 따라간다. 캔버스는 최상위
  `ReferencedAssets` 키, 패밀리는 칸의 GUID. 빠진 GUID 하나면 빌드가 멈춘다.
- 읽기: 색인을 `unordered_map` 에 싣고 `__packed/<guid>` 가상 경로로 등록. 페이로드를 읽을 때마다 `std::ifstream` 을 새로 열어 통째로 읽었다.
  플랫폼 추상을 거치지 않았다.

### 1.2 아팠던 것 (그쪽 문서가 스스로 적은 것 포함)

| 번호 | 문제 | 새 설계의 대응 |
|---|---|---|
| K1 | **쓰는 곳이 둘**이었다. 에디터의 C++ 와 빌드 스크립트 안의 C#(`JBroPackWriterV2`). 스크립트 쪽은 스프라이트를 굽지 않아 웹이 원본을 받았다 | 쓰기는 `JBroPackage` 하나다. 에디터 메뉴와 (나중의) 명령줄 도구가 같은 함수를 부른다(§2.4) |
| K2 | 색인에 임포트 옵션 YAML 이 들었다(그쪽이 "옮겨야 한다" 고 적었다) | 메타는 항목의 **자기 블롭**이다. 색인은 위치만 든다(§2.1) |
| K3 | 읽기가 `std::ifstream` 이었다 - 플랫폼 파일 규칙(D-112) 밖이고 안드로이드 APK 안에서 되는지 확인되지 않았다 | `IPlatform::OpenFileStream` 으로만 연다(§2.2) |
| K4 | 페이로드마다 파일을 새로 열었다. 스트리밍 오디오는 파일로 풀어야 했다 | 패키지를 한 번 열고, 항목 하나를 **창 스트림**(자기 파일 핸들, 범위 안만 읽기)으로 내준다(§2.2) |
| K5 | 고정 키 난독화는 도구 하나로 풀렸고 비용만 들었다 | 키는 빌드마다 뽑는다. 그래도 **키는 게임과 함께 가므로 완전히 막을 수 없다** - 목표는 일반 도구로 열리지 않는 것이다(§2.3) |
| K6 | 캔버스 YAML 원문이라 웹에 YAML 파서가 들어가야 했다 | 새 엔진은 이미 런타임에 YAML 을 읽는다. 바이너리 캔버스는 이 계획 밖이다(§4) |

## 2. 설계

### 2.1 파일 모양 `JPAK`

```
[머리 64 B] magic "JBROPAK\0" · version u32 · headerSize u32 · entryCount u32 · flags u32
            indexOffset u64 · indexSize u64 · indexHash u64 · key u64 · reserved u64
[블롭들]    16 바이트 정렬, 난독화
[색인]      끝에. 난독화. 레코드 = id(16) · type u16 · kind u8 · pad · owner(16) · offset u64 · size u64 · hash u64 · pathLength u32 · path
```

- **한 에셋에 블롭이 여럿**이다. `kind` 가 가른다: `Record`(블롭 없음 - 이미지의 Sprite 레코드), `Meta`(`.jmeta` 원문),
  `Source`(원본 바이트), `CookedTexture`(RGBA8 과 폭·높이), `FontAtlas`(미리 뜬 아틀라스). 같은 (id, kind) 는 하나다.
- 경로는 에셋 폴더 기준 상대경로를 그대로 둔다. 시작 캔버스·빌드 캔버스를 경로로 찾기 때문이다. 색인이 난독화되어 겉으로 보이지 않는다.
- 해시는 64 비트 FNV-1a(평문). 색인 해시가 틀리면 열지 않고, 블롭 해시가 틀리면 그 블롭을 읽지 않는다 - 깨진 패키지를 조용히 쓰지 않는다.

### 2.2 모듈과 읽기

- 새 Tier E 모듈 **`JBroPackage`**: `PackageWriter`(블롭을 더해 한 파일로), `PackageReader`(열기·찾기·블롭 읽기·창 스트림), 쿡(§2.4),
  참조 따라가기(§2.4). Core·Platform·AssetTypes·Asset 에 기댄다.
- `JBroAsset` 에 **`IAssetSource`** 를 둔다: 원본 읽기·메타 읽기·원본 스트림. 기본은 지금의 느슨한 파일(`LooseAssetSource`, 모듈 안)이고,
  `JBroPackage` 의 `PackageAssetSource` 가 패키지에서 준다. 에셋 시스템은 어느 쪽인지 모른다. 쿡된 블롭이 있으면(`CookedTexture`)
  디코드를 건너뛴다.
- 패키지로 여는 프로젝트는 `.jproject` 의 새 키 **`AssetPackage`**(프로젝트 기준 상대경로)를 가진다. 있으면 엔진이 에셋 폴더를 스캔하지 않고
  패키지의 색인으로 레지스트리를 채운다. 파일 감시는 없다.

### 2.3 보호

- 블롭과 색인을 xorshift64* 열쇠 흐름으로 섞는다. 열쇠 흐름의 씨는 머리의 `key`(빌드마다 `Uuid::Generate` 에서 뽑는다)와 블롭의 오프셋이다.
- **한계를 적어 둔다**: 게임이 읽으려면 키가 게임과 함께 가야 하므로, 실행 파일과 패키지를 뜯는 사람을 막을 수 없다. 목표는 이미지 뷰어·압축 도구 같은
  일반 도구로 원본이 열리지 않는 것이다. 그림은 PNG 가 아니라 푼 픽셀이라 파일째 꺼내 쓸 수도 없다. 사용자가 이 한계로 받아들였다("된다면 상관 X").

### 2.4 게임 빌드

- **포함 집합**(참조 따라가기): 씨는 `Build.StartupCanvas`·`Build.BuildCanvases`·프로젝트 폰트(`Fonts`)·모든 문자열 표(키는 실행 중에 찾으므로)다.
  포함된 에셋의 메타(패밀리의 칸이 여기 있다)와 캔버스 원문에서 32 자리 아이디를 읽어 레지스트리에 있는 것을 더한다. 다른 타입의 원문은 읽지 않는다.
  이미지는 Texture 와 그 Sprite 들이 함께 간다. 레지스트리에 없는 아이디는 빌드를 멈추지 않고 경고로 모은다(지운 에셋을 가리키는 옛 캔버스가 흔하다).
- **쿡**: Texture 는 `CookedTexture`(디코드한 RGBA8), 폰트는 `Source` + 미리 채우기가 켜진 폰트의 `FontAtlas`(§2.5), 나머지는 `Source`.
  모든 에셋은 `Meta` 블롭을 가진다.
- **내놓는 것**: `<OutputDirectory>/<ProductName>/` 에 게임 호스트 실행 파일, 스크립트 DLL(있으면), `<ProductName>.jproject`(`AssetPackage` 를 적은 사본),
  `Content/game.jpak`. 게임 호스트는 `--project` 가 없으면 실행 파일 옆의 `.jproject` 를 연다.
- 에디터 메뉴 "게임 빌드" 가 `BuildGame` 을 부르고 결과(포함 수·크기·경고)를 알림으로 보인다.

### 2.5 미리 뜬 아틀라스

- `GlyphAtlas` 에 **내보내기·되살리기**를 더한다: 페이지(알파 한 채널)·글리프 표·페이지마다의 선반 커서. 되살린 뒤 새 글자가 같은 페이지를
  이어 채운다.
- 블롭에는 유효 표지(폰트 원본 해시·렌더 모드·미리 채우기 집합과 크기·SDF 크기와 번짐)를 함께 둔다. `TextLibrary` 는 표지가 맞으면 미리 채우기
  태스크 없이 되살리고, 틀리면 지금처럼 뜬다.

## 3. 단계

1. **`JBroPackage` 형식**: 쓰기·읽기·창 스트림·난독화·해시 검사. 완료 조건: 왕복, 깨진 색인·블롭 거절, 16 바이트 정렬, 창 스트림이 범위 밖을 읽지 않음, 음성(스크립트가 집으면 C1083).
2. **패키지에서 에셋 싣기**: `IAssetSource`·`PackageAssetSource`·`CookedTexture`·`AssetPackage` 키·엔진 배선. 완료 조건: 같은 프로젝트를 패키지로 열어
   텍스처·스프라이트·폰트·패밀리·문자열 표·오디오(스트리밍 포함)가 느슨한 파일과 바이트까지 같다.
3. **게임 빌드**: 참조 따라가기·쿡·내놓기·에디터 메뉴·게임 호스트의 옆 프로젝트 열기. 완료 조건: 빌드한 폴더의 게임 호스트가 원본 프로젝트 없이 시작 캔버스를 그린다.
4. **미리 뜬 아틀라스**: 완료 조건: 패키지로 연 폰트가 미리 채우기 태스크 없이 같은 페이지 픽셀을 갖고, 표지가 틀리면 다시 뜬다.

## 4. 이 계획 밖 (`[열림]`)

- 압축(zstd 따위). 지금은 없다 - 푼 픽셀이 커지므로 다음에 볼 첫째 후보다.
- 바이너리 캔버스, 웹·안드로이드의 패키지 파일 위치, 스크립트가 아이디로만 싣는 에셋(참조로 찾을 수 없다 - `Build.AlwaysInclude` 같은 키가 필요할 것이다).
- 여러 패키지(내려받는 추가 콘텐츠).

## 5. 진행 기록

1. ~~**형식**~~ → 완료 2026-09-26 · `Modules/JBroPackage`(`PackageFormat`·`PackageWriter`·`PackageReader`) · 왕복, 원문·경로가 파일에 보이지 않음, 16 바이트 정렬,
   (id, kind) 차례, 창 스트림(가운데서 읽기·끝에서 멈춤·범위 밖 이동 거절), 블롭 한 바이트가 틀리면 그 블롭만 거절, 색인·표지·판·잘린 파일은 열지 않음,
   키가 다르면 같은 블롭도 다른 바이트(`Tests/PackageTests.cpp`).
2. ~~**패키지에서 싣기**~~ → 완료 2026-09-26 · `JBroAsset/AssetSource`(`IAssetSource`·`LooseAssetSource`·`WriteCookedTexture`), `JBroPackage/PackageAssetSource`·
   `PackageCook`, `JBroHost`(`AssetPackage`·`IsRunningFromPackage`·`OpenAudioStream`) · 원본 폴더를 구운 뒤 **지우고** 패키지로 연 에셋 시스템이 텍스처(픽셀까지)·
   스프라이트·폰트(바이트까지)·문자열 표·디스크 스트리밍 소리(스트림이 원본 WAV 와 같다, 봉우리도)를 느슨한 파일과 같게 낸다. 텍스처는 PNG 가 아니라 푼 픽셀로 간다.
3. ~~**게임 빌드**~~ → 완료 2026-09-27 · `JBroPackage/PackageCollect`, `JBroHost/GameBuild`·`GameHostArguments`(`FindProjectBesideExecutable`·`ResolvePackagedStartupCanvas`),
   `JBroGameHost/Main`, 에디터 파일 메뉴 "게임 빌드"(`EditorApplication::BuildGameForProject`·`FindGameHostExecutable`) · 시작 캔버스가 쓰는 스프라이트·프로젝트 폰트·
   문자열 표만 가고 쓰지 않는 그림은 빠진다(5 개). 내놓은 폴더에 `<제품명>.exe`·`.jproject`·`Content/game.jpak`, 사본은 패키지·에셋 폴더 기준 캔버스·물리 워커 수를
   적고 스크립트가 없으면 비운다. 원본 프로젝트를 지운 뒤 내놓은 프로젝트를 연 엔진이 캔버스를 패키지에서 읽고 스프라이트를 푼 픽셀로 싣고 틱한다. 패키지가 없으면
   열지 않는다. 32 자리 낱말만 아이디로 읽는다.
   - **실제 게임 호스트**: `Debug_Game2D` 의 `JBroGameHost.exe` 를 빌드에 넣어(시험을 잠시 고쳐 카메라와 300 배 스프라이트를 더했다) 원본 프로젝트를 지운 뒤 내놓은 폴더의
     `Probe Game.exe` 를 인자 없이 띄웠다. 옆의 `.jproject` 를 열어 빨간 바탕 위에 패키지의 2 x 2 그림(파랑·초록·분홍, 투명 한 칸)을 그렸다. 오류 로그는 없었다.
   - 함께 고친 것: 빈 값이 `Key: ` 로 적혀 읽을 때 기본값이 되살던 결함(스크립트 경로가 개발 경로로 되살았다). `""` 로 적고, 원문에 비어 있던 줄은 그대로 둔다.
4. ~~**미리 뜬 아틀라스**~~ → 완료 2026-09-27 · `JBroText/GlyphAtlas`(`Bake`·`Restore`·`BakedAtlasStamp`), `JBroAsset`(`FontData::bakedAtlas`·`NormalizeFontOptions`),
   `JBroTextRendering/TextLibrary`(`BakeFontAtlas`·`PrewarmStampOf`·되살리기) · 되살린 아틀라스는 뜬 것과 페이지 픽셀·칸이 같고, 새 글자는 같은 자리에 이어 들어간다.
   표지(원본 해시·크기·벌)·페이지 크기가 다르거나 잘렸거나 칸이 페이지 밖이면 되살리지 않고 지금 것을 건드리지 않는다. 패키지로 연 폰트는 뜨지 않고 되살리며
   (되살림 1 번, 미리 뜬 칸 124), 첫 업로드 수와 그려진 글자의 픽셀이 느슨한 파일로 뜬 것과 같다. 한 페이지는 알파 한 채널이라 RGBA 의 4 분의 1 이다.
   - 음성: 스크립트가 `<JBro/Package/PackageReader.h>` 를 집으면 C1083(`/p:JBroTierProbe=Package`).

남긴 것(§4 에 더해): 패키지는 한 번에 메모리에서 만들어 쓴다(4 GB 까지, 큰 게임이면 흘려 쓰기가 필요하다). 게임 빌드는 편집기 스레드에서 돈다(에셋이 많으면
멈춘다 - 로딩 표시와 워커는 태스크 계층을 쓸 때 본다). 프로젝트 사본에 `BuildCanvases` 는 적히지 않는다(게임은 물리 워커 수를 셀 때만 썼고 그 수를 빌드가 적는다).
