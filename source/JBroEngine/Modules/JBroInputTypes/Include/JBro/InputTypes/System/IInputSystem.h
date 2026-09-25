#pragma once

#include <JBro/InputTypes/InputView.h>

namespace JBro::System
{
    // 서비스가 호스트의 입력 시스템에 닿는 인터페이스다(ProjectRule §10.3). 스크립트는 이것을 보지 않는다 -
    // 프렐류드가 include 하지 않고, 서비스 .cpp 만 `InputSystemContext` 로 받아 부른다.
    class IInputSystem
    {
    public:
        // 레이어 체인이 다 돈 뒤 남은 입력이다(D-201). 체인이 돌기 전에는 이번 프레임 전체다.
        virtual const InputView& GetResidualView() const noexcept = 0;

    protected:
        ~IInputSystem() = default;
    };
}
