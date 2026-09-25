#pragma once

#include <JBro/InputTypes/InputView.h>

#include <cstdint>

namespace JBro::System
{
    // 서비스가 호스트의 입력 시스템에 닿는 인터페이스다(ProjectRule §10.3). 스크립트는 이것을 보지 않는다 -
    // 프렐류드가 include 하지 않고, 서비스 .cpp 만 `InputSystemContext` 로 받아 부른다.
    class IInputSystem
    {
    public:
        // 레이어 체인이 다 돈 뒤 남은 입력이다(D-210). 체인이 돌기 전에는 이번 프레임 전체다.
        virtual const InputView& GetResidualView() const noexcept = 0;

        // 게임패드 진동(D-210). `seconds` 가 0 보다 크면 그만큼 뒤에 스스로 멈추고, 0 이면 멈추라고 할 때까지 돈다.
        // 창이 포커스를 잃거나 패드가 빠지면 멈춘다.
        virtual void SetGamepadVibration(std::uint32_t slot, float low, float high, float seconds) noexcept = 0;
        // 스틱의 둥근 데드존과 트리거의 문턱(0..0.95). 모든 패드에 같이 걸린다.
        virtual void SetGamepadDeadzones(float stick, float trigger) noexcept = 0;

    protected:
        ~IInputSystem() = default;
    };
}
