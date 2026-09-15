# 9. 에디터

에디터는 **사람이 하루 종일 들여다보는 화면**이고, 그 사람이 한 일을 되돌릴 수 있어야 하는 도구다.
기능이 도는 것과 쓸 만한 것은 다르고, 그 차이를 좁히려고 기존 엔진이 깎아 둔 것을 그대로 이어받는다. 비슷하게 새로 만들지 않는다.
규칙의 원문은 `ProjectRule.md` §11 이다.

## 구조

실행 파일은 `JBroEditorHost` 이고 게임 실행(`JBroGameHost`)과 나란히 있다(D-65). 인자로 프레임 수를 주면 그만큼 돌고 끝난다. 사람 없이 띄워 화면을 찍기 위한 손잡이다.

```
EditorApplication::Tick
  ├ BuildEditorUi      입력 펌프 → ImGui 에 입력 전달 → BeginFrame → 메뉴바 → dockspace → 패널들 → EndFrame
  │                    (텍스처·정점 버퍼 업로드는 RHI 프레임 밖이어야 한다)
  └ engine->Tick
      └ Renderer::EndFrame
          ├ RecordViews   게임 → 게임 뷰 텍스처 (FrameTarget)
          └ overlay       EditorUI::Draw(commands, frameSlot) → 백버퍼
```

- UI 는 ImGui 다(D-60). 인스펙터는 매 프레임 프로퍼티 표에서 다시 그려지는 것이고 이미모드가 그 모양이다. 리테인드 UI 를 쓰면 뷰모델을 따로 들고 동기화해야 한다.
- ImGui 백엔드(`EditorUI`)는 **JBroRHI 위에 직접** 썼다. `imgui_impl_dx12` 를 쓰려면 RHI 가 감춘 D3D12 핸들을 도로 꺼내야 한다. 정점·인덱스 버퍼는 프레임 슬롯마다 나뉜다(D-66).
- 씬 에디터는 네이티브, 코드 에디터는 Code-OSS 포크 **[계획]** 다. 둘은 파일(`.jproject`·`.jcanvas`·`.jscript`)로 대화한다.
- 게임 뷰 렌더 타깃은 **프로젝트 해상도**로 만들고 패널에는 레터박스로 붙인다. 패널 크기로 만들면 게임이 보는 화면 크기가 에디터 창에 따라 달라져 `ScreenToWorld` 가 어긋난다.
- 프로젝트가 없어도 에디터는 그린다(D-67). 그때가 메뉴와 프로젝트 브라우저가 필요한 때다.
- 생김새(색 70개, 치수, malgun 15px)는 기존 엔진 테마를 그대로 옮겼다(D-73). 좁은 문자열 리터럴은 `/utf-8` 이다(D-74).

## 패널

`EditorPanel` 을 상속해 `AddPanel` 로 등록한다. 같은 제목은 거절한다. ImGui 가 제목으로 창을 식별해서 둘이 한 창을 나눠 쓰기 때문이다.

```cpp
class EditorPanel
{
public:
    virtual const char* GetTitle() const = 0;              // 로컬라이징 키. ImGui 창 id 로도 쓴다
    virtual const char* GetDisplayTitle() const;           // 화면에 보이는 이름
    virtual bool HasCloseButton() const;                   // 띄울지 말지는 창이 정한다
    virtual bool OnCreate(EditorApplication& editor);
    virtual void OnDestroy();
    virtual void OnUpdate(float deltaTime);                // 닫혀 있어도 돈다
    virtual void OnDraw() = 0;
    virtual void OnMenuBar();
    virtual EditorDock GetPreferredDock() const;           // Center / Left / Right / Bottom
};
```

훅이 다섯인 것은 재 봤기 때문이다(D-70). 기존 엔진의 `IImWindow` 는 스물한 개를 두었는데 실제 패널 열세 개가 override 하는 것을 세어 보니 이 다섯뿐이었다.
나머지 아홉은 override 가 0건이었다. 나중에 넣는 것이 지우는 것보다 쉽다.

지금 패널은 넷이다. `GameViewPanel`, `HierarchyPanel`, `InspectorPanel`, `StatsPanel`. 각자 `GetPreferredDock()` 으로 자기 자리를 말하고, 기본 자리는 첫 프레임에 한 번만 잡는다.

인스펙터는 컴포넌트 타입을 **하나도 모른다.** 리플렉션 표를 타고 내려가 잎사귀에서 코덱을 만나고, `ReadOnly`·`Range`·`Tooltip` 을 존중한다.

## 편집은 커맨드로만 한다

값을 직접 쓰지 않는다(D-71). 위젯이 한 번 쓰고 커맨드가 한 번 쓰면 되돌리기가 무엇을 되돌리는지 둘로 갈린다.
인스펙터는 위젯이 바꾼 값을 **도로 되돌려 놓고** 커맨드로 다시 적용한다. 쓰는 길이 하나로 남아야 한다.

```cpp
class EditorCommand
{
public:
    virtual const char* GetName() const = 0;
    virtual bool Execute() = 0;                              // 성공해야 스택에 쌓인다
    virtual void Undo() = 0;
    virtual void Redo() = 0;
    virtual bool CanMerge(const EditorCommand& newer) const;
    virtual bool TryMerge(const EditorCommand& newer);
};

class EditorCommandManager
{
public:
    bool Execute(OwnerPtr<EditorCommand> command);
    bool Undo();  bool Redo();  bool CanUndo() const;  bool CanRedo() const;
    void MarkSaved();  bool IsDirty() const;                 // 저장 여부는 불리언이 아니라 판번호다
};
```

| 커맨드 | 하는 일 |
|---|---|
| `SetPropertyCommand` | 필드 하나의 값. 한 줄 숫자 묶음(`Vec2`·`Color`·`Rect`)도 잎사귀이고, 글자·컨테이너는 값 전체의 YAML 이다(D-89) |
| `CreateObjectCommand` / `DeleteObjectCommand` | 삭제의 되돌리기는 스냅샷이다. 나무는 평평하게 + 부모 인덱스로 뜬다 |
| `AddComponentCommand` / `RemoveComponentCommand` | 떼었다 되돌린 컴포넌트는 **원래 슬롯**으로 돌아간다(D-85) |
| `MoveInHierarchyCommand` | 부모·형제 자리·로컬 트랜스폼이 함께 바뀐다. 한 손짓이면 커맨드도 하나다(D-84) |
| `CompoundCommand` | 여럿을 묶는다. 전부 되거나 하나도 안 된다(D-83) |

기존 엔진에는 서른 개 가까이 있다. `CompoundCommand` 가 생겼으므로 "한 동작이 여러 값을 바꾼다" 는 자리는 새 커맨드 없이 선다.

### 되돌리기를 지키는 규칙

- **드래그 하나가 되돌리기 하나다.** 슬라이더를 끄는 동안 프레임마다 커맨드가 생기는데 합치지 않으면 되돌리기를 수십 번 눌러야 한다.
  경계는 마우스 왼쪽 버튼의 **누른 시간**으로 알아낸다. 누르는 동안 단조 증가하고 새로 누르면 0 으로 돌아간다. 인스펙터도 기즈모도 배선할 것이 없다.
- **커맨드는 대상을 포인터로 들지 않는다**(D-72). 삭제를 되돌리면 오브젝트가 새로 만들어져 주소가 달라진다. 오브젝트는 **에디터 번호**(`EditorObjectRegistry`)로,
  컴포넌트는 **(오브젝트 번호, 타입, 같은 타입 중 몇 번째)** 로 가리킨다. 되살릴 때 `Rebind` 로 같은 번호에 다시 건다. 모르는 번호에는 걸지 않는다.
- **되살릴 수 없는 것은 하지 않는다**(D-76). 지우거나 떼기 전에 되살릴 값을 먼저 뜬다. 뜨지 못했으면 거절한다.
  "비었는가" 로는 모자란다. 나무를 뜨다 자식 하나에서 막히면 배열은 비어 있지 않다. 실제로 그렇게 자식이 영영 사라지는 결함이 있었다.
- **여럿 고른 대상에는 결과 값이 아니라 연산을 다시 적용한다**(D-83, D-86). 숫자는 델타로, 목록은 "몇 번째를 지운다" 로 옮긴다. 연산이 맞지 않는 대상은 통째로 뺀다.
- **값을 글자로 뜨고 되살리는 길은 저장 파일과 같은 걸음 하나다**(D-86). `ReflectedYaml` 이다.
- 되돌릴 것이 없는 편집(제자리로 옮기기, 같은 값 다시 넣기)은 스택에 올리지 않는다. 스택 상한은 256 이다.
- 텍스트 필드에 타자 치는 중이면 Ctrl+Z 는 씬이 아니라 글자를 되돌린다.
- **그리는 도중에 계층이나 컴포넌트 배열을 바꾸지 않는다.** 프레임이 끝난 뒤에 적용하거나 그 프레임의 나머지 그리기를 멈춘다.

## 공용 위젯

패널이 ImGui 를 직접 부르지 않는다. `JBro::Widget` 을 거친다(D-79). 없어서 새로 만들었다면 그것도 공용 위젯으로 만들어 둔다.

| 위젯 | 무엇 |
|---|---|
| `FormLayout` | 2열 표. 왼쪽 라벨, 오른쪽 위젯(`SetNextItemWidth(-FLT_MIN)`). `Row`·`FullRow`·`Break` |
| `Fields` / `Scalar` | 값 한 줄. `Vec2` 는 `DragFloat2` 한 줄, `Color` 는 `ColorEdit4` 한 줄 |
| `EnumCombo` | `EnumNames` 를 받아 드롭다운 |
| `List` / `ListVirtual` | 저장소를 모르는 목록. 원소 접근을 콜백으로 받아 타입이 지워진 리플렉션 `Array` 도 같은 UI 로 그린다. 추가·삭제·드래그 재정렬·읽기 전용이 안에 있다 |
| `Tree` / `TreeBegin` / `TreeEx` | 행 사각형과 내용 사각형을 따로 돌려주는 트리. 썸네일·배지·버튼을 행에 얹을 수 있다 |
| `TextField`, `Button`, `FieldLabel`, `Splitter`, `LoadingSpinner`, `CheckMark` | |

레이아웃 규칙 셋(D-81). **라벨과 위젯은 두 칸으로 나눈다**(위젯에 라벨을 넘기면 패널이 좁아질 때 라벨이 잘린다. 실제로 그렇게 났다).
**한 값은 한 줄이다**(색 하나가 네 줄을 먹으면 사용자는 숫자 네 개를 맞춰야 한다). **표 칸 하나보다 넓어야 하는 것은 표를 끊고 줄 전체를 쓴다**(D-89).

못 옮긴 위젯: 에셋 필드, 오디오 셋, 경로 필드, 레이어 머리, 팝업, FontAwesome 아이콘.

## 화면에 나오는 글자는 키다

사용자에게 보이는 문자열을 소스에 직접 쓰지 않는다(D-80). `Loc::Text(key)` / `Loc::TextOr(key, fallback)` 이고 키는 `LocalizationKeys.h` 한곳에 모은다.
표는 `Localization/ko-KR.yaml`, `en-US.yaml` 이다. 기본 `ko-KR`, 폴백 `en-US`.

```yaml
Locale: ko-KR
Entries:
  panel.game: 게임 뷰
  panel.hierarchy: 계층 구조
  menu.save_canvas: 캔버스 저장
  menu.undo: 실행 취소
  hierarchy.no_project: 열린 프로젝트가 없습니다
```

**한국어 문구는 번역체로 쓰지 않는다**(D-91). 영어 원문을 낱말마다 옮기지 말고 한국어 소프트웨어가 **그 자리에** 쓰는 말을 쓴다. 그래서 옮기기 전에 키가 어디에 쓰이는지 코드에서 확인한다.

- 명령(메뉴·단추)은 "대상 + 동작 명사": `컴포넌트 추가`, `삭제`, `실행 취소`. `붙이기`·`떼기`·`지우기` 같은 풀어쓴 동사형은 쓰지 않는다.
- 켜고 끄는 칸의 라벨은 기능 이름: `활성`, `사용`. `켜짐` 같은 상태말은 쓰지 않는다.
- 안내 문장은 "~습니다": `선택한 오브젝트가 없습니다`. 짧은 상태 표시는 "~됨"·"없음": `저장되지 않음`.
- 용어는 널리 쓰는 편집기 용어를 따른다. select → `선택`, search → `검색`, element → `항목`, property → `속성`.
- **컴포넌트 이름은 번역하지 않는다.** 인스펙터 머리·추가 목록 어디서나 접두어를 뗀 타입 이름(`Transform2D`)이다. 코드와 문서와 화면이 같은 이름을 써야 찾을 수 있다. 필드 라벨은 필드 이름이다.

## "돌아간다" 는 검증이 아니다

- UI 를 바꿨으면 **띄워서 보고**, 무엇을 봤는지 보고에 적는다. 테스트가 잡는 것은 배선이고, 잘린 라벨과 네 줄짜리 색은 화면에만 있다. 테스트에 `JBRO_EDITOR_SHOT` 을 주면 화면을 파일로 찍는다.
- 기존 엔진에 같은 화면이 있으면 나란히 놓고 다른 점을 적는다. 이유 없이 다르면 이식이 덜 된 것이다.
- 테스트 프로세스에서 단언은 대화상자를 띄우면 안 된다(D-69). 뮤테이션 한 번이 7분을 멈춰 있었고, ImGui 단언이 CRT 창을 띄우고 사람을 기다리고 있었다.
- 에디터 구간에 뮤테이션 150개 남짓을 돌렸다. 살아남은 것은 테스트를 채우거나 동치라고 판단해 근거를 적었다(D-77). 그 과정에서 진짜 결함 넷이 나왔다.

## 다음에 할 만한 것

`tasks/todo.md` 의 Editor Snapshot 에 적힌 것이다. 뿌리끼리의 차례 바꾸기(캔버스에 오브젝트 순서가 없다), 게임 뷰 매 프레임 opt-in, 팝업과 아이콘 폰트,
`Save Canvas` 메뉴(파일 대화상자가 먼저다), 표의 키 칸 입력, 구조체 원소 목록의 끌어 옮기기.
