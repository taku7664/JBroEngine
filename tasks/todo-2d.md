# 2D 남은 일

> 공용 남은 일과 Decisions 는 `todo.md`, 3D 는 `todo-3d.md` 다(D-116). 여기는 2D 프레임워크·2D 에디터 화면·스프라이트
> 파이프라인의 것만 적는다. 상태는 `[진행 예정]` `[논의]` `[열림]` 으로 붙인다. `[논의]` 는 사용자와 방향을 정하기
> 전이고, 정해지면 Decisions 로 옮기고 여기서는 `[진행 예정]` 이 된다.

## 에셋 4 단계 - 에디터 (asset-plan §3-4)

- `[진행 예정]` **에셋 필드는 드롭다운이다**(2026-09-20 결정). 인스펙터의 `spriteId` 같은 `AssetId` 필드 자리에 레지스트리의
  같은 타입 에셋을 이름(상대경로)으로 고르는 드롭다운을 그린다. 지금은 32 자리 16 진수 글자 칸이다.
  **공용 위젯으로 만들어 일괄 적용한다.** 기존 엔진은 `ImFilterCombo`(검색 칸이 달린 목록 드롭다운, 항목 벡터·현재 번호·
  빈 글·최대 표시 수)를 두고 `ImAssetField`·`ImAudioBusField`·컴포넌트 추가가 그것을 썼다. 우리 위젯 계층에는 `EnumCombo`
  하나뿐이라 `Widget::FilterCombo`(항목 뷰·현재 번호·검색·빈 글) 를 먼저 세우고, 그 위에 `Widget::AssetField`(타입 필터·
  비우기·없는 에셋 표시)를 얹는다. **일괄 적용 대상**: `InspectorPanel::DrawAddComponent` 가 ImGui `Button`·`OpenPopup`·
  `BeginPopup` 을 직접 불러 컴포넌트 목록을 그린다(§11.1 위반) → `FilterCombo` 로. `EnumCombo` 도 같은 몸을 쓰게 한다.
  값 변경은 `SetPropertyCommand` 하나다(아이디가 바뀌면 프레임워크의 `BindCanvasAssets` 를 다시 부른다, D-115).
- `[진행 예정]` **에셋 브라우저 패널**(2026-09-20 결정). 폴더 트리 + 파일 목록(레지스트리 기준, 타입별 아이콘·이름), 고르면
  인스펙터에 그 에셋의 임포트 옵션(`SpriteImportOptions`: 슬라이싱·피벗·PPU)이 뜬다. 옵션 편집은 커맨드이고(되살릴 옵션
  YAML 을 먼저 뜬다) 적용은 `AssetSystem::ReloadInPlace` 라 핸들이 산다. 메타 다시 쓰기는 리플렉션 쓰기로 옵션을 보존해야
  한다 - 지금의 `SaveAssetMetaFile` 은 네 키만 적는다(asset-plan §3-1 남긴 것). 새 패널이라 위젯 계층(`Tree`·`List`)만 쓴다.
- `[진행 예정]` **파일 감시** (D-117 로 결정: 아래 (b)·권고대로). 기존 엔진은 `IFileWatcher::Poll()` 이 폴더를 걸어 `last_write_time` 을 비교하는 **mtime 폴링**이었고
  (`WindowsFileWatcher.cpp`), 비동기 IO 스레드가 아니었다. 갈림길:
  - (a) 폴링: 구현이 작고 스레드가 없다. 에셋 수에 비례해 매 주기 폴더를 걷는다(수천 개면 수십 ms). `.jmeta` 자기 반향은
    확장자로 걸러야 한다.
  - (b) `ReadDirectoryChangesW`(Windows) + 완료 포트를 워커 스레드에서 돌리고, 이벤트를 POD 로 메인 스레드 큐에 넘긴다.
    즉시 반응하고 비용이 변경 수에 비례한다. 플랫폼마다 구현이 다르다(Linux inotify, macOS FSEvents) - `IPlatform` 의 파일
    시스템(D-112) 뒤에 `WatchDirectory` 로 들어가는 것이 맞다. `SafePtr` 는 메인 스레드 전용이므로 워커는 경로 문자열만 든다.
  - 감지한 뒤: 원본이 바뀌면 자동 `ReloadInPlace` 인지 알림만인지. 기존 엔진은 자동 재로드였고 `.jmeta` 는 무시했다.
  권고: (b) 를 `IPlatform::WatchDirectory` 로, 이벤트는 메인 스레드가 프레임 밖에서 꺼내 처리, 원본 변경은 자동 재로드,
  `.jmeta` 변경은 무시, 이동은 경로만, 삭제는 참조 수 0 까지 유지. 게임 실행에는 감시가 없다.

### 4 단계 순서 (계획, 2026-09-20)

각 걸음은 테스트가 먼저고 걸음마다 커밋한다. 위젯은 §11.1 대로 패널이 ImGui 를 직접 부르지 않는다.

1. **`Widget::FilterCombo`** - 항목 뷰(`JArrayView<const char*>` 또는 이름 표), 현재 번호, 검색 칸, 빈 글, 최대 표시 수.
   `EnumCombo` 를 이 몸 위에 얹고, 인스펙터의 "컴포넌트 추가" 팝업을 이것으로 바꾼다. 테스트: 에디터 UI 테스트로 열고
   검색해 고르기, 빈 목록, 키보드 이동.
2. **`Widget::AssetField`** - 타입 필터·비우기·없는 에셋 표시. 인스펙터가 `AssetId` 필드(`TypeDescriptorOf<Uuid>` 이고 이름이
   `Id` 로 끝나는 것)를 만나면 글자 칸 대신 이것을 그린다. 항목은 레지스트리의 같은 타입 레코드(상대경로로 표시). 고르면
   `SetPropertyCommand` 하나, 실행 뒤 프레임워크의 `BindCanvasAssets`. 테스트: 필드가 드롭다운으로 그려지는지, 고르면
   커맨드 하나와 해석된 핸들, 되돌리기.
3. **스프라이트 크기와 피벗** - `SpriteRenderer2D::sizeMode`(`FromSprite` 기본), 추출 단계에서 `SpriteLibrary::Resolve` 가
   프레임 픽셀·피벗·PPU 를 주고 시스템이 크기를 만든다. `.jproject` 의 `PixelsPerUnit` 제거(`ProjectFile.pixelsPerUnit`,
   파서, 테스트 둘). 테스트: 2x2 텍스처 PPU 2 → 1x1 유닛, 시트 칸의 크기, `Custom` 은 저작 값.
4. **샘플러** - `.jproject` `TextureFilter`, `TextureImportOptions { filter }`(메타의 `ImportOptions` 블록, 리플렉션),
   `SpriteLibrary` 가 `SpriteSubmit::filter` 로 넘김. 픽셀 테스트: Linear 로 2x2 를 키우면 가운데가 섞인다.
5. **에셋 브라우저 패널** - 폴더 `Tree` + 파일 `List`(레지스트리 기준), 선택 → 인스펙터에 임포트 옵션. 옵션 편집은
   커맨드(되살릴 옵션을 먼저 뜬다) → 메타를 리플렉션으로 다시 쓰고(`ImportOptions` 보존) `ReloadInPlace`. 로컬라이징 키.
6. **파일 감시** - `IPlatform::WatchDirectory`/`TakeFileEvents`(Windows 구현, Web·Android 는 거짓), 에디터가 프레임 밖에서
   꺼내 처리. 테스트: 임시 폴더에 쓰고 이벤트가 오는지, `.jmeta` 는 무시, 이동·삭제.

## 스프라이트

- `[진행 예정]` **스프라이트 크기 정책** (D-117 로 결정: `.jproject` 의 `PixelsPerUnit` 을 없애고 에셋 PPU, 아래 (C) 의 `FromSprite` 부터). 지금 `SpriteRenderer2D` 는 `size`(유닛)·`pivot` 을 저작 값으로 들고 텍스처 크기와
  무관하다. 다른 엔진:
  - Unity: 텍스처 임포터의 Pixels Per Unit 이 크기를 정한다(`픽셀 / PPU` 유닛). `SpriteRenderer` 에는 크기 필드가 없고
    스케일은 Transform 이 한다. Draw Mode 가 Sliced·Tiled 일 때만 `size` 가 생긴다.
  - Unreal Paper2D: 스프라이트 에셋마다 Pixels Per Unit(기본 1, 즉 픽셀 = 언리얼 유닛).
  - Godot: 텍스처 픽셀 하나가 유닛 하나다. PPU 개념이 없고 스케일은 노드가 한다.
  - GameMaker·Cocos: 픽셀 = 유닛(포인트).
  - 기존 엔진(JBroEngine_old): Unity 와 같다. 프레임 픽셀 / 유효 PPU(에셋의 PPU, 0 이면 프로젝트 기본 PPU).
  우리 자리: `.jproject` 에 `PixelsPerUnit`(기본 100)이 이미 있고 `SpriteImportOptions.pixelsPerUnit`(0 = 프로젝트 기본)도
  섰다. 남은 것은 컴포넌트의 `size` 다. 후보:
  - (A) Unity·기존 엔진 방식: 크기 = 프레임 픽셀 / 유효 PPU. `size` 필드를 없애고 스케일은 `Transform2D` 가 한다.
  - (B) 지금처럼 저작 `size` 유지: 이미지를 바꿔도 화면 크기가 안 바뀐다 - 스프라이트 시트 애니메이션에서 칸 크기가
    다르면 찌그러진다.
  - (C) `sizeMode { FromSprite, Custom }`: 기본은 (A), Sliced·Tiled 같은 뒤의 드로우 모드에서만 Custom.
  권고: (C) 로 가되 이번에는 `FromSprite` 만 구현한다(= A 의 동작, 필드는 남긴다). `pivot` 은 프레임의 피벗을 기본으로
  쓰고 컴포넌트 값은 덮어쓰기 옵션으로 둔다.
- `[진행 예정]` **샘플러(필터) 선택이 사는 자리** (D-117 로 결정: 권고대로). 렌더러 패킷 `SpriteSubmit::filter`(D-113)는 그 스프라이트를 Nearest(텍셀
  그대로, 픽셀 아트)로 샘플링할지 Linear(부드럽게)로 할지다. 지금은 프레임워크가 언제나 Nearest 를 보내고 컴포넌트나
  에셋에는 고를 자리가 없다. Unity 는 텍스처 임포터의 Filter Mode(기본 Bilinear, 픽셀 아트는 Point)다.
  권고: `.jproject` 에 프로젝트 기본(`TextureFilter: Nearest|Linear`, 2D 픽셀 아트 프로젝트는 Nearest)을 두고 텍스처의
  임포트 옵션이 그것을 덮어쓴다. 컴포넌트에는 두지 않는다.
- `[열림]` 스크립트가 런타임에 `AssetId` 필드를 바꾸는 경우의 프레임 끝 해석(asset-plan §2.6). 지금은 캔버스를 읽은 뒤와
  에디터의 편집 뒤에만 `BindCanvasAssets` 가 돈다.
- `[열림]` 기존 엔진의 2D 라이팅·소프트 섀도(`RenderWeave` 의 occluder·light·composite·tonemap 패스)·텍스트
  (FreeType + HarfBuzz)·Shape 렌더러. 렌더 패스 그래프(공용 todo)가 먼저다.
