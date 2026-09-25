#pragma once

#include <JBro/Core/Core.h>
#include <JBro/Core/InputKeys.h>

namespace JBro
{
    // 플랫폼이 모아 주는 입력이다(D-62).
    //
    // **상태가 아니라 이벤트다.** 매 프레임 키 배열을 읽어 가면 한 프레임 안에 눌렀다
    // 뗀 키와 글자 입력 순서가 사라진다. 에디터의 텍스트 필드가 바로 그것을 필요로 한다.
    //
    // 값은 전부 POD 다. 게임 DLL 경계를 넘어간다.

    enum class InputEventKind : std::uint8_t
    {
        KeyDown,
        KeyUp,
        // 글자 하나다. `Key` 와 별개다 - 같은 키라도 배열과 조합에 따라 다른 글자가 된다.
        Text,
        MouseMove,
        MouseButtonDown,
        MouseButtonUp,
        MouseWheel,
        FocusGained,
        FocusLost
    };

    struct InputEvent
    {
        InputEventKind kind = InputEventKind::FocusLost;
        Key key = Key::Unknown;
        MouseButton button = MouseButton::Left;
        KeyModifiers modifiers = KeyModifierNone;
        // 자동 반복으로 다시 온 KeyDown 이다.
        bool repeat = false;
        // MouseMove 는 클라이언트 영역 픽셀 좌표, MouseWheel 은 칸 수다.
        // 나머지 종류에서는 0 이다.
        float x = 0.0f;
        float y = 0.0f;
        // Text 의 유니코드 코드포인트다. 나머지 종류에서는 0 이다.
        std::uint32_t codePoint = 0;
    };

    static_assert(sizeof(InputEvent) == 20, "InputEvent crosses the game DLL boundary");
}
