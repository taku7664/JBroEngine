# 에디터 가이드 포커스 계획

> 결정은 [todo.md](./todo.md) 의 D-251, 계약은 [ProjectRule.md](../docs/ProjectRule.md) §11 이다. **1 단계(모델과 입력 문)가 섰다.** 막·표식·열기·가이드는 아직 없다.
> 계기(2026-09-28): 사용자 문의 "반투명 렉트에 해당 위젯 부분만 마스킹해서 포커스를 두고, 그 위젯만 상호작용 가능하게"
> 이어서 "애니메이션", "닫혀 있던 메뉴·트리도 열리게", "부모부터 차근차근 열리게".
> 이름은 사용자가 정했다: "튜토리얼을 직접적으로 언급하기보단 GuideFocus 정도가 나은 듯". 그래서 기구도 내용도
> `Tutorial` 이라는 말을 쓰지 않는다.

## 1. 기존 엔진과 지금의 에디터

### 1.1 기존 엔진 - 없다

`C:\Users\박주형\source\repos\JBroEngine` 에 튜토리얼·온보딩·스포트라이트가 없다. `tutorial`·`onboard`·`spotlight`·`walkthrough`·
`coach`·`tour`·`guide`·`highlight`·`hint`·`overlay`·`dim`·`welcome` 을 소스·YAML·문서·git 로그 전체에서 찾았고 걸린 것은
전부 다른 뜻(조명 오브젝트 이름, ImGui 내부의 내비 강조, 입력 칸의 힌트 글자)이었다. 가장 가까운 것은 단축키 참조 창이다.
기존 위젯 래퍼(`Application/Editor/ImItem/`)는 자유 함수이고 **위젯을 이름으로 찾거나 그 화면 자리를 묻는 길이 없었다** -
창 단위 ID 만 있었다. 그래서 새 계약을 정한다.

### 1.2 지금의 에디터에서 쓸 수 있는 것과 없는 것

- **모든 위젯이 거치는 우리 쪽 자리가 없다.** 래퍼마다 ImGui 를 직접 부르고, 공통으로 거치는 곳은 ImGui 의 `ItemAdd` 뿐이다
  (`ThirdParty/imgui/imgui.cpp` 의 테스트 엔진 훅은 `imconfig.h` 에서 꺼져 있다).
- **위젯 ID 는 대상 지정에 쓸 수 없다.** 메뉴 ID 는 번역된 글자라 언어를 바꾸면 바뀌고(`EditorApplication.cpp` `DrawRootMenuBar`),
  계층의 오브젝트 줄은 **포인터 주소**로 `PushID` 한다(`HierarchyPanel.cpp` 약 817줄).
- **입력은 한 자리로 들어간다.** 공식 백엔드 없이 `EditorUI::PushInput`(`EditorUI.cpp` 347~411줄)이 플랫폼 이벤트를
  `AddMousePosEvent`·`AddMouseButtonEvent`·`AddKeyEvent` 로 넣는다. 부르는 곳은 `BuildEditorUi` 의 첫머리이고,
  게임에 넘기는 `SubmitHostInput` 도 같은 이벤트를 받는다.
- **에디터 단축키는 ImGui 의 hover 를 거치지 않는다.** `EditorShortcutManager::ProcessInput` 은 키 상태를 직접 읽으므로 위에
  무엇을 덮어도 막히지 않는다. 대신 `SetSuspended` 가 있다(키를 새로 잡는 동안 쓰는 것, D-230).
- **조상을 펼치는 장치가 이미 하나 있다.** 계층 패널의 `m_reveal`·`IsOnRevealPath` 가 조상 줄에 `SetNextItemOpen(true)` 를 주고
  도착하면 `SetScrollHereY(0.5f)` 한 뒤 비운다. 이것은 **한 프레임에 전부** 연다.
- 패널은 `SetOpen`·`RequestFocus` 로 열고 앞으로 가져올 수 있다(`EditorPanel.h`). 탭은 `Widget::BeginTab(label, open, select)` 의
  `select` 로, 접는 헤더·마디·트리는 `SetNextItemOpen` 으로 밖에서 연다. **메뉴를 코드로 여는 API 는 없다.**
- 멀티 뷰포트는 꺼져 있다(`ImGuiConfigFlags_ViewportsEnable` 을 켜는 곳이 없다). 화면은 메인 뷰포트 하나다. ImGui 1.92.9b docking.
- foreground draw list 를 쓰는 코드는 없다. "모든 것 위" 의 선례는 알림이다 - 팝업 뒤에 그리고 매 프레임
  `BringWindowToDisplayFront` 한다(`Notification.cpp`).
- 애니메이션 유틸은 알림 안의 지역 함수 `EaseOut`·`Approach` 뿐이다(`EditorNotifications.cpp`).
- 모델과 그리기를 나누는 선례: `EditorNotifications`(시간과 상태, ImGui 모름) ↔ `Widget::NotificationStack`(그리기).

## 2. 설계

### 2.1 나누는 법과 이름

기구(막·구멍·입력 문·부모부터 열기)와 내용(단계의 순서·문구·다음으로 가는 조건)을 나눈다. 기구는 가이드 말고도 쓴다 -
검증 오류가 난 필드로 데려가기, `RevealAssetInBrowser` 가 찾은 에셋을 짚어 주기.

| 역할 | 이름 | 비고 |
|---|---|---|
| 기구의 모델(대상 경로·열기 진행·입력 문·애니메이션 상태) | `EditorGuideFocus` | ImGui 를 모른다. `EditorApplication` 이 값으로 든다 |
| 막과 말풍선 그리기 | `Widget::GuideFocus` | `Widget::NotificationStack` 과 같은 결 |
| 대상 하나의 이름 | `GuideFocusTarget` | `{ NameId name; std::uint64_t key; }` 값 |
| 대상에 이르는 경로 | `GuideFocusPath` | 부모부터 적은 `GuideFocusTarget` 의 고정 용량 배열 |
| 다음 위젯에 표식 달기 | `Widget::SetNextItemTarget` | ImGui 의 `SetNextItemOpen` 과 같은 결 |
| 가이드 한 편 | `Guide` | 단계 목록 |
| 한 단계 | `GuideStep` | 경로·문구 키·여는 주체·끝나는 조건 |
| 가이드 진행기 | `EditorGuide` | 단계를 넘기며 `EditorGuideFocus` 를 몬다 |

- 사용자는 "GuideFocus" 를 주었다. 나머지는 그 말에 맞춰 지은 것이다. 기구가 `GuideFocus` 이고, 기구를 모는 내용이 `Guide` 다.

- **`Anchor` 는 쓰지 않는다.** `Transform2D.anchor`(D-237)와 겹친다.
- **`Manager`·`System`·`Service` 는 쓰지 않는다.** §10.3 이 `Manager` 를 막고, `System`·`Service` 는 엔진·스크립트 레이어의 말이다.
  에디터 쪽 관례는 `Editor<명사>`(`EditorNotifications`·`EditorObjectRegistry`)다.
  (`EditorShortcutManager`·`EditorCommandManager` 는 §10.3 보다 먼저 있던 이름으로 보인다 - 이 계획에서 건드리지 않는다.)

### 2.2 대상은 위젯 ID 가 아니라 표식으로 가리킨다

- 가리킬 수 있는 것은 **표식을 단 위젯뿐이다.** 그리는 쪽이 위젯 바로 앞에서 `Widget::SetNextItemTarget(target)` 을 부르고,
  래퍼는 ImGui 를 부르기 전에 표식을 꺼내(열어야 하는 마디인지 묻는 데 쓴다) 부른 뒤 `GetItemRectMin/Max` 를 기록한다.
  표식을 받는 래퍼: `Button`·`TextButton`·`ActionButton`·`MenuItem`·`BeginMenu`·`FoldNode`·`CollapsingSection`·`BeginTab`·
  `Tree`/`TreeBegin`·`TextField`·`FilterCombo`·`Checkbox` 와 필드 줄(`FieldLabel`). 필요한 만큼 늘린다.
- 이름은 번역과 무관한 안정된 이름이다(`NameId = MakeStableTypeId("menu.file")`). 같은 이름이 여러 줄에 나오는 것은 `key` 로 가른다.
  - 패널: `("panel", 제목의 NameId)` - `GetTitle()` 은 번역하지 않는 이름이다.
  - 메뉴: `("menu.file")`, `("menu.file.build_game")` - 로컬라이징 키에서 따온 이름.
  - 계층의 오브젝트 줄: `("hierarchy.object", EditorObjectId)` - 주소가 아니라 저장되지 않는 안정 번호(D-72 와 같은 까닭).
  - 인스펙터의 컴포넌트 머리: `("inspector.component", 컴포넌트 타입 ID)`, 필드 줄: `("inspector.field", 타입 ID 와 필드 이름의 해시)`.
- 기록하는 표는 **고정 용량**이고 프레임마다 스탬프로 거둔다(D-249 의 `RemoveStaleEntries`). 이름은 정수라 매 프레임
  글자를 짓거나 견주지 않는다(§9). 표는 모든 표식이 아니라 **지금 경로에 든 것만** 적는다 - 가이드 포커스가 꺼져 있으면
  표식 한 번이 정수 비교 한 번이다.
- 위젯 계층은 에디터를 모른다. `EditorApplication` 이 프레임 첫머리에 `Widget::SetGuideFocus(&m_guideFocus)` 로 지금의 모델을 건다
  (ImGui 의 현재 컨텍스트와 같은 모양). `Widget/Notification.h` 가 `EditorNotifications.h` 를 include 하는 것과 같은 방향이다.

### 2.3 부모부터 차례로 연다

경로는 부모부터 적는다. 예: `panel:Inspector / inspector.component:Transform2D / inspector.field:position`,
`menu.file / menu.file.build_game`. 경로의 칸마다 아래를 돈다.

```
이동: 구멍이 그 칸의 사각형으로 옮겨 간다(애니메이션)
머묾: 잠시 머문다 (기본 0.35 초)
열기: 자동이면 기구가 열고, 사용자 몫이면 사용자가 누를 때까지 기다린다
다음: 열렸으면 다음 칸으로. 이미 열려 있던 칸은 머물지 않고 지나간다
```

| 칸의 종류 | 자동으로 여는 법 | 사용자가 열면 |
|---|---|---|
| 패널 | `SetOpen(true)` + `RequestFocus()` | 창 메뉴에서 연다 |
| 탭 | `BeginTab` 의 `select` | 탭을 누른다 |
| 접는 헤더·마디·트리 | 래퍼가 `SetNextItemOpen(true)` | 화살표를 누른다 |
| 계층의 오브젝트 조상 | 오브젝트의 부모 사슬로 경로를 **데이터에서** 만든다. 칸마다 `SetNextItemOpen` | 화살표를 누른다 |
| 스크롤 밖에 있는 것 | 그려졌는데 창의 클립 밖이면 `SetScrollHereY(0.5f)` | - |
| 메뉴·우클릭 메뉴·콤보 | **1 단계에서는 없다.** `[열림]` | 누른다. 열린 팝업은 입력 문이 연다(§2.4) |

- 메뉴를 코드로 여는 것은 `OpenPopupEx` 같은 내부 API 에 기대고, 포커스를 잃으면 닫혀 붙잡아 두어야 한다. **사용자가 여는
  단계만 두면 이 어려움이 없다** - 메뉴는 ImGui 가 평소처럼 열고, 우리는 열린 팝업을 허용 영역에 더하기만 한다.
- 인스펙터의 컴포넌트를 가리키려면 그 오브젝트가 선택되어 있어야 한다. 선택은 편집이 아니므로 커맨드 없이 한다(`SetSelectedObject`).
- **경로가 끊기면 로그를 남기고 그 단계를 건너뛴다.** 칸이 0.5 초 동안 한 번도 그려지지 않으면 끊긴 것이다
  (오브젝트가 지워졌다, 그 컴포넌트가 없다). 무효 접근을 로그로 남기고 무시하는 `GameObjectHandle` 과 같은 결이다.
- 계층 패널의 `m_reveal` 은 그대로 둔다(끌어 놓은 뒤 한 번에 펼치는 것은 그 자리에 맞다). 조상 사슬을 걷는 코드만 함께 쓴다.

### 2.4 입력 문 - ImGui 에 넣기 전에 거른다

`BuildEditorUi` 가 `PushInput`·`SubmitHostInput` 에 넘기기 전에 이벤트를 거른다. 거른 이벤트는 멤버 배열에 담아 다시 쓰므로
프레임마다 잡지 않는다. 걸러 내는 규칙은 ImGui 를 모르는 순수 함수라 따로 시험한다.
**꺼져 있어도 매 프레임 거친다** - 그대로 넘기면서 눌린 버튼과 마우스 자리를 센다. 켜기 전에 누른 버튼은 ImGui 가 이미
누름을 보았으므로 켠 뒤에도 그 뗌을 넘겨야 하는데, 켤 때에야 세기 시작하면 그 버튼을 모른다(1 단계에서 고쳤다).

- **허용 영역** = 구멍 사각형(여백 포함) ∪ 이 단계가 시작된 뒤 열린 팝업 창들 ∪ 말풍선 창. 사각형은 지난 프레임에 기록한 것이다.
  구멍이 옮겨 가는 동안에는 **도착할 자리**로 판정한다 - 움직이는 구멍으로 판정하면 옮겨 가는 도중에 엉뚱한 것이 눌린다.
- 마우스 위치: 허용 영역 밖이면 화면 밖 좌표로 바꿔 넣는다. 밖에 있는 위젯이 올림 색을 띠면 누를 수 있는 것처럼 보인다.
  **허용 영역 안에서 눌러 시작한 끌기가 이어지는 동안은** 실제 좌표를 넣는다(슬라이더를 끌다 밖으로 나가는 경우).
- 누름: 허용 영역 밖이면 버린다. 뗌: 짝이 되는 누름을 넘겼으면 **늘 넘긴다** - 버리면 ImGui 에 버튼이 눌린 채로 남는다.
- 휠: 허용 영역 밖이면 버린다. 스크롤은 열기가 대신한다.
- 키와 글자: 버린다. 단계가 키보드를 쓰겠다고 하면(글자 칸에 이름을 치는 단계) 넘긴다. Esc 는 기구가 먼저 받는다(건너뛰기).
- 단축키: 가이드 포커스가 켜진 동안 `EditorApplication` 이 `ProcessInput` 을 부르지 않는다. 처음에는 `SetSuspended` 를 쓰려 했으나
  키를 새로 잡는 설정 창(`EditorSettingsPanel`)이 그것을 켜고 끄므로, 둘이 함께 쓰면 한쪽이 켠 멈춤을 다른 쪽이 푼다.
  키를 허용한 단계에서도 단축키는 돌지 않는다.
- 게임 입력: 게임 뷰가 대상이 아니면 넘기지 않는다.
- 창 포커스 이벤트는 늘 넘긴다(§ `PushInput` 의 "눌려 있던 키가 남지 않게").

### 2.5 그리기와 애니메이션

- 막은 foreground draw list 가 아니라 **입력을 받지 않는 창**(`NoInputs`·`NoNav`·`NoDocking`·`NoFocusOnAppearing`)에 그리고,
  말풍선 창은 그 뒤에 그려 둘 다 `BringWindowToDisplayFront` 한다. foreground draw list 는 모든 창 위에 그려져서 **말풍선까지
  덮는다** - 말풍선에는 누를 단추(다음·건너뛰기)가 있어야 한다.
- 막은 허용 영역의 사각형들을 뺀 나머지를 사각형 몇 개로 나눠 칠한다(사각형이 몇 개 되지 않으므로 격자 나누기로 충분하다).
  막 창이 대상이 연 팝업보다 위에 서더라도 그 자리가 구멍이라 가려지지 않는다.
- 막 색은 작업 공간 색(`#11151A`)에 알파 0.6 을 기본으로 둔다. 구멍 테두리는 **파랑**(`#3B82F6`)이다 - "여기를 누른다" 이므로
  상호작용 색이다(§11.3.1, D-245). 색은 `EditorTheme` 에 이름으로 더한다.
- 움직임:
  - 막은 0.18 초에 걸쳐 나타나고 사라진다(알림의 `FadeSeconds` 와 같다).
  - 구멍은 다음 칸으로 `Approach`(지수 접근)로 옮겨 가며 크기도 함께 바뀐다. 대상이 스크롤·창 크기 변경으로 움직여도 같은
    식으로 따라가므로 튀지 않는다.
  - 머무는 동안 구멍 테두리가 숨 쉬듯 두께·알파가 오르내린다.
  - 말풍선은 대상 옆에서 남는 자리가 가장 넓은 쪽에 서고, 그쪽에서 미끄러져 들어온다.
- `EaseOut`·`Approach` 는 알림 안의 지역 함수다. 둘이 쓰게 되면 `Widget/Common.h` 로 올린다.
- 시간은 `BuildEditorUi(deltaTime)` 의 델타다.

### 2.6 가이드

- 단계 = `{ 경로, 제목 키, 본문 키, 칸마다 여는 주체(자동·사용자), 끝나는 조건, 키보드 허용 }`.
- 끝나는 조건: 경로의 마지막 칸을 눌렀다(래퍼가 기록한 `IsItemClicked`/`IsItemDeactivatedAfterEdit`), 말풍선의 다음 단추,
  또는 조건 함수(`Delegate<bool(EditorApplication&)>`, D-246 - "Transform2D 가 붙었다" 처럼).
- 문구는 로컬라이징 키다(§11.2). 컴포넌트 이름은 타입 이름 그대로 보인다.
- 1 단계에서 가이드는 C++ 로 등록한다. 파일 형식(YAML)은 `[열림]` 이다 - 가이드가 몇 편 모여 모양이 보인 뒤에 정한다.
- 여는 자리: 가이드가 실제로 하나 생긴 뒤에 메뉴 항목을 단다(§11 "눌러도 아무 일도 없는 메뉴 항목을 두지 않는다").
- 가이드 중 편집은 평소처럼 커맨드를 거친다. 가이드가 편집을 대신하지 않는다.

### 2.7 정한 것 (2026-09-28, 사용자 확인 "2번은 권고대로")

- **가이드가 연 패널·마디는 끝난 뒤 닫지 않는다.** 방금 배운 자리가 사라지면 어디였는지 다시 찾게 된다. 그래서 열기 전 상태를
  적어 둘 필요도 없다.
- **가이드 중에 모달(확인 창·게임 빌드 결과)이 뜨면, 모달이 대상 경로에 없는 한 막을 걷고 입력 문을 연 채 기다린다.** 모달이 닫히면
  그 단계를 잇는다. 모달은 사용자가 답해야 하는 것이라 막으면 에디터가 멈춘 것처럼 보인다.

## 3. 단계

1. **[섰다] 모델과 입력 문.** `EditorGuideFocus`(경로·허용 영역·입력 문, `Modules/JBroEditor/.../EditorGuideFocus.h`)와
   `EditorApplication` 의 배선(`BuildEditorUi` 첫머리에서 거른 것을 `PushInput`·`SubmitHostInput` 에 넘기고, 켜진 동안
   `ProcessInput` 을 건너뛰고, Esc 면 끄고, `CloseProject` 가 끈다). 시험은 `Tests/EditorGuideFocusTests.cpp` 열세 개 -
   열둘은 ImGui 없이 거르기를 재고, 하나는 실제 에디터(숨긴 창)에 `PostMessageW` 로 눌러 ImGui 의 `MouseDown`·`MousePos`·
   `IsKeyDown` 과 시험용 단축키의 호출 수를 본다.
   **실측(2026-09-28):** 변이 18 개를 넣어 17 개가 잡혔다(각 4~11 초, 실패 메시지로 확인). 살아남은 하나는 "뒤집힌 사각형을 건너뛴다"
   는 검사를 지운 것이었는데, `Rect::Contains` 가 뒤집힌 사각형에서 이미 거짓이라 그 검사가 죽은 코드였다 - 지웠다.
   막·표식이 없으므로 이 단계에서 허용 영역은 부르는 쪽이 `AddAllowedRect` 로 직접 넣는다.
   **실제 에디터(2026-09-28):** 거르기가 꺼진 채로도 매 프레임 입력을 거치므로 평소 조작이 그대로인지 `JBroEditorHost.exe` 를 띄워
   `PostMessageW` 로 몰고 `PrintWindow` 로 찍어 보았다. 메뉴 열기(편집), 계층 빈 곳 우클릭 메뉴, 검색 칸 타자, 우클릭 메뉴의
   오브젝트 추가(인스펙터에 `Transform2D` 까지)가 모두 됐다. 첫 타자가 안 들어간 것은 큰 이동 뒤 첫 클릭이 빗나가는 몰이 쪽 사정이었고,
   한 번 더 누르자 들어갔다. 가이드 포커스를 켜는 길은 아직 화면에 없어 켠 상태는 숨긴 창 시험으로만 보았다.
2. **표식.** `Widget::SetNextItemTarget` 과 래퍼들의 기록. 패널·메뉴·계층·인스펙터에 표식을 단다. 시험: 표식 단 위젯의 사각형이
   `GetItemRect` 와 같다, 경로에 없는 표식은 적지 않는다.
3. **그리기와 애니메이션.** 막 창·구멍·테두리·말풍선. 실제 에디터를 띄워 본다(§11.4).
4. **부모부터 열기.** 패널·탭·헤더·트리·스크롤·계층 조상. 메뉴와 팝업은 사용자가 여는 단계. 시험: 닫힌 인스펙터 필드까지 칸마다
   한 단계씩 열린다, 끊긴 경로는 건너뛰고 로그를 남긴다, 밖을 누르면 커맨드 판번호가 그대로다.
5. **가이드.** `Guide`·`GuideStep`·`EditorGuide`, 첫 가이드 한 편(예: 오브젝트 만들기 → `Transform2D` 필드 바꾸기 →
   컴포넌트 추가)과 그것을 여는 메뉴 항목.

## 4. `[열림]`

- 메뉴·콤보를 기구가 자동으로 여는 것(`OpenPopupEx`). 사용자가 여는 단계로 모자라면 그때 본다.
- 위젯 계층을 거치지 않는 호출(`AssetBrowserPanel` 의 `BeginChild`, `EditorApplication` 의 `BeginPopupModal` 등)의 안쪽은 가리킬 수 없다.
  가리켜야 하는 날 그 자리를 위젯 계층으로 옮긴다.
- 가이드 파일 형식과 진행 기록(어디까지 봤는지)을 어디에 둘지 - 사용자별 에디터 설정 파일이 후보다.
