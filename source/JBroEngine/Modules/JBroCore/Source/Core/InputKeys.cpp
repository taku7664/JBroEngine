#include <JBro/Core/InputKeys.h>

#include <cstddef>
#include <cstring>

// 이름 표는 열거자 차례 그대로다(D-214). 열거자를 더하거나 빼면 아래 static_assert 가 운다 - 차례를 바꾸는 것은
// 잡지 못하므로 테스트(`InputActionTests`)가 이름마다 되돌아오는지 잰다.
namespace JBro
{
    namespace
    {
        constexpr const char* KeyNames[] =
        {
            "Unknown",
            "Tab",
            "Left",
            "Right",
            "Up",
            "Down",
            "PageUp",
            "PageDown",
            "Home",
            "End",
            "Insert",
            "Delete",
            "Backspace",
            "Space",
            "Enter",
            "Escape",
            "LeftControl",
            "LeftShift",
            "LeftAlt",
            "LeftSuper",
            "RightControl",
            "RightShift",
            "RightAlt",
            "RightSuper",
            "Menu",
            "Digit0",
            "Digit1",
            "Digit2",
            "Digit3",
            "Digit4",
            "Digit5",
            "Digit6",
            "Digit7",
            "Digit8",
            "Digit9",
            "A",
            "B",
            "C",
            "D",
            "E",
            "F",
            "G",
            "H",
            "I",
            "J",
            "K",
            "L",
            "M",
            "N",
            "O",
            "P",
            "Q",
            "R",
            "S",
            "T",
            "U",
            "V",
            "W",
            "X",
            "Y",
            "Z",
            "F1",
            "F2",
            "F3",
            "F4",
            "F5",
            "F6",
            "F7",
            "F8",
            "F9",
            "F10",
            "F11",
            "F12",
            "Apostrophe",
            "Comma",
            "Minus",
            "Period",
            "Slash",
            "Semicolon",
            "Equal",
            "LeftBracket",
            "Backslash",
            "RightBracket",
            "GraveAccent",
            "CapsLock",
            "ScrollLock",
            "NumLock",
            "PrintScreen",
            "Pause",
            "Keypad0",
            "Keypad1",
            "Keypad2",
            "Keypad3",
            "Keypad4",
            "Keypad5",
            "Keypad6",
            "Keypad7",
            "Keypad8",
            "Keypad9",
            "KeypadDecimal",
            "KeypadDivide",
            "KeypadMultiply",
            "KeypadSubtract",
            "KeypadAdd",
            "KeypadEnter",
        };
        static_assert(sizeof(KeyNames) / sizeof(KeyNames[0]) == static_cast<std::size_t>(Key::Count),
            "the Key name table must follow the enum");

        constexpr const char* MouseButtonNames[] =
        {
            "Left",
            "Right",
            "Middle",
            "Extra1",
            "Extra2",
        };
        static_assert(sizeof(MouseButtonNames) / sizeof(MouseButtonNames[0]) == static_cast<std::size_t>(MouseButton::Count),
            "the MouseButton name table must follow the enum");

        constexpr const char* GamepadButtonNames[] =
        {
            "South",
            "East",
            "West",
            "North",
            "LeftShoulder",
            "RightShoulder",
            "Start",
            "Select",
            "DPadUp",
            "DPadDown",
            "DPadLeft",
            "DPadRight",
            "LeftThumb",
            "RightThumb",
        };
        static_assert(sizeof(GamepadButtonNames) / sizeof(GamepadButtonNames[0]) == static_cast<std::size_t>(GamepadButton::Count),
            "the GamepadButton name table must follow the enum");

        constexpr const char* GamepadAxisNames[] =
        {
            "LeftX",
            "LeftY",
            "RightX",
            "RightY",
            "LeftTrigger",
            "RightTrigger",
        };
        static_assert(sizeof(GamepadAxisNames) / sizeof(GamepadAxisNames[0]) == static_cast<std::size_t>(GamepadAxis::Count),
            "the GamepadAxis name table must follow the enum");

        struct KeyAlias
        {
            const char* oldName;
            Key key;
        };

        // 기존 엔진 `EKeyCode` 의 옛 이름이다. 옛 프로젝트 파일의 바인딩이 그대로 열려야 한다.
        constexpr KeyAlias KeyAliases[] =
        {
            {"Equals", Key::Equal},
            {"Grave", Key::GraveAccent},
            {"LeftCtrl", Key::LeftControl},
            {"Num0", Key::Digit0},
            {"Num1", Key::Digit1},
            {"Num2", Key::Digit2},
            {"Num3", Key::Digit3},
            {"Num4", Key::Digit4},
            {"Num5", Key::Digit5},
            {"Num6", Key::Digit6},
            {"Num7", Key::Digit7},
            {"Num8", Key::Digit8},
            {"Num9", Key::Digit9},
            {"Numpad0", Key::Keypad0},
            {"Numpad1", Key::Keypad1},
            {"Numpad2", Key::Keypad2},
            {"Numpad3", Key::Keypad3},
            {"Numpad4", Key::Keypad4},
            {"Numpad5", Key::Keypad5},
            {"Numpad6", Key::Keypad6},
            {"Numpad7", Key::Keypad7},
            {"Numpad8", Key::Keypad8},
            {"Numpad9", Key::Keypad9},
            {"NumpadAdd", Key::KeypadAdd},
            {"NumpadDecimal", Key::KeypadDecimal},
            {"NumpadDivide", Key::KeypadDivide},
            {"NumpadEnter", Key::KeypadEnter},
            {"NumpadMultiply", Key::KeypadMultiply},
            {"NumpadSubtract", Key::KeypadSubtract},
            {"RightCtrl", Key::RightControl},
        };

        template<typename Enum, std::size_t Count>
        const char* NameOf(const char* const (&names)[Count], Enum value)
        {
            const std::size_t index = static_cast<std::size_t>(value);
            if (index >= Count)
            {
                return "";
            }
            return names[index];
        }

        template<typename Enum, std::size_t Count>
        bool Find(const char* const (&names)[Count], const char* name, Enum& value)
        {
            if (name == nullptr)
            {
                return false;
            }
            for (std::size_t index = 0; index < Count; ++index)
            {
                if (std::strcmp(names[index], name) == 0)
                {
                    value = static_cast<Enum>(index);
                    return true;
                }
            }
            return false;
        }
    }

    const char* GetKeyName(Key key)
    {
        return NameOf(KeyNames, key);
    }

    bool FindKeyByName(const char* name, Key& key)
    {
        if (Find(KeyNames, name, key))
        {
            return true;
        }
        if (name == nullptr)
        {
            return false;
        }
        for (const KeyAlias& alias : KeyAliases)
        {
            if (std::strcmp(alias.oldName, name) == 0)
            {
                key = alias.key;
                return true;
            }
        }
        return false;
    }

    const char* GetMouseButtonName(MouseButton button)
    {
        return NameOf(MouseButtonNames, button);
    }

    bool FindMouseButtonByName(const char* name, MouseButton& button)
    {
        return Find(MouseButtonNames, name, button);
    }

    const char* GetGamepadButtonName(GamepadButton button)
    {
        return NameOf(GamepadButtonNames, button);
    }

    bool FindGamepadButtonByName(const char* name, GamepadButton& button)
    {
        return Find(GamepadButtonNames, name, button);
    }

    const char* GetGamepadAxisName(GamepadAxis axis)
    {
        return NameOf(GamepadAxisNames, axis);
    }

    bool FindGamepadAxisByName(const char* name, GamepadAxis& axis)
    {
        return Find(GamepadAxisNames, name, axis);
    }
}
