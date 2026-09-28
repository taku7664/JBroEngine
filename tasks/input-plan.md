# 입력 계획 (기존 엔진 입력 이식과 구조 재검토)

> 계약은 `docs/ProjectRule.md`, 결정은 `tasks/todo.md` Decisions 다. 이 문서는 그 둘을 향해 가는 순서와
> 상태를 적는다. 상태는 항목마다 `[완료]` `[진행]` `[제안]` `[가정]` `[열림]` 으로 붙인다.
> `[제안]` 은 **사용자 확인 전**이다. 2026-09-25 사용자 지시("인풋 처리 가자. 기존 엔진 처리 확인해 보고, 하위 레이어
> 블로킹은 할 수 있게끔만 설계했으면 좋겠다. 현재 엔진의 아키텍처와 규칙에 맞게 이식하는 구조를 제안하고, 기존 엔진의
> 문제점을 개선해서 이식")로 시작했다. 같은 날 §5 의 다섯 가지가 모두 권고대로 확인돼("1 권고대로, 2 허용, 3·4·5 ㅇㅇ") **D-214** 이 됐다.
> 아래 `[제안]` 표시는 그 시점에 모두 확정이 됐다. 디스패치 자리만 세부를 고쳤다(§3.4, D-214 (6)). 코드는 브랜치 `input` 에서 1 단계부터 진행한다.

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
JBroCore (Tier S)        Key·MouseButton·KeyModifiers(JBroPlatform 에서 옮김, <JBro/Core/InputKeys.h>) - 1 단계에서 고친 자리, 아래 참고
JBroInputTypes (Tier S)  장치 스냅숏(KeyboardState·MouseState),
                         InputView(핸들러가 받는 것), InputHandler<Layer, Order>, InputResult, InputActionId,
                         Service::InputService. 헤더 위주, 서비스 .cpp 하나
JBroInput (Tier E)       System::InputSystem - 이벤트 → 프레임 상태, 액션 평가, 핸들러 체인과 소비 마스크
JBroPlatform (Tier E)    InputEvent 는 그대로. Key 들은 JBroInputTypes 에서 include (Tier E → Tier S, 허용 방향)
JBroFramework2DSystem    ScriptSystem 이 핸들러 체인을 함께 세우고 OnUpdate 앞에서 디스패치를 부른다
JBroHost                 EngineInstance 가 InputSystem 을 소유하고(ProjectRule §7 "Input 의 수명은 엔진이"), 이벤트를 넘긴다
```

- 이름은 `JBroAssetTypes`↔`JBroAsset`, `JBroAudioTypes`↔`JBroAudio` 의 관례를 따른다.
- 입력은 차원과 무관하다. 2D 프레임워크에 두면 3D 가 같은 것을 또 만든다.
- `[완료]` **키 이름은 `JBroCore` 에 둔다**(1 단계에서 고침). 처음에는 장치 상태와 한 모듈에 모으려 했으나, 플랫폼 헤더(`Input.h`)가
  키 이름을 include 해야 하므로 `JBroInputTypes` 에 두면 플랫폼을 보는 14 개 프로젝트(Asset·RHI 셋·Graphics·Host·Editor·호스트 둘·
  프레임워크 시스템 둘·테스트)가 모두 새 include 경로를 받아야 했다. 키 이름은 차원과 무관한 값 타입이라 "JBroCore 에 한 번" 규칙(§4)에
  그대로 맞고, 그러면 include 경로를 하나도 바꾸지 않는다. 장치 상태·뷰·핸들러·서비스는 그대로 `JBroInputTypes` 다.

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
- **디스패치 시점**: 시작 훅(OnCreate·OnStart)을 받은 활성 스크립트만, 그 프레임의 `OnUpdate` 보다 먼저다. 부르는 자리는
  `Framework2D::Update` 에서 고정 스텝(`RunFixedSteps`)보다 앞이다(`ScriptSystem::DispatchInput`). `ScriptSystem::OnUpdate` 안에 두면
  그보다 먼저 도는 `OnFixedUpdate` 의 폴링이 막히기 전의 입력을 본다. 그 프레임에 막 생긴 스크립트는 다음 프레임부터 받는다(기존과 같다).
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

1. `[완료]` **모듈과 프레임 상태**(`3f6468d`·`03412c7`). `JBroInputTypes`(`InputState.h`·`InputView.h`)·`JBroInput`(`System::InputSystem`)을
   세웠고 키 이름은 `JBroCore` 의 `InputKeys.h` 로 옮겼다(§3.1). `InputSystem::BeginFrame(events, mapping)` 이 이벤트를 접는다.
   테스트(`InputSystemTests`, 스위트 앞쪽에서 1 초 안에 끝난다): 한 프레임 안의 눌렀다 떼기가 누름과 뗌 둘 다, 누른 채 다음 프레임은
   눌림만, 반복은 누름이 아님, 누름을 못 본 채 온 반복은 눌림(아래), `FocusLost` 가 눌린 것을 뗌과 위치를 잊음, 돌아온 뒤 첫 위치는
   이동이 아님, 글자 순서와 32 상한, 레터박스 매핑과 이동·휠 합, 범위 밖 이름 무시, 실제 창에 `PostMessageW` 로 넣은 눌렀다 떼기,
   200 프레임 접기의 CRT 할당 0. 음성: 스크립트 프로브 `/p:JBroTierProbe=Input` 이 `JBro/Input/InputSystem.h` 에서 C1083 하나로 실패.
   뮤테이션: 15/15 잡힘. 첫 판 13 개 중 `반복을 누름으로` 하나가 살았다 - 먼저 누름을 받은 뒤의 반복은 `Press` 의 "이미 눌림" 검사가
   거르므로 반복 검사가 일하는 곳은 **누름을 못 본 채 반복만 오는 때**(키를 누른 채 창으로 돌아올 때)뿐이었고, 그때 코드는 키를
   떼어진 것으로 두고 있었다. 그런 반복은 눌림으로 두고 누름은 세지 않게 고친 뒤(`03412c7`) 두 변이를 더해 잡았다.
   러너는 `tools/mutate.py` 가 아니라 스크래치의 것을 썼다 - 그쪽은 `taskkill /IM JBroTests.exe` 로 **다른 세션의 테스트까지** 죽인다.
2. `[완료]` **폴링 서비스와 게임 호스트.** `Service::InputService`(`GetView`·`Keyboard`·`Mouse`)와 `InputServiceContext`·`InputSystemContext`
   (`IInputSystem` 인터페이스 포인터)를 `JBroInputTypes` 에 두고, 블록은 네트워크처럼 **호스트가** 낸다(`Make/FindInput*ContextBlock`).
   `JBroRuntime` 의 `ServiceContext` 에 넣지 않았다 - 그러면 Runtime 이 입력 모듈을 알아야 한다. `EngineInstance` 가 `InputSystem` 을 소유하고,
   `TickFrame` 이 펌프 직후 `BeginFrame` 을 부른다(호스트가 입력을 가져가는 동안은 빈 목록). 호스트 모듈 사본에도 묶어 정적으로 붙인
   스크립트가 같은 서비스를 읽는다. 두 프렐류드가 `<JBro/InputTypes/ServiceContext.h>` 를 include 한다. `JBroInputTypes` 는 이제 정적
   라이브러리이고 스크립트 DLL 도 링크한다.
   테스트: 호스트 안의 서비스가 이번 프레임을 보고 묶이지 않으면 빈 입력(`InputSystemTests`), **실제 호스트가 실제 스크립트 DLL 을 실은 채**
   창에 `WM_KEYDOWN` 을 넣고 틱하면 DLL 안의 서비스가 눌림을 보고 `WM_KEYUP` 뒤에는 뗌을 본다(`ScriptDLLLoaderTests`). `Debug`·`Debug_Game2D`·
   `Debug_Game3D` 빌드 경고 0, 전체 스위트 통과. 뮤테이션 3/4 잡힘(블록을 안 냄·접지 않음·호스트 사본을 묶지 않음 - 마지막 것은 처음에
   재지 않아 테스트를 더해 잡았다). 서비스 블록을 안 내는 변이는 **동치**다: `InputServiceContext` 안의 `InputService` 는 멤버가 없는 값이라
   묶든 안 묶든 DLL 사본의 내용이 같다(상태는 시스템 블록이 나른다). 서비스에 상태가 생기면 이 판단은 다시 한다.
3. `[완료]` **핸들러 체인과 블로킹**(`dfa9e14`). `InputHandler<Layer, Order>`(C++20 문자열 템플릿 인자, 레이어 `NameId` 는 컴파일 타임)와
   `IInputHandler::OnInput(InputView&)`·`InputResult`. 썽크는 `MakeScriptInputBinding<T>` 하나가 만들고 `ScriptTypeInfo::input`(이름으로 붙인 것)과
   컴포넌트 버킷(정적으로 붙인 것)이 든다 - `Runtime` 은 `IInputHandler` 를 전방 선언만 한다. `Canvas::FindScriptInputBinding` 이 둘 중 맞는 쪽을
   찾고 `ScriptSystem::Rebuild` 가 체인을 세운다. `InputSystem` 은 `BeginDispatch`·`Deliver`·`EndDispatch` 로 소비를 나르고, 핸들러가 가져간 장치는
   **그 핸들러가 돌아온 뒤에** 아래에 걸린다(가져간 핸들러 자신은 끝까지 읽는다). 레이어 순서 기본값은 Modal·UI·Game·World·Debug 이고
   `SetLayerOrder` 로 바꾼다(프로젝트 파일에서 읽는 것은 5 단계). `Framework2D::Update` 가 고정 스텝 앞에서 `DispatchInput` 을 부른다.
   테스트(`InputChainTests`): 순서(레이어·Order·실행 순서, 없는 레이어는 맨 아래), 없는 레이어 경고는 체인을 다시 세워도 한 번, 시작 전 스크립트는
   다음 프레임부터, `Block` 이 아래 핸들러와 폴링을 막고 풀면 돌아옴, 마우스만 소비하면 키보드는 남음, 꺼진 핸들러와 위에서 그 프레임에 끈 핸들러는
   안 부름, 레이어 순서를 바꾸면 다시 줄 섬, 이름으로 붙인 스크립트도 핸들러, 체인이 돌지 않은 프레임은 막히지 않음, 200 프레임 디스패치의 CRT
   할당 0, 프레임워크가 고정 스텝 앞에서 돌려 `OnFixedUpdate` 의 폴링도 막힘. 뮤테이션 21/21 잡힘(첫 판에 `체인이 돌지 않은 프레임이 지난 블록을
   유지` 가 살아 테스트를 더해 잡았다). 핸들러 안의 파괴를 큐로 보내는 `IterationGuard` 는 재지 않았다 - 없애면 죽은 객체를 부르는 UB 라 테스트가
   확정적으로 울지 않는다. `OnUpdate` 와 같은 가드이고 같은 줄을 쓴다.
4. `[완료]` **에디터**(`80a0f63`). 패널을 그릴 때마다 에디터가 `EditorPanel::SetFocused` 로 포커스를 적고, 게임 뷰가 `ReportGameView` 로
   자기 포커스와 레터박스 그림 사각형을 알린다. `BuildEditorUi` 가 이번 프레임의 이벤트를 UI 에 넣을 때 **재생 중·멈추지 않음·지난 프레임에
   게임 뷰 포커스**이면 같은 이벤트를 `EngineInstance::SubmitHostInput` 으로 건네고, 다음 틱이 게임 뷰 매핑으로 접는다. 게임 뷰를 떠나는
   프레임에는 `FocusLost` 하나를 건넨다. 게임이 키를 받는 동안 단축키는 F5·F6 만 돈다(D-214 (7)). `InputSurfaceMapping` 은 플랫폼 입력 헤더로
   옮겨 호스트 API 가 입력 모듈 헤더 없이 받는다. 멀티 뷰포트를 켜지 않았으므로 ImGui 화면 좌표가 곧 창 클라이언트 좌표다(기존 엔진은 켜서
   뷰포트 원점을 뺐다). `[가정]` 에디터 UI 와 창 클라이언트가 같은 픽셀 단위다(DPI 배율을 ImGui 에 따로 걸지 않는다).
   테스트(`EditorApplicationTests`): 재생 전에는 게임 뷰 포커스여도 받지 않음, 재생 중 게임 뷰 포커스면 창에 넣은 W 가 호스트 서비스에 눌림,
   그동안 Delete 단축키가 커맨드를 만들지 않음, 인스펙터로 옮기면 W 가 떼어지고 다음 프레임들에 다시 접히지 않으며 거기서 친 A 는 게임에 가지 않음.
   **실제 `JBroEditorHost.exe` 확인**: 창에만 메시지를 부치는 스크립트로 오브젝트를 만들고 F5 → 게임 뷰 클릭 → Delete 하면 오브젝트가 남고,
   레이어 패널을 누른 뒤 같은 Delete 는 지우며, 정지하면 재생 전 캔버스로 돌아온다. 게임 뷰 마우스 매핑은 에디터 안에서 재지 않았다
   (카메라와 게임 텍스처가 있어야 사각형이 선다) - 매핑 자체는 1 단계 단위 테스트가 잰다.
   뮤테이션 9/9 잡힘. 첫 판에 둘이 살았고 둘 다 테스트가 약했다: `재생 여부를 보지 않고 넘김` 은 재생 전 구간의 게임 뷰가 **실제로는
   포커스를 갖지 못해서**(첫 프레임들의 도크 배치가 포커스 요청을 덮었다) 검사가 공허했고, `건네받은 입력을 비우지 않음` 은 검사가
   `FocusLost` 를 처음 접은 프레임(W 가 아직 눌린 채 시작한다)에 돌아서 다시 접힌 누름이 보이지 않았다. 둘 다 고친 뒤 잡혔다.
   에디터 테스트는 다른 세션의 `JBroTests.exe` 가 함께 돌면 포커스를 빼앗겨 흔들린다(`[flake] active=other`) - 그때의 실패는 다시 잰다.
5. `[완료]` **액션과 프로젝트 설정**(`30fd229`·`c651a53`). `InputActionMap`(`JBroInputTypes`) 은 액션 64 개, 액션마다 바인딩 8 개의
   고정 POD 표이고 이름의 `NameId` 로 찾는다(§3.5). 평가는 **물을 때 그 자리의 뷰로** 한다 - 위에서 소비한 장치는 액션에서도 빠진다.
   값 종류·원천·키 합성·길이 1 자르기는 기존 엔진과 같고, 두 키에 묶은 액션은 한쪽만 떼면 뗀 것이 아니다. 없는 이름은 0 이고 이름으로
   한 번 경고한다(표를 다시 넣으면 다시 말한다). `.jproject` 는 기존 엔진 모양의 `InputLayers`·`InputActions` 를 읽고 쓰고, 기존 엔진의
   옛 키 이름(`Num0`·`LeftCtrl`·`Equals`·`Grave`·`Numpad0`…)을 읽어 새 이름(`Digit0`·`LeftControl`…)으로 적는다. 이름 표는 `JBroCore`
   (`GetKeyName`·`FindKeyByName` 등)에 있고 열거자에서 뽑아 만들었다. 입력 설정이 없는 프로젝트는 저장해도 한 줄도 늘지 않는다.
   호스트는 프로젝트를 열 때와 `SetProjectFile` 때 레이어 순서와 액션을 넣는다(`ApplyInputSettings`). 설정 화면의 입력 갈래: 레이어
   (위로·삭제·추가 - 처음 추가하면 기본 순서를 먼저 옮겨 적는다), 액션마다 접는 마디(이름·종류, 바인딩마다 `Source`·`Code`·
   패드 원천이면 `GamepadIndex`·Vector2 면 `Composite`). 다른 설정처럼 편집본을 고치고 저장할 때 파일에 쓴다(D-137 - 커맨드가 아니다).
   테스트(`InputActionTests`·`ScriptDLLLoaderTests`·`EditorApplicationTests`): 이름 표가 열거자마다 되돌아옴과 옛 이름, Bool 의 두 키,
   WASD 대각선 길이 1, 소비된 키보드의 액션, 없는 액션의 한 번 경고, 기존 엔진 파일 모양 읽기·쓰기·고친 뒤 다시 읽기·잘못된 블록 여섯
   가지 거절, 실제 호스트에서 `SetProjectFile` 로 넣은 액션이 서비스로 눌림, 설정 화면이 마디를 열어 그리고 고친 바인딩을 저장(첫 판에
   레이어 줄의 표가 `PopID` 뒤에 닫혀 ImGui 단언이 터진 것을 잡았다). 뮤테이션(6 단계와 함께): 액션·파일·호스트 적용 14 개 중 12 개가 첫 판에 잡혔고,
   둘은 고쳤다 - 바인딩 목록을 닫는 줄은 아래의 들여쓰기 조건이 같은 일을 해서 **죽은 코드**라 지웠고(`4b760ac`), 표를 다시 넣을 때 경고
   기억을 지우는 줄은 경고가 빈 표로만 재서 못 잡던 것을 경고가 쌓인 표로 재게 해 잡았다.
6. `[완료]` **게임패드**(`fb35d7d`). 플랫폼은 네 자리의 날 상태만 준다(`IPlatform::PollGamepad`·`SetGamepadVibration`, Windows 는
   XInput·`Xinput9_1_0.lib`). 둥근 데드존(0.24)·트리거 문턱(0.12)·지난 폴링과 견준 누름·뗌 수·빠진 패드의 뗌·빈 자리의 120 프레임
   재확인·진동 만료는 `InputSystem` 이 한다 - 가짜 플랫폼으로 잰다. 이벤트가 아니라 폴링이라 두 폴링 사이의 눌렀다 떼기는 보이지 않는다.
   진동은 서비스(`SetGamepadVibration(자리, 낮은, 높은, 초)`)로 걸고 메인 스레드가 시간을 잰다(기존 엔진의 워커 타이머와 `shared_ptr` 은
   두지 않는다, §1.3 P7). 창 포커스를 잃거나 패드가 빠지거나 에디터가 게임 뷰를 떠나거나 엔진이 내려가면 모터가 멈춘다. 다시 꽂은
   패드는 옛 진동을 이어 받지 않는다. 에디터는 게임 입력과 같은 조건(`SetHostGameInputActive`)으로만 패드를 게임에 준다. 액션의
   패드 바인딩: 버튼은 어느 패드든, 축·스틱은 연결된 첫 패드(-1) 또는 그 자리. `InputDevice::Gamepad` 는 네 자리를 함께 소비한다.
   테스트(`InputGamepadTests`·`RendererContractTests`): 누름·유지·뗌, 데드존·문턱과 끄기, 빠진 패드, 빈 자리 재확인 횟수와 꽂으면 한
   주기 안에 보임, 진동의 만료·같은 값 다시 보내지 않음·포커스·빠진 패드, 패드 바인딩의 액션과 소비, 실제 XInput 이 패드 없이도 죽지
   않음(이 기계에는 패드가 없다), 가짜 플랫폼과 가짜 RHI 로 세운 `EngineInstance` 가 틱마다 읽고 호스트가 입력을 가진 동안 주지 않으며
   내려갈 때 모터를 멈춤. `[열림]` 실제 패드로 재지 않았다 - 버튼 비트와 축 부호는 기존 엔진의 표를 옮겼다.
   뮤테이션: 게임패드·엔진 배선 15/15 잡힘.
7. **남은 것.**
   - `[완료]` **터치**(`a3875c1`). Windows 는 `WM_POINTER*` 를 받아 터치·펜만 남기고(마우스 포인터는 WM_MOUSE 로 온다), 그 뒤에도
     `DefWindowProcW` 로 넘겨 Windows 의 마우스 흉내가 에디터 UI 를 손가락으로 누르게 둔다. 이벤트는 `TouchBegan/Moved/Ended/Cancelled` 이고
     포인터 번호는 `codePoint` 에 싣는다(`InputEvent` 20 바이트 그대로). 정보를 못 얻은 떼기도 자리 NaN 으로 알린다 - 알리지 않으면 손가락이
     영영 닿아 있다. 입력 시스템은 손가락 열 개를 게임 화면 픽셀로 들고, 뗀 손가락을 한 프레임 더 보이고(기존 `e2274d1b`), 포커스를 잃으면
     모두 취소하고, 닿는 것을 못 본 손가락의 이동·뗌은 버린다. 스크립트는 `InjectTouch` 로 손가락을 만든다(기존 엔진과 같다 - 가상 조이스틱·
     자동 검사) - 다음 프레임에 같은 길로 접힌다. `InputDevice::Touch` 를 소비할 수 있다. **실측**: `PostMessageW` 가 포인터 메시지를
     1002(`ERROR_INVALID_MESSAGE`)로 거절해 플랫폼 시험은 `SendMessageW` 로 창 프로시저를 부른다 - 포인터 메시지는 `TranslateMessage` 를
     거치지 않으므로 실제로 도는 처리 그대로다. `[열림]` 실제 터치 화면으로 재지 않았다(가짜 포인터 번호는 `GetPointerInfo` 가 모른다).
     Web·Android 의 터치 생산자는 그 플랫폼이 설 때 한다. 뮤테이션 12 개 중 10 개가 첫 판에 잡혔다. 주입 큐를 비우지 않는 변이는 뗀 뒤의
     프레임을 보게 해 잡았고, 주입에서 `Stationary` 를 거르는 조건은 접는 쪽이 어차피 무시해 **동치**라 지웠다.
   - `[닫힘]` **키 단위 소비** - §5 질문 1 에서 (b)(장치 단위)로 정했다. 필요해지면 다시 연다.
   - `[완료]` **액션 세트**(D-218). 액션마다 세트 하나(`.jproject` 의 `Set:`, 적지 않으면 `Default`)에 속하고, 꺼진 세트의 액션은
     0 으로 읽힌다. 켜짐은 표의 `uint32` 비트 하나라 전환에 드는 것이 없고, 처음에는 `Default` 만 켜져 있다. 스크립트는
     `InputService::EnableActionSet`·`DisableActionSet`·`IsActionSetEnabled` 로 바꾸고 곧바로 걸린다. **세트는 막지 않는다** - 레이어 체인은
     누가 받는지를, 세트는 같은 키가 무슨 뜻인지를 정한다. 전환을 스택(push/pop)으로 두지 않은 것은 체인이 이미 우선순위 스택이라 입력이
     어느 쪽 때문에 사라졌는지 둘을 다 봐야 하기 때문이다. 세트는 32 개까지이고 넘친 세트의 액션은 버린다(`Default` 로 옮기면 끄려던 액션이
     늘 켜진다). 에디터는 재생을 멈출 때 프로젝트 상태로 되돌린다(`EngineInstance::ResetGameInput`). 설정 화면은 액션마다 `Set` 칸을 둔다.
     떼기 전에 세트를 끄면 그 액션의 뗌은 오지 않는다(문서에 적었다). 입력 시스템 컨텍스트의 ABI 가 2 가 됐다.
   - `[완료]` **선입력 도구 `InputBuffer`**(D-218). 신호가 마지막으로 참이었던 뒤의 초 하나를 드는 값 타입이고 두 프렐류드가 공개한다.
     스크립트가 제 뷰에서 읽은 값을 매 프레임 `Feed` 하고 `Peek`·`Take`·`Clear` 로 쓴다. **엔진은 지난 입력을 들지 않는다** - 뷰를 거쳐
     넣으므로 위의 레이어가 막은 누름은 버퍼에도 들어오지 않는다(엔진이 과거 입력을 들면 UI 가 가져간 누름이 나중에 점프로 나온다).
     코요테 타임은 입력이 아니라 "땅을 떠난 지 얼마" 라서 같은 도구에 땅 판정을 넣어 쓴다(헤더에 예가 있다).
   - **뮤테이션(D-218 셋).** 액션 세트 15/15, `InputBuffer` 5/5 가 첫 판에 제 검사로 잡혔다. 리바인딩은 27 개 중 18 개가 잡혔고, 살아남은
     아홉 중 여섯(호스트 버퍼의 크기 검사·패드와 방향만 다른 바인딩·콜론 없는 줄 하나·주석·지운 액션·서비스의 포착)은 시험을 보태 다시 잡았고,
     셋(수 뒤의 칸 비우기 둘, 크기가 바뀔 수 없는 `resize`)은 **동치**라 지웠다. 에디터가 재생을 멈출 때 되돌리는 한 줄은 에디터 시험
     (`TestTheInputSettingsDrawAndSave`)이 재생·세트 켜기·정지로 잰다 - 에디터 시험이 20 분이라 변이로는 돌리지 않았다.
   - `[열림]` **이벤트 시각과 리플레이.** 시각은 한 프레임 안의 타이밍이 필요한 리듬 게임 정도가 쓰고 `InputEvent` 20 바이트를 늘린다.
     리플레이는 입력만으로는 재현되지 않는다(가변 dt·물리·난수가 결정적이어야 한다). 기록 쪽은 `InputFrame` 이 POD 라 프레임마다 복사하면 된다.
   - `[완료]` **런타임 리바인딩**(D-218). 프로젝트의 표는 기본값이고 게임이 바꾼 것은 그 위에 얹힌다(`InputSystem` 이 두 표를 든다).
     `InputService` 의 `Get/Set/RemoveActionBinding`·`ResetActionBindings`·`ResetAllActionBindings` 이고 곧바로 걸리며, 에디터는 재생을 멈출 때
     세트와 함께 되돌린다. `CaptureBinding` 은 남은 입력에서 이번 프레임에 새로 누른 키·마우스 버튼·패드 버튼 하나를 잡고 `Escape` 는 물러나기다.
     **축과 트리거는 잡지 않는다** - 뷰는 이번 프레임만 들어 넘은 순간을 모르고, 누른 채로 설정을 열면 곧바로 잡힌다(`[열림]`).
     바꾼 것은 `WriteBindingOverrides`·`ReadBindingOverrides` 가 YAML 의 이름 → 글자 맵(`Jump: "Key Enter, GamepadButton South @1"`)으로 쓰고 읽는다.
     프로젝트와 다른 액션만 적고, 키는 이름이라 열거자 차례가 바뀌어도 깨지지 않으며, 틀린 줄은 그 액션만 그대로 두고 지운 액션은 조용히 건너뛴다.
     **글자는 호스트가 만든다** - 견줄 프로젝트 표와 고칠 살아 있는 표가 둘 다 입력 시스템에 있다. 서비스는 크기를 묻고 제 힙에
     버퍼를 키워 바이트만 받는다(커밋 `f671400` 의 메시지는 "이름표가 호스트에만 있어서" 라고 적었으나 틀렸다 - 게임 DLL 도 로드 때 호스트의
     이름표에 묶인다). 입력은 세이브를 모른다: 게임이 그 글자를 `SaveService` 로 남긴다.
   - `[완료]` **마우스 엄지 버튼**(2026-09-29). 열거값 `Extra1`(XBUTTON1, 뒤로)·`Extra2`(XBUTTON2, 앞으로)와 이름표·프레임 상태·바인딩·설정 화면·
     ImGui 매핑은 이미 있었고, Win32 창 프로시저에 빈 곳이 둘 있었다. (1) 붙잡음을 빼앗길 때(`WM_CAPTURECHANGED`, D-158) 왼쪽·오른쪽·가운데만
     떼어서, 엄지 버튼을 누른 채 Alt+Tab 하면 눌린 채로 남았다 - 이제 `MouseButton::Count` 까지 모두 뗀다. (2) `WM_XBUTTON*` 을 처리한 뒤
     `DefWindowProcW` 로 넘겨 Windows 가 `WM_APPCOMMAND`(브라우저 뒤로·앞으로)를 따로 만들었다 - 문서대로 `TRUE` 를 돌려준다.
     시험 `TestTheThumbButtonsComeOut`(`InputTests`): 실제 창에 넣은 XBUTTON1·2 가 `Extra1`·`Extra2` 와 위치로 나오고, `SendMessageW` 의 반환이 `TRUE`,
     엄지 버튼을 누른 채 붙잡음을 잃으면 뗌이 온다. 고치기 전에는 `TRUE` 단언에서, 반복을 앞의 세 버튼으로 되돌린 변이에서는 붙잡음 단언에서 죽었다.
     웹·안드로이드 플랫폼은 아직 마우스 이벤트를 내지 않으므로 해당이 없다.
   - `[열림]` 실제 게임패드·터치 화면 실측, IME 조합 글자(텍스트 계획의 입력 칸과 함께), 게임 뷰 마우스 매핑의 에디터 안 실측.

## 5. 확인할 질문

1. **블로킹의 단위.** (a) 반환값 `Block` 만(기존 A안) (b) `Block` + 장치 단위 `Consume` **[권고]** (c) (b) + 키 단위 `Consume`.
2. **`OnUpdate` 폴링을 허용하나.** (a) 허용하고, 체인이 막고 남은 것만 보인다 **[권고]** (b) 기존처럼 핸들러로만 받는다.
3. **핸들러 선언 모양.** (a) 기존과 같은 믹스인 `InputHandler<"UI", 10>` **[권고]** (b) `GameScriptBase` 에 가상 `OnInput` 을
   두고 레이어는 필드로 - 모든 스크립트를 매 프레임 부르게 되어 권하지 않는다.
4. **모듈.** 새 Tier S `JBroInputTypes` + Tier E `JBroInput`, `Key` 들을 `JBroPlatform` 에서 옮김 **[권고]**.
5. **디스패치 시점.** `ScriptSystem::OnUpdate` 안, 시작 훅 뒤·`OnUpdate` 앞 **[권고]**.
