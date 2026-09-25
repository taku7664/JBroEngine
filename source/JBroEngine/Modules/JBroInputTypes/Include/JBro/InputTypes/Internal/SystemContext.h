#pragma once

#include <JBro/InputTypes/System/IInputSystem.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace JBro
{
    inline constexpr std::uint32_t InputSystemContextAbiVersion = 1;

    // 입력 시스템 인터페이스 묶음이다. 호스트가 소유한 `System::InputSystem` 을 가리킨다.
    // `JBroRuntime::SystemContext` 에 넣지 않고 네트워크처럼 D-37 확장 블록으로 넘긴다(D-201) -
    // 그러면 Runtime 이 입력 모듈을 알 필요가 없다. 서비스 구현만 읽는다.
    struct InputSystemContext
    {
        std::uint32_t AbiVersion = InputSystemContextAbiVersion;
        System::IInputSystem* Input = nullptr;
    };

    static_assert(std::is_standard_layout_v<InputSystemContext>);
    static_assert(std::is_trivially_copyable_v<InputSystemContext>);
    static_assert(offsetof(InputSystemContext, AbiVersion) == 0);

    // Main-thread only. 호스트의 값을 이 모듈 사본에 복사한다.
    void BindInputSystemContext(const InputSystemContext& context);
    const InputSystemContext& GetInputSystems();
}
