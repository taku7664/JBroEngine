#include <JBro/InputTypes/InputView.h>

#include <JBro/Core/Log.h>

#include <cmath>
#include <cstddef>
#include <JBro/Types/Float.h>
#include <JBro/Types/UInt.h>
#include <JBro/Types/Bool.h>

// 액션 평가(D-214). 부를 때 그 자리의 뷰로 계산한다 - 미리 평가해 두면 위에서 소비한 장치가 액션에 남는다.
// 바인딩 몇 개를 도는 일이라 싸고, 할당도 문자열도 없다(§9).
namespace JBro
{
    namespace
    {
        // Bool 액션이 축을 "눌림" 으로 보는 문턱이다.
        constexpr Float AxisPressThreshold = 0.5f;

        struct ButtonSample
        {
            Bool down = false;
            Bool pressed = false;
            Bool released = false;
        };

        void Merge(ButtonSample& into, const ButtonState& button)
        {
            into.down = into.down || button.down;
            into.pressed = into.pressed || button.pressCount > 0;
            into.released = into.released || button.releaseCount > 0;
        }

        // 버튼 바인딩 하나의 표본이다. 패드 -1 은 어느 패드든이다.
        ButtonSample SampleButton(const InputView& view, const InputBinding& binding)
        {
            ButtonSample sample;
            switch (binding.source)
            {
            case InputBindingSource::Key:
                if (binding.code < KeyCount)
                {
                    Merge(sample, view.Keyboard().keys[binding.code]);
                }
                break;
            case InputBindingSource::MouseButton:
                if (binding.code < MouseButtonCount)
                {
                    Merge(sample, view.Mouse().buttons[binding.code]);
                }
                break;
            case InputBindingSource::GamepadButton:
                if (binding.code < GamepadButtonCount)
                {
                    for (UInt32 pad = 0; pad < MaxGamepads; ++pad)
                    {
                        if (binding.gamepad >= 0 && static_cast<std::uint32_t>(binding.gamepad) != pad)
                        {
                            continue;
                        }
                        Merge(sample, view.Gamepad(pad).buttons[binding.code]);
                    }
                }
                break;
            default:
                break;
            }
            return sample;
        }

        // 축·스틱이 읽을 패드다. -1 이면 연결된 첫 패드, 없으면 nullptr.
        const GamepadState* PadFor(const InputView& view, const InputBinding& binding)
        {
            if (binding.gamepad >= 0)
            {
                if (static_cast<std::uint32_t>(binding.gamepad) >= MaxGamepads)
                {
                    return nullptr;
                }
                const GamepadState& pad = view.Gamepad(static_cast<std::uint32_t>(binding.gamepad));
                return pad.connected ? &pad : nullptr;
            }
            for (UInt32 index = 0; index < MaxGamepads; ++index)
            {
                const GamepadState& pad = view.Gamepad(index);
                if (pad.connected)
                {
                    return &pad;
                }
            }
            return nullptr;
        }

        Float SampleAxis(const InputView& view, const InputBinding& binding)
        {
            const GamepadState* pad = PadFor(view, binding);
            if (pad == nullptr || binding.code >= GamepadAxisCount)
            {
                return 0.0f;
            }
            return pad->axes[binding.code];
        }

        InputVector2 SampleStick(const InputView& view, const InputBinding& binding)
        {
            const GamepadState* pad = PadFor(view, binding);
            if (pad == nullptr)
            {
                return {};
            }
            const Bool right = binding.code == 1;
            const GamepadAxis x = right ? GamepadAxis::RightX : GamepadAxis::LeftX;
            const GamepadAxis y = right ? GamepadAxis::RightY : GamepadAxis::LeftY;
            return {pad->GetAxis(x), pad->GetAxis(y)};
        }

        Bool IsButtonSource(InputBindingSource source)
        {
            return source == InputBindingSource::Key || source == InputBindingSource::MouseButton
                || source == InputBindingSource::GamepadButton;
        }

        void WarnUnknownAction(InputActionMap* map, InputActionId action)
        {
            if (map == nullptr)
            {
                return;
            }
            for (UInt32 index = 0; index < map->warnedCount; ++index)
            {
                if (map->warned[index] == action)
                {
                    return;
                }
            }
            const UInt32 capacity = static_cast<std::uint32_t>(sizeof(map->warned) / sizeof(map->warned[0]));
            if (map->warnedCount >= capacity)
            {
                return;
            }
            map->warned[map->warnedCount] = action;
            ++map->warnedCount;
            // 경고 경로(콜드)다. 원문이 이름표에 있으면 그것으로 말한다.
            const char* text = NameTable::Get().Resolve(action);
            Log::Write(LogLevel::Warning, "input", "the project has no input action \"%s\"; it reads as zero",
                text != nullptr ? text : "?");
        }
    }

    InputActionValue InputView::Action(InputActionId action) const
    {
        InputActionValue value;
        const InputActionDesc* desc = m_actions != nullptr ? m_actions->Find(action) : nullptr;
        if (desc == nullptr)
        {
            WarnUnknownAction(m_actions, action);
            return value;
        }
        // 꺼진 세트의 액션이다. 없는 액션이 아니니 말하지 않는다.
        if (false == m_actions->IsSetActive(desc->set))
        {
            return value;
        }

        ButtonSample buttons;
        const UInt32 count = desc->bindingCount < MaxInputBindingsPerAction
            ? UInt32(desc->bindingCount) : MaxInputBindingsPerAction;
        Float scalar = 0.0f;
        InputVector2 vector;
        for (UInt32 index = 0; index < count; ++index)
        {
            const InputBinding& binding = desc->bindings[index];
            if (IsButtonSource(binding.source))
            {
                const ButtonSample sample = SampleButton(*this, binding);
                buttons.down = buttons.down || sample.down;
                buttons.pressed = buttons.pressed || sample.pressed;
                buttons.released = buttons.released || sample.released;
                if (sample.down)
                {
                    scalar = 1.0f;
                    switch (binding.composite)
                    {
                    case InputComposite::Up:
                        vector.y += 1.0f;
                        break;
                    case InputComposite::Down:
                        vector.y -= 1.0f;
                        break;
                    case InputComposite::Left:
                        vector.x -= 1.0f;
                        break;
                    case InputComposite::Right:
                        vector.x += 1.0f;
                        break;
                    default:
                        break;
                    }
                }
            }
            else if (binding.source == InputBindingSource::GamepadAxis)
            {
                const Float axis = SampleAxis(*this, binding);
                if (std::fabs(axis) > std::fabs(scalar))
                {
                    scalar = axis;
                }
                buttons.down = buttons.down || std::fabs(axis) >= AxisPressThreshold;
            }
            else if (binding.source == InputBindingSource::GamepadStick)
            {
                const InputVector2 stick = SampleStick(*this, binding);
                vector.x += stick.x;
                vector.y += stick.y;
                const Float length = std::sqrt(stick.x * stick.x + stick.y * stick.y);
                if (length > std::fabs(scalar))
                {
                    scalar = length;
                }
                buttons.down = buttons.down || length >= AxisPressThreshold;
            }
        }

        switch (desc->type)
        {
        case InputActionType::Bool:
            value.x = buttons.down ? 1.0f : 0.0f;
            value.down = buttons.down;
            break;
        case InputActionType::Float:
            value.x = scalar;
            value.down = scalar != 0.0f;
            break;
        case InputActionType::Vector2:
        {
            // 대각선이 빨라지지 않게 길이 1 로 자른다.
            const Float lengthSquared = vector.x * vector.x + vector.y * vector.y;
            if (lengthSquared > 1.0f)
            {
                const Float inverse = 1.0f / std::sqrt(lengthSquared);
                vector.x *= inverse;
                vector.y *= inverse;
            }
            value.x = vector.x;
            value.y = vector.y;
            value.down = lengthSquared > 0.0f;
            break;
        }
        }
        value.pressed = buttons.pressed;
        // 다른 바인딩이 아직 누르고 있으면 뗀 것이 아니다(두 키로 묶은 점프에서 한쪽만 뗐다).
        value.released = buttons.released && false == value.down;
        return value;
    }

    Bool InputView::IsActionDown(InputActionId action) const
    {
        return Action(action).down;
    }

    Bool InputView::IsActionPressed(InputActionId action) const
    {
        return Action(action).pressed;
    }

    Bool InputView::IsActionReleased(InputActionId action) const
    {
        return Action(action).released;
    }

    Float InputView::GetActionFloat(InputActionId action) const
    {
        return Action(action).x;
    }

    InputVector2 InputView::GetActionVector(InputActionId action) const
    {
        const InputActionValue value = Action(action);
        return {value.x, value.y};
    }
}
