#pragma once

#include <JBro/InputTypes/InputView.h>

namespace JBro::Service
{
    // 스크립트가 `OnUpdate`·`OnFixedUpdate` 에서 입력을 읽는 표면이다(D-201).
    //
    // **레이어 체인이 막고 남은 것만 보인다.** 위의 핸들러가 `Block` 을 돌려주었으면 여기서 읽는
    // 키보드와 마우스는 빈 장치이고, 마우스만 가져갔으면 마우스만 비어 있다. 그래서 간단한 게임은
    // 핸들러 없이 여기서 읽기만 해도 되고, UI 를 얹는 순간 그 블로킹이 이 폴링에도 걸린다.
    //
    // Main-thread only. 호스트의 입력 시스템을 소유하지 않고 바인딩된 것을 읽는다.
    class InputService
    {
    public:
        const InputView& GetView() const;
        const KeyboardState& Keyboard() const;
        const MouseState& Mouse() const;
    };
}
