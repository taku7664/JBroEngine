# Input System Roadmap

게임 엔진 입력 계층 재설계 + 향후 작업 목록.
현재 구조(`CInput` = WndProc `SubmitMessage` 기반 상태 미러 + 레이어 블로킹 핸들러)를
아래 합의 설계로 교체한다.

> 이 문서는 긴 설계 토론의 **최종 합의본**이다. 결정 사항은 전부 확정이며, 임의로 바꾸지 말 것.
> C++ 표준: **C++20**(stdcpp20) — 문자열 NTTP(템플릿 인자 문자열) 사용 가능.

---

## 1. 핵심 원칙

- **윈도우 메시지 구동 dispatch 폐기.** WndProc `SubmitMessage` 라우팅 제거. 반응속도 때문.
  - "메시지 기반 폐기"의 진의 = **dispatch를 메시지가 구동하지 않음**(프레임이 구동).
    state 수집을 이벤트로 하는 것 자체는 무방(아래 플랫폼 백엔드 참고).
- **dispatch는 프레임당 1회.** InputSystem 업데이트에서 디바이스 갱신 → 레이어 순 핸들러 호출.
- **핸들러 등록해야만 입력 사용 가능.** 폴링 escape hatch 없음 → 블로킹 완전 일관.
- **스크립트는 디바이스 직접 접근 불가.** `Input.Keyboard` 식 노출 안 함.
  오직 핸들러 콜백 인자 `InputDeviceContext` 안에서만 디바이스 폴링.

---

## 2. 객체 구조 / 역할

### InputSystem (스크립트 **비공개**, 엔진 내부, **Platform 위**, `Engine.InputSystem`)
- 모든 디바이스 갱신 담당(per-device System 없음 — InputSystem이 통합 관리).
- 매 프레임: 디바이스 state 갱신 → `InputDeviceContext` 스냅샷 생성 → 레이어 순 핸들러 dispatch.
- **핸들러 등록/해제의 *유일한* 주체.** `RegisterHandler`/`UnregisterHandler` 는 여기 있고,
  엔진(ScriptComponent 수명)이 `Engine.InputSystem->RegisterHandler` 로 **직접** 호출한다.
  → Input facade 를 거치지 않는다(사용자/Input 미개입).
- 게임패드 폴링/진동/설정, 전역 디바이스 on/off 의 실제 구현.
- `BeginFrame`/`EndFrame` 등 스크립트가 알 필요 없는 내부 함수 소유.
- ScriptCore 에 노출 금지(호스트 전용). 호스트 코드만 `Engine.InputSystem` 접근.

### InputDevice (Keyboard / Mouse / Gamepad / Touch)
- **prev + current 보관**(엣지 `WasPressed`/`WasReleased` 계산 위해). "프레임 입력 스냅샷".
- **고정크기 POD** (DLL 경계 안전 — `std::string`/STL/`std::function` 금지):
  - 키/버튼/스틱/커서/휠delta/마우스delta → 고정 POD 배열.
  - 멀티터치 → `Touch touches[MAX_TOUCH]; int count;` 고정배열.
  - 텍스트 → `char32_t buffer[MAX]; int len;` 고정 링버퍼.
- (POD 이유: host/DLL/라이브재컴파일 사이 STL ABI 동일 보장 불가. 고정배열은 그 의존성 0.)

### InputDeviceContext (핸들러 인자, **host 소유**)
- 디바이스 상태 묶음. 현재 `InputMessage` 역할 대체.
- `const InputDeviceContext&` 로 핸들러에 전달.
  - POD 라 inline 폴링 안전 → **vtable 불필요, 복사 불필요**(주소만 넘김, DLL 힙할당 0).
- **탈출 방지**: 복사생성/이동/대입 전부 `delete` + 생성자 `private`(InputSystem만 friend로 생성).
  값 저장 원천봉쇄. (주소 보관은 C++라 못 막음 → 주석 경고: 저장 말고 그 프레임에 다 읽어라.)

### Input (스크립트 **공개**, `Script.Input`)
- **핸들러 등록/해제 API 없음.** 등록은 InputSystem 의 역할(위 참고). facade 는 관여하지 않는다.
- 스크립트 공개 표면 = **전역 설정 + 게임패드 출력/조회**:
  `SetDeviceEnabled` / `SetGamepadVibration` / `StopGamepadVibration` /
  `SetStickDeadzone` / `SetTriggerThreshold` / `GetConnectedGamepadCount` / `IsGamepadConnected`.
  (전부 내부적으로 `m_system`(=CInputSystem)에 위임. BindSystem 으로 호스트가 1회 연결.)
- `IsKeyDown` 등 디바이스 폴링 API **없음**(입력은 핸들러 ctx 로만).

---

## 3. 핸들러 (사용 방식 — 확정)

### 상속 체인
```
IInputHandler                    // 순수 인터페이스
  └ InputHandler<Layer, Order>   // 템플릿: 게터만 채움. CGameScript 와 무관(상속 안 함!)
```
사용자 스크립트는 **CGameScript 와 InputHandler<T,N> 를 둘 다** 상속한다.
**InputHandler 가 CGameScript 를 상속하는 게 절대 아님.**

### 코드
```cpp
// 인터페이스 (엔진 제공)
class IInputHandler
{
public:
    virtual bool        HandleInput(const InputDeviceContext& ctx) = 0;
    virtual const char* GetInputLayer() const = 0;
    virtual int         GetInputOrder() const = 0;
};

// 문자열을 템플릿 인자로 받는 래퍼 (C++20 NTTP)
template<size_t N>
struct LayerName
{
    char value[N];
    constexpr LayerName(const char (&s)[N]) { std::copy_n(s, N, value); }
};

// 템플릿: IInputHandler 만 상속. 레이어/오더 게터 자동 구현. CGameScript 안 건드림.
template<LayerName Layer = LayerName("Game"), int Order = 0>
class InputHandler : public IInputHandler
{
public:
    const char* GetInputLayer() const override { return Layer.value; }
    int         GetInputOrder() const override { return Order; }
};

// 사용자 스크립트: CGameScript + InputHandler<T,N> 둘 다 상속
class CPlayer : public CGameScript, public InputHandler<"UI", 10>
{
public:
    void Update(float dt) override { /* 게임 로직 */ }

    bool HandleInput(const InputDeviceContext& ctx) override
    {
        const Keyboard& kb = ctx.Keyboard;
        if (kb.IsDown(EKeyCode::LeftCtrl) && kb.IsPressed(EKeyCode::C)) Copy();
        if (kb.IsPressed(EKeyCode::Escape)) Close();
        return true; // 모달이면 처리함 → 하위 차단
    }
};
```

### 사용자가 쓰는 것 / 안 쓰는 것
- **씀**: 상속 줄 `InputHandler<"UI", 10>`(레이어/오더 인라인) + `HandleInput` 본문.
- **안 씀**: `GetInputLayer`/`GetInputOrder`(템플릿이 자동), 생성자/소멸자, 등록/해제 코드.

### 반환 시맨틱 (consume = A안 확정)
- 반환 = "처리했나(handled)".
- **`true` = consume → 하위 레이어 전부 차단. `false` = 통과(기본).**
- 부분 consume(키/디바이스 단위) **안 함** — 전부 차단 아니면 전부 통과.
  (반환을 bool 로 둠. 카테고리별 consume은 미래 옵션, 지금 안 함.)

### 호출
- 매 프레임 레이어 순 호출(입력 변화 없어도 호출 — 반응형).
- 필터 없음 — 핸들러 내부에서 직접 폴링.

---

## 4. 발견 & 등록/해제 (RTTI 안 씀 — 확정)

**dynamic_cast 안 씀.** `RegisterScript<T>` 의 `if constexpr (is_base_of<IInputHandler,T>)` 가
`CGameScript*→IInputHandler*` 정적 캐스트 썽크(`ScriptTypeInfo.ToInputHandler`)를 채운다(RTTI 0).
아니면 null → 입력 비핸들러.

**등록 주체 = InputSystem.** 호스트가 `Engine.InputSystem->RegisterHandler/UnregisterHandler`
를 **직접** 호출한다(Input facade 미경유). 호출 지점:
```cpp
// ScriptSystem::OnUpdate (호스트) — Start 후 1회 등록. 비활성 전환 시 해제.
//   인스턴스가 에디트타임에 미리 생성됐을 수 있어 썽크 캐싱은 등록 직전에 보장.
if (!script.InputRegistered && Engine.InputSystem.IsValid()) {
    if (!script.InputHandler) script.InputHandler = info->ToInputHandler(script.Instance);
    if (script.InputHandler) { Engine.InputSystem->RegisterHandler(script.InputHandler); script.InputRegistered = true; }
}

// ScriptComponent::ResetInstance (호스트 전용 emit — 파괴/핫리로드/씬언로드 choke point)
if (InputRegistered && InputHandler && Engine.InputSystem.IsValid())
    Engine.InputSystem->UnregisterHandler(InputHandler);
```
- `Engine.InputSystem` 은 호스트 전용. ScriptComponent.h 가 Ref.cpp 경유로 DLL 에서 *컴파일*되지만
  ResetInstance 를 odr-use 하지 않아 DLL 에 emit 안 됨 → 전역 Engine 참조는 호스트에만 생김(안전).

이 방식이 해결하는 것:
- **RTTI 미사용**(`if constexpr` + `static_cast`).
- **생성자 등록의 예외 전부 회피**:
  1. 편집모드/역직렬화/비활성 상태 오등록 → OnEnable 게이트로 차단.
  2. disable→enable 재토글 → OnEnable/OnDisable 로 재등록 가능.
  3. 생성 중 vtable(pure virtual HandleInput) 위험 → 생성 완료 후 등록이라 없음.
  4. 등록/해제 비대칭 → OnEnable↔OnDisable 동일 레벨 짝.
- **핫리로드(E4)**: 컴포넌트 파괴(OnDestroy)가 FreeLibrary **전에** 해제 보장.
  (FreeLibrary 는 힙 인스턴스 소멸자 안 부름 → dtor RAII 에 의존 금지.)
- 입력 안 쓰는 스크립트(InputHandler 상속 안 함) → `m_inputHandler == nullptr` → 등록 안 됨.

**떠도는 DLL 전역 핸들러 금지** — 핸들러는 컴포넌트(수명관리됨)에만 붙임.

---

## 5. 레이어 (순수 문자열 — 확정)

- **순수 문자열.** enum/codegen 없음.
- 레이어/오더는 템플릿 인자(`InputHandler<"UI", 10>`)로 지정 → 게터 자동.
- 없는 레이어 → **컴파일 통과**, 런타임 경고 로그 + **기본 밴드 폴백**(드롭 아님).
- 레이어 밴드 종류/우선순위는 **프로젝트 세팅**(문자열 목록 + 순서)에서 정의/조정.
- 같은 레이어 내 순서 = `Order` 값 → 같으면 등록순 tiebreak.
- 동적 스택(열린 순서대로 먹어야 하는 UI 창)은 **UI 서브시스템이 UI 밴드 내부에서 자체 관리**.
  창마다 엔진 레이어 만들지 않음(어차피 hit-test/focus 스택 가져야 하니 거기서 라우팅).

---

## 6. 플랫폼 백엔드 (state 수집 방식)

- 추상: 플랫폼 InputBackend가 매 프레임 device state 채움. 채우는 방식은 플랫폼 자유.
  InputSystem은 스냅샷만 읽음.
- **윈도우 = 직접 폴링**(GetAsyncKeyState / RawInput 버퍼). 메시지큐 안 거침 → 최저지연.
  (WM_KEYDOWN 메시지큐는 백로그/디스패치 타이밍 지연 → 안 씀.)
- **웹 / 모바일 = 이벤트 누적**(브라우저/OS가 이벤트만 제공 → 선택지 없음).
- **게임패드 = 폴링**(XInput / 웹 Gamepad API) — 전 플랫폼.
- **휠 / char**는 윈도우도 폴링API 없음 → 메시지 누적(지연critical 아님).
  이동키/버튼/커서 = 폴링.
- 순수 폴링 불가 신호(휠 / 마우스 raw delta / 텍스트 char)는 이벤트 누적이 본질 →
  플랫폼에서 누적만(dispatch 구동 아님), 매 프레임 스냅샷에 합산.

---

## 7. 예외/주의 (Phase 1 필수 반영)

- **E1 에디터/게임 입력 라우팅** — 게임 입력은 뷰포트 **포커스 상태에서만**.
  에디터는 ImGui 입력 자유 사용(툴 개발자 책임). 비포커스 시 게임 InputSystem 폴링 스킵.
  → **호스트/에디터는 InputSystem 자체를 안 씀**(에디터=ImGui). 즉 `IInputHandler`는 **항상 스크립트(DLL)**.
- **E2 포커스 상실 처리** — Surface `FocusLost` 연동: 비포커스면 디바이스 클리어 + 폴링 스킵
  (전역 키상태가 다른 앱 입력 먹는 것 방지). `FocusGained` 시 디바이스 리셋.
- **E3 dispatch 중 등록/해제 = deferred** — `HandleInput` 중 핸들러 등록(메뉴 열기)/
  해제(닫기)는 리스트 수정 → 반복자 무효화. **등록·해제 둘 다 deferred 큐**에 넣고
  프레임 끝 flush. (ImWindow QueueDeferred 패턴.)
- **E4 핫리로드 핸들러 수명** — 위 4절 라이프사이클로 해결(FreeLibrary 전 OnDestroy 해제 보장).

---

## ⚠️ 사용자 지시 (2026-06-07) — 잊지 말 것
- **모바일 NativeActivity/UIKit 글루는 보류** — 이 머신에 Android SDK/NDK/Gradle 미설치라 컴파일/검증 불가.
- **입력 시스템 작업(Phase 2 등)이 끝나면, 다른 작업 넘어가기 전에 모바일 글루 착수를 *먼저 제안*할 것.**
  (사용자가 "중요한 사항"으로 명시. 툴체인 설치 동반 필요.)
- 현재 진행: Phase 2(키테이블 보강 → 액션 매핑) 부터.

## 작업 목록

### 실측/디버깅 세션 (2026-06-07, 이어서)

**런타임 실측 진행:**
- SampleProject `PlayerScript` 에 입력 핸들러 추가 — `: public CGameScript, public InputHandler<"Game">`,
  `HandleInput` 에서 WASD + 게임패드0 좌스틱 → `MoveDirection`. 기존 OnUpdate 가 이동 적용.
- ScriptAPI.h 에 `Core/Input/IInputHandler.h` 추가(InputHandler 템플릿 노출). SDK 동기화.
- **결과: WASD 이동 동작 확인됨(사용자 확인).**

**버그 수정 (이동 안 되던 원인):**
- 에디트타임 인스턴스(EnsureEditTimeInstance, 인스펙터용)가 Play 전에 생성됨 → Play 시
  ScriptSystem 생성 블록 스킵 → 거기서만 하던 InputHandler 썽크 캐싱 누락 → 등록 안 됨.
- 수정: 썽크 캐싱을 생성 블록이 아니라 **등록 직전**(Start 후)에 보장. ScriptSystem.cpp.

**진단 로그(임시 — 제거 완료 2026-06-07):**
- ~~`CInputSystem::RegisterHandler` 등록 로그~~ / ~~`CInputSystem::Dispatch` 첫 디스패치 로그~~ → 검증 후 제거됨.

**라이브컴파일 스크립트 디버깅 지원 (중단점 바인딩):**
- 증상: VS 가 `GameScript_<serial>.dll` 에 "디버그 정보로 빌드 안 됨" — DLL 자체에 디버그정보 없음.
- 원인: repo `SDK/Templates/GameScript.vcxproj.template` Debug 설정이 **stale** —
  `<GenerateDebugInformation>false</...>` + `DebugInformationFormat OldStyle`. 링커가 PDB 안 만듦.
  (Dist 쪽 템플릿은 이미 고쳐져 있었음 — repo 소스만 누락.)
- 수정:
  - 템플릿 Debug: `GenerateDebugInformation=true`, `DebugInformationFormat=ProgramDatabase`, `Optimization=Disabled`.
    (SDK/Templates + 기존 생성된 SampleProject/Contents/GameScript.vcxproj 둘 다.)
  - `LiveCompileManager`: serial DLL 복사 시 **PDB 동반 복사**(링커 임베드 이름) — 빌드출력 pdb 가
    다음 빌드에 덮여 GUID 어긋나는 문제 대비, serial dll 옆에 매칭 pdb 보장.
- 검증: `GameScript.pdb` 생성 확인(20MB). Debug_Editor / Debug_Game / GameScript.dll 전부 green.
- 디버깅 절차: 에디터 실행 → VS 프로세스 연결(네이티브) → 라이브 리로드 1회 → PlayerScript.cpp 중단점.

**남은 정리:**
- ~~진단 로그 2개 제거~~ → 완료(2026-06-07).
- (아래 Phase 1 남은 작업: 터치/모바일/char/뷰포트포커스/웹진동)

### 레이어 프로젝트 세팅 UI/직렬화 (2026-06-07 완료)

`ConfigureLayers` 훅을 프로젝트 세팅에 연결 — 에디터에서 입력 레이어 목록/순서 편집 가능(실측 막던 본질).

- **ProjectInfo.InputLayers** (`std::vector<std::string>`, 기본 `Modal/UI/Game/World/Debug`) 신설. ProjectTypes.h.
- **직렬화(YAML)**: `.Jproject` 의 `InputLayers` 시퀀스. ProjectManager LoadProject 파싱(없으면 기본값 유지)
  + SaveProject 출력. AssetWatchIgnorePatterns 와 동일 패턴(트림/CRLF 방어).
- **적용**: `CProjectManager::ApplyInputLayersToSystem()` 가 `Engine.InputSystem->ConfigureLayers(InputLayers)` 호출.
  - 프로젝트 로드 완료부(locale 적용 직후) 1회.
  - `SetInputLayers()` (Apply 시) 즉시 재적용.
- **에디터 UI**: ProjectSettings 에 **Input** 카테고리 추가. 멀티라인 편집(한 줄=한 레이어, 위=최우선).
  AssetWatcher 패턴 UI 복제. Loc 키 `project_settings.category.input` / `.input.title` / `.desc` / `.hint` (ko/en).
- **검증**: Debug_Editor / Debug_Game green. 헤더 변경 없어 게임 DLL/SDK 영향 없음(InputSystem.cpp·에디터 호스트 코드만).

### Phase 1 구현 현황 (2026-06-07)

**완료 (호스트 Debug_Editor / Debug_Game 빌드 green, 인레포 GameScriptSample DLL 포함):**
- `CInputSystem`(엔진 내부) 신설 — 프레임 폴링 + dispatch + deferred 큐 + 레이어 정렬 + 포커스 게이트.
- 디바이스 POD: `Keyboard`/`Mouse`/`Gamepad`/`Touch` + `InputDeviceContext`(복사/이동/대입 delete, private ctor).
- `IInputHandler` + `InputHandler<Layer,Order>`(C++20 LayerName NTTP, 게터 자동).
- WndProc `SubmitMessage` 입력 경로 제거 → 윈도우 직접 폴링(GetAsyncKeyState/커서/버튼). 휠만 메시지 누적.
- 발견: `ScriptTypeInfo.ToInputHandler` + `RegisterScript<T>` `if constexpr`(RTTI 없음, static_cast 썽크).
- 등록/해제: ScriptComponent 수명 — Start 후 등록 / 비활성 전환·ResetInstance(파괴·핫리로드) 해제.
- `CInput` → 스크립트 facade(전역설정+등록위임). InputSystem 은 EngineCore 만, ScriptCore 비노출.
- 포커스 게이트: 메인 surface FocusGained/Lost → 폴링/디스패치 on/off + 디바이스 클리어.
- 레이어 문자열 + 폴백(미설정 시 최하위 + 1회 경고). 기본 밴드 Modal/UI/Game/World/Debug.
- SDK/Include 헤더 동기화(Input/InputDevices/IInputHandler/ReflectionRegistry/ScriptComponent).

**게임패드 백엔드 완료 (Debug_Editor/Debug_Game green):**
- 멀티 패드(슬롯 0..3 = 플레이어). Windows=XInput, Web=HTML5 Gamepad API.
- 버튼 enum 보강: DPad 4방향 + L3/R3 추가(South..Select + DPad + Thumb).
- 핫플러그: 미연결 슬롯 throttle 재확인(120프레임), 연결/해제 시 상태 리셋.
- 라디얼 deadzone(스틱) + 트리거 threshold — `SetStickDeadzone`/`SetTriggerThreshold`(전 패드 공통).
- 진동(rumble) 출력: `SetGamepadVibration(index,left,right,durationSeconds=0)`/`StopGamepadVibration`.
  변경 시에만 XInputSetState. 포커스 상실/Shutdown 시 하드웨어 직접 0(알트탭 스턱 방지).
  **타이머: duration>0 이면 TaskManager 워커 스레드가 sleep 후 정지 — 메인(게임 로직) 행 걸려도 모터 끔(무한진동 방지).**
  목표값/세대는 shared_ptr 공유 블록(InputSystem 먼저 파괴돼도 UAF 없음), gen 으로 스테일 타이머 취소.
  워커 미지원(웹) 시 메인 만료시각 폴백. 웹 진동(Haptic)은 표준경로 미지원 → 후속.
- 연결 조회: `GetConnectedGamepadCount`/`IsGamepadConnected`. 전부 Script.Input facade 공개.
- `Xinput9_1_0.lib` `#pragma comment` 링크(윈도우 기본 탑재, 재배포 불필요 — 사용자 제공 X).

**남은 Phase 1:**
- ~~터치 백엔드~~ → **완료(2026-06-07)**. 코어 + 웹 emscripten 콜백 + 모바일 inject + Windows WM_POINTER. 아래 "터치 입력" 절.
- ~~모바일 이벤트누적 백엔드~~ → **착수(2026-06-07)**. 터치 inject 계약(글루 대기). 아래 "터치 입력" 절.
- ~~텍스트(char/IME) 누적~~ → **완료(2026-06-07)**. 아래 "텍스트 입력" 절.
- ~~웹 게임패드 진동(Haptic Actuator)~~ → **완료(2026-06-07)**. EM_JS 로 GamepadHapticActuator 직접 호출. 아래 "웹 진동" 절.
- ~~프로젝트 세팅에서 레이어 목록/순서 주입~~ → **완료(2026-06-07)**. ProjectInfo.InputLayers + YAML + Input 카테고리 UI + ConfigureLayers 연결.
- ~~에디터 뷰포트 포커스 게이트 정밀화~~ → **완료(2026-06-07)**. 아래 "뷰포트 포커스 게이팅" 절.
- 런타임 실측(에디터 인터랙티브 — 빌드만 검증함).

### InputMap (액션 매핑) (2026-06-08 완료)

이름 기반 액션. 핸들러가 디바이스 대신 액션을 읽는다: `ctx.GetAction().GetValue<Vector2>("Move")`.

- **값타입** Bool/Float/Vector2(엔진 `Bool`/`Vector2` POD). 이름=순수 문자열.
- **강제변환**: Vector2→Bool(nonzero)/Float(x). Bool→Vector2 금지(Zero).
- **타입**(InputAction.h, DLL 공유): `InputActionValue`+`ActionState`(POD 고정배열, `GetValue<T>` 헤더 인라인 → 경계 안전). 저작용 `InputBinding{Source,Code,GamepadIndex,Composite}`/`InputActionDef`.
- **바인딩 소스**: Key / MouseButton / GamepadButton / GamepadAxis(Float) / GamepadStick(Vector2 직결). Vector2 는 키 컴포지트(Up/Down/Left/Right 역할) 또는 스틱. 멀티디바이스 OR/합성. Vector2 단위 클램프.
- **평가**: `CInputSystem::EvaluateActions`(매 프레임 PollDevices 후 → ctx 액션 스토어). 평가 헬퍼는 공개 ctx 접근자만 사용.
- **직렬화**: `ProjectInfo.InputActions`(InputLayers 옆) → `.Jproject` YAML. ProjectManager 가 **magic_enum**(호스트 전용)으로 enum↔문자열(수동 테이블 X, enum 재정렬 안전). Code 는 generic int → Source 로 분기 변환.
- **주입**: `ApplyInputMapToSystem` → `Engine.InputSystem->SetInputMap`(프로젝트 로드 + SetInputActions 시).
- **에디터 UI**: ProjectSettings Input 카테고리에 액션/바인딩 편집(magic_enum 콤보, Code 콤보는 Source 따라 전환). Add/Remove, 지연 삭제.
- **검증**: Debug_Editor/Debug_Game/GameScript.dll green. 웹빌드 검증(eval=InputSystem.cpp, magic_enum 미사용이라 웹 안전).
- **후속**: 런타임 리바인딩 API(`Input.SetBinding/SaveBindings`) + persistent path 오버라이드 레이어. 컨텍스트 스택(Phase 4).

### 뷰포트 포커스 게이팅 (2026-06-07 완료)

게임 입력이 GameView 패널 포커스 시에만 디스패치되도록 게이팅(인스펙터 등 다른 패널 편집 중 WASD 누출 방지). E1/E2 정밀화.

- `CInputSystem::m_viewportActive`(기본 true) + `SetViewportActive(bool)`. `Update()` 게이트 = `surfaceFocused && m_viewportActive`.
- **스탠드얼론 게임**: GameViewTool 없음 → 기본 true 유지 → 윈도우 포커스만으로 게이팅(기존 동작).
- **에디터**: `CGameViewTool` 가 포커스 수명 훅으로 토글 —
  - `OnCreate`/`OnDestroy` → false(baseline: 포커스 전엔 게임 입력 비활성).
  - `OnFocusEnter` → true, `OnFocusExit` → false.
  - `ImWindow::HandleHidden` 이 숨김 시 `OnFocusExit` 를 발화하므로 패널 닫힘/탭전환 엣지도 자동 해제(추가 처리 불필요).
- 포커스 판정 = `ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows)`(ImWindow::HandleFocus 기존 경로).

### 웹 진동 (GamepadHapticActuator) (2026-06-07 완료)

emscripten html5 C 래퍼에 haptic 이 없어 표준 JS 를 EM_JS 로 직접 호출. (XInput 경로는 Windows.)
- `EM_JS JBro_GamepadPlayEffect(index, strong, weak, durationMs)` — `navigator.getGamepads()[i].vibrationActuator.playEffect("dual-rumble", …)`.
  strong=저주파(왼쪽 모터), weak=고주파(오른쪽). `JBro_GamepadResetEffect(index)` — reset/0.
- 브라우저 effect 는 유한 지속 → 지속 진동은 `ApplyVibration` 에서 주기 재발행(keep-alive, chunk 250ms/재발행 200ms).
  변경 시 발행/정지, 미변경+활성이면 주기 재발행. 목표값은 기존 공유블록(TargetLeft/Right) 그대로 사용.
- 연결 해제 시 StopGamepadVibration(파리티), 포커스 상실/Shutdown 시 HaltVibrationHardware → 전 슬롯 reset.
- 검증: 웹 빌드 컴파일(emsdk). 실제 진동은 브라우저+패드 필요 — 미실측. 브라우저별 playEffect 지원 편차 있음.

### 터치 입력 (멀티터치) (2026-06-07 착수/완료)

mobile plan §Input("raw touch list 보존") 정렬. 코어 1개 + 플랫폼 생산자 2개(웹/모바일).
- **ETouchPhase**(InputTypes.h): Began/Moved/Ended/Cancelled.
- **CInputSystem::AccumulateTouch(id,x,y,phase)** + 영속 작업버퍼 `m_workingTouches[MaxTouchCount]`.
  id 추적: Began=빈 슬롯 점유, Moved=좌표 갱신, Ended/Cancelled=슬롯 해제. 한도 초과 드롭.
  (휠/텍스트는 프레임 transient 지만 터치는 손가락 뗄 때까지 활성 유지 → 별도 영속 버퍼.)
- **PollDevices**: 작업버퍼 활성분을 ctx.Touch 로 압축 스냅샷(공통 경로). 디바이스 비활성 시 count=0. ClearDevices(포커스상실) 시 버퍼 리셋.
- **웹**: emscripten touchstart/move/end/cancel 콜백(Initialize 등록/Shutdown 해제) → isChanged 터치만 AccumulateTouch(targetX/Y).
- **모바일**: `CMobilePlatform::InjectTouch(id,x,y,phase)` → `Engine.InputSystem->AccumulateTouch`. NativeActivity/UIKit 글루가 호출할 계약(surface/focus/pause/resume inject 와 동일 패턴). **글루(Phase2/3) 미구현 — 생산자 대기.**
- **Windows**: WndProc `WM_POINTERDOWN/UPDATE/UP/CAPTURECHANGED`(Win8+ 통합 포인터) → `AccumulateTouchPointer` → AccumulateTouch.
  `GetPointerInfo` 로 좌표(스크린→클라이언트) + 타입 필터(PT_TOUCH/PT_PEN, 마우스 제외). Ended/Cancelled 는 정보 조회 실패해도 슬롯 해제.
- 스레드: inject 는 메인 스레드(프레임 사이) 가정(휠/텍스트와 동일). 크로스스레드 글루면 락 필요(후속).
- 빌드 누락 수정: `BuildScripts/Web/web_game_sources.txt` 에 `InputSystem.cpp` 가 빠져 있어 웹빌드가 링크 실패했음 → 추가.
- 검증: 코어/모바일 inject = Windows 빌드 검증. 웹 = emsdk(C:\emsdk) 컴파일에서 **InputSystem.cpp(keypress/touch/EM_JS 진동) 에러 0** 확인.
  실제 터치/진동 = 디바이스/브라우저 필요 — 미실측.
- **웹빌드 풀 그린(2026-06-07)**: emsdk(C:\emsdk) 로 SampleProject 웹 패키지 생성 성공. 선결 버그 연쇄 수정:
  1. `WebCanvasSurface.cpp/.h` 콜백 반환형 `int`→`EM_BOOL`(최신 emsdk 에서 EM_BOOL=bool, typedef 정합).
  2. `web_game_sources.txt` 에 `InputSystem.cpp` 누락 추가(CInputSystem 심볼).
  3. `web_game_sources.txt` 에 `ScriptCore.cpp` 누락 추가(BindScriptCore/UnbindScriptCore 정의).
  (전부 이번 세션 전부터 깨져있던 웹빌드 누락/드리프트.)

### 텍스트 입력 (char) (2026-06-07 완료)

키 상태와 별개의 "완성 문자" 스트림(채팅/이름입력). 폴링 불가 → 플랫폼 누적 + 프레임 스냅샷 합산(휠과 동일 패턴).

- **Keyboard**(InputDevices.h): `char32_t m_text[MaxTextLength=32]` + `m_textLength`. 공개 `GetTextLength()`/`GetTextChar(i)`(유니코드 코드포인트). prev/current 없음 — 프레임 누적.
- **CInputSystem**: `AccumulateText(char32_t)`(폭주 방어 — 프레임 한도 초과 드롭) + `m_accumText/m_accumTextLen`.
  `PollDevices` 에서 누적→스냅샷 복사 후 비움(키보드 비활성 시 드롭). `ClearDevices`(포커스 상실) 시 누적 클리어.
- **Windows**: WndProc `WM_CHAR` — UTF-16 상위/하위 서로게이트 결합 → 완성 코드포인트(함수 로컬 static 으로 상위 서로게이트 상태). 짝 없는 유닛은 BMP 단일로 처리.
- **Web**: `emscripten_set_keypress_callback`(Initialize 등록 / Shutdown 해제) — keypress `charCode` 누적. 멀티플랫폼 병렬.
- 주의: 코드포인트 *원본* 전달(제어문자 0x08/0x0D 등 미필터) — 텍스트필드 소비자가 필터링(키 상태로 Backspace 등 별도 처리).
- SDK 동기화: InputDevices.h, InputSystem.h.

### Phase 1 — 구조 재편 (이번 핵심)
- [ ] `InputSystem` 신설(엔진 내부, 스크립트 비공개). Platform 위 배치. 디바이스 통합 갱신.
- [ ] WndProc `SubmitMessage` dispatch 경로 제거 → 프레임 폴링 갱신으로 교체.
- [ ] 디바이스 분리: `Keyboard` / `Mouse` / `Gamepad` / `Touch` (고정 POD, prev+current).
- [ ] `InputDeviceContext` 신설(디바이스 묶음, const ref 전달, 복사/이동/대입 delete + private ctor).
- [ ] `IInputHandler` 인터페이스 + `InputHandler<Layer, Order>` 템플릿(LayerName NTTP, 게터 자동).
- [ ] 발견/등록: 팩토리 `if constexpr (is_base_of<IInputHandler,T>)` + `static_cast` 1회 캐싱(RTTI 금지).
- [ ] 등록/해제: ScriptComponent `OnEnable` 등록 / `OnDisable` 해제 / `OnDestroy` 안전망.
- [ ] dispatch 중 등록·해제 **deferred 큐**(프레임 끝 flush).
- [ ] consume = A안: `true`=하위 전부 차단, `false`=통과(기본).
- [x] 레이어 = 순수 문자열, 없으면 경고+폴백. 프로젝트 세팅에 레이어 목록/순서.(2026-06-07)
- [ ] `CInput` 정리: 폴링 API 제거, 전역 설정(`SetDeviceEnabled` 등)만 남김.
- [x] 포커스 게이트: 뷰포트 포커스 시만 게임 입력. `FocusLost` 시 디바이스 클리어+폴링 스킵.(2026-06-07)
- [x] 휠/delta/char 누적 → 프레임 스냅샷 합산.(휠·char 완료 2026-06-07. raw delta 는 Phase 3)
- [~] 플랫폼 백엔드: Windows=직접폴링, Web/Mobile=이벤트누적, 게임패드=폴링. 병렬 구현.
      (키/마우스/휠/게임패드/터치/텍스트 완료. 웹 터치/텍스트/진동 콜백 완료. Windows WM_POINTER 터치 완료.
       모바일 키/마우스 네이티브(NativeActivity 글루)만 후속. 2026-06-07)
- [ ] ScriptCore/SDK 노출 정리: `Input`(전역설정) + `IInputHandler`/`InputHandler`/`InputDeviceContext`/
      디바이스/`EKeyCode` 등 헤더 SDK Include 내보내기. `InputSystem`은 비공개 유지.

### Phase 2 — 필수 기능 (당장 막힘)
- [x] **키 테이블 보강(2026-06-07)** — `EKeyCode`에 모디파이어(L/R Shift·Ctrl·Alt), F1~F12,
      편집/네비(Insert/Delete/Home/End/PageUp/Down/CapsLock), 기호(US `-=[]\;',./``), 숫자패드(0~9+연산) 추가.
      Windows `ToVirtualKey` VK 매핑 확장(F/Numpad 는 범위, 나머지 switch). A-Z/Num0-9 연속성 유지 위해 append.
      웹 키 상태 백엔드는 아직 없음(키스테이트 #else stub) → 웹 키매핑은 web keydown/keyup 백엔드 작업 시 함께. NumpadEnter=VK_RETURN(분리 불가).
- [x] **액션 매핑/바인딩 추상화(InputMap)(2026-06-08)** — 이름 기반 액션→바인딩 N개.
      키보드+게임패드 통합, 프로젝트세팅 YAML. 아래 "InputMap" 절. (리바인딩/persistent 오버라이드는 후속.)
- [ ] **터치/모바일 입력** — 멀티터치/탭/핀치/팬. 멀티플랫폼 규칙(윈도우 구현 시 웹/모바일 병렬) 준수.

### Phase 3 — 게임 실전
- [ ] 마우스 raw delta + 커서 캡처/락/visibility.
- [ ] 게임패드 deadzone(radial) + 감도 곡선. (현재 raw → 스틱 드리프트.)
- [ ] 텍스트 입력(char/IME) — 채팅/이름입력/에디터 텍스트필드.

### Phase 4 — 확장 계약 (지금 안 박으면 나중에 구조 깸)
- [ ] 타임스탬프/입력 버퍼 — 선입력(점프 버퍼/coyote), 격겜 커맨드, 리플레이/녹화, 롤백.
- [ ] 액션맵 컨텍스트 스택 — 걷기맵/차량맵/메뉴맵 전환(레이어 블로킹과 별개 개념).
- [ ] 게임패드 핫플러그 + rumble(출력 채널) + device→player 라우팅(로컬 코옵).

### 별도: 현 블로킹 시스템 결함 (재편 시 자연 해소되나 참고)
- [ ] (구) 블로킹이 이벤트만 막고 폴링은 안 막음 — 폴링 escape hatch 제거로 해소.
- [ ] (구) Dispatch 중 핸들러 수정 크래시 — deferred 큐로 해소.
- [ ] (구) layer raw int / 핸들러 raw 포인터 수명 — 문자열 레이어 + 컴포넌트 수명관리로 해소.
