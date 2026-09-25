#include <JBro/Input/InputSystem.h>

#include <JBro/Core/Log.h>

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace JBro::System
{
    namespace
    {
        void Press(ButtonState& button)
        {
            // 이미 눌려 있으면 누름이 아니다. 뗌을 잃은 채 다시 온 KeyDown 을 두 번 누른 것으로 세지 않는다.
            if (button.down)
            {
                return;
            }
            button.down = true;
            if (button.pressCount < 255)
            {
                ++button.pressCount;
            }
        }

        void Release(ButtonState& button)
        {
            if (false == button.down)
            {
                return;
            }
            button.down = false;
            if (button.releaseCount < 255)
            {
                ++button.releaseCount;
            }
        }

        ButtonState* FindKey(KeyboardState& keyboard, Key key)
        {
            const std::size_t index = static_cast<std::size_t>(key);
            if (key == Key::Unknown || index >= KeyCount)
            {
                return nullptr;
            }
            return &keyboard.keys[index];
        }

        ButtonState* FindButton(MouseState& mouse, MouseButton button)
        {
            const std::size_t index = static_cast<std::size_t>(button);
            if (index >= MouseButtonCount)
            {
                return nullptr;
            }
            return &mouse.buttons[index];
        }
    }

    InputSystem::InputSystem()
    {
        m_residual.m_frame = &m_frame;
        m_dispatch.m_frame = &m_frame;
        m_residual.m_actions = &m_actions;
        m_dispatch.m_actions = &m_actions;
        m_systemContext.Input = this;
        SetLayerOrder({});
    }

    void InputSystem::BeginFrame(JArrayView<InputEvent> events, const InputSurfaceMapping& mapping)
    {
        // 남은 입력은 이번 프레임 전체에서 다시 시작한다. 레이어 체인이 돌면 그 끝에서 다시 정해진다.
        m_residual.m_consumed = 0;
        m_residual.m_pendingConsumed = 0;

        // 지금 눌림과 위치는 프레임을 넘어 이어진다. 프레임 하나의 것만 비운다.
        for (ButtonState& key : m_frame.keyboard.keys)
        {
            key.pressCount = 0;
            key.releaseCount = 0;
        }
        for (ButtonState& button : m_frame.mouse.buttons)
        {
            button.pressCount = 0;
            button.releaseCount = 0;
        }
        m_frame.keyboard.textLength = 0;
        m_frame.mouse.deltaX = 0.0f;
        m_frame.mouse.deltaY = 0.0f;
        m_frame.mouse.wheelX = 0.0f;
        m_frame.mouse.wheelY = 0.0f;
        for (GamepadState& pad : m_frame.gamepads)
        {
            for (ButtonState& button : pad.buttons)
            {
                button.pressCount = 0;
                button.releaseCount = 0;
            }
        }

        if (events.data == nullptr)
        {
            return;
        }
        for (std::uint32_t index = 0; index < events.size; ++index)
        {
            Fold(events.data[index], mapping);
        }
    }

    const InputFrame& InputSystem::GetFrame() const
    {
        return m_frame;
    }

    const InputView& InputSystem::GetResidualView() const noexcept
    {
        return m_residual;
    }

    void InputSystem::BeginDispatch()
    {
        m_dispatch.m_consumed = 0;
        m_dispatch.m_pendingConsumed = 0;
    }

    bool InputSystem::Deliver(IInputHandler& handler)
    {
        m_dispatch.m_pendingConsumed = 0;
        const InputResult result = handler.OnInput(m_dispatch);
        // 이 핸들러가 가져간 것은 돌아온 뒤에야 아래에 걸린다. 핸들러 자신은 끝까지 읽을 수 있어야 한다.
        m_dispatch.m_consumed |= m_dispatch.m_pendingConsumed;
        m_dispatch.m_pendingConsumed = 0;
        if (result == InputResult::Block)
        {
            m_dispatch.m_consumed = InputView::AllDevices;
            return true;
        }
        return false;
    }

    void InputSystem::EndDispatch()
    {
        m_residual.m_consumed = m_dispatch.m_consumed;
        m_residual.m_pendingConsumed = 0;
    }

    void InputSystem::SetLayerOrder(JArrayView<NameId> layers)
    {
        m_layers.Clear();
        if (layers.data == nullptr || layers.size == 0)
        {
            // 기존 엔진의 기본 밴드와 같다. 위가 먼저 받는다.
            m_layers.Add(MakeNameId("Modal"));
            m_layers.Add(MakeNameId("UI"));
            m_layers.Add(MakeNameId("Game"));
            m_layers.Add(MakeNameId("World"));
            m_layers.Add(MakeNameId("Debug"));
        }
        else
        {
            for (std::uint32_t index = 0; index < layers.size; ++index)
            {
                m_layers.Add(layers.data[index]);
            }
        }
        m_warnedLayers.Clear();
        ++m_layerRevision;
    }

    std::uint32_t InputSystem::GetLayerPriority(NameId layer, const char* text)
    {
        for (std::size_t index = 0; index < m_layers.Size(); ++index)
        {
            if (m_layers[index] == layer)
            {
                return static_cast<std::uint32_t>(index);
            }
        }
        // 없는 레이어는 막지 않는다 - 컴파일은 되고 맨 아래에서 받는다. 대신 한 번은 말한다.
        if (m_warnedLayers.TryAdd(layer, std::uint8_t{1}))
        {
            Log::Write(LogLevel::Warning, "input", "unknown input layer \"%s\"; it receives input after every known layer",
                text != nullptr ? text : "?");
        }
        return static_cast<std::uint32_t>(m_layers.Size());
    }

    void InputSystem::SetActionMap(const InputActionMap& actions)
    {
        m_actions = actions;
        m_actions.warnedCount = 0;
    }

    const InputActionMap& InputSystem::GetActionMap() const
    {
        return m_actions;
    }

    std::uint64_t InputSystem::GetLayerRevision() const
    {
        return m_layerRevision;
    }

    const InputSystemContext& InputSystem::GetSystemContext() const
    {
        return m_systemContext;
    }

    const InputServiceContext& InputSystem::GetServiceContext() const
    {
        return m_serviceContext;
    }

    void InputSystem::Fold(const InputEvent& event, const InputSurfaceMapping& mapping)
    {
        switch (event.kind)
        {
        case InputEventKind::KeyDown:
            m_frame.keyboard.modifiers = event.modifiers;
            if (ButtonState* key = FindKey(m_frame.keyboard, event.key))
            {
                // 자동 반복은 누름이 아니다. 글자 칸이 쓰는 반복은 Text 이벤트가 따로 준다.
                // **그래도 키가 눌려 있다는 사실은 알려 준다.** 키를 누른 채 창으로 돌아오면 첫 누름은
                // 다른 창이 받았고 이 창에는 반복만 온다 - 무시하면 사용자가 누르고 있는 키가 떼어진 것으로 보인다.
                if (event.repeat)
                {
                    key->down = true;
                }
                else
                {
                    Press(*key);
                }
            }
            break;

        case InputEventKind::KeyUp:
            m_frame.keyboard.modifiers = event.modifiers;
            if (ButtonState* key = FindKey(m_frame.keyboard, event.key))
            {
                Release(*key);
            }
            break;

        case InputEventKind::Text:
            if (m_frame.keyboard.textLength < MaxTextPerFrame)
            {
                m_frame.keyboard.text[m_frame.keyboard.textLength] = event.codePoint;
                ++m_frame.keyboard.textLength;
            }
            break;

        case InputEventKind::MouseMove:
        {
            const float x = (event.x - mapping.originX) * mapping.scaleX;
            const float y = (event.y - mapping.originY) * mapping.scaleY;
            // 첫 위치는 이동이 아니다. (0, 0) 에서 거기까지 뛴 것으로 세면 첫 프레임에 화면이 튄다.
            if (m_frame.mouse.hasPosition)
            {
                m_frame.mouse.deltaX += x - m_frame.mouse.x;
                m_frame.mouse.deltaY += y - m_frame.mouse.y;
            }
            m_frame.mouse.x = x;
            m_frame.mouse.y = y;
            m_frame.mouse.hasPosition = true;
            break;
        }

        case InputEventKind::MouseButtonDown:
            m_frame.keyboard.modifiers = event.modifiers;
            if (ButtonState* button = FindButton(m_frame.mouse, event.button))
            {
                Press(*button);
            }
            break;

        case InputEventKind::MouseButtonUp:
            m_frame.keyboard.modifiers = event.modifiers;
            if (ButtonState* button = FindButton(m_frame.mouse, event.button))
            {
                Release(*button);
            }
            break;

        case InputEventKind::MouseWheel:
            m_frame.mouse.wheelX += event.x;
            m_frame.mouse.wheelY += event.y;
            break;

        case InputEventKind::FocusLost:
            ReleaseAll();
            m_focused = false;
            break;

        case InputEventKind::FocusGained:
            m_focused = true;
            break;
        }
    }

    namespace
    {
        // 둥근 데드존이다(기존 엔진과 같다): 길이가 데드존 안이면 0, 밖이면 데드존..1 을 0..1 로 편다. 방향은 그대로다.
        void ApplyStickDeadzone(float rawX, float rawY, float deadzone, float& x, float& y)
        {
            const float length = std::sqrt(rawX * rawX + rawY * rawY);
            if (length <= deadzone || length <= 0.0f)
            {
                x = 0.0f;
                y = 0.0f;
                return;
            }
            const float clamped = length > 1.0f ? 1.0f : length;
            const float scale = ((clamped - deadzone) / (1.0f - deadzone)) / length;
            x = rawX * scale;
            y = rawY * scale;
        }

        float ApplyTriggerThreshold(float raw, float threshold)
        {
            if (raw <= threshold)
            {
                return 0.0f;
            }
            const float value = (raw - threshold) / (1.0f - threshold);
            return value > 1.0f ? 1.0f : value;
        }
    }

    void InputSystem::FoldGamepads(const GamepadRawState (&raw)[MaxGamepads])
    {
        for (std::uint32_t slot = 0; slot < MaxGamepads; ++slot)
        {
            GamepadState& pad = m_frame.gamepads[slot];
            const GamepadRawState& source = raw[slot];
            if (false == source.connected)
            {
                // 빠진 패드는 눌린 것을 뗀 것으로 접는다(키보드의 포커스 잃음과 같다). 모터도 멈춘다.
                for (ButtonState& button : pad.buttons)
                {
                    Release(button);
                }
                for (float& axis : pad.axes)
                {
                    axis = 0.0f;
                }
                pad.connected = false;
                m_vibration[slot].low = 0.0f;
                m_vibration[slot].high = 0.0f;
                m_vibration[slot].timed = false;
                continue;
            }
            pad.connected = true;
            for (std::size_t index = 0; index < GamepadButtonCount; ++index)
            {
                const bool down = (source.buttons & (1u << index)) != 0;
                if (down)
                {
                    Press(pad.buttons[index]);
                }
                else
                {
                    Release(pad.buttons[index]);
                }
            }
            const auto axis = [&source](GamepadAxis which)
            {
                return source.axes[static_cast<std::size_t>(which)];
            };
            ApplyStickDeadzone(axis(GamepadAxis::LeftX), axis(GamepadAxis::LeftY), m_stickDeadzone,
                pad.axes[static_cast<std::size_t>(GamepadAxis::LeftX)], pad.axes[static_cast<std::size_t>(GamepadAxis::LeftY)]);
            ApplyStickDeadzone(axis(GamepadAxis::RightX), axis(GamepadAxis::RightY), m_stickDeadzone,
                pad.axes[static_cast<std::size_t>(GamepadAxis::RightX)], pad.axes[static_cast<std::size_t>(GamepadAxis::RightY)]);
            pad.axes[static_cast<std::size_t>(GamepadAxis::LeftTrigger)] =
                ApplyTriggerThreshold(axis(GamepadAxis::LeftTrigger), m_triggerThreshold);
            pad.axes[static_cast<std::size_t>(GamepadAxis::RightTrigger)] =
                ApplyTriggerThreshold(axis(GamepadAxis::RightTrigger), m_triggerThreshold);
        }
    }

    void InputSystem::PollGamepads(IPlatform& platform, float deltaTime)
    {
        if (false == m_focused)
        {
            ReleaseGamepads(platform);
            return;
        }
        GamepadRawState raw[MaxGamepads];
        for (std::uint32_t slot = 0; slot < MaxGamepads; ++slot)
        {
            // 빈 자리는 가끔만 묻는다. 꽂으면 길어야 그만큼 뒤에 보인다(60 fps 에서 2 초).
            if (false == m_frame.gamepads[slot].connected && m_gamepadRecheck[slot] > 0)
            {
                --m_gamepadRecheck[slot];
                continue;
            }
            if (false == platform.PollGamepad(slot, raw[slot]))
            {
                raw[slot] = {};
                m_gamepadRecheck[slot] = GamepadRecheckFrames;
            }
        }
        FoldGamepads(raw);

        for (std::uint32_t slot = 0; slot < MaxGamepads; ++slot)
        {
            Vibration& vibration = m_vibration[slot];
            if (vibration.timed)
            {
                vibration.remaining -= deltaTime;
                if (vibration.remaining <= 0.0f)
                {
                    vibration.low = 0.0f;
                    vibration.high = 0.0f;
                    vibration.timed = false;
                }
            }
            // 바뀔 때만 건다. 매 프레임 거는 것은 XInput 에 헛일이다.
            if (m_frame.gamepads[slot].connected
                && (vibration.low != vibration.appliedLow || vibration.high != vibration.appliedHigh))
            {
                platform.SetGamepadVibration(slot, vibration.low, vibration.high);
                vibration.appliedLow = vibration.low;
                vibration.appliedHigh = vibration.high;
            }
            else if (false == m_frame.gamepads[slot].connected)
            {
                vibration.appliedLow = 0.0f;
                vibration.appliedHigh = 0.0f;
            }
        }
    }

    void InputSystem::ReleaseGamepads(IPlatform& platform)
    {
        GamepadRawState none[MaxGamepads];
        FoldGamepads(none);
        for (std::uint32_t slot = 0; slot < MaxGamepads; ++slot)
        {
            Vibration& vibration = m_vibration[slot];
            // 포커스를 잃으면 폴링을 멈춘다 - 모터는 직접 멈춰야 한다(알트탭한 뒤에도 울리던 것을 기존 엔진이 고쳤다).
            if (vibration.appliedLow != 0.0f || vibration.appliedHigh != 0.0f)
            {
                platform.SetGamepadVibration(slot, 0.0f, 0.0f);
            }
            vibration = {};
            // 돌아오면 곧바로 다시 묻는다.
            m_gamepadRecheck[slot] = 0;
        }
    }

    float InputSystem::GetAppliedVibration(std::uint32_t slot, bool high) const
    {
        if (slot >= MaxGamepads)
        {
            return 0.0f;
        }
        return high ? m_vibration[slot].appliedHigh : m_vibration[slot].appliedLow;
    }

    void InputSystem::SetGamepadVibration(std::uint32_t slot, float low, float high, float seconds) noexcept
    {
        if (slot >= MaxGamepads)
        {
            return;
        }
        Vibration& vibration = m_vibration[slot];
        vibration.low = std::clamp(low, 0.0f, 1.0f);
        vibration.high = std::clamp(high, 0.0f, 1.0f);
        vibration.timed = seconds > 0.0f;
        vibration.remaining = seconds;
    }

    void InputSystem::SetGamepadDeadzones(float stick, float trigger) noexcept
    {
        m_stickDeadzone = std::clamp(stick, 0.0f, 0.95f);
        m_triggerThreshold = std::clamp(trigger, 0.0f, 0.95f);
    }

    void InputSystem::ReleaseAll()
    {
        // **조용히 지우지 않고 뗀 것으로 접는다.** 창이 포커스를 잃으면 뗌 이벤트가 오지 않는다.
        // 지우기만 하면 "떼면 멈춘다" 를 기다리는 스크립트가 영영 멈추지 못한다.
        for (ButtonState& key : m_frame.keyboard.keys)
        {
            Release(key);
        }
        for (ButtonState& button : m_frame.mouse.buttons)
        {
            Release(button);
        }
        m_frame.keyboard.modifiers = KeyModifierNone;
        // 창 밖으로 나간 커서의 자리는 모른다. 다시 받을 때 튀지 않게 첫 위치로 다룬다.
        m_frame.mouse.hasPosition = false;
    }
}
