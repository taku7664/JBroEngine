#pragma once

#include <JBro/InputTypes/InputAction.h>
#include <JBro/InputTypes/InputView.h>
#include <JBro/Types/String.h>

#include <cstddef>
#include <cstdint>

namespace JBro
{
    // 런타임 리바인딩(D-218). 게임의 "키 설정" 화면이 쓴다.
    //
    // 프로젝트의 액션 표(`.jproject`)는 기본값이고, 게임이 바꾼 것은 그 위에 얹힌다. 바꾼 것은 `InputService` 가 글자로 쓰고
    // 읽는다 - 입력 모듈은 세이브를 모르고, 게임이 그 글자를 `SaveService` 로 남긴다:
    //
    //     String text;
    //     input.WriteBindingOverrides(text);
    //     save.WriteText("input_bindings.yaml", text);
    //     ...
    //     if (save.ReadText("input_bindings.yaml", text))
    //     {
    //         input.ReadBindingOverrides(text);
    //     }
    //
    // 글자는 YAML 의 이름 → 글자 맵이다. 프로젝트와 다른 액션만 적고, 키는 번호가 아니라 이름이라 열거자 차례가 바뀌어도 깨지지 않는다:
    //
    //     Jump: "Key Space, GamepadButton South"
    //     Move: "Key W Up, Key S Down, GamepadStick Left @1"
    //     Fire: ""
    //
    // 바인딩 하나는 `원천 코드 [방향] [@패드]` 이다. 원천·코드·방향의 이름은 `.jproject` 와 같다.

    enum class InputCaptureResult : std::uint8_t
    {
        // 이번 프레임에 누른 것이 없다. 다음 프레임에 다시 부른다.
        None,
        Captured,
        // `Escape` 를 눌렀다. 설정 화면은 바꾸지 않고 물러난다.
        Cancelled
    };

    // 이번 프레임에 **새로 누른** 키·마우스 버튼·게임패드 버튼 하나를 바인딩으로 잡는다. 여럿이면 키 → 마우스 → 패드, 각각 번호 차례다.
    // 잡은 바인딩의 방향은 None, 패드는 -1(아무 패드)이다 - 합성 액션이면 부르는 쪽이 원래 자리의 방향을 옮겨 적는다.
    // **축과 트리거는 잡지 않는다.** 뷰는 이번 프레임만 들어 "넘은 순간" 을 알 수 없고, 누른 채로 설정을 열면 곧바로 잡힌다.
    // 뷰로 읽으므로 위의 레이어가 막은 입력은 잡지 않는다.
    InputCaptureResult CaptureBinding(const InputView& view, InputBinding& out);

    // 두 바인딩이 같은가(원천·코드·방향·패드).
    bool IsSameBinding(const InputBinding& a, const InputBinding& b);
}
