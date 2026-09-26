#pragma once

#include <JBro/Core/InputKeys.h>
#include <JBro/Types/NameTable.h>

#include <cstdint>
#include <type_traits>

namespace JBro
{
    // 이름 붙인 입력(액션)이다(D-214). 스크립트는 "W 가 눌렸나" 대신 "Move 가 얼마인가" 를 읽고, 어느 키가 Move 인지는
    // 프로젝트의 `InputActions` 가 정한다. 기존 엔진과 같은 모양이다(값 종류 셋, 바인딩 원천 다섯, 키 합성 넷).
    //
    // **이름은 정수로 찾는다.** 기존 엔진은 부를 때마다 이름을 `strcmp` 로 훑었다(§9 위반). 여기서는 `NameId` 이고
    // `MakeNameId("Move")` 가 컴파일 시간에 끝난다. 표는 호스트가 들고 POD 라 게임 DLL 이 그대로 읽는다.

    using InputActionId = NameId;

    enum class InputActionType : std::uint8_t
    {
        // 눌림 여부. 바인딩 중 하나라도 눌리면 눌림이다.
        Bool,
        // 0..1 한 값(트리거·키는 1). 바인딩 중 가장 큰 값이다.
        Float,
        // 두 값(이동). 키 합성과 스틱을 더하고 길이 1 로 자른다 - 대각선이 빨라지지 않는다.
        Vector2
    };

    enum class InputBindingSource : std::uint8_t
    {
        Key,
        MouseButton,
        GamepadButton,
        // 축 하나(`GamepadAxis`). Float 에 쓴다.
        GamepadAxis,
        // 스틱 하나(0 = 왼쪽, 1 = 오른쪽). Vector2 에 곧바로 이어진다.
        GamepadStick
    };

    // Vector2 에서 이 바인딩이 맡는 방향이다. 스틱과 단일 바인딩은 None 이다.
    enum class InputComposite : std::uint8_t
    {
        None,
        Up,
        Down,
        Left,
        Right
    };

    struct InputBinding
    {
        InputBindingSource source = InputBindingSource::Key;
        InputComposite composite = InputComposite::None;
        // 게임패드 자리(0..3). -1 이면 연결된 아무 패드다(버튼은 어느 패드든, 축·스틱은 연결된 첫 패드).
        std::int8_t gamepad = -1;
        std::uint8_t reserved = 0;
        // 원천에 따른 열거자 값이다(`Key`·`MouseButton`·`GamepadButton`·`GamepadAxis`, 스틱은 0·1).
        std::uint16_t code = 0;
    };

    inline constexpr std::uint32_t MaxInputBindingsPerAction = 8;
    inline constexpr std::uint32_t MaxInputActions = 64;

    struct InputActionDesc
    {
        InputActionId name = InvalidNameId;
        InputActionType type = InputActionType::Bool;
        std::uint8_t bindingCount = 0;
        InputBinding bindings[MaxInputBindingsPerAction] = {};
    };

    // 프로젝트의 액션 전부다. 호스트가 프로젝트를 열 때 채우고 프레임 경로에서는 읽기만 한다.
    struct InputActionMap
    {
        std::uint32_t count = 0;
        InputActionDesc actions[MaxInputActions] = {};
        // 없는 이름을 물은 것을 한 번만 말하려고 기억해 둔다. 가득 차면 더는 말하지 않는다.
        std::uint32_t warnedCount = 0;
        InputActionId warned[8] = {};

        const InputActionDesc* Find(InputActionId name) const
        {
            for (std::uint32_t index = 0; index < count && index < MaxInputActions; ++index)
            {
                if (actions[index].name == name)
                {
                    return &actions[index];
                }
            }
            return nullptr;
        }
    };

    struct InputVector2
    {
        float x = 0.0f;
        float y = 0.0f;
    };

    // 한 액션의 이번 프레임 값이다. 종류와 무관하게 다 채운다: Bool 은 x 가 0 또는 1, Float 은 x, Vector2 는 x·y.
    struct InputActionValue
    {
        float x = 0.0f;
        float y = 0.0f;
        // 지금 눌려 있는가(Float·Vector2 는 0 이 아닌가).
        bool down = false;
        // 이번 프레임에 눌렸는가·떼졌는가. 버튼 바인딩에서만 온다 - 축은 문턱을 넘은 순간을 세지 않는다.
        bool pressed = false;
        bool released = false;
    };

    static_assert(std::is_trivially_copyable_v<InputActionMap>, "InputActionMap crosses the game DLL boundary");
    static_assert(std::is_standard_layout_v<InputActionMap>, "InputActionMap crosses the game DLL boundary");
}
