#pragma once

#include <JBro/Core/Core.h>
#include <JBro/Core/InputKeys.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/UInt.h>

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
        FocusLost,
        // 손가락·펜(D-214). 포인터 번호는 `codePoint` 에, 자리는 `x`·`y`(클라이언트 픽셀)에 온다. 마우스는 여기로 오지 않는다.
        TouchBegan,
        TouchMoved,
        TouchEnded,
        // 시스템이 가져갔다(제스처·붙잡음 잃음). 뗀 것과 같이 다루되 "탭" 으로 치면 안 된다.
        TouchCancelled
    };

    struct InputEvent
    {
        InputEventKind kind = InputEventKind::FocusLost;
        Key key = Key::Unknown;
        MouseButton button = MouseButton::Left;
        KeyModifiers modifiers = KeyModifierNone;
        // 자동 반복으로 다시 온 KeyDown 이다.
        Bool repeat = false;
        // MouseMove 는 클라이언트 영역 픽셀 좌표, MouseWheel 은 칸 수다.
        // 나머지 종류에서는 0 이다.
        Float x = 0.0f;
        Float y = 0.0f;
        // Text 의 유니코드 코드포인트, 터치의 포인터 번호다. 나머지 종류에서는 0 이다.
        UInt32 codePoint = 0;
    };

    static_assert(sizeof(InputEvent) == 20, "InputEvent crosses the game DLL boundary");

    // 게임패드 한 자리의 날 상태다(D-214). 게임패드는 이벤트가 아니라 **폴링**이다 - XInput 이 그렇다. 데드존·누름 세기는
    // 플랫폼이 하지 않는다(`System::InputSystem` 이 한다). 그래서 플랫폼마다 같은 규칙으로 접힌다.
    struct GamepadRawState
    {
        Bool connected = false;
        // `GamepadButton` 차례의 비트다(1 << South ...).
        std::uint16_t buttons = 0;
        // `GamepadAxis` 차례다. 스틱은 -1..1(위가 +), 트리거는 0..1 - 아직 데드존 전이다.
        Float axes[static_cast<std::size_t>(GamepadAxis::Count)] = {};
    };

    // 창 클라이언트 좌표를 게임 화면 픽셀로 옮기는 값이다. 게임 화면 픽셀 = (클라이언트 - origin) * scale.
    // 게임 호스트는 창 전체가 게임 화면이라 기본값(그대로)이다. 에디터는 시뮬레이션 뷰의 사각형과 렌더 타깃 크기에서 만든다.
    struct InputSurfaceMapping
    {
        Float originX = 0.0f;
        Float originY = 0.0f;
        Float scaleX = 1.0f;
        Float scaleY = 1.0f;
    };
}
