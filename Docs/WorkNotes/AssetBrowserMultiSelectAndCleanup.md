# 에셋 브라우저 다중 선택 + 정리 작업 노트

## 작업 규칙 (맨 위에 누적)
- 모든 보고/설명/주석은 한국어로 작성한다.
- **에디터에서는 고정(하드코딩) 문자열 사용을 최대한 피한다. 단 ImGui 위젯 ID(화면에 안 보이는 `"##.."` 식별자 등)는 예외.** 화면에 보이는 문자열은 `Loc::Text("key")` 로 참조한다. (사용자 피드백 2026-05-31)
- **로컬라이징은 솔루션 루트 `Localization/` 폴더에서 작업한다**(`ko-KR.yaml`, `en-US.yaml`). 정본은 루트 `Localization/`. (사용자 피드백 2026-05-31)
- "과감히" 고친다 — 이상한 코드/불필요한 오버헤드/레이아웃 결함은 의견 묻지 말고 판단 근거를 적고 수정한다.
- 단, 판단 근거(왜 그렇게 바꿨는지)를 이 노트에 남긴다.
- 기존 코드 스타일(탭 들여쓰기, `false == x` 비교 관용구, 주석 밀도)을 따른다.
- 블라스트 레이더스가 큰 변경(드래그드롭 페이로드 구조 변경 등)은 함부로 건드리지 않고 Deferred 에 적는다.

## 목표 (MainDockWindow.cpp:97-99 주석 근거)
- `ImGui::ShowDemoWindow()` 안의 `ShowExampleAppAssetsBrowser()` 를 참고해 에셋 브라우저 드로우를 개선.
- "드래깅(박스/러버밴드 선택)이나 다중 선택(Ctrl/Shift)을 통해 다중 선택이 가능해야 함."

## 조사 결과
- 현재 에셋 브라우저는 단일 선택만 지원 (`m_selectedEntryPath` 1개).
- 동봉된 ImGui 버전 = `1.92.7 WIP` (IMGUI_VERSION_NUM 19266) → 최신 Multi-Select API 전체 사용 가능
  (`BeginMultiSelect`/`EndMultiSelect`, `ImGuiSelectionBasicStorage`, `ImGuiMultiSelectFlags_BoxSelect2d`).
- `ShowExampleAppAssetsBrowser()` 핵심 패턴:
  - `BeginMultiSelect(ClearOnEscape | ClearOnClickVoid | BoxSelect2d | NavWrapX, size, count)`
  - 아이템마다 `SetCursorScreenPos` 로 정확히 배치 → `SetNextItemSelectionUserData(idx)` → 빈 `Selectable(size)` → drawlist 로 아이콘/라벨 오버레이.
  - `ImGuiListClipper` 로 가상 스크롤. `RangeSrcItem` 라인은 clip 제외.
- `Editor` 의 에셋 선택 API 는 **단일**(`SelectAsset(guid, path)`) 만 존재 → 다중 선택 시 Inspector 는 단일(primary)만 표시하는 게 자연스러움.
- `EditorDragDrop::AssetPayload` 는 **단일 에셋** 페이로드. 다중 에셋을 드래그-아웃 하려면 페이로드 구조 + 모든 Accept 측(SceneView/Inspector 등) 변경 필요 → 블라스트 레이더스 큼 → 이번 범위 밖(Deferred).

## 발견한 결함 / 정리 대상 (과감히 수정)
1. `DrawListEntries`: `BeginTable(..., ImVec2(700.0f, 0.0f))` — **하드코딩 700px 폭**. 패널을 못 채우는 레이아웃 버그. → `ImVec2(0,0)` 로 채우기.
2. `DrawBrowserColumns`: "즐겨찾기 폴더"(`favorite_folders`) `CollapsingHeader` 가 **본문이 완전히 빈 데드 UI**. 게다가 `asset_browser.contents_folders` / `asset_browser.favorite_folders` 키가 **로컬라이제이션 yaml 에 존재하지 않음**(키 그대로 노출되는 잠재 버그). → 두 헤더 제거, 폴더 트리를 패널에 직접 그림.
3. `DrawIconEntries`: rename 분기와 일반 분기가 뒤섞여 `continue` + 이중 `BeginAssetDragDropSource` + 수동 `SameLine` 로 매우 난해. → 데모식 명시적 좌표 배치로 재작성하며 Multi-Select 통합.
4. `FindSelectedEntry()` — **선언/정의만 있고 호출처 0** 인 데드코드. → 제거.
5. List 모드 GUID 칸의 "캐시된 ModifiedTimeText 사용" 주석이 엉뚱한 줄(GUID)에 붙어 있음. → 정리.
6. `DrawBrowserColumns` 의 영어 주석 + 공백 들여쓰기(스플리터 블록)가 파일 전체 탭 스타일과 불일치. → 탭/한국어로 정리.

## 설계 결정
- 선택 상태를 `ImGuiSelectionBasicStorage m_selection` 으로 일원화.
  - 안정적 식별자: 엔트리 절대경로 UTF-8 의 `ImHashStr` → `AssetBrowserEntry::SelectionId`.
    경로 기반이라 같은 폴더 내 리프레시(파일 변경 폴링) 후에도 선택이 유지됨. 폴더 이동 시에는 명시적으로 Clear.
  - 어댑터: 표시 인덱스(`m_filteredEntryIndices` 순서) → `SelectionId`.
- Inspector 연동: 선택이 **사용자 상호작용으로 변경된 프레임**(`ms_io->Requests.Size > 0`)에만 Editor 선택을 갱신.
  단일 선택 → `Editor::SelectAsset`, 다중/0 → 에셋 선택 해제. (매 프레임 덮어쓰지 않아 Hierarchy 선택과 충돌 안 함.)
- 아이콘/리스트 두 뷰 모두 Multi-Select 적용. 아이콘=`BoxSelect2d`, 리스트(풀로우)=`BoxSelect1d`.
- 컨텍스트 메뉴: 우클릭 대상이 이미 다중 선택에 포함돼 있으면 선택 유지(다중 삭제), 아니면 그 항목만 단일 선택으로 교체.
  삭제는 다중 지원(`m_deleteTargets`), 이름변경/복제는 단일 대상에만.
- 드래그-아웃은 기존 단일 에셋 페이로드 유지(범위 밖).

## 변경 내역
- (작성 중) 아래 "진행 로그" 에 순차 기록.

## 진행 로그
- 2026-05-31: 조사 완료, 본 노트 작성. 구현 착수.
- 2026-05-31: 구현 완료. 변경 파일 목록:
  - `AssetBrowserTool.h`
    - `imgui.h` 포함, `AssetBrowserEntry::SelectionId`(ImGuiID) 추가.
    - 멤버 `ImGuiSelectionBasicStorage m_selection`, `bool m_selectionChangedThisFrame` 추가.
    - `m_deleteTargetPath`(단일) → `std::vector<File::Path> m_deleteTargets`(다중).
    - `SelectEntry` 의미 변경(단일 선택 교체) + `ApplyMultiSelectRequests`/`SyncEditorSelection`/`CollectSelectedPaths` 선언.
    - 데드코드 `FindSelectedEntry` 선언 제거.
  - `AssetBrowserTool.cpp`
    - `RefreshCurrentFolderEntries`: `SelectionId = ImHashStr(AbsolutePathUtf8)` 채움.
    - `ResetProjectState`/`SetFocusFolderPath`: `m_selection.Clear()`.
    - `DrawEntries`: 프레임 시작 시 변경 플래그 리셋, 그리기 후 변경 시 `SyncEditorSelection`.
    - `DrawListEntries`: 표준 테이블 Multi-Select 패턴(BeginMultiSelect 를 **테이블 내부** 헤더 뒤에서 호출),
      `BoxSelect1d`, 700px 하드코딩 → `ImVec2(0,0)`, 헤더 ScrollFreeze, GUID 칸 오배치 주석 정리.
    - `DrawIconEntries`: 데모식 명시 좌표 배치 + `BoxSelect2d`/`NavWrapX`, 가시성 검사 후 drawlist 오버레이로 전면 재작성.
    - `DrawBrowserColumns`: 빈 "즐겨찾기 폴더" + 미존재 키 헤더 2개 제거, 폴더 트리 직접 렌더, 스플리터 주석/들여쓰기 한국어·탭 정리.
    - 컨텍스트 메뉴: 우클릭 대상이 선택에 포함되면 유지, 아니면 단일 교체. 다중 선택 시 개수 표시 + 이름변경/복제 숨김, 삭제는 다중 지원.
    - `DrawDeleteConfirmPopup`: 대상 목록 BulletText 표시 + 일괄 삭제.
    - `SelectEntry` 재작성, `ApplyMultiSelectRequests`/`SyncEditorSelection`/`CollectSelectedPaths` 정의, `FindSelectedEntry` 정의 제거.
  - `MainDockWindow.cpp`: `OnMenuBar` 의 `ImGui::ShowDemoWindow()` 개발용 호출 제거(구현 완료), 주석 갱신.
  - 로컬라이제이션: `asset_browser.selection_count` 키를 `Application/Localization/{ko-KR,en-US}.yaml`,
    `Localization/{ko-KR,en-US}.yaml` 양쪽 4개 파일에 추가("선택: %d개" / "Selected: %d items").
- 2026-05-31: `Application` 프로젝트 `Debug_Editor|x64` 빌드 성공(에러 0, 기존 yaml-cpp PDB 경고만 존재).

## 검증
- 빌드: MSBuild 솔루션 타깃 `Application` / `Debug_Editor` / `x64` → `Application.exe` 생성, exit 0.
- 런타임 GUI 동작(박스 드래그/Ctrl/Shift 선택, 다중 삭제)은 에디터 실행 환경에서 별도 확인 필요(헤드리스 검증 불가).

## 2026-05-31 후속: 드래그(박스) 다중선택 미작동 버그 수정
- **증상**(사용자 보고): 에셋 브라우저에서 드래그 박스 선택이 전혀 안 됨.
- **근본 원인**: 엔트리 child 윈도우 `BeginChild("AssetBrowserEntries", ..., true)` 에 `NoMove` 플래그가 없었음.
  ImGui `EndMultiSelect()`(imgui_widgets.cpp:8066)의 박스 선택 시작 조건은 `scope_hovered && g.HoveredId == 0 && g.ActiveId == 0 && MouseClickedCount==1`.
  그런데 `NoMove` 가 없으면 **빈 공간 좌클릭이 "윈도우 이동" 용 ActiveId(window MoveId)를 선점** → `g.ActiveId != 0` →
  `BoxSelectPreStartDrag` 가 호출되지 않아 박스 선택이 **절대 시작 안 됨**. (데모 child 가 `ImGuiWindowFlags_NoMove` 를 주는 이유.)
- **수정**: `BeginChild("AssetBrowserEntries", ImVec2(0,0), ImGuiChildFlags_Borders, ImGuiWindowFlags_NoMove)`.
- **참고**: Ctrl/Shift+클릭 선택은 `MultiSelectItemFooter`(8212)에서 `pressed` 시 `IsFocused=true` 가 되어 NoMove 와 무관하게 원래 동작했음. 이번 버그는 박스(드래그) 선택 한정이었다.
- 빌드: `Application` / `Debug_Editor` / `x64` 재빌드 성공(exit 0).
- 작업 규칙 2건 추가(맨 위 규칙 섹션): 고정 문자열 회피(ID 제외) / 로컬라이징은 루트 `Localization/` 에서.

### 실기기 검증 (computer-use 로 에디터 실행, 바탕화면 `Project/Project.Jproject` 로드)
- **드래그 박스 선택: 정상 동작 확인.** 빈 공간에서 드래그하여 NewMaterial.jmat + NewScene.jscene 두 항목만 선택됨(Sound 폴더 제외). → NoMove 수정 유효.
- **Ctrl+클릭 추가 선택: 정상 동작 확인.** NewMaterial 단일 선택 후 NewScene Ctrl+클릭 → 둘 다 선택됨.
- (부수 관찰, 본 작업과 별개) 실행 작업 폴더가 `Build/Debug_Editor` 라 거기 `Localization/` 사본이 구버전 → 일부 신규 키가 `!!key!!` 로 표시됨(예: `asset_browser.compile_fail.*`, 신규 `asset_browser.selection_count`). 빌드 파이프라인에 루트 `Localization/` → 실행 폴더 복사 단계가 없음. 정본(루트 `Localization/`)은 갱신 완료. 실행 폴더 동기화는 사용자 판단에 맡김(빌드 산출물이라 임의 수정 보류).

## 2026-05-31 후속 2: CollapsingHeader 복구 + 드래그 이동 / 잘라내기·복사·붙여넣기
사용자 요청. (참고: 앞서 "데드 UI"라며 제거한 CollapsingHeader 는 사용자가 의도적으로 둔 것이었음 → 복구.)

### A. 좌측 패널 CollapsingHeader 복구
- `DrawBrowserColumns`: "콘텐츠"(폴더 트리) + "즐겨찾기"(추후 구현) `CollapsingHeader` 두 개 복구.
- 로컬라이징 키 신규: `asset_browser.contents_folders`(콘텐츠/Contents), `asset_browser.favorite_folders`(즐겨찾기/Favorites).
  (이전에 키가 없어 깨졌던 문제도 해소.)

### B. 드래그&드롭 폴더 이동
- 드래그 소스: `BeginAssetDragDropSource` 가 활성화되면 `m_dragPrimaryPath = entry.AbsolutePath` 기록.
- 드롭 타겟 3곳: 아이콘 뷰 폴더 셀 / 리스트 뷰 폴더 행 / 폴더 트리 노드 → `DropAssetsIntoFolder(folder)`.
- 이동 대상 = 드래그된 항목이 다중 선택에 포함되면 **선택 전체**, 아니면 그 항목만(`CollectOperationTargets`).
- 실제 이동: `EPendingOperationType::MoveInto` → ProcessPendingOperations.
  - Asset root 내 이동: `CRenamePathCommand`(메타 동반 + **언두 가능**).
  - Script root 내 이동: `std::filesystem::rename`(언두 없음).

### C. 잘라내기 / 복사 / 붙여넣기 API + 우클릭 메뉴
- 멤버: `m_clipboardPaths`, `m_clipboardIsCut`.
- `CutToClipboard` / `CopyToClipboard` / `PasteIntoFolder`.
- 우클릭 메뉴: 엔트리 메뉴에 잘라내기/복사/붙여넣기 추가(폴더 우클릭 시 그 폴더로, 파일이면 현재 폴더로 붙여넣기). 빈공간/폴더 트리 메뉴에 붙여넣기 추가. 클립보드 비면 비활성.
- 붙여넣기: cut → MoveInto(후 클립보드 비움), copy → CopyInto.
  - `CopyInto`: 파일은 사본(고유 이름, 메타 미복사 → 새 GUID 재임포트). 폴더는 재귀 복사 후 내부 `.Jmeta` 제거(GUID 충돌 방지).

### 검증 규칙 — `CanPlaceInto(source, targetFolder, isMove)`
- 공통: 같은 루트(Asset↔Asset / Script↔Script) + 폴더를 자기 자신/하위로 배치 금지.
- 이동(isMove=true): "이미 그 폴더에 있음"도 금지.
- 복사(isMove=false): 같은 폴더 사본 허용(복사-붙여넣기로 Duplicate 가능).
- (초기 구현에서 CopyInto 가 이동용 규칙을 써서 같은 폴더 복사가 막히던 버그를 리팩터로 수정.)

### 로컬라이징
- 신규 키: `common.cut`(잘라내기/Cut), `common.paste`(붙여넣기/Paste), `asset_browser.contents_folders`, `asset_browser.favorite_folders`.
- 정본 루트 `Localization/` 에 작성 후, 런타임 사본(`Application/Localization/`, `Build/Debug_Editor/Localization/`)을 루트에서 복사 동기화(현재 3곳 byte-identical).

### 빌드 / 검증
- `Application` / `Debug_Editor` / `x64` 빌드 성공(exit 0).
- 이번엔 computer-use(데스크톱 제어) MCP 서버가 끊겨 **라이브 GUI 검증은 미실시**. 코드 리뷰 + 빌드까지 확인. 사용자 실기 확인 필요.

### 알려진 제약(Deferred 후보)
- 이동 시 대상 폴더에 동일 이름이 이미 있으면 그 항목 이동은 실패(경고 로그). 충돌 시 자동 고유화는 미구현.
- Script 파일 이동 / 복사(파일·폴더)는 커맨드가 아니라 직접 파일 연산이라 **언두 불가**(Asset root 이동만 언두 가능).
- 루트를 넘나드는 이동/복사(Asset↔Script)는 막아둠.

## Deferred (이번 범위 밖)
- 다중 에셋 **드래그-아웃**(여러 에셋을 한 번에 씬/인스펙터로 드롭): 페이로드 구조 + 모든 수용측 변경 필요.
- Ctrl+휠 줌(아이콘 크기 동적 조절): 데모엔 있으나 본 엔진 UX 정책 정해진 뒤 도입.
