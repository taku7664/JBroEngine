#pragma once

#include <JBro/InputTypes/InputView.h>

namespace JBro::Service
{
    // 스크립트가 `OnUpdate`·`OnFixedUpdate` 에서 입력을 읽는 표면이다(D-214).
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
        // 게임패드 자리(0..3)다. 막혔거나 연결되어 있지 않으면 빈 패드다.
        const GamepadState& Gamepad(std::uint32_t slot) const;

        // 두 모터(낮은 쪽이 왼쪽 큰 모터)를 0..1 로 돌린다. `seconds` 가 0 보다 크면 그만큼 뒤에 멈추고, 0 이면 멈출 때까지 돈다.
        // 창이 포커스를 잃거나 패드가 빠지면 멈춘다(기존 엔진처럼 알트탭한 뒤에도 울리지 않는다).
        void SetGamepadVibration(std::uint32_t slot, float low, float high, float seconds = 0.0f) const;
        void StopGamepadVibration(std::uint32_t slot) const;
        // 스틱의 둥근 데드존(기본 0.24)과 트리거의 문턱(기본 0.12)이다. 모든 패드에 같이 걸린다.
        void SetGamepadDeadzones(float stick, float trigger) const;

        const TouchState& Touch() const;
        // 손가락을 스스로 만든다(가상 조이스틱·자동 검사). 다음 프레임에 보인다. `Stationary` 는 아무 일도 하지 않는다.
        void InjectTouch(std::uint32_t id, float x, float y, TouchPhase phase) const;

        // 액션 세트(걷기·차량·메뉴)를 켜고 끈다. 꺼진 세트의 액션은 0 으로 읽히고, 곧바로 걸린다.
        // 세트는 막지 않는다 - 메뉴가 게임을 막아야 하면 레이어 체인의 `Block` 을 쓴다. 처음에는 `Default` 만 켜져 있다.
        // 떼기 전에 세트를 끄면 그 액션의 뗌은 오지 않는다. 없는 세트면 거짓이다.
        bool EnableActionSet(NameId set) const;
        bool DisableActionSet(NameId set) const;
        bool IsActionSetEnabled(NameId set) const;
    };
}
