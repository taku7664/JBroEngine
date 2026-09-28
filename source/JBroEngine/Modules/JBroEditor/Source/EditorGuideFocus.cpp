#include <JBro/Editor/EditorGuideFocus.h>

#include <cassert>

namespace JBro
{
    namespace
    {
        constexpr std::uint32_t TouchBitCount = 32;

        std::uint32_t ButtonBit(MouseButton button)
        {
            return 1u << static_cast<std::uint32_t>(button);
        }

        InputEvent MakeMove(float x, float y)
        {
            InputEvent move;
            move.kind = InputEventKind::MouseMove;
            move.x = x;
            move.y = y;
            return move;
        }
    }

    bool GuideFocusPath::Push(const GuideFocusTarget& target)
    {
        if (false == target.IsValid() || count >= Capacity)
        {
            return false;
        }
        targets[count] = target;
        ++count;
        return true;
    }

    bool EditorGuideFocus::Begin(const GuideFocusPath& path)
    {
        if (path.IsEmpty())
        {
            return false;
        }
        m_path = path;
        m_active = true;
        m_keyboardAllowed = false;
        m_skipRequested = false;
        ClearAllowedRects();
        // 눌린 버튼은 그대로 둔다. 켜기 전에 누른 것은 ImGui 가 이미 누름을 보았으므로 그 뗌도 넘겨야 한다 -
        // 꺼져 있는 동안에도 거르기가 버튼을 세는 까닭이 이것이다.
        return true;
    }

    void EditorGuideFocus::End()
    {
        m_active = false;
        m_path = {};
        m_keyboardAllowed = false;
        m_skipRequested = false;
        ClearAllowedRects();
        // 눌린 버튼과 `m_pointerHidden` 은 남긴다 - 뗌은 계속 넘겨야 하고, 다음 거르기가 마우스를 실제 자리로 되돌린다.
    }

    void EditorGuideFocus::ClearAllowedRects() noexcept
    {
        m_allowedCount = 0;
    }

    bool EditorGuideFocus::AddAllowedRect(const Rect& rect)
    {
        if (m_allowedCount >= AllowedRectCapacity)
        {
            return false;
        }
        m_allowed[m_allowedCount] = rect;
        ++m_allowedCount;
        return true;
    }

    const Rect& EditorGuideFocus::GetAllowedRect(std::uint32_t index) const
    {
        assert(index < m_allowedCount);
        return m_allowed[index];
    }

    bool EditorGuideFocus::IsAllowed(const Vector2& point) const noexcept
    {
        for (std::uint32_t index = 0; index < m_allowedCount; ++index)
        {
            // 뒤집힌 사각형은 `Contains` 가 아무것도 담지 않는다 - 그리지 않은 위젯의 빈 사각형이 한 점을 열어 두지 않는다.
            if (m_allowed[index].Contains(point))
            {
                return true;
            }
        }
        return false;
    }

    bool EditorGuideFocus::IsPointerAllowed() const noexcept
    {
        return m_pointerKnown && IsAllowed(m_pointer);
    }

    void EditorGuideFocus::ResetPointerState() noexcept
    {
        m_passedButtons = 0;
        m_passedTouches = 0;
    }

    void EditorGuideFocus::TrackPassed(const InputEvent& event) noexcept
    {
        switch (event.kind)
        {
        case InputEventKind::MouseMove:
            m_pointer = Vector2{ event.x, event.y };
            m_pointerKnown = true;
            break;
        case InputEventKind::MouseButtonDown:
            m_passedButtons |= ButtonBit(event.button);
            break;
        case InputEventKind::MouseButtonUp:
            m_passedButtons &= ~ButtonBit(event.button);
            break;
        case InputEventKind::FocusLost:
            ResetPointerState();
            break;
        case InputEventKind::TouchBegan:
            if (event.codePoint < TouchBitCount)
            {
                m_passedTouches |= 1u << event.codePoint;
            }
            break;
        case InputEventKind::TouchEnded:
        case InputEventKind::TouchCancelled:
            if (event.codePoint < TouchBitCount)
            {
                m_passedTouches &= ~(1u << event.codePoint);
            }
            break;
        default:
            break;
        }
    }

    bool EditorGuideFocus::ConsumeSkipRequest() noexcept
    {
        const bool requested = m_skipRequested;
        m_skipRequested = false;
        return requested;
    }

    void EditorGuideFocus::FilterInput(JArrayView<InputEvent> events, Array<InputEvent>& out)
    {
        out.Clear();

        if (false == m_active)
        {
            // 꺼진 뒤 처음이다. 숨겼던 마우스를 되돌리지 않으면 사용자가 움직일 때까지 ImGui 는 마우스가 없다고 본다.
            if (m_pointerHidden && m_pointerKnown)
            {
                out.Add(MakeMove(m_pointer.x, m_pointer.y));
            }
            m_pointerHidden = false;
            for (std::uint32_t index = 0; index < events.size; ++index)
            {
                TrackPassed(events.data[index]);
                out.Add(events.data[index]);
            }
            return;
        }

        for (std::uint32_t index = 0; index < events.size; ++index)
        {
            const InputEvent& event = events.data[index];
            switch (event.kind)
            {
            case InputEventKind::MouseMove:
            {
                m_pointer = Vector2{ event.x, event.y };
                m_pointerKnown = true;
                // 안에서 눌러 시작한 끌기는 밖으로 나가도 실제 자리를 받아야 한다 - 슬라이더가 멈춘다.
                if (m_passedButtons != 0 || IsAllowed(m_pointer))
                {
                    out.Add(event);
                    m_pointerHidden = false;
                }
                else
                {
                    out.Add(MakeMove(HiddenPointer, HiddenPointer));
                    m_pointerHidden = true;
                }
                break;
            }

            case InputEventKind::MouseButtonDown:
                if (IsPointerAllowed())
                {
                    m_passedButtons |= ButtonBit(event.button);
                    out.Add(event);
                }
                break;

            case InputEventKind::MouseButtonUp:
                // 넘긴 누름의 뗌만 넘긴다. 넘기지 않은 누름의 뗌은 ImGui 가 모르는 버튼을 뗀다.
                if ((m_passedButtons & ButtonBit(event.button)) != 0)
                {
                    m_passedButtons &= ~ButtonBit(event.button);
                    out.Add(event);
                }
                break;

            case InputEventKind::MouseWheel:
                if (IsPointerAllowed())
                {
                    out.Add(event);
                }
                break;

            case InputEventKind::KeyDown:
                if (event.key == Key::Escape)
                {
                    if (false == event.repeat)
                    {
                        m_skipRequested = true;
                    }
                    break;
                }
                if (m_keyboardAllowed)
                {
                    out.Add(event);
                }
                break;

            case InputEventKind::KeyUp:
                // 늘 넘긴다. 막을 켜기 전에 누른 키가 눌린 채로 남으면 안 된다.
                out.Add(event);
                break;

            case InputEventKind::Text:
                if (m_keyboardAllowed)
                {
                    out.Add(event);
                }
                break;

            case InputEventKind::FocusGained:
            case InputEventKind::FocusLost:
                if (event.kind == InputEventKind::FocusLost)
                {
                    ResetPointerState();
                }
                out.Add(event);
                break;

            case InputEventKind::TouchBegan:
                if (event.codePoint < TouchBitCount && IsAllowed(Vector2{ event.x, event.y }))
                {
                    m_passedTouches |= 1u << event.codePoint;
                    out.Add(event);
                }
                break;

            case InputEventKind::TouchMoved:
                if (event.codePoint < TouchBitCount && (m_passedTouches & (1u << event.codePoint)) != 0)
                {
                    out.Add(event);
                }
                break;

            case InputEventKind::TouchEnded:
            case InputEventKind::TouchCancelled:
                if (event.codePoint < TouchBitCount && (m_passedTouches & (1u << event.codePoint)) != 0)
                {
                    m_passedTouches &= ~(1u << event.codePoint);
                    out.Add(event);
                }
                break;
            }
        }
    }
}
