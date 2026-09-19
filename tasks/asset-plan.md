# 에셋 시스템 계획 (기존 엔진 재검토 뒤 재설계)

> 계약은 `docs/ProjectRule.md`, 결정은 `tasks/todo.md` Decisions 다. 이 문서는 그 둘을 향해 가는 순서와
> 상태를 적는다. 상태는 항목마다 `[완료]` `[진행]` `[제안]` `[가정]` `[열림]` 으로 붙인다.
> `[제안]` 은 **사용자 확인 전**이다. 2026-09-18 에 §2.1·§2.3·§2.2 의 키·§4-5 가 확인돼 D-111 이 됐다.
> 2026-09-18 늦게 `stb_image`·UV 사각형·파일 시스템의 플랫폼 경계(D-112)도 확정됐다. §4 에 열린 것이 없다.

## 0. 지금 부족한 것 (2026-09-18 실측)

렌더링 쪽 두 번의 검토와 기존 엔진(`C:\Users\박주형\source\repos\JBroEngine`) 비교에서 드러난 것이다.
순서는 의존 관계다 - 위가 없으면 아래가 설 자리가 없다.

1. **에셋 시스템이 통째로 스텁이다.** `Modules/JBroAsset/Source/Asset.cpp` 65 줄. `AssetRegistry::Find` 는 항상
   `nullptr`, `AssetSystem::Load` 는 빈 핸들. 프로젝트 폴더를 읽는 코드도, 메타 파일도, 이미지 디코더도 없다.
   `SpriteRenderer2D` 의 `spriteId → sprite` 해석 패스는 주석으로 "붙일 데가 없다" 고 적혀 있다.
2. **텍스처가 화면에 나오지 않는다.** RHI 에는 `SetTexture`·`SetSampler`·`WriteTexture` 가 있고(D-61)
   에디터 UI 가 그것으로 폰트 아틀라스를 그린다. 그러나 `BuiltinSprite.hlsl` 은 틴트만 내고, 렌더러에는
   텍스처를 등록하는 API 가 없다. `SpriteSubmit::sprite` 는 읽히지 않는다.
3. **스프라이트 에셋이 없다.** 기존 엔진의 `CSpriteAsset`(픽셀 + 시트 슬라이싱 + 피벗 + PPU)에 해당하는 것이 없다.
   `SpriteSubmit` 에 UV 사각형이 없어 시트의 한 칸을 그릴 길도 없다.
4. **재질이 없다.** 기존 엔진도 `ERenderQueue` 하나였다(§1.3). 새 엔진은 필드와 타입 이름만 있다.
   방향(빌트인 프리셋 vs 사용자 셰이더)이 기존 엔진에서도 미결로 남았고, 새 엔진 D-33 은 반대편을 정했다.
5. **에디터에 에셋 필드와 에셋 브라우저가 없다.** 인스펙터의 에셋 참조는 규칙상 글자 칸이 아니라 에셋 필드여야
   하는데(ProjectRule §11) 지금은 `AssetId` 숫자다.
6. **렌더 패스 구조가 고정이다.** 후처리·라이팅을 얹을 자리가 없다(framework3d-plan §2.11 뒤 검토).
7. **게임 익스포트가 에셋을 싸 갈 길이 없다.** 기존 엔진의 `.jpak` 에 해당하는 것이 없다.

이 문서는 1·2·3 을 다룬다. 4 는 사용자 결정 뒤 별도 계획, 5 는 3 뒤에 에디터 계획으로, 6·7 은 `[열림]` 으로 둔다.

## 1. 기존 엔진의 에셋 시스템 - 무엇이 있었고 무엇이 아팠나

읽은 것: `Engine/Core/Asset/*`(매니저·레지스트리·메타 파일·경로·`AssetRef`·패키지·타입 규칙·일시 로드),
`tasks/asset-system-followups.md`, `tasks/EngineFeatureBacklog.md`, `TestProject` 의 `.jmeta` 실물.

### 1.1 있었던 것

- **식별**: `AssetGuid` = 128 비트 GUID 의 **텍스트**. 임포트 때 생성해 `.jmeta` 에 저장. 경로만으로 등록하는
  길(`RegisterAssetByPath`)은 경로 해시로 결정적 GUID 를 만들었다.
- **메타 파일** `.jmeta`(YAML): `Version`·`Guid`·`Type`·`DisplayName`·`Importer`·`ImportOptions`(타입별 YAML 블록).
  경로는 저장하지 않고 스캔 때 채운다(파일을 옮겨도 GUID 가 산다).
- **레지스트리**: GUID → 메타, 경로 → 메타. 스캔은 `AssetRoot` 아래 전부.
- **매니저**: 로더 등록(`IAssetLoader` 타입별 15 종), 로드·언로드·재로드·이동·패키지 빌드·use-count.
  `IAsset` 가상 기반 + `dynamic_cast` 대신 `StaticAssetType()` 으로 타입 확인.
- **참조**: `AssetRef<T>`(strong, use-count) 와 `SafePtr<IAsset>`(weak). 컴포넌트는 GUID 를 저장하고
  런타임 캐시 필드(`SpriteAssetCache`)를 따로 둔다.
- **스프라이트 에셋**: 픽셀 + 슬라이싱(`None/CellSize/CellCount`, 여백·간격) + 프레임별 피벗·불투명 경계 + PPU.
  `stb_image` 로 디코드. `ApplyImportOptions` 로 객체 주소를 보존한 채 in-place 갱신.
- **타입 규칙**: 확장자로 타입을 추정하는 것은 `.jmeta` 가 없을 때의 부트스트랩이고, 메타가 있으면 메타가 진실.
- **패키지**: `.jpak` 레코드(GUID·타입·페이로드 종류·오프셋·크기·해시), 참조 기반 포함 집합.
- **일시 로드** `CTransientAssetLoad`: 인스펙터 미리보기가 캐시를 누적시키던 것을 막는 일회성 소유자.

### 1.2 아팠던 것 (그쪽 문서가 스스로 적은 것)

| 번호 | 문제 | 새 설계의 대응 |
|---|---|---|
| P1·P2 | `RefreshAssetRegistry` 가 이름과 달리 비영구 에셋을 전부 언로드해 인스펙터 Apply 한 번에 씬이 깨졌다 | 레지스트리 재스캔과 언로드를 다른 함수로 두고, 언로드는 참조 수가 0 인 것만 |
| P3 | 소유 모델이 영구/비영구 둘뿐 - "씬이 쓰는 중" 이 없었다 | 캔버스 로드 집합 단위의 참조 수(§2.6) |
| P4 | 매 프레임 `LoadAsset` 이 뮤텍스를 잡았다 | 프레임 경로에 조회 없음. 해석 패스가 캔버스 로드 뒤와 편집 뒤에만 돈다(§2.6) |
| P5 | 에셋이 GPU 텍스처를 소유해 언로드가 곧 렌더 깨짐 | 에셋은 CPU 자료만. GPU 는 프레임워크 시스템의 라이브러리가 든다(§2.5) |
| P6 | 프로젝트 열 때 전부 선로드 | 참조 때 로드. 캔버스 열 때 그 캔버스의 집합만(§2.6) |
| P8·P9 | 에디터가 쓴 `.jmeta` 를 워처가 다시 감지 | 워처는 `.jmeta` 를 무시하고, 옵션 변경은 메모리 값을 직접 갱신(§2.8) |
| A-3 | 재질의 실제 자료가 큐 하나 | 이 문서의 범위 밖. §4 결정 항목 |
| (실물) | 스캔이 숨김 폴더를 걸러 내지 않아 `.omc/state/*.jsonl` 이 `Custom` 에셋으로 등록됐다 | `.` 으로 시작하는 폴더와 무시 패턴을 스캔에서 뺀다(§2.2) |

### 1.3 가져오는 것과 버리는 것

- **가져온다**: `.jmeta` 의 키와 "경로는 저장하지 않는다" 원칙, 확장자 → 타입 부트스트랩 규칙, 스프라이트
  슬라이싱·피벗·PPU 자료 모델, in-place 재로드, 워처의 `.jmeta` 무시, 참조 기반 패키지 레코드 모양.
- **버린다**: 텍스트 GUID(매 조회에 문자열 비교·트랜스코딩이 붙었다 - 그쪽 `FindOrLoadAssetByGuidText` 주석이
  그 비용을 적고 있다), `IAsset` 가상 계층과 `AssetRef` 의 복사마다 use-count, 에셋이 GPU 를 소유하는 것,
  매니저 하나가 임포트·로드·패키지·워처를 전부 드는 것, 프로젝트 열 때 전부 로드.

## 2. 새 설계 `[제안]`

지켜야 하는 현재 계약: 에셋은 `JBroAssetTypes`(Tier S, 값 타입)와 `JBroAsset`(Tier E)로 나뉜다(D-50).
스크립트는 `AssetId`·`AssetHandle` 만 보고 픽셀을 보지 않는다. 프레임 경로에 힙 할당·문자열·`dynamic_cast` 없음.
`std::` 컨테이너 대신 `Array`·`Table`·`String`. 핸들은 index + generation. 에디터 편집은 커맨드로만.

### 2.1 식별자

- **`[완료]` `Uuid { uint64 high; uint64 low; }` 가 JBroCore 에 있고 `using AssetId = Uuid` 다**(사용자 결정
  2026-09-18, D-111). 별개 타입을 만들지 않는다 - 아이디 역할에 더 가질 것이 없다. 임포트 때 `Uuid::Generate`(버전 4
  난수), 빌트인은 `Uuid::FromName`(버전 8, FNV-1a 두 시드)이라 두 가족이 겹치지 않는다. 텍스트(32 자리 16 진수,
  하이픈 표기도 읽음)는 `.jmeta` 와 `.jcanvas` 에 적을 때만 만들고(`GetUuidCodec`, `TypeDescriptorOf<Uuid>` 는
  `CoreTypeDescriptors.h`), 메모리와 표의 키는 항상 정수 둘이다(`Hash<Uuid>`). 기존 엔진은 `File::Guid` 가 `fs::path` 를
  상속한 **문자열**이었고 뒤에 `Guid128 { Hi, Lo }` 를 덧붙여 정수화했다 - 처음부터 정수 둘로 가면 그 두 벌이
  생기지 않는다. 128 비트라 충돌 검사는 두지 않는다. `MeshLibrary::BuiltinCubeId` 는 `FromName("builtin/cube")` 다.
  테스트: `Tests/UuidTests.cpp`(난수 4096 개 무충돌·버전 비트, 이름 결정성, 글자 왕복과 거절 여섯 가지, 코덱).
- `AssetHandle { index, generation }` 은 이번 실행의 자리다. 저장하지 않는다(현재 규약 유지).

### 2.2 레지스트리 (`AssetRegistry`, Tier E)

- 프로젝트를 열 때 `.jproject` 의 `rootPath` 아래 **에셋 폴더**를 한 번 스캔한다. 에셋 폴더는 `.jproject` 의
  `AssetDirectory` 키가 정하고 기본값은 `Contents/Assets` 다(사용자 결정, D-111). 기존 엔진은 이 키가 없었다
  (`AssetRoot` 는 코드 기본값 `Assets`).
- 스캔 규칙: `.` 으로 시작하는 폴더는 들어가지 않는다. `.jmeta` 는 짝 파일의 메타로만 읽는다. 무시 패턴은
  `.jproject` 의 `AssetIgnorePatterns`(기존 엔진 `AssetWatchIgnorePatterns` 를 이어받되 스캔에도 적용). (D-111)
- 짝 `.jmeta` 가 없는 파일은 확장자로 타입을 추정해 **에디터가** 메타를 만든다. 게임 실행은 메타 없는 파일을 등록하지
  않는다(에디터가 아닌 곳에서 파일을 새로 쓰지 않는다).
- 레코드: `AssetRecord { AssetId id; AssetType type; String relativePath; String importOptionsYaml; uint32 version; }`.
  표는 `Table<AssetId, AssetRecord>` 하나와 경로 → 아이디 `Table<String, AssetId>` 하나. 둘 다 프로젝트 여닫는
  시점과 편집 시점에만 바뀐다(문자열은 이 층에서만 산다).
- `AssetMetadata`(Tier S) 는 `id`·`type`·`sourcePath` 뷰를 주는 읽기 전용 값이다. 지금 정의(`JStringView type`) 는
  타입을 문자열로 들고 있어 `AssetType` 열거로 바꾼다. `[제안]`

### 2.3 타입 표

`enum class AssetType : uint8 { Unknown, Texture, Sprite, Mesh, Material, Shader, Canvas, Prefab, Audio, Font }`.
이번 계획에서 실체가 서는 것은 **Texture·Sprite** 이고 Mesh 는 `MeshLibrary` 의 빌트인만 잇는다. 확장자 표는
기존 엔진 것을 그대로 쓴다(이미지 `.png .jpg .jpeg .bmp .tga`, 메시 `.obj .gltf .glb`, 재질 `.jmat`, 셰이더 `.hlsl`,
캔버스 `.jcanvas`, 프리팹 `.jprefab`). 열거 값은 `.jmeta` 에 이름으로 적으므로 순서가 바뀌어도 파일이 깨지지 않는다.

**Texture 와 Sprite 를 갈라 둔다.** 기존 엔진은 둘을 `CSpriteAsset` 하나로 합쳤다(주석에 "이전에 분리돼 있던
CTextureAsset 의 역할도 통합"). 새 엔진은 `Asset::TextureAsset` 과 `Asset::SpriteAsset` 이 이미 따로 있고, 3D 재질이
같은 텍스처를 쓰게 되므로 나눈다. **이미지 파일 하나가 임포트되면 Texture 하나와 Sprite 하나가 함께 등록된다**
(Sprite 의 메타가 슬라이싱·피벗·PPU 를 갖고 Texture 를 가리킨다). 이미지 파일마다 `.jmeta` 는 하나고, 그 안에
두 아이디가 있다. (D-111)

### 2.4 시스템 (`AssetSystem`, Tier E)

- **타입별 풀.** `IAsset` 가상 기반을 두지 않는다. 타입마다 `Array<Slot<T>>` 와 자유 목록을 두고 핸들의 `index` 가
  풀의 자리, `generation` 이 재사용을 가른다(RHI 핸들과 같은 규약). 접근은 타입이 있는 함수다:
  `const TextureData* GetTexture(AssetHandle)`, `const SpriteData* GetSprite(AssetHandle)`. 타입이 다른 핸들은 `nullptr`.
  핸들에 타입 비트를 넣지 않는다 - 풀이 다르면 세대가 맞아도 뜻이 없으므로 풀마다 세대 공간을 나눠 잡는다
  (`index` 상위 4 비트를 타입으로 쓴다). `[가정]`
- **자료는 CPU 것만.** `TextureData { extent, format, Array<byte> pixels, uint32 pixelGeneration }`,
  `SpriteData { AssetHandle texture; Array<SpriteFrame> frames; float pixelsPerUnit; ... }`. GPU 핸들은 없다.
- **로드는 동기이고 메인 스레드다.** 첫 판은 워커를 두지 않는다. 기존 엔진의 P4 는 워커가 있어서 생긴 문제였고,
  지금 필요한 것은 캔버스 열 때 그 캔버스의 에셋을 올리는 것이다. 미리 읽기(prefetch)는 `[열림]`.
- **API**: `AssetHandle Load(AssetId)`(로드돼 있으면 참조 수만 올림), `void Release(AssetHandle)`, `bool IsLoaded`,
  `AssetHandle Find(AssetId)`(로드 안 돼 있으면 빈 핸들, 참조 수 안 올림), `bool ReloadInPlace(AssetId)`.
  `LoadTexture/LoadSprite/...` 다섯 함수는 없앤다 - 타입은 레지스트리가 알고, 결과 핸들의 풀이 타입을 말한다.
- **디코더**: 이미지는 `stb_image`(퍼블릭 도메인, 헤더 하나). 기존 엔진이 썼고 Web 타깃까지 같은 코드다.
  WIC 는 Windows 전용이라 버린다. **서드파티 추가는 사용자 확인 대상**이다. `[열림]`

### 2.5 GPU 는 프레임워크 시스템이 든다 (기존 P5 의 대응)

`MeshLibrary`(Framework3DSystem) 가 이미 이 모양이다: 렌더러에 올리고 `AssetId → AssetHandle` 을 든다.
같은 모양으로 **`SpriteLibrary`(Framework2DSystem)** 를 둔다.

- `Renderer::RegisterTexture(TextureDesc, JArrayView<byte> pixels) → AssetHandle` 과 `UnregisterTexture`. `RegisterMesh` 와
  같은 규약(프레임 밖에서만). 렌더러 안 `TextureResource { TextureHandle texture; }`. 샘플러는 렌더러가 둘
  (point·linear) 을 갖고 `SpriteSubmit` 에 비트 하나로 고른다. `[가정]`
- `SpriteLibrary::Resolve(spriteAssetHandle) → { rendererTexture, uvRect, pivot, size }`. 캔버스 로드 뒤 해석 패스가
  컴포넌트의 `sprite` 핸들을 채우고, `SpriteRender2DSystem` 은 매 프레임 그 핸들로 라이브러리 배열을 **인덱스**로
  읽는다(조회 없음).
- 텍스처 자료가 in-place 재로드되면 `pixelGeneration` 이 오르고, 라이브러리는 다음 프레임 밖 시점에 그 텍스처만
  다시 올린다. 핸들은 그대로다.

### 2.6 참조 수와 수명 (기존 P3·P6 의 대응)

- **캔버스가 자기 에셋 집합을 안다.** `.jcanvas` 를 읽을 때 컴포넌트의 `AssetId` 필드를 리플렉션으로 모아 집합을
  만들고, 그 집합을 `Load` 한다. 캔버스를 닫을 때 같은 집합을 `Release` 한다. 캔버스 전환은 두 집합의 차이만
  처리한다(기존 C4 결정을 그대로 잇는다).
- **편집으로 아이디가 바뀌면** 커맨드 실행 뒤의 해석 패스가 옛것 `Release`, 새것 `Load`. 스크립트가 런타임에
  `AssetId` 필드를 바꾸는 것도 같은 패스가 프레임 끝에 한 번 처리한다(프레임 안 로드 없음).
- **참조 수 0 이 곧 언로드는 아니다.** 명시적 `CollectUnused()` 가 프로젝트 닫기·캔버스 전환 뒤에 돈다. 인스펙터
  미리보기는 기존 `CTransientAssetLoad` 처럼 자기가 올린 것만 내리는 RAII 소유자를 쓴다.
- 영구(persistent) 플래그는 두지 않는다. 필요해지면 에디터 리소스만 예외로 한다. `[가정]`

### 2.7 임포트 옵션과 in-place 재로드

- `.jmeta` 의 `ImportOptions` 블록은 타입별 구조체를 리플렉션(`ReflectedYaml`)으로 읽고 쓴다. 손으로 파서를 쓰지
  않는다. `SpriteImportOptions { sliceType, rowCount, columnCount, cellWidth, cellHeight, marginX/Y, gapX/Y, pivotX/Y,
  pixelsPerUnit }` 은 기존 모델 그대로다.
- 옵션 변경은 세 단계로 나뉜다(기존 C1' 분류): 곱셈만(PPU) → 메모리 값 갱신, CPU 재계산(피벗·슬라이스) → 프레임
  재빌드, GPU 재생성(색 공간 등) → `pixelGeneration` 증가. 어느 단계든 핸들과 세대는 보존된다.
- 인스펙터의 옵션 편집은 커맨드다(되살릴 옵션 YAML 을 먼저 뜬다).

### 2.8 파일 감시 (에디터만)

- 콘텐츠 폴더의 변경을 에디터가 폴링으로 본다(기존 엔진도 mtime 폴링이었다). `.jmeta` 변경은 무시한다(자기 반향).
  원본 변경은 `ReloadInPlace`, 삭제는 레지스트리에서 빼되 로드된 자료는 참조 수가 0 이 될 때까지 산다,
  이동은 경로만 바꾼다(아이디 보존).
- 이것은 3 단계(§3) 뒤의 에디터 작업이다.

### 2.9 스크립트 표면

`Service::AssetService`(값형): `AssetHandle Load(AssetId)`, `void Release(AssetHandle)`, `bool IsLoaded(AssetHandle)`.
픽셀·프레임 자료는 주지 않는다. 스프라이트 애니메이션 같은 것은 컴포넌트 필드(`frameIndex`)로 표현한다.

### 2.10 `SpriteSubmit` 의 변경 (D-32 ABI)

`[완료]` 시트의 한 칸을 그리려면 UV 사각형이 필요하다. `SpriteSubmit` 에 `float uvRect[4]`(uMin, vMin, uScale, vScale) 을,
`GpuSpriteInstance` 에도 같은 16 바이트를 더했다(스트라이드 44 → 60). 셰이더는 `uv = uv * scale + min`. D-113 이
그 Decision 이다.

## 3. 단계 `[제안]`

각 단계는 테스트가 먼저다. 빌드 성공은 검증이 아니다.

1. **`[완료]` 레지스트리와 메타** (2026-09-18, `ffdddb3`·`ade7e2a`): `Uuid`(§2.1), `AssetType` 열거와
   `AssetTypeRules`(이름·확장자 표·메타 경로), `AssetMetaFile`(읽기·쓰기, 이미지는 `Sprite.Id` 필수), `AssetRegistry`
   (`Scan`·`Register`·`Unregister`·`Rename`·`Find`·`FindByPath`·`GetMetadata`, 배열 + 아이디 표 + 경로 표),
   `.jproject` 의 `AssetDirectory`·`AssetIgnorePatterns`. `AssetSystem` 은 아직 스텁이다(2 단계).
   테스트 `Tests/AssetRegistryTests.cpp`: 한글 이름의 임시 폴더에 파일을 놓고 스캔 → 숨김 폴더는 들어가지 않고 메타도
   생기지 않음, 메타 없는 파일에 메타 생성(이미지는 아이디 둘), 모르는 타입·무시 패턴·고아 메타·읽히지 않는 메타·중복
   아이디(Texture 쪽과 Sprite 쪽 각각)가 세어지고 등록되지 않음, 두 번째 스캔에서 아이디 보존, 메타와 함께 옮긴 파일의
   아이디 보존, 메타를 만들지 않는 스캔은 세기만 함, 끝을 당겨 채우는 지우기 뒤 두 표가 맞음, 이름 바꾸기가 Sprite 도
   따라옴, Texture 를 빼면 Sprite 도 빠짐, 글롭 패턴 다섯 경우. 뮤테이션 6/6 죽음(숨김 폴더 진입, 지우기 뒤 표 어긋남, 반쪽 등록 이미지, 확장자 대소문자, 무시 파일 타입 판정, 이름 바꾸기의 Sprite 누락; 32 분·9 분 실행).
   **실측**: `fopen_s` 는 UTF-8 경로를 ANSI 로 읽어 한글 폴더에서 조용히 실패했다 - 파일은 플랫폼이 열고(D-112)
   Windows 는 `filesystem::path` 의 UTF-8 생성자를 거친다. 좁은 문자열 리터럴의 `\uXXXX` 는 실행 문자 집합을 타므로
   경로 테스트는 와이드 리터럴로 쓴다. `JStringView`·`JArrayView` 는 ABI 용 POD 라 Tier E 안에서는 `std::string_view`
   를 쓴다. 스캔은 먼저 목록을 모으고 고아 판정은 그 목록만 본다(파일 시스템을 다시 묻지 않는다).
   **남긴 것**: `SaveAssetMetaFile` 은 네 키만 적으므로 `ImportOptions` 가 있는 메타를 덮어쓰면 옵션이 사라진다 -
   쓰는 쪽은 새 메타에만 쓴다. 옵션을 보존하는 다시 쓰기는 2 단계(임포트 옵션)에서 리플렉션 쓰기와 함께 온다.
2. **`[완료]` 시스템과 텍스처 로드** (2026-09-18, `931998d`): `AssetSystem` 이 Texture·Sprite 풀을 들고 핸들의
   `index` 상위 4 비트가 타입이다(`GetHandleType`). `Load`(참조 수, 같은 핸들)·`Release`·`Find`·`IsLoaded`·
   `GetTexture`·`GetSprite`·`ReloadInPlace`(핸들 보존, 텍스처는 `pixelGeneration` 증가)·`CollectUnused`(스프라이트가
   먼저 내려가고 그것이 놓은 텍스처가 따라 내려감). 디코더는 `stb_image`(PNG·JPEG·BMP·TGA, 파일 IO 없음), 파일은
   플랫폼이 읽는다(D-112). 스프라이트는 `.jmeta` 의 `Sprite.ImportOptions` 를 리플렉션으로 읽고(`SpriteImportOptions`,
   `SpriteSliceType`, `SpriteFrame` 은 AssetTypes 의 값 타입) `BuildSpriteFrames` 로 프레임을 만들며 자기 텍스처를
   참조 수로 잡는다. **해석 패스** `BindComponentAssets(표, 컴포넌트, acquired)`: `xxxId`(Uuid) 필드마다 로드해 짝
   `xxx`(AssetHandle) 필드를 채우고 빈 아이디는 핸들을 비운다. 배선: `EngineInstance::OpenProjectFile` 이 `AssetDirectory`
   를 스캔해(`EngineConfig::createMissingAssetMeta`, 에디터만 참) 시스템을 잇고, `EditorApplication::LoadCanvas` 가
   캔버스의 모든 컴포넌트에 해석 패스를 돌린 뒤 `CollectUnused` 하며, 앞 캔버스와 프로젝트 닫기에서 잡은 핸들을 놓는다.
   `LoadTexture/LoadSprite/...` 다섯 함수는 없앴다. Mesh·Material·Shader·Canvas 는 `Load` 가 빈 핸들이다(3 단계 이후).
   테스트 `Tests/AssetSystemTests.cpp`: 2x2 PNG 디코드(네 픽셀·쓰레기 거절), 프레임 표(None·CellCount 2x4·CellSize 여백
   간격 15 칸·거절 넷), 로드·참조 수·`Find`·수집 순서·세대 재사용·0 아래로 내려가지 않음, in-place 재로드(옵션이 있는 메타,
   읽히지 않는 옵션은 거절하고 옛 자료 유지), 해석 패스(채움·빈 아이디 비움·모르는 아이디·`ReleaseAll`). 뮤테이션 6/6 죽음(수집이 스프라이트의 텍스처를 안 놓음, 재로드가 참조 수를 안 올림, 비운 슬롯의 세대 유지, 해석 패스가 필드 이름 무시, 셀 폭을 행 수로 나눔, 재로드가 픽셀 세대를 되돌림; 37 분).
   **남긴 것**: ~~게임 호스트는 아직 캔버스 파일을 읽지 않는다~~ → D-115 에서 `--project`·`--canvas` 와 시작 캔버스 로드,
   프레임워크의 `BindCanvasAssets` 로 닫았다. 스크립트가 런타임에 `AssetId` 필드를 바꾸는 경우의 프레임 끝 해석(§2.6)은
   아직 없다 - 지금은 캔버스를 읽은 뒤와 (에디터의) 편집 뒤에만 푼다.
3. **`[완료]` 텍스처 있는 스프라이트** (2026-09-19, `3432ed0`, D-113): `Renderer::RegisterTexture/UpdateTexture/
   UnregisterTexture`, 1x1 흰색 기본 텍스처와 Nearest·Linear 샘플러, `SpriteSubmit { texture, uvRect, filter }`, GPU 인스턴스
   60B, 셰이더 `t0`/`s0` 샘플링, 텍스처·샘플러가 같은 이웃을 묶는 `SpriteRun`, `SpriteLibrary`(Framework2DSystem, 슬롯
   배열·한 번 업로드·`pixelGeneration` 재업로드·종료 때 반납), `SpriteRender2DSystem` 이 추출 단계에서 해석,
   `SpriteRenderer2D::frameIndex`. 세 바이트코드를 다시 구웠다(메시 셰이더는 바이트가 같았다 - 재현성 확인).
   픽셀 테스트(세 백엔드, 검증 레이어 켬): 2x2 텍셀이 스프라이트의 네 사분면에, UV 사각형이 초록 칸만, 텍스처 있는
   스프라이트 뒤의 텍스처 없는 스프라이트가 제 틴트(묶음이 흰색으로 돌아옴), 죽은 핸들은 흰색 x 틴트 + 카운트.
   `Tests/SpriteLibraryTests.cpp`(D3D12): 같은 텍스처 한 번 업로드, 칸 번호 넘침은 마지막 칸, 두 번째 이미지는 두 번째
   텍스처, 시트 옵션 재로드 뒤 칸 UV, 텍스처 재로드는 같은 핸들, 에셋 해제·재로드 뒤 슬롯의 옛 업로드 교체, 종료 때 반납.
   벤치마크: **회귀가 있었고 절반을 되찾았다.** 같은 기계 상태에서 3 단계 이전 커밋(A)과 잇달아 재어 소음을 갈랐다(메시 장면은
   둘이 같다). 스프라이트 60000, avg ms(제출/끝):
   A `661366a`: D3D12 0.80(0.31/0.49), D3D11 0.59~0.85(0.21~0.36/0.38~0.49), Vulkan 0.96~1.12(0.21~0.29/0.75~0.83).
   B `3432ed0`(두 번 지나감): D3D12 1.44~1.85(0.47~0.58/0.96~1.27), D3D11 1.34~1.50(0.48~0.54/0.87~0.96), Vulkan 1.89~1.94(0.45/1.43~1.49).
   C 한 번 지나감(이 커밋): D3D12 1.29~1.31(0.54~0.55/0.74~0.77), D3D11 1.20~1.49(0.52~0.67/0.68~0.83), Vulkan 1.57~1.69(0.46~0.51/1.11~1.18);
   텍스처 둘·묶음 600: D3D12 1.29~1.52, D3D11 1.25~1.28, Vulkan 1.74~1.77(묶음 600 개의 값은 0.1ms 안이다).
   남은 차이의 원인은 크기다: 패킷이 60B → 80B(`texture`·`uvRect`·`filter`), 인스턴스가 44B → 60B 라 제출의 memcpy 와
   업로드 힙(write-combined)으로의 memcpy 가 각각 1/3 늘었고, 픽셀 셰이더가 텍스처를 샘플링한다. **`[완료]` 인스턴스를
   40B 로 줄였다**(D-114, `5caaa94`): 틴트 `UByte4Norm`(4B), UV 사각형 `UShort4Norm`(8B). 그 뒤 A/B: 40B(이 커밋, 조용한 기계): D3D12 1.03~1.09(제출 0.33~0.38/끝 0.70~0.72), D3D11 1.26~1.29(0.56~0.57/0.71), Vulkan 1.30~1.49(0.34~0.48/0.96~1.01); 텍스처 둘·묶음 600: D3D12 1.14~1.18, D3D11 1.56~1.63, Vulkan 1.35~1.69. 60B 한 번 지나감(앞 실측, D-113): D3D12 1.29~1.31, D3D11 1.20~1.49, Vulkan 1.57~1.69. 3 단계 이전: D3D12 0.80, D3D11 0.59~0.85, Vulkan 0.96~1.12. 즉 D3D12·Vulkan 은 회귀의 절반을 더 되찾았고 D3D11 은 거의 그대로다. 남은 +0.2ms 는 패킷 80B 의 제출 memcpy 와 픽셀 셰이더의 샘플링이다(같은 세션의 A 측 재측정은 다른 세션의 빌드가 겹쳐 메시 장면까지 두 배로 나와 버렸다).
   패킷 쪽(80B)은 `material` 이 아직 읽히지 않으므로 그대로 둔다.
   뮤테이션: 6 개 중 5 죽음(묶음을 절대 합치지 않음, 죽은 핸들 안 셈, uMin 안 옮김, 라이브러리 UV 의 x 를 y 에서, 라이브러리가 매번
   다시 올림; 19 분). 살아남은 하나(`RegisterTexture` 의 바이트 수 검사 제거)는 **동치**다 - 백엔드의 `WriteTexture` 가 같은
   크기 오류를 이미 거절해 결과가 같다. 렌더러의 검사는 텍스처를 만들고 버리는 낭비를 막는 것뿐이라 그대로 둔다.
   **남긴 것** `[열림]`: `size`·`pivot` 은 저작 값이다(프레임 픽셀 / PPU 자동 크기는 미정). 샘플러 선택(`filter`)은 패킷에만
   있고 컴포넌트·에셋 옵션에는 없다. 텍스처 하나가 여러 뷰에 걸치는 경우의 묶음은 뷰마다 새로 시작한다(설계).
4. **에디터** - 결정은 D-116·D-117, 순서와 항목은 `todo-2d.md` 의 "에셋 4 단계"(여섯 걸음, 1·2 가 D-118, 3·4 가 D-119 로 섰다). 요약: 에셋 필드는 공용 드롭다운 위젯
   (`FilterCombo` → `AssetField`, 컴포넌트 추가 팝업까지 일괄 적용), 에셋 브라우저 패널, 임포트 옵션 편집 커맨드(옵션을
   보존하는 메타 다시 쓰기 포함), 파일 감시는 `[논의]`. 스프라이트 크기 정책과 샘플러 선택의 자리도 같은 문서에 `[논의]`.

## 4. 사용자 결정이 필요한 것 `[열림]`

1. ~~`stb_image` 를 서드파티로 들인다~~ **확정**(2026-09-18, D-111). 임포트 경로에서만 쓴다.
2. ~~`SpriteSubmit`·GPU 인스턴스에 UV 사각형 추가~~ **확정**(2026-09-18). 3 단계에서 D-32 ABI 를 고칠 때 Decision 으로.
3. ~~이미지 하나 = Texture + Sprite 두 에셋~~ **확정**(D-111).
4. ~~`.jproject` 새 키~~ **확정**: `AssetDirectory` 기본값 `Contents/Assets`, `AssetIgnorePatterns`(D-111).
5. ~~재질의 방향~~ **확정**: D-33 의 Shader Graph 방향. 자료 모델은 `{ Shader 에셋, 파라미터 블록, 텍스처 슬롯 }`,
   첫 구현은 빌트인 셰이더를 Shader 에셋으로 등록(D-111).
6. 미리 읽기 워커, `.jpak` 패키지, 렌더 패스 그래프는 이 계획에 넣지 않는다.
7. ~~파일 IO 의 플랫폼 경계~~ **확정: 파일 시스템 전체를 플랫폼이 관리한다**(D-112, 2026-09-18). 사용자는 스캔까지
   플랫폼 뒤에 두기를 택했다(플랫폼마다 읽는 길이 천차만별). `IPlatform` 에 읽기·쓰기·존재 확인·열거가 생겼고
   `AssetRegistry::Scan(IPlatform&, ...)`·`Load/SaveAssetMetaFile(IPlatform&, ...)` 이 그것만 쓴다 - JBroAsset 에
   `std::filesystem` 이 남지 않았다. Web·Android 는 거짓을 돌려주는 자리만 있다. 남았던 직접 열기 다섯 곳도 옮겼다
   (D-112 본문, 예외 둘).
