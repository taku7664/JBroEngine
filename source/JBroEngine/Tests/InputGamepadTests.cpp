#include <JBro/Input/InputSystem.h>
#include <JBro/Platform/Platform.h>
#include <JBro/Platform/WindowsPlatform.h>

#include <cmath>
#include <iostream>
#include <stdexcept>

// 게임패드(D-210, input-plan §4 의 6). 플랫폼은 날 상태만 주고, 데드존·누름·빈 자리 재확인·진동 만료는 입력 시스템이 한다 -
// 그래서 가짜 플랫폼으로 잰다. 이 기계에 패드가 꽂혀 있지 않아도 돈다.
namespace
{
    using namespace JBro;

    void Check(bool condition, const char* message)
    {
        if (false == condition)
        {
            std::cout << "test failure: " << message << '\n';
            throw std::runtime_error(message);
        }
    }

    class GamepadPlatform final : public IPlatform
    {
    public:
        bool Initialize(const JMemoryContext&) override
        {
            return true;
        }

        void Shutdown() override
        {
        }

        WindowHandle OpenPlatformWindow(const WindowDesc&) override
        {
            return {};
        }

        void ClosePlatformWindow(WindowHandle) override
        {
        }

        SurfaceHandle CreateSurface(WindowHandle) override
        {
            return {};
        }

        void PumpEvents() override
        {
        }

        JArrayView<InputEvent> GetInputEvents() const override
        {
            return {};
        }

        void WaitForEvents(std::uint32_t) override
        {
        }

        bool ShouldClose(WindowHandle) const override
        {
            return false;
        }

        bool GetWindowState(WindowHandle, WindowState&) const override
        {
            return false;
        }

        DynamicLibrary LoadDynamicLibrary(const char*) override
        {
            return {};
        }

        void* GetSymbol(DynamicLibrary, const char*) override
        {
            return nullptr;
        }

        void UnloadDynamicLibrary(DynamicLibrary) override
        {
        }

        bool PollGamepad(std::uint32_t slot, GamepadRawState& state) override
        {
            ++polls[slot];
            state = pads[slot];
            return state.connected;
        }

        void SetGamepadVibration(std::uint32_t slot, float low, float high) override
        {
            ++vibrationCalls;
            motorLow[slot] = low;
            motorHigh[slot] = high;
        }

        GamepadRawState pads[MaxGamepads] = {};
        std::uint32_t polls[MaxGamepads] = {};
        std::uint32_t vibrationCalls = 0;
        float motorLow[MaxGamepads] = {};
        float motorHigh[MaxGamepads] = {};
    };

    std::uint16_t Bit(GamepadButton button)
    {
        return static_cast<std::uint16_t>(1u << static_cast<std::uint32_t>(button));
    }

    void Frame(System::InputSystem& input, GamepadPlatform& platform, float deltaTime = 1.0f / 60.0f)
    {
        input.BeginFrame({});
        input.PollGamepads(platform, deltaTime);
    }

    void TestButtonsPressHoldAndRelease()
    {
        System::InputSystem input;
        GamepadPlatform platform;
        platform.pads[1].connected = true;
        platform.pads[1].buttons = Bit(GamepadButton::South);
        Frame(input, platform);
        const GamepadState& pad = input.GetFrame().gamepads[1];
        Check(pad.connected, "a pad in slot 1 is connected");
        Check(pad.IsDown(GamepadButton::South) && pad.IsPressed(GamepadButton::South), "a held button is pressed on its first poll");
        Check(false == input.GetFrame().gamepads[0].connected, "an empty slot is not connected");

        Frame(input, platform);
        Check(pad.IsDown(GamepadButton::South) && false == pad.IsPressed(GamepadButton::South), "and only held after that");

        platform.pads[1].buttons = 0;
        Frame(input, platform);
        Check(false == pad.IsDown(GamepadButton::South) && pad.IsReleased(GamepadButton::South), "letting go releases it");
    }

    // 둥근 데드존이다. 드리프트는 0 이고, 데드존 밖은 0..1 로 펴지며 방향은 그대로다.
    void TestSticksAndTriggersPassTheirDeadzones()
    {
        System::InputSystem input;
        GamepadPlatform platform;
        GamepadRawState& raw = platform.pads[0];
        raw.connected = true;
        raw.axes[static_cast<std::size_t>(GamepadAxis::LeftX)] = 0.1f;
        raw.axes[static_cast<std::size_t>(GamepadAxis::LeftY)] = 0.1f;
        raw.axes[static_cast<std::size_t>(GamepadAxis::LeftTrigger)] = 0.05f;
        Frame(input, platform);
        const GamepadState& pad = input.GetFrame().gamepads[0];
        Check(pad.GetAxis(GamepadAxis::LeftX) == 0.0f && pad.GetAxis(GamepadAxis::LeftY) == 0.0f, "stick drift inside the deadzone is zero");
        Check(pad.GetAxis(GamepadAxis::LeftTrigger) == 0.0f, "a trigger under its threshold is zero");

        raw.axes[static_cast<std::size_t>(GamepadAxis::LeftX)] = 1.0f;
        raw.axes[static_cast<std::size_t>(GamepadAxis::LeftY)] = 0.0f;
        raw.axes[static_cast<std::size_t>(GamepadAxis::RightY)] = -0.62f;
        raw.axes[static_cast<std::size_t>(GamepadAxis::RightTrigger)] = 1.0f;
        Frame(input, platform);
        Check(std::fabs(pad.GetAxis(GamepadAxis::LeftX) - 1.0f) < 1.0e-5f, "a full push reads 1");
        const float expected = -(0.62f - 0.24f) / (1.0f - 0.24f);
        Check(std::fabs(pad.GetAxis(GamepadAxis::RightY) - expected) < 1.0e-5f, "a partial push is spread past the deadzone");
        Check(std::fabs(pad.GetAxis(GamepadAxis::RightTrigger) - 1.0f) < 1.0e-5f, "a full trigger reads 1");

        input.SetGamepadDeadzones(0.0f, 0.0f);
        raw.axes[static_cast<std::size_t>(GamepadAxis::LeftX)] = 0.1f;
        Frame(input, platform);
        Check(std::fabs(pad.GetAxis(GamepadAxis::LeftX) - 0.1f) < 1.0e-5f, "without a deadzone the raw value comes through");
    }

    // 빠진 패드는 눌린 것을 뗀다. "떼면 멈춘다" 를 기다리는 스크립트가 영영 기다리지 않게.
    void TestAnUnpluggedPadReleasesEverything()
    {
        System::InputSystem input;
        GamepadPlatform platform;
        platform.pads[2].connected = true;
        platform.pads[2].buttons = Bit(GamepadButton::DPadLeft);
        platform.pads[2].axes[static_cast<std::size_t>(GamepadAxis::LeftX)] = 1.0f;
        Frame(input, platform);
        platform.pads[2] = {};
        Frame(input, platform);
        const GamepadState& pad = input.GetFrame().gamepads[2];
        Check(false == pad.connected, "the pad is gone");
        Check(pad.IsReleased(GamepadButton::DPadLeft) && false == pad.IsDown(GamepadButton::DPadLeft), "its held button is released");
        Check(pad.GetAxis(GamepadAxis::LeftX) == 0.0f, "and its stick is zero");
    }

    // 빈 자리를 묻는 것은 비싸다(XInput 은 수 ms). 빈 자리는 정해진 프레임마다만 묻는다.
    void TestEmptySlotsAreAskedRarely()
    {
        System::InputSystem input;
        GamepadPlatform platform;
        platform.pads[0].connected = true;
        // 물은 뒤 `GamepadRecheckFrames` 프레임을 건너뛰므로 한 주기는 그보다 한 프레임 길다. 두 주기 + 한 프레임이면 세 번이다.
        const std::uint32_t frames = (System::InputSystem::GamepadRecheckFrames + 1) * 2 + 1;
        for (std::uint32_t frame = 0; frame < frames; ++frame)
        {
            Frame(input, platform);
        }
        Check(platform.polls[0] == frames, "a connected pad is read every frame");
        Check(platform.polls[3] == 3, "an empty slot is asked only once per recheck period");

        // 꽂으면 늦어도 한 주기 안에 보인다.
        platform.pads[3].connected = true;
        for (std::uint32_t frame = 0; frame <= System::InputSystem::GamepadRecheckFrames; ++frame)
        {
            Frame(input, platform);
        }
        Check(input.GetFrame().gamepads[3].connected, "a newly plugged pad shows up within one recheck period");
    }

    void TestVibrationTimesOutAndStopsWithTheFocus()
    {
        System::InputSystem input;
        GamepadPlatform platform;
        platform.pads[0].connected = true;
        input.SetGamepadVibration(0, 0.5f, 1.0f, 0.1f);
        Frame(input, platform, 0.05f);
        Check(platform.motorLow[0] == 0.5f && platform.motorHigh[0] == 1.0f, "the motors turn with the asked strength");
        const std::uint32_t callsAfterStart = platform.vibrationCalls;
        Frame(input, platform, 0.02f);
        Check(platform.vibrationCalls == callsAfterStart, "an unchanged vibration is not sent again");
        Frame(input, platform, 0.05f);
        Check(platform.motorLow[0] == 0.0f && platform.motorHigh[0] == 0.0f, "a timed vibration stops by itself");

        // 멈추라고 할 때까지 도는 진동은 포커스를 잃으면 멈춘다.
        input.SetGamepadVibration(0, 1.0f, 0.0f, 0.0f);
        Frame(input, platform, 10.0f);
        Check(platform.motorLow[0] == 1.0f, "an untimed vibration keeps going");
        InputEvent lost;
        lost.kind = InputEventKind::FocusLost;
        input.BeginFrame({&lost, 1});
        input.PollGamepads(platform, 0.016f);
        Check(platform.motorLow[0] == 0.0f, "losing the focus stops the motors");
        Check(false == input.GetFrame().gamepads[0].connected, "and hides the pad from the game");

        InputEvent gained;
        gained.kind = InputEventKind::FocusGained;
        input.BeginFrame({&gained, 1});
        input.PollGamepads(platform, 0.016f);
        Check(input.GetFrame().gamepads[0].connected, "the pad comes back with the focus");
        Check(platform.motorLow[0] == 0.0f, "but the vibration does not start again");

        // 빠진 패드의 진동은 사라진다 - 다시 꽂았을 때 혼자 울지 않는다.
        input.SetGamepadVibration(0, 1.0f, 1.0f, 0.0f);
        Frame(input, platform);
        platform.pads[0] = {};
        Frame(input, platform);
        platform.pads[0].connected = true;
        for (std::uint32_t frame = 0; frame <= System::InputSystem::GamepadRecheckFrames; ++frame)
        {
            Frame(input, platform);
        }
        Check(input.GetFrame().gamepads[0].connected && input.GetAppliedVibration(0, false) == 0.0f,
            "a pad plugged back in does not resume an old vibration");
    }

    // 게임패드 바인딩으로 액션을 읽는다(5 단계의 평가가 6 단계의 상태에 닿는다).
    void TestGamepadBindingsDriveActions()
    {
        System::InputSystem input;
        GamepadPlatform platform;
        InputActionMap map;
        InputActionDesc& jump = map.actions[map.count++];
        jump.name = MakeNameId("PadJump");
        jump.bindingCount = 1;
        jump.bindings[0].source = InputBindingSource::GamepadButton;
        jump.bindings[0].code = static_cast<std::uint16_t>(GamepadButton::South);
        jump.bindings[0].gamepad = 1;
        InputActionDesc& move = map.actions[map.count++];
        move.name = MakeNameId("PadMove");
        move.type = InputActionType::Vector2;
        move.bindingCount = 1;
        move.bindings[0].source = InputBindingSource::GamepadStick;
        InputActionDesc& throttle = map.actions[map.count++];
        throttle.name = MakeNameId("PadThrottle");
        throttle.type = InputActionType::Float;
        throttle.bindingCount = 1;
        throttle.bindings[0].source = InputBindingSource::GamepadAxis;
        throttle.bindings[0].code = static_cast<std::uint16_t>(GamepadAxis::RightTrigger);
        input.SetActionMap(map);

        platform.pads[0].connected = true;
        platform.pads[0].buttons = Bit(GamepadButton::South);
        platform.pads[0].axes[static_cast<std::size_t>(GamepadAxis::LeftX)] = 1.0f;
        platform.pads[1].connected = true;
        platform.pads[0].axes[static_cast<std::size_t>(GamepadAxis::RightTrigger)] = 0.56f;
        platform.pads[1].axes[static_cast<std::size_t>(GamepadAxis::RightTrigger)] = 1.0f;
        Frame(input, platform);
        const InputView& view = input.GetResidualView();
        Check(false == view.IsActionDown(MakeNameId("PadJump")), "a binding for pad 1 ignores pad 0");
        const InputVector2 stick = view.GetActionVector(MakeNameId("PadMove"));
        Check(std::fabs(stick.x - 1.0f) < 1.0e-5f, "a stick binding on any pad reads the first connected pad");
        Check(std::fabs(view.GetActionFloat(MakeNameId("PadThrottle")) - (0.56f - 0.12f) / (1.0f - 0.12f)) < 1.0e-5f,
            "an axis binding on any pad reads the first connected pad");

        platform.pads[1].buttons = Bit(GamepadButton::South);
        Frame(input, platform);
        Check(view.IsActionPressed(MakeNameId("PadJump")), "pad 1's button presses its action");

        struct PadTaker final : IInputHandler
        {
            InputResult OnInput(InputView& input) override
            {
                input.Consume(InputDevice::Gamepad);
                return InputResult::Pass;
            }
        } taker;
        input.BeginDispatch();
        input.Deliver(taker);
        input.EndDispatch();
        Check(false == view.IsActionDown(MakeNameId("PadJump")) && false == view.Gamepad(1).connected,
            "a consumed gamepad drops out of the view and its actions");
    }

    // 실제 XInput 이다. 이 기계에 패드가 있든 없든 죽지 않고, 범위 밖 자리는 거절한다.
    void TestTheWindowsPlatformAnswers()
    {
        WindowsPlatform platform;
        GamepadRawState state;
        state.connected = true;
        Check(false == platform.PollGamepad(9, state) && false == state.connected, "a slot past the four is refused and cleared");
        std::uint32_t connected = 0;
        for (std::uint32_t slot = 0; slot < MaxGamepads; ++slot)
        {
            if (platform.PollGamepad(slot, state))
            {
                ++connected;
                platform.SetGamepadVibration(slot, 0.0f, 0.0f);
            }
        }
        std::cout << "  gamepads connected on this machine: " << connected << std::endl;
        platform.SetGamepadVibration(9, 1.0f, 1.0f);
    }
}

int RunInputGamepadTests()
{
    TestButtonsPressHoldAndRelease();
    TestSticksAndTriggersPassTheirDeadzones();
    TestAnUnpluggedPadReleasesEverything();
    TestEmptySlotsAreAskedRarely();
    TestVibrationTimesOutAndStopsWithTheFocus();
    TestGamepadBindingsDriveActions();
    TestTheWindowsPlatformAnswers();
    std::cout << "Input gamepad tests passed.\n";
    return 0;
}
