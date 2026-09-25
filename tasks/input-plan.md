# 입력 계획 (기존 엔진 입력 이식과 구조 재검토)

> 계약은 `docs/ProjectRule.md`, 결정은 `tasks/todo.md` Decisions 다. 이 문서는 그 둘을 향해 가는 순서와
> 상태를 적는다. 상태는 항목마다 `[완료]` `[진행]` `[제안]` `[가정]` `[열림]` 으로 붙인다.
> `[제안]` 은 **사용자 확인 전**이다. 2026-09-25 사용자 지시("인풋 처리 가자. 기존 엔진 처리 확인해 보고, 하위 레이어
> 블로킹은 할 수 있게끔만 설계했으면 좋겠다. 현재 엔진의 아키텍처와 규칙에 맞게 이식하는 구조를 제안하고, 기존 엔진의
> 문제점을 개선해서 이식")로 시작했다. 코드는 아직 없다. §5 의 질문이 정해지면 Decisions 로 옮긴다.

## 0. 한 줄 요약

기존 엔진은 **프레임마다 `GetAsyncKeyState` 로 키 상태를 긁고**, 스크립트가 `InputHandler<"UI", 10>` 을 함께 상속하면
레이어 순서로 `HandleInput` 을 불러 **`true` 를 돌려주면 그 아래 전부를 막았다**. 레이어 블로킹이라는 뼈대는 옳았다.
그러나 (1) 한 프레임 안에 눌렀다 뗀 키가 사라지고, (2) 막기가 "전부 아니면 없음" 이라 마우스만 쓰는 UI 가 게임의 키보드까지
막으며, (3) 엔진 단계(`Button2DSystem`)는 `GetDeviceContext()` 로 체인을 건너뛰어 블로킹이 새고, (4) 액션을 부를 때마다
이름을 `strcmp` 로 찾았고, (5) 날 포인터 등록·해제가 두 파일에 흩어져 등록 누락 버그를 한 번 냈다.
권고는 **새 엔진의 이벤트 입력(D-62)에서 프레임 상태를 만들고**, 블로킹은 **반환값(전부 막기) + 장치 단위 소비**로 두며,
핸들러 목록은 따로 등록하지 않고 **스크립트 실행 순서 목록을 세울 때 함께 세우는 것**이다. `OnUpdate` 의 폴링은 막히고
남은 입력만 보게 해서, 폴링을 허용하면서도 블로킹이 새지 않게 한다.

## 1. 기존 엔진의 입력 - 무엇이 있었고 무엇이 아팠나

위치는 `C:\Users\박주형\source\repos\JBroEngine` 기준이다. 2026-09-25 에 읽은 판이다.
설계 합의본은 `tasks/InputSystemRoadmap.md`(413 줄)이고, 코드는 `Engine/Core/Input/`(2,329 줄, 그중 `InputSystem.cpp` 1,393 줄)이다.

### 1.1 구조

| 조각 | 파일 | 하는 일 |
|---|---|---|
| `CInputSystem` | `InputSystem.h/.cpp` | 엔진 내부. 장치 갱신 → `InputDeviceContext` 스냅숏 → 액션 평가 → 핸들러 디스패치. `CEngine::BeginFrame` 이 프레임마다 한 번 `Update(m_surfaceFocused)` |
| 장치 스냅숏 | `InputDevices.h` | `Keyboard`·`Mouse`·`Gamepad`(4)·`Touch`(10) 가 고정 배열 POD. prev/current 로 `IsPressed`/`IsReleased`. 텍스트는 프레임당 32 코드포인트 |
| `InputDeviceContext` | `InputDevices.h` | 핸들러가 받는 묶음. 복사·이동 금지, 생성자 private |
| `IInputHandler`·`InputHandler<Layer, Order>` | `IInputHandler.h` | C++20 문자열 NTTP. 스크립트가 `CGameScript` 와 **함께** 상속한다 |
| 액션 | `InputAction.h` | 이름 기반 `Bool`/`Float`/`Vector2`, 64 개, 이름 64 바이트. 바인딩은 키·마우스·패드 버튼·축·스틱, 키 합성(상하좌우) |
| `CInput` | `Input.h/.cpp` | 스크립트 공개 표면. 장치 켜고 끄기·패드 진동·데드존·터치 주입뿐. **폴링 API 없음** |
| 레이어 설정 | `ProjectInfo.InputLayers` | 기본 `Modal/UI/Game/World/Debug`. 모르는 레이어는 맨 아래 + 한 번 경고 |

디스패치(`InputSystem.cpp:1339`)는 정렬된 핸들러를 돌다가 `HandleInput` 이 `true` 이면 `break` 한다.
정렬 키는 (레이어 순위 오름차순, `Order` 내림차순, 등록 순)이다(`:608`).
등록은 `ScriptSystem.cpp:47` 이 Start 뒤에, 해제는 `ScriptSystem.cpp:34`(비활성)와 `Canvas.cpp:783`(파괴)이 한다.
디스패치 중 등록·해제는 지연 큐로 모았다가 프레임 끝에 반영한다. 핸들러 포인터는 `ReflectionRegistry.h:335` 의
`if constexpr (is_base_of<IInputHandler, T>)` + `static_cast` 썽크로 얻는다(RTTI 없음).

### 1.2 잘한 것 (그대로 가져온다)

- **디스패치는 메시지가 아니라 프레임이 구동한다.** 프레임당 한 번, 정해진 자리에서 돈다.
- 핸들러가 받는 값이 고정 배열 POD 라 DLL 경계를 그대로 넘는다. 받는 묶음은 복사할 수 없다.
- 레이어 이름은 문자열이고 순서는 프로젝트 설정이다. 창마다 레이어를 만들지 않고 UI 는 자기 밴드 안에서 쌓는다.
- 포커스를 잃으면 장치를 비우고(눌린 채 남는 키 방지) 돌아올 때 한 번 더 비운다(가짜 눌림 방지).
- 마우스 좌표는 **게임 화면 픽셀**이다(`bc0bb4df`). 에디터 게임 뷰의 레터박스를 벗겨 준다(`SetGameSurfaceRect`).
- 뗀 손가락도 한 프레임은 발행한다(`e2274d1b`). 그러지 않으면 탭이 클릭이 되지 못한다.
- 대각선 이동 벡터는 길이 1 로 자른다.

### 1.3 아팠던 것 (고쳐서 가져온다)

- **P1. 한 프레임 안의 눌림이 사라진다.** `IsVirtualKeyDown` 이 `GetAsyncKeyState(vk) & 0x8000`(`InputSystem.cpp:222`)
  하나만 본다. 프레임과 프레임 사이에 눌렀다 뗀 키는 두 번의 폴링 모두에서 "안 눌림" 이다. 30 fps 에서 가볍게 친 점프가
  빠진다. 게다가 이것은 **전역 키 상태**라서 포커스 게이트가 없으면 다른 앱에 친 키도 읽는다.
  새 엔진은 이미 D-62 로 이 길을 기각했다(이벤트로 모은다).
- **P2. 막기가 전부 아니면 없음이다.** 로드맵 §3 이 "부분 consume 안 함" 으로 정했다. 그래서 마우스로만 조작하는 인벤토리
  창이 마우스를 가지려고 `true` 를 돌려주면 게임의 WASD 도 멈춘다. 그것을 피하려면 창이 `false` 를 돌려줘야 하고, 그러면
  창을 누른 클릭이 게임에도 간다. 둘 다 틀린 동작이다.
- **P3. 블로킹이 샌다.** `GetDeviceContext()`(`InputSystem.h:124`) 가 "정해진 시점에 돌아야 하는 엔진 단계" 용으로 열려 있고,
  `Button2DSystem.cpp:143` 이 그것으로 포인터를 읽는다. 모달 스크립트가 `true` 를 돌려줘도 그 아래의 `Button2D` 는 눌린다.
  반대로 버튼을 누른 클릭이 게임 스크립트에도 간다. 로드맵이 "폴링 escape hatch 없음 → 블로킹 완전 일관" 을 원칙으로 두었는데
  엔진 자신이 그 구멍을 냈다.
- **P4. 액션을 부를 때마다 이름을 문자열로 찾는다.** `ActionState::Find` 가 `strcmp` 선형 탐색이다(`InputAction.h`).
  매 프레임 경로의 문자열 비교이므로 ProjectRule §9 위반이다. 64 바이트 이름을 POD 에 품어 스냅숏이 4 KB 를 넘는다.
- **P5. 등록·해제가 흩어져서 한 번 빠졌다.** 인스펙터용 편집 시점 인스턴스가 먼저 생기면 생성 블록이 건너뛰어져 썽크가
  채워지지 않았고, 핸들러가 등록되지 않아 이동이 안 됐다(로드맵 "실측/디버깅 세션"). 날 `IInputHandler*` 를 들고 있으므로
  해제가 한 군데라도 빠지면 DLL 을 내린 뒤 죽은 vtable 을 부른다.
- **P6. 핸들러는 모든 장치를 한 묶음으로 받는다.** 소비한 장치를 아래에서 비워 보이는 수단이 없어서, P2 를 고치려 해도
  넣을 자리가 없다. 액션도 소비와 무관하게 디스패치 전에 한 번 평가된다.
- **P7. 진동 타이머가 과하다.** 길이 있는 진동을 끄려고 `CTaskManager` 워커가 잠들었다 깨고, 목표값은 `shared_ptr` 로
  공유하며 세대 번호로 낡은 타이머를 거른다(`InputSystem.h:17`). 목적은 "메인 스레드가 멈춰도 모터가 꺼진다" 인데,
  메인 스레드가 멈추면 게임 전체가 멈춘 것이고 포커스를 잃으면 어차피 모터를 끈다. 워커와 공유 소유를 들일 값이 없다.
- **P8. 좌표가 정수다.** 마우스 위치·이동량이 `int` 라서 고해상도 화면의 배율이 걸리면 1 픽셀 아래가 버려진다.

## 2. 새 엔진에 지금 있는 것

- `[완료]` **플랫폼은 입력을 이벤트로 모은다(D-62).** `IPlatform::GetInputEvents()` 가 지난 `PumpEvents` 의
  `JArrayView<InputEvent>` 를 준다. `InputEvent` 는 20 바이트 POD, 종류는 `KeyDown`/`KeyUp`(`repeat` 표시)/`Text`/
  `MouseMove`/`MouseButtonDown`/`MouseButtonUp`/`MouseWheel`/`FocusGained`/`FocusLost`. 키는 물리 키이고 좌우 Shift 와
  키패드 Enter 가 갈린다. 한 프레임 상한 4096.
- `[완료]` **꺼내 가는 쪽이 비운다(D-177).** 에디터는 `SetInputOwnedByHost(true)` 로 이벤트를 자기 UI 에 넣고 비운다.
  게임 호스트는 `EngineInstance::TickFrame` 이 펌프 전에 비운다. **게임 쪽 소비자는 아직 없다**(todo.md D-177 주변).
- `[완료]` 스크립트는 `ScriptSystem`(2D, 실행 순서 200)이 레이어 합성 순서의 깊이 우선으로 돌린다(D-45). 목록은
  `Canvas::GetScriptOrderRevision()` 이 바뀔 때만 다시 세운다.
- `[완료]` 스크립트 서비스는 `ServiceContext`/`Framework2DServiceContext` 에 값으로 있고, 서비스 `.cpp` 가 `SystemContext`
  로 시스템을 찾는다(ProjectRule §10.3, `Physics2DService.cpp`).
- **막힌 곳**: `Key`·`MouseButton`·`KeyModifiers` 가 `JBroPlatform`(Tier E)의 `Input.h` 에 있다. 스크립트 타깃은 Tier S
  Include 경로만 받으므로(§3) **스크립트는 키 이름조차 볼 수 없다.**

## 3. 설계

### 3.1 모듈 `[제안]`

```
JBroInputTypes (Tier S)  Key·MouseButton·KeyModifiers(JBroPlatform 에서 옮김), 장치 스냅숏(KeyboardState·MouseState),
                         InputView(핸들러가 받는 것), InputHandler<Layer, Order>, InputResult, InputActionId,
                         Service::InputService. 헤더 위주, 서비스 .cpp 하나
JBroInput (Tier E)       System::InputSystem - 이벤트 → 프레임 상태, 액션 평가, 핸들러 체인과 소비 마스크
JBroPlatform (Tier E)    InputEvent 는 그대로. Key 들은 JBroInputTypes 에서 include (Tier E → Tier S, 허용 방향)
JBroFramework2DSystem    ScriptSystem 이 핸들러 체인을 함께 세우고 OnUpdate 앞에서 디스패치를 부른다
JBroHost                 EngineInstance 가 InputSystem 을 소유하고(ProjectRule §7 "Input 의 수명은 엔진이"), 이벤트를 넘긴다
```

- 이름은 `JBroAssetTypes`↔`JBroAsset`, `JBroAudioTypes`↔`JBroAudio` 의 관례를 따른다.
- 입력은 차원과 무관하다. 2D 프레임워크에 두면 3D 가 같은 것을 또 만든다.
- `Key` 를 `JBroCore` 에 두는 길도 있다("차원과 무관한 공개 값 타입은 JBroCore 에 한 번"). 그러나 장치 스냅숏·핸들러·서비스가
  같이 가야 하므로 한 모듈에 모으는 편이 의존이 짧다. `JBroCore` 는 값 타입·컨테이너만 갖는다는 표(§3)와도 맞다.

### 3.2 받는 방식: 이벤트에서 프레임 상태를 만든다 `[제안]`

기존 엔진의 P1 을 고치는 핵심이다. 폴링을 하지 않고 D-62 의 이벤트를 프레임마다 한 번 접는다.

- 키·버튼마다 **지금 눌림**(`down`) 과 **이번 프레임의 눌림 수·뗌 수**(`pressCount`·`releaseCount`, 각 1 바이트)를 둔다.
  `IsPressed` 는 `pressCount > 0`, `IsReleased` 는 `releaseCount > 0` 이다. 한 프레임 안에 눌렀다 떼면 `down` 은 거짓이지만
  `IsPressed` 와 `IsReleased` 가 **둘 다 참**이다. prev/current 비교로는 이것을 나타낼 수 없다.
- 자동 반복(`repeat = true`)은 눌림 수에 넣지 않는다. 텍스트 칸이 필요로 하는 반복은 `Text` 이벤트가 따로 준다.
- `FocusLost` 는 눌린 것을 **모두 뗀 것으로** 접는다(그 프레임에 `IsReleased` 가 참). 기존처럼 조용히 지우면 "떼면 멈춘다" 를
  기다리는 스크립트가 영영 멈추지 못한다. `FocusGained` 는 아무것도 누르지 않은 상태에서 시작한다.
- 마우스 위치는 `float` 게임 화면 픽셀, 이동량은 이벤트 합이다(P8). 휠은 칸 수의 합이다.
- 텍스트는 코드포인트 고정 배열(프레임당 32, 넘치면 버림)로 순서대로 둔다.
- 전부 고정 크기다. 프레임 경로에 힙 할당이 없다(§9).
- 게임패드는 이벤트가 아니라 **폴링**이다(XInput 이 그렇다). 플랫폼에 `PollGamepads(GamepadState* out, count)` 를 더해
  같은 모양의 상태를 만든다. 뒤 단계다(§4 의 6).

**누가 이벤트를 넘기나.** `InputSystem::BeginFrame(ArrayView<InputEvent>, const SurfaceMapping&)` 을 호스트가 부른다.

- 게임 호스트: 플랫폼 이벤트 전부, 창 전체 = 게임 화면.
- 에디터: **재생 중이고 게임 뷰가 포커스를 가졌을 때만** 이벤트를 넘기고, 게임 뷰의 레터박스 사각형을 매핑으로 준다.
  포커스가 게임 뷰를 떠나는 프레임은 `FocusLost` 를 하나 만들어 넘긴다(눌린 채 남기 방지). 에디터 UI 는 지금처럼 ImGui 로 받는다.
  `[가정]` 게임 뷰가 포커스를 가진 동안 ImGui 가 그 키를 다른 패널에 쓰지 않는다. 단축키(`Ctrl+S` 등)가 게임 뷰 포커스에서도
  도는지는 4 단계에서 재서 정한다 - 기존 엔진은 둘 다 받게 두었다.

### 3.3 블로킹: 반환값 + 장치 단위 소비 `[제안]`

사용자 요구("하위 레이어 블로킹은 할 수 있게끔만")를 **반환값 하나로 된다**는 모양으로 지키면서 P2·P6 을 푼다.

```cpp
// JBroInputTypes
enum class InputResult : std::uint8_t { Pass, Block };

class Pause final : public GameScript2D, public InputHandler<"UI", 10>
{
    InputResult OnInput(InputView& input) override
    {
        if (input.Keyboard().IsPressed(Key::Escape))
        {
            Toggle();
        }
        // 열려 있으면 아래(게임)는 아무것도 받지 않는다.
        return m_open ? InputResult::Block : InputResult::Pass;
    }
};

class Inventory final : public GameScript2D, public InputHandler<"UI", 0>
{
    InputResult OnInput(InputView& input) override
    {
        if (m_open && m_rect.Contains(input.Mouse().Position()))
        {
            HandleClick(input.Mouse());
            // 마우스만 가져간다. 아래의 게임은 키보드를 그대로 받는다.
            input.Consume(InputDevice::Mouse);
        }
        return InputResult::Pass;
    }
};
```

- `Block` = 아래 핸들러를 부르지 않는다(기존 `true` 와 같다). 기본은 `Pass`.
- `InputView::Consume(InputDevice)` = 그 장치를 아래 핸들러에게 **빈 장치로 보이게** 한다. 소비 마스크는 체인을 따라 내려가며
  쌓이고, `InputView` 는 읽을 때마다 마스크를 본다. 복사가 없다.
- 키 하나 단위(`Consume(Key::Escape)`)는 뒤로 미룬다(§5 질문 1). 장치 단위로 P2 의 예가 풀리고, 키 단위는 마스크가
  비트 배열이 되어 읽기마다 비트 검사가 붙는다.
- 액션도 소비를 따른다(P6). 액션 값은 미리 평가해 두지 않고 `InputView::Action(id)` 를 부를 때 **그 시점의 마스크를 건 장치**로
  계산한다. 바인딩 몇 개를 도는 일이라 싸다.
- **`OnUpdate` 에서의 폴링은 체인의 맨 아래다**(P3 의 해법). `Service::InputService` 가 주는 `InputView` 는 체인이 다 돈 뒤
  **막히고 남은 것**이다. 모달이 `Block` 했으면 `OnUpdate` 의 `IsDown` 도 거짓이다. 그래서 간단한 게임은 핸들러 없이
  `OnUpdate` 폴링만으로 쓰고, UI 를 얹는 순간 블로킹이 그 폴링에도 그대로 걸린다. 기존 엔진은 폴링을 아예 막아서
  일관성을 얻었지만 그 대가로 모든 스크립트가 핸들러를 상속해야 했다.
- **엔진 단계도 체인에 선다.** 뒤에 올 `Button2D` 같은 엔진 시스템은 `GetDeviceContext()` 같은 뒷문이 아니라 같은 체인에
  레이어·순서를 가진 핸들러로 들어간다(`InputSystem::AddSystemHandler`). 지금은 그런 시스템이 없으니 자리만 둔다.

**체인 순서**는 (레이어 순위 오름차순, `Order` 내림차순, 스크립트 실행 순서)다. 기존의 "등록 순" 대신 **실행 순서**(D-45)를
쓰면 저장했다 다시 연 캔버스에서도 순서가 같다. 등록 순은 로드 순서에 따라 흔들린다.

### 3.4 핸들러를 찾는 방법: 따로 등록하지 않는다 `[제안]`

P5 의 해법이다. 날 포인터 등록·해제를 없앤다.

- `InputHandler<Layer, Order>` 는 `IInputHandler`(순수 가상 `OnInput`)만 상속하는 믹스인이다. 기존과 같이 C++20 문자열 NTTP 다.
- `MakeScriptTypeInfo<T>()` 가 `if constexpr (std::is_base_of_v<IInputHandler, T>)` 로 `ScriptTypeInfo` 에
  `ToInputHandler` 썽크와 레이어 `NameId`·`Order` 를 채운다. `dynamic_cast` 가 없다.
- `ScriptSystem::Rebuild`(이미 리비전이 바뀔 때만 돈다)가 실행 순서 목록을 세울 때 **핸들러 목록도 같이 세워**
  `InputSystem` 에 넘긴다. 매 프레임에는 "활성이고 시작한 것" 만 부른다. 켜고 끄기·파괴·핫 리로드는 전부 리비전이 올라가는
  일이므로 따로 해제할 곳이 없다.
- **디스패치 시점**: `ScriptSystem::OnUpdate` 안에서 시작 훅(OnCreate·OnStart) 뒤, `OnUpdate` 앞이다. 기존과 같이 입력이
  그 프레임의 `OnUpdate` 보다 먼저 오고, 시작하지 않은 스크립트는 입력을 받지 않는다.
- 디스패치 중에 스크립트가 생기거나 꺼지면 그 프레임의 체인은 그대로 돌고 다음 리비전에서 반영된다(기존의 지연 큐와 같은 효과).
  꺼진 스크립트는 그 프레임에 이미 지나간 뒤가 아니면 부르지 않는다 - 부르기 직전에 활성 검사를 한다.

### 3.5 액션 `[제안]`

- 아이디는 `InputActionId`(= `NameId`, `constexpr MakeNameId("Move")`)다. 스크립트는 `input.Action(Actions::Move)` 처럼
  정수로 부른다. 이름 문자열은 `.jproject` 와 에디터에만 있다(P4).
- 값 종류와 바인딩은 기존과 같다(`Bool`/`Float`/`Vector2`, 키·마우스 버튼·패드 버튼·축·스틱, 키 합성, 길이 1 자르기).
- 저장은 `.jproject` 의 `InputLayers`·`InputActions` 두 블록이고, 읽고 쓰기는 리플렉션(`ReflectedYaml`)으로 한다.
  기존은 `magic_enum` 을 썼다. 새 엔진에는 `EnumDescriptor` 가 있다.
- 편집 화면은 `ProjectSettingsPanel` 의 새 갈래이고 위젯 계층만 쓴다(§11.1). 편집은 커맨드다.
- 모르는 액션 아이디를 물으면 0 값을 주고 **한 번만** 경고한다(아이디 → 이름은 `NameTable` 로 찾는다, 경고 경로는 콜드).

### 3.6 진동·터치·텍스트 `[제안]`

- 진동은 메인 스레드의 만료 시각으로 끈다(P7). 포커스를 잃거나 엔진이 내려가면 즉시 끈다. 워커·`shared_ptr` 없음.
- 터치는 기존의 규칙(뗀 프레임도 한 번 발행, 포인터 판정은 터치 먼저)을 그대로 가져온다. 플랫폼에 터치 이벤트가 아직 없으므로
  Web·Android 가 설 때 한다.
- 게임의 텍스트 입력은 `Text` 이벤트를 그대로 싣는다. IME 조합 중인 글자(조합창)는 텍스트 계획(D-200)의 입력 칸이 설 때 본다.

### 3.7 규칙 대조

| 규칙 | 어떻게 지키나 |
|---|---|
| DLL 경계는 POD (§5) | `InputView`·장치 상태·`InputEvent` 는 고정 배열 POD. 읽기 멤버는 헤더 인라인 |
| 프레임 경로에 힙·문자열·`dynamic_cast` 없음 (§9) | 고정 배열, 액션은 정수 아이디, 핸들러 썽크는 `if constexpr` |
| Tier S / Tier E (§3) | 스크립트가 보는 것은 `JBroInputTypes` 뿐. `InputSystem` 은 Tier E |
| System / Service (§10.3) | 접는 일은 `System::InputSystem`, 스크립트 표면은 값형 `Service::InputService` |
| 서비스 수명은 엔진 (§7) | `EngineInstance` 가 `InputSystem` 을 소유한다 |
| `SafePtr` 는 메인 스레드 | 입력에는 워커가 없다 |
| 한 줄 제어문 금지 | 예시 코드도 블록으로 썼다 |

## 4. 단계와 완료 조건 `[제안]`

각 단계는 테스트가 먼저고 단계마다 커밋한다. 경계 규칙은 음성 테스트로 본다.

1. **모듈과 프레임 상태.** `JBroInputTypes`·`JBroInput` 을 세우고 `Key` 들을 옮긴다(`JBroPlatform` 은 include 만).
   `InputSystem::BeginFrame` 이 이벤트를 키보드·마우스 상태로 접는다.
   완료: 한 프레임 안의 눌렀다 떼기가 `IsPressed`·`IsReleased` 둘 다 참, 반복은 눌림이 아님, `FocusLost` 가 눌린 것을 뗌,
   매핑이 레터박스를 벗김, 프레임 경로 할당 0. 음성: 스크립트 타깃이 `JBroInput` 헤더를 include 하면 컴파일 실패.
2. **폴링 서비스와 게임 호스트.** `Service::InputService` 를 `ServiceContext` 에 넣고(ABI 버전 올림) 게임 호스트가 이벤트를 넘긴다.
   완료: 게임 DLL 의 스크립트가 `OnUpdate` 에서 키를 읽어 오브젝트를 움직이는 호스트 테스트(창에 `PostMessageW`, D-62 방식).
3. **핸들러 체인과 블로킹.** `InputHandler<Layer, Order>`, `ScriptTypeInfo` 썽크, `ScriptSystem` 이 체인을 세움, `Block`·`Consume`.
   완료: `Block` 이 아래와 `OnUpdate` 폴링을 막음, 마우스 소비가 키보드를 남김, 순서(레이어·Order·실행 순서), 꺼진 스크립트는
   부르지 않음, 디스패치 중 생성은 다음 프레임부터, 모르는 레이어는 맨 아래 + 한 번 경고. 뮤테이션으로 각 조건을 깨 본다.
4. **에디터.** 재생 중 + 게임 뷰 포커스일 때만 넘김, 떠날 때 `FocusLost`, 게임 뷰 사각형 매핑.
   완료: 실제 `JBroEditorHost` 에서 게임 뷰를 눌러 키를 치면 움직이고, 인스펙터에 글자를 치는 동안은 움직이지 않는다.
5. **액션과 프로젝트 설정.** `.jproject` 두 블록, 평가, 설정 화면(커맨드).
   완료: 키 합성·스틱·소비된 장치의 바인딩이 빠짐, 저장·되돌리기.
6. **게임패드.** XInput 폴링, 핫플러그 간격 확인, 데드존·트리거 문턱, 진동과 만료.
7. `[열림]` 터치(Web·Android 플랫폼과 함께), 키 단위 소비, 액션 맵 전환(걷기·차량·메뉴), 입력 버퍼(선입력·리플레이),
   런타임 리바인딩과 사용자 저장.

## 5. 확인할 질문

1. **블로킹의 단위.** (a) 반환값 `Block` 만(기존 A안) (b) `Block` + 장치 단위 `Consume` **[권고]** (c) (b) + 키 단위 `Consume`.
2. **`OnUpdate` 폴링을 허용하나.** (a) 허용하고, 체인이 막고 남은 것만 보인다 **[권고]** (b) 기존처럼 핸들러로만 받는다.
3. **핸들러 선언 모양.** (a) 기존과 같은 믹스인 `InputHandler<"UI", 10>` **[권고]** (b) `GameScriptBase` 에 가상 `OnInput` 을
   두고 레이어는 필드로 - 모든 스크립트를 매 프레임 부르게 되어 권하지 않는다.
4. **모듈.** 새 Tier S `JBroInputTypes` + Tier E `JBroInput`, `Key` 들을 `JBroPlatform` 에서 옮김 **[권고]**.
5. **디스패치 시점.** `ScriptSystem::OnUpdate` 안, 시작 훅 뒤·`OnUpdate` 앞 **[권고]**.
