# 에디터 리플렉션 계획 (패널 종류 표와 행동 표)

todo "에디터 공용 기반" 5 번(외부 에디터 등록 API)과 8 번의 남은 것(우클릭 메뉴 확장)을 묶은 계획이다. 결정은 Decisions 의 D-284.

## 1. 왜 하는가 (2026-10-06 확인)

- **창 단위 등록이 문자열 꼬리표로만 남았다.** 사용자는 2026-09-26 에 "에디터(패널·툴·외부 에디터)마다 제 Action 을 등록한다"고 했지만
  D-228 은 등록의 주인을 에디터 전체의 `EditorShortcutManager` 로 잡고 창은 `scope`(패널 제목 글자)로만 남겼다. 패널은 `OnCreate` 에서
  직접 등록하고 핸들을 들고 있다가 `OnDestroy` 에서 직접 푼다(`CanvasViewPanel.cpp`). 생성 때 등록하므로 **열린 적 없는 패널의 단축키는
  단축키 목록과 키매핑 설정에 나오지 않는다.**
- **같은 행동이 네 곳에 따로 적혀 있다.** 우클릭 메뉴(`EditorActions.cpp` 가 손으로 그림), 편집 메뉴·단축키(`EditorShortcuts`, 이름 `editor.copy`),
  가이드(`EditorGuideActions.cpp` 의 행동 표, 이름 `object.copy`, 어느 메뉴에 있는지를 `ObjectMenu | EditMenu` 로 손으로 적음),
  컴포넌트 메뉴(`ComponentMenuTable`). 메뉴에 항목을 더하면 가이드 표를 손으로 맞춰야 한다.
- **스프라이트 뷰어가 패널이 아니다.** `EditorPanel::GetPreferredDock` 이 메인 도크 안의 칸만 고를 수 있어, 뿌리에 붙어야 하는 뷰어를
  `EditorApplication` 이 직접 만들고 그린다(D-155).
- 기존 엔진(`source\repos\JBroEngine`)에는 이런 장치가 없었다: 창은 `CImEditor::CreateImWindow` 의 키 표, 어떤 창이 있는지는
  `MainDockWindow.cpp` 에 손으로 박은 목록, 단축키는 고정 열거 아홉, 오브젝트 우클릭 메뉴는 `LayerTool.cpp` 에 고정. 뷰어·이펙트 편집기는
  뿌리에 붙는 공용 도크 창(`CSpriteViewerDockWindow`) 안에 파일마다 자식 창(`CSpriteViewerPanel`, 키는 에셋 GUID)을 만들었다.
  에디터 창을 리플렉션에 올리는 코드는 없었다(`CReflectionRegistry` 는 컴포넌트용).

## 2. 설계

### 2.1 엔진 리플렉션과 나눈다

엔진 리플렉션(`JBroCore/Reflection`, `JBRO_FIELD`·`PropertyRegistry`)과 컴포넌트 종류 표(`ComponentRegistry`)는 그대로 둔다.
에디터 리플렉션은 `JBroEditor` 모듈 안에 두고 엔진 쪽을 참조만 한다.
- Core 는 게임 빌드에도 들어간다. 합치면 Core 가 패널·메뉴·단축키를 알게 되고(역방향 의존) 게임 익스포트에 에디터 정보가 실린다.
- 엔진 쪽은 컴파일 때 정해진 필드 표이고, 에디터 쪽은 `EditorApplication` 을 받는 함수를 든 실행 중의 표다. 공통으로 뺄 것은 "이름 → 항목 표" 정도라 묶지 않는다.

### 2.2 패널 종류 표 (`EditorPanelRegistry`)

`ComponentRegistry` 와 같은 모양이다: 전역 `EditorPanelRegistry::Get()`, 시작할 때 `RegisterEditorPanelType<T>()` 로 모든 종류를 올린다.
종류 하나는 이름(저장 키, 번역하지 않음)·고유/비고유·소속 도크·UI 를 켤 때 만들지·만드는 함수다. 표에 오른 차례가 처음 배치의 차례다.

**패널은 둘로 나뉜다.** `EditorPanel` 이 부모이고 `UniquePanel` · `InstancePanel` 이 상속한다. 모든 패널은 UUID 를 가진다.

| | 고유 패널 | 비고유 패널 |
|---|---|---|
| UUID | 종류 이름에서 정해지는 값(`Uuid::FromName`) | 만들 때마다 새로(`Uuid::Generate`) |
| 만들기 | 이미 있으면 그 패널을 앞으로 가져오고 끝 | 언제나 새로 만든다 |
| 닫기(X) | 숨긴다. 다시 열면 상태 그대로 | 파기한다(그리는 도중이 아니라 프레임 끝에) |
| `창` 메뉴 | 켜기/끄기 항목 | 항목 없음 |
| ImGui 창 이름 | `보이는이름###종류` | `보이는이름###종류/UUID` |
| 도킹 자리 | 저장된다 | 저장하지 않는다. 열면 소속 도크의 기본 자리 |
| 다음 실행 | 시작할 때 만든다 | 다시 열지 않는다(사용자 결정) |

**찾기는 둘이 같은 API 다.** `FindPanel(Uuid)` 는 하나, `FindPanels(종류)` 는 목록(고유면 0 이나 1 개), `FindPanel(종류)` 는 그 목록의 맨 앞.

**소속 도크는 반드시 있다.** 뿌리 도크에는 도크만 붙고 패널은 붙지 못한다. 도크는 지금 메인 도크(`Main`)이고 4 단계에서 스프라이트 뷰어 도크가 선다.
도크도 표에 이름으로 올린다. 모르는 도크를 말하는 패널은 받지 않는다.

### 2.3 행동 표 (`EditorActionRegistry`, 2 단계)

행동 하나는 이름(`object.copy`, 저장 키)·보이는 글자 키·무리·기본 단축키·범위(전역 또는 패널 종류)·나오는 메뉴(편집·오브젝트·빈자리·컴포넌트)·
할 수 있는지·까닭·하기다. 메뉴, 단축키 관리자, 단축키 목록·키매핑 설정, 가이드, 나중의 명령 팔레트(9 번)가 이 표를 읽는다.
**등록은 에디터가 켜질 때 종류 단위로 한 번이다** - 패널이 열려 있든 아니든 모든 행동이 표에 있다. 패널 범위 행동의 할 일은 그 종류에서
포커스를 가진 패널이 한다. 패널 객체가 등록 핸들을 들고 직접 푸는 코드는 없앤다.

### 2.4 가이드 (3 단계)

가이드의 행동 표가 "어느 메뉴에 있는지"를 행동 표에서 받는다. 이름을 하나로 맞춘다(`editor.copy` → `object.copy` 등).
사용자 키매핑 파일에 옛 이름이 적혀 있으면 읽을 때 새 이름으로 옮긴다. 컴포넌트·필드가 필요한 단계는 지금처럼 엔진 리플렉션을 쓴다.

### 2.5 스프라이트 뷰어 (4 단계)

뿌리에 "스프라이트 뷰어" 도크가 서고, 그림마다 비고유 패널(`SpriteViewerPanel`)이 그 안에 열린다. 같은 그림을 다시 열면 그 패널을 앞으로
가져오는 것은 뷰어가 판단한다(`FindPanels("SpriteViewer")` 에서 그 그림을 보는 패널). `EditorApplication` 의 뷰어 전용 코드를 뺀다.

## 3. 단계

각 단계는 빌드·테스트·뮤테이션 뒤 커밋한다.

1. ~~패널 종류 표, 고유/비고유, UUID·종류로 찾기, 소속 도크. 완료 조건: 기존 패널 11 개가 표에서 서고 동작이 같다, 고유 패널을 다시 만들면
   같은 패널이 앞으로 온다, 비고유 패널은 매번 새로 서고 닫으면 파기된다, 모르는 도크의 패널은 거절된다.~~ → 완료 2026-10-06(D-284 의 "1 단계").
   `EditorPanelRegistry.h/.cpp`, `EditorPanel.h`(`UniquePanel`·`InstancePanel`), `EditorApplication::CreatePanel`·`ClosePanel`·`FindPanel(s)`,
   시험 `TestUniqueAndInstancePanels`. 뮤테이션 6/6.
2. ~~행동 표와 단축키·편집 메뉴·우클릭 메뉴(오브젝트·빈자리·컴포넌트). 완료 조건: 패널을 하나도 열지 않은 에디터의 단축키 목록에 패널 범위
   단축키가 다 나온다, 외부 등록 항목이 오브젝트·빈자리 메뉴에 선다, 캔버스 뷰가 등록 핸들을 들지 않는다.~~ → 완료 2026-10-06(D-284 의 "2 단계").
   `EditorActionRegistry.h/.cpp`, `CanvasViewPanel::RegisterActions`, `EditorShortcutManager::RenamedId`, 시험 `TestEditorActionsAreRegisteredFromTheStart`·
   `TestRegisteredActionsAppearInContextMenus`. 이름을 이 단계에서 하나로 맞췄다(3 단계에서 하려던 것). 뮤테이션 7/7.
3. ~~가이드가 행동 표를 읽는다. 완료 조건: 가이드 표에서 메뉴 위치를 손으로 적은 줄이 없다(옛 키매핑 이름 옮기기는 2 단계에서 섰다).~~
   → 완료 2026-10-06(D-284 의 "3 단계"). `EditorGuideActions.cpp` 의 `MenusOf`, 행동 `game.build`. 컴포넌트 추가 하나만 손으로 적는다(하위 메뉴라 행동이 아니다).
4. 스프라이트 뷰어 도크와 비고유 패널. 완료 조건: 기존 뷰어 시험이 그대로 통과한다, `EditorApplication` 에 뷰어 멤버가 없다.

## 4. `[열림]`

- 외부 에디터를 DLL 로 받는 것은 하지 않는다(정적 링크). 등록·해제 짝은 두므로 그때 API 는 그대로 쓴다.
- 비고유 패널을 다음 실행에 되살리지 않는다.
