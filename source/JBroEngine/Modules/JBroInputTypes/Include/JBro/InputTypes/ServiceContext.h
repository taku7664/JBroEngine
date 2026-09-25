#pragma once

#include <JBro/InputTypes/Service/InputService.h>

#include <cstdint>

namespace JBro
{
    inline constexpr std::uint32_t InputServiceContextAbiVersion = 1;

    // 값 서비스 묶음이다. 두 차원의 프렐류드가 이것을 공개한다(D-214). 소유하지 않는다 -
    // 호스트의 것을 로드·재바인딩 때 복사한다.
    struct InputServiceContext
    {
        std::uint32_t AbiVersion = InputServiceContextAbiVersion;
        Service::InputService Input;
    };

    // Main-thread only.
    void BindInputServiceContext(const InputServiceContext& context);
    const InputServiceContext& GetInputServices();
}
