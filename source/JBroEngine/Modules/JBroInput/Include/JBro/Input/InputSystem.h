#pragma once

#include <JBro/InputTypes/InputHandler.h>
#include <JBro/InputTypes/Internal/SystemContext.h>
#include <JBro/InputTypes/ServiceContext.h>
#include <JBro/InputTypes/System/IInputSystem.h>
#include <JBro/Platform/Input.h>
#include <JBro/Platform/Platform.h>
#include <JBro/Types/Array.h>
#include <JBro/Types/NameTable.h>
#include <JBro/Types/Table.h>

#include <cstdint>

namespace JBro::System
{
    // 게임 입력을 접고 레이어 체인에 내려보낸다(D-214). 엔진이 소유한다(ProjectRule §7).
    //
    // **폴링하지 않는다.** 플랫폼이 모은 이벤트(D-62)를 프레임마다 한 번 접는다 - 기존 엔진은
    // `GetAsyncKeyState` 로 긁어서 한 프레임 안에 눌렀다 뗀 키를 잃었다.
    // 메인 스레드 전용이다.
    class InputSystem final : public IInputSystem
    {
    public:
        InputSystem();
        InputSystem(const InputSystem&) = delete;
        InputSystem& operator=(const InputSystem&) = delete;

        // 이번 프레임을 연다. 지난 프레임의 눌림·뗌 수, 글자, 이동, 휠을 비우고 이벤트를 순서대로 접는다.
        // 입력을 받지 않는 프레임(에디터에서 게임 뷰가 포커스를 갖지 않을 때)도 빈 목록으로 불러야 한다 -
        // 그러지 않으면 지난 프레임의 `IsPressed` 가 남는다. 남은 입력(폴링이 보는 것)도 여기서 이번 프레임 전체로 돌아간다.
        void BeginFrame(JArrayView<InputEvent> events, const InputSurfaceMapping& mapping = {});

        const InputFrame& GetFrame() const;

        // 레이어 체인이 다 돈 뒤 남은 것이다. 체인이 돌지 않은 프레임은 이번 프레임 전체다.
        const InputView& GetResidualView() const noexcept override;

        // 스크립트 DLL 과 이 모듈 사본이 받을 값이다. 주소가 호스트의 수명 동안 바뀌지 않는다 -
        // 확장 블록이 이 주소를 가리킨다.
        const InputSystemContext& GetSystemContext() const;
        const InputServiceContext& GetServiceContext() const;

        // 레이어 체인이다(D-214). 누구를 어떤 차례로 부를지는 부르는 쪽(`ScriptSystem`)이 정하고, 여기는 소비를 나른다.
        //   BeginDispatch() → 켜진 핸들러마다 Deliver() → EndDispatch()
        // `Deliver` 가 참이면 그 핸들러가 `Block` 한 것이고, 부르는 쪽은 거기서 멈춘다. 멈추지 않아도 아래는 빈 입력만 본다.
        void BeginDispatch();
        bool Deliver(IInputHandler& handler);
        // 체인을 닫는다. 폴링(`Service::InputService`)이 보는 남은 입력이 여기서 정해진다.
        void EndDispatch();

        // 프로젝트가 정한 레이어 순서다. 앞이 먼저 받는다. 비어 있으면 기본 순서(Modal, UI, Game, World, Debug)다.
        void SetLayerOrder(JArrayView<NameId> layers);
        // 레이어의 순위다. 작을수록 먼저다. 없는 레이어는 맨 아래(레이어 수)이고 이름마다 한 번 경고한다 -
        // `text` 는 그 경고에만 쓴다. 체인을 세울 때(콜드 경로)만 부른다.
        std::uint32_t GetLayerPriority(NameId layer, const char* text);
        // 레이어 순서가 바뀔 때마다 오른다. 체인을 들고 있는 쪽이 이 값으로 다시 줄 세울지 안다.
        std::uint64_t GetLayerRevision() const;

        // 프로젝트의 액션 표다(D-214). 표를 복사해 두고, 체인과 폴링의 뷰가 이것을 읽는다. 없는 이름의 경고 기억도 여기서 지운다.
        // 프로젝트의 표를 따로 들어 두고, 게임이 바꾼 것(켠 세트·리바인딩)은 `ResetActions` 가 이 표로 되돌린다.
        void SetActionMap(const InputActionMap& actions);
        const InputActionMap& GetActionMap() const;
        // 게임이 바꾼 액션 상태를 프로젝트의 표로 되돌린다. 에디터가 재생을 멈출 때 부른다 - 다음 재생이 지난 재생의 세트로 시작하지 않는다.
        void ResetActions();
        bool SetActionSetEnabled(NameId set, bool enabled) noexcept override;
        bool IsActionSetEnabled(NameId set) const noexcept override;
        std::uint32_t GetActionBindingCount(InputActionId action) const noexcept override;
        bool GetActionBinding(InputActionId action, std::uint32_t index, InputBinding& out) const noexcept override;
        bool SetActionBinding(InputActionId action, std::uint32_t index, const InputBinding& binding) noexcept override;
        bool RemoveActionBinding(InputActionId action, std::uint32_t index) noexcept override;
        bool ResetActionBindings(InputActionId action) noexcept override;
        void ResetAllActionBindings() noexcept override;
        bool WriteBindingOverrides(char* buffer, std::size_t capacity, std::size_t& outSize) const noexcept override;
        bool ReadBindingOverrides(const char* text, std::size_t length) noexcept override;

        // ── 게임패드 (D-214) ──
        // 날 상태 네 자리를 이번 프레임으로 접는다: 둥근 데드존과 트리거 문턱, 누름·뗌 수(지난 폴링과 견준다), 빠진 패드는
        // 눌린 것을 모두 뗀다. `BeginFrame` 뒤에 부른다. 폴링이라 두 폴링 사이의 눌렀다 떼기는 보이지 않는다(XInput 의 한계).
        void FoldGamepads(const GamepadRawState (&raw)[MaxGamepads]);
        // 플랫폼에서 읽어 접고 진동을 적용한다. 빈 자리는 `GamepadRecheckFrames` 프레임마다만 묻는다 - 빈 자리를 묻는 것이
        // 비싸다. 창이 포커스를 잃었으면 읽지 않고 `ReleaseGamepads` 한다.
        void PollGamepads(IPlatform& platform, float deltaTime);
        // 게임이 게임패드를 받지 않는다(포커스 잃음, 에디터의 게임 뷰 밖, 내려감): 눌린 것을 떼고 축을 0 으로, 모터를 멈춘다.
        void ReleaseGamepads(IPlatform& platform);
        // 이번 프레임에 모터에 건 값이다(시험이 본다).
        float GetAppliedVibration(std::uint32_t slot, bool high) const;
        static constexpr std::uint32_t GamepadRecheckFrames = 120;

        void SetGamepadVibration(std::uint32_t slot, float low, float high, float seconds) noexcept override;
        void SetGamepadDeadzones(float stick, float trigger) noexcept override;
        void InjectTouch(std::uint32_t id, float x, float y, TouchPhase phase) noexcept override;

    private:
        InputActionDesc* FindLiveAction(InputActionId action);
        void Fold(const InputEvent& event, const InputSurfaceMapping& mapping);
        void ReleaseAll();
        // 손가락 하나를 접는다. `x`·`y` 는 이미 게임 화면 픽셀이다.
        void FoldTouch(std::uint32_t id, float x, float y, TouchPhase phase);

        InputFrame m_frame;
        InputView m_residual;
        // 체인을 따라 내려가는 뷰다. `m_residual` 과 따로 두어, 체인이 도는 동안의 폴링이 반쯤 소비된 것을 보지 않게 한다.
        InputView m_dispatch;
        InputActionMap m_actions;
        InputActionMap m_projectActions;
        Table<NameId, std::uint8_t> m_warnedSets;
        // 창이 포커스를 가졌는가. 게임패드는 이벤트가 아니라서 포커스를 따로 기억한다.
        bool m_focused = true;
        float m_stickDeadzone = 0.24f;
        float m_triggerThreshold = 0.12f;
        std::uint32_t m_gamepadRecheck[MaxGamepads] = {};
        struct Vibration
        {
            float low = 0.0f;
            float high = 0.0f;
            // 0 보다 크면 남은 초다. 0 이하이면 멈추라고 할 때까지 돈다.
            float remaining = 0.0f;
            bool timed = false;
            float appliedLow = 0.0f;
            float appliedHigh = 0.0f;
        };
        Vibration m_vibration[MaxGamepads];
        // 스크립트가 만든 손가락이다. 다음 `BeginFrame` 이 플랫폼의 이벤트 뒤에 접는다.
        struct InjectedTouch
        {
            std::uint32_t id = 0;
            float x = 0.0f;
            float y = 0.0f;
            TouchPhase phase = TouchPhase::Began;
        };
        static constexpr std::uint32_t MaxInjectedTouches = 16;
        InjectedTouch m_injected[MaxInjectedTouches];
        std::uint32_t m_injectedCount = 0;
        Array<NameId> m_layers;
        Table<NameId, std::uint8_t> m_warnedLayers;
        std::uint64_t m_layerRevision = 1;
        InputSystemContext m_systemContext;
        InputServiceContext m_serviceContext;
    };
}
