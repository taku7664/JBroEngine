# 2D 남은 일

> 공용 남은 일과 Decisions 는 `todo.md`, 3D 는 `todo-3d.md` 다(D-116). 여기는 2D 프레임워크·2D 에디터 화면·스프라이트
> 파이프라인의 것만 적는다. 상태는 `[진행 예정]` `[논의]` `[열림]` 으로 붙인다. `[논의]` 는 사용자와 방향을 정하기
> 전이고, 정해지면 Decisions 로 옮기고 여기서는 `[진행 예정]` 이 된다. 구현을 마친 항목은 지우지 않고 취소선으로 긋고
> 언제(날짜·커밋)와 어디서(파일·함수) 고쳤는지 붙인다(D-204).

## 물리 (physics-plan)

- `[진행]` **2D 물리 이식** (D-199). 기존 엔진 물리를 이어받되 오목 폴리곤 결함(계획서 §1.2 의 여섯 원인)을 고친다. 단계는 계획서 §4:
  1 커널 뼈대와 기하 → 2 좁은 판정·브로드페이즈 → 3 솔버 → 4 어댑터·이벤트 → 5 에디터 → 6 질의 확장(후순위지만 구현).
  `[완료]` 1~4 단계 - 커널 `JBroPhysics2D`(기하·판정·월드와 솔버)와 어댑터 `Physics2DSystem`(동기화·되쓰기·충돌/트리거 훅·
  질의)이 섰다. 훅 발송은 D-207(2D 스크립트는 모두 `GameScript2D`, 등록이 컴파일 시간에 검사). 테스트와 뮤테이션이 계획서 §4 에 있다.
  `[진행 예정]` 5 단계 에디터(인스펙터의 `points` 편집, 캔버스 뷰의 폴리곤·조각 그리기), 6 단계 질의 확장(후순위지만 구현).
  `[열림]` 4 단계가 남긴 것 다섯(크기 애니메이션의 도형 재생성, 캡슐, 부모의 찌그러짐, 꺼진 부모, 실제 에디터 확인) - 계획서 §4 의 4.

## 오디오 (audio-plan §3-3, D-197)

- `[완료]` **`Component::AudioListener2D` 와 `System::Audio2DSystem`**(D-201). 리스너가 없으면 게임 카메라 자리에서 듣고, 가까운
  소리가 한쪽 귀로 꺾이지 않게 `panDistance` 깊이를 둔다. 실측과 테스트는 계획서 §3-3.

## 에셋 4 단계 - 에디터 (asset-plan §3-4)

- `[완료]` **에셋 필드는 드롭다운이다**(2026-09-20 결정, D-118 로 섰다 - 아래 4 단계 순서의 1·2). 인스펙터의 `spriteId` 같은 `AssetId` 필드 자리에 레지스트리의
  같은 타입 에셋을 이름(상대경로)으로 고르는 드롭다운을 그린다. 지금은 32 자리 16 진수 글자 칸이다.
  **공용 위젯으로 만들어 일괄 적용한다.** 기존 엔진은 `ImFilterCombo`(검색 칸이 달린 목록 드롭다운, 항목 벡터·현재 번호·
  빈 글·최대 표시 수)를 두고 `ImAssetField`·`ImAudioBusField`·컴포넌트 추가가 그것을 썼다. 우리 위젯 계층에는 `EnumCombo`
  하나뿐이라 `Widget::FilterCombo`(항목 뷰·현재 번호·검색·빈 글) 를 먼저 세우고, 그 위에 `Widget::AssetField`(타입 필터·
  비우기·없는 에셋 표시)를 얹는다. **일괄 적용 대상**: `InspectorPanel::DrawAddComponent` 가 ImGui `Button`·`OpenPopup`·
  `BeginPopup` 을 직접 불러 컴포넌트 목록을 그린다(§11.1 위반) → `FilterCombo` 로. `EnumCombo` 도 같은 몸을 쓰게 한다.
  값 변경은 `SetPropertyCommand` 하나다(아이디가 바뀌면 프레임워크의 `BindCanvasAssets` 를 다시 부른다, D-115).
- `[완료]` **에셋 브라우저 패널**(2026-09-20 결정, D-120 으로 섰다 - 아래 4 단계 순서의 5). 폴더 트리 + 파일 목록(레지스트리 기준, 타입별 아이콘·이름), 고르면
  인스펙터에 그 에셋의 임포트 옵션(`SpriteImportOptions`: 슬라이싱·피벗·PPU)이 뜬다. 옵션 편집은 커맨드이고(되살릴 옵션
  YAML 을 먼저 뜬다) 적용은 `AssetSystem::ReloadInPlace` 라 핸들이 산다. 메타 다시 쓰기는 리플렉션 쓰기로 옵션을 보존해야
  한다 - 지금의 `SaveAssetMetaFile` 은 네 키만 적는다(asset-plan §3-1 남긴 것). 새 패널이라 위젯 계층(`Tree`·`List`)만 쓴다.
- `[완료]` **파일 감시** (D-117 로 결정, D-121 로 섰다 - 아래 4 단계 순서의 6). 기존 엔진은 `IFileWatcher::Poll()` 이 폴더를 걸어 `last_write_time` 을 비교하는 **mtime 폴링**이었고
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

1. `[완료]` **`Widget::FilterCombo`** (`be2e7cd`·`a3f55ed`, D-118) - `ArrayView<const char* const>` 항목, 현재 번호, 검색 칸(열 때마다
   비우고 포커스, Enter 는 보이는 첫 항목), 빈 글, 최대 표시 수(1~8). `EnumCombo` 가 이 몸을 쓰고(이름이 여덟을 넘을 때만
   검색 칸), 인스펙터의 "컴포넌트 추가" 는 현재 번호 -1 인 같은 위젯이다. `MatchesFilter` 는 에셋 브라우저가 쓰라고 공개했다.
   테스트(`EditorWidgetTests`): 마우스로 열고 글자를 쳐 Enter 로 고르기, 다시 열면 검색이 빔, 맞는 것이 없는 Enter 는 팝업을
   두고 값을 두지 않음, 같은 항목 다시 고르기는 변경 아님, 빈 목록, `EnumCombo` 를 마우스로 눌러 `FromIndex` 가 불림.
2. `[완료]` **`Widget::AssetField`** (`00c0b90`, D-118) - 이름 뷰와 아이디 뷰를 받는다(위젯은 레지스트리를 모른다). 맨 위 비우기,
   빈 아이디는 `asset.none`, 목록에 없는 아이디는 `asset.missing` 으로 보이고 사용자가 고르기 전에는 값을 고치지 않는다.
   인스펙터는 `JBro.Uuid` 이고 이름이 `Id` 로 끝나는 필드(해석 패스와 같은 규칙)에 이것을 그리고, 이름 앞부분이 타입 이름이면
   (`spriteId` → Sprite) 그 타입만 보인다. 고르면 `SetPropertyCommand` 하나. 해석은 `EditorApplication::Tick` 이 커맨드
   판번호가 움직였을 때 `BindCanvasAssets` 를 다시 불러 되돌리기·붙여넣기도 덮는다. 테스트: 위젯 단위(이름으로 고르면 짝
   아이디, 비우기, 없는 아이디 방치)와 에디터 창(진짜 PNG 를 둔 프로젝트를 열어 `spriteId` 줄에서 `hero` 를 쳐 고르면
   커맨드 하나와 해석된 핸들, 되돌리면 둘 다 빔). 원소 안의 `AssetId` 는 아직 글자 칸이다(목록 원소 편집 경로, D-89).
   뮤테이션: FilterCombo 6/6 잡힘(첫 판에 짧은 enum 에 검색 칸을 늘 그리는 변이가 살아 활성 입력 칸 없음 검사를 더해 잡았다), AssetField 8/8 잡힘(비우기 항목이 첫 아이디를 씀, 번호 밀림 무시, 없는 아이디 덮어쓰기, 타입 필터 제거, 커맨드 없음, 커맨드 뒤 재해석 없음, Enter 가 아무것도 안 고름).

3. `[완료]` **스프라이트 크기와 피벗** (`805ef60`, D-119) - `SpriteRenderer2D::sizeMode { FromSprite, Custom }`(기본 `FromSprite`).
   `SpriteLibrary::Resolve` 가 `SpriteFrameView`(칸 픽셀 / 에셋 PPU 의 유닛 크기, 칸의 피벗)를 함께 주고 시스템이
   `FromSprite` 면 그것을, `Custom` 이거나 스프라이트가 풀리지 않았으면 저작 `size`·`pivot` 을 쓴다. `SpriteImportOptions.
   pixelsPerUnit` 기본 100(0 이하로 적힌 옛 메타도 100). `.jproject` 의 `PixelsPerUnit` 은 필드·파서에서 뺐고 남은 키는 모르는
   키로 건너뛴다. 테스트: 라이브러리(2x2 PPU 100 → 0.02, 시트 칸 PPU 2 → 0.5 와 시트 피벗, 2x1 칸 → 1 x 0.5), 캔버스를 시스템에
   돌려 `FromSprite`·`Custom`·미해석 세 경우, 필드 수 12. 뮤테이션: 6 개 중 5 잡힘(높이가 너비를 쓰는 변이가 살아 2x1 칸 검사를 ④ 에 더해 잡았다).
4. `[완료]` **샘플러** (`0e71bd8`, D-119) - `TextureFilter { Default, Nearest, Linear }` 와 `TextureImportOptions { filter }`
   (메타 `Texture.ImportOptions`, 리플렉션)가 AssetTypes 에, `.jproject` 에 `TextureFilter: Nearest|Linear`(없으면 Nearest,
   `Default` 는 거절). 에셋 시스템이 `SetDefaultTextureFilter` 로 프로젝트 기본을 받아 로드·재로드 때 텍스처의 유효 필터를
   정하므로(`TextureData::filter`, `Default` 는 오지 않는다) 라이브러리·렌더 아이템·브리지는 값을 나를 뿐이고 브리지가
   `SpriteFilter` 로 옮긴다. 테스트: 프로젝트 파일 읽기·거절, 에셋 시스템(텍스처 옵션이 이김, `Default` 는 프로젝트 기본,
   모르는 이름 거절), 렌더러 픽셀(Linear 는 텍셀 경계에서 섞임), 프레임워크 전체를 세운 픽셀(진짜 PNG, 프로젝트 Nearest 면
   경계가 순수 초록, 메타를 Linear 로 바꿔 재로드하면 섞임), 엔진 호스트가 프로젝트 파일의 필터를 에셋 시스템에 넘김.
   뮤테이션: 10/10 잡힘(프로젝트 기본 미적용, 메타 필터 무시, 틀린 옵션 허용, Default 기본 유지, 프로젝트 키 무시, 라이브러리·시스템·브리지가 필터를 버림, 엔진이 기본을 안 넘김 - 마지막 것은 처음 살아 엔진 호스트 검사를 더해 잡았다).

5. `[완료]` **에셋 브라우저 패널** (`e6d3e96`·`ab6a0aa`, D-120) - `AssetMetaFile` 이 `Texture.ImportOptions`·`Sprite.ImportOptions`
   두 블록을 리플렉션으로 읽고 쓴다(있는 블록만 적고, 틀린 값은 파일 전체의 실패) - asset-plan §3-1 의 "네 키만 적는다" 가
   닫혔고 에셋 시스템도 이 파서 하나로 옵션을 읽는다. `AssetBrowserPanel`(아래쪽 독): 레지스트리를 폴더 나무로, 검색 칸,
   파일마다 한 줄(이미지의 Sprite 레코드는 Texture 줄에 접힘), 줄 전체가 누름을 받아 `SetSelectedAsset`. 에셋 선택은
   오브젝트 선택과 배타다. 인스펙터는 오브젝트가 없고 에셋이 있으면 텍스처·스프라이트 임포트 옵션 블록을 컴포넌트와 같은
   모양(슬롯 → 접는 머리 → `##import` 표)으로 그리고, 편집은 `SetAssetMetaCommand`(되살릴 값 = 메타 글자 전체, 실행 =
   새 글자 쓰기 + 로드된 것 `ReloadInPlace`, 드래그는 한 덩어리)다. 에디터는 커맨드 판번호가 움직이면 고른 메타를 다시
   읽는다. 테스트: 메타 왕복과 틀린 값 거절, 에디터 창에서 탭을 앞으로 꺼내 `art/hero.png` 줄을 눌러 고르고 인스펙터의
   `pixelsPerUnit` 을 끌면 디스크의 메타에 블록이 생기고 아이디가 보존되며 로드된 스프라이트가 제자리 재로드되고, 되돌리면
   글자와 스프라이트가 원래대로, 오브젝트를 고르면 에셋 선택이 빔. 뮤테이션: 10/10 잡힘(스프라이트·텍스처 블록 안 씀, 틀린 옵션 허용, 되돌리기가 새 글자를 씀, 제자리 재로드 없음 - 이것은 처음 재지 않아 검사를 더한 뒤 잡았다, 커맨드 뒤 메타 안 읽음, 고친 블록 표시 안 함, 오브젝트 선택이 에셋 선택을 안 비움, 줄 클릭 무시, 엔진이 프로젝트 필터를 안 넘김).
6. `[완료]` **파일 감시** (`0bea257`, D-121) - `IPlatform::WatchDirectory`/`StopWatching`/`TakeFileEvents` 와 POD `FileEvent`
   {Created, Modified, Removed, Renamed(oldPath), Overflow}. Windows 는 `ReadDirectoryChangesW` 를 워커에서 돌리고 고정
   고리 버퍼(256)에 쌓는다 - 첫 요청은 워커를 띄우기 전에 건다(늦게 걸면 감시 직후의 변경을 놓쳤다). 폴더를 건너 옮기는
   것은 Windows 가 삭제+생성으로 알린다. `EngineInstance::PollAssetChanges`(`EngineConfig::watchAssetDirectory`, 에디터만):
   원본 변경 → 텍스처와 짝 스프라이트 `ReloadInPlace`, 이름 바꾸기 → `Rename`, 삭제 → `Unregister`(로드된 자료는 참조 수
   0 까지), 생성·모르는 삭제·넘침 → 다시 스캔, `.jmeta` 는 무시. 에디터는 `Tick` 첫머리(프레임 밖)에서 꺼내 적용하고 다시
   스캔했으면 `BindCanvasAssets`. 테스트: 플랫폼(생성·쓰기·하위 폴더의 한글 이름·같은 폴더 이름 바꾸기·폴더 이동·삭제·
   멈춘 뒤 없음), 엔진 호스트(원본을 다시 쓰면 같은 핸들로 세대가 오름, 새 파일은 다시 스캔되고 옛 아이디 유지, 지우면
   레코드는 빠지고 자료는 남음), 에디터(열린 채로 생긴 파일이 등록됨). 뮤테이션: 9/9 잡힘(백슬래시 유지, 이름 바꾸기의 옛 이름 버림, 멈춤 신호 없음 - 워커 합류가 영원히 기다려 테스트가 멈추는 것으로 잡힘, 첫 요청을 늦게 걺, 변경을 재로드 안 함, 생성을 다시 스캔 안 함, 삭제를 안 뺌, 감시를 안 켬, 에디터가 안 꺼냄).


## 스프라이트

- `[완료]` **스프라이트 크기 정책** (D-117 로 결정, D-119 로 섰다 - 위 4 단계 순서의 3). 지금 `SpriteRenderer2D` 는 `size`(유닛)·`pivot` 을 저작 값으로 들고 텍스처 크기와
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
- `[완료]` **샘플러(필터) 선택이 사는 자리** (D-117 로 결정, D-119 로 섰다 - 위 4 단계 순서의 4). 렌더러 패킷 `SpriteSubmit::filter`(D-113)는 그 스프라이트를 Nearest(텍셀
  그대로, 픽셀 아트)로 샘플링할지 Linear(부드럽게)로 할지다. 지금은 프레임워크가 언제나 Nearest 를 보내고 컴포넌트나
  에셋에는 고를 자리가 없다. Unity 는 텍스처 임포터의 Filter Mode(기본 Bilinear, 픽셀 아트는 Point)다.
  권고: `.jproject` 에 프로젝트 기본(`TextureFilter: Nearest|Linear`, 2D 픽셀 아트 프로젝트는 Nearest)을 두고 텍스처의
  임포트 옵션이 그것을 덮어쓴다. 컴포넌트에는 두지 않는다.
- `[열림]` 스크립트가 런타임에 `AssetId` 필드를 바꾸는 경우의 프레임 끝 해석(asset-plan §2.6). 지금은 캔버스를 읽은 뒤와
  에디터의 편집 뒤에만 `BindCanvasAssets` 가 돈다.
- `[진행 예정]` **텍스트** (D-200, [text-plan.md](./text-plan.md)). 커널 `JBroText` + 글자마다 스프라이트 인스턴스 + `TextStore`.
  비트맵 글자는 렌더 패스 그래프 없이 지금의 스프라이트 경로로 선다. 다음은 계획서 §5 의 1 단계(커널 뼈대와 레이아웃)이고,
  `stb_truetype.h` 와 OFL 시험 폰트를 리포에 들이는 일이 먼저다.
- `[열림]` 기존 엔진의 2D 라이팅·소프트 섀도(`RenderWeave` 의 occluder·light·composite·tonemap 패스)·Shape 렌더러.
  렌더 패스 그래프(공용 todo)가 먼저다.
