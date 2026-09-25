#include <JBro/Input/InputSystem.h>

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

    void InputSystem::BeginFrame(JArrayView<InputEvent> events, const InputSurfaceMapping& mapping)
    {
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

    void InputSystem::Fold(const InputEvent& event, const InputSurfaceMapping& mapping)
    {
        switch (event.kind)
        {
        case InputEventKind::KeyDown:
            m_frame.keyboard.modifiers = event.modifiers;
            // 자동 반복은 누름이 아니다. 글자 칸이 쓰는 반복은 Text 이벤트가 따로 준다.
            if (false == event.repeat)
            {
                if (ButtonState* key = FindKey(m_frame.keyboard, event.key))
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
            break;

        case InputEventKind::FocusGained:
            break;
        }
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
