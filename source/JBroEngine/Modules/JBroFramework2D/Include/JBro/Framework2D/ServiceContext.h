#pragma once

#include <JBro/Framework2D/Service/Physics2DService.h>

#include <cstdint>

namespace JBro
{
    // 2D 스크립트 DLL 과의 약속 전체의 판번호다. 이 구조체뿐 아니라 `GameScript2D` 의 가상 함수 표도 여기에 든다 -
    // 사용자 스크립트가 그 표를 물려받으므로 훅을 더하면 옛 DLL 은 받아들이면 안 된다(D-28).
    // 2: `OnTriggerEnter`·`OnTriggerExit` 를 더했다(D-203).
    inline constexpr std::uint32_t Framework2DServiceContextAbiVersion = 2;

    // Non-owning value services. Kept outside Runtime's dimension-independent context.
    struct Framework2DServiceContext
    {
        std::uint32_t AbiVersion = Framework2DServiceContextAbiVersion;
        Service::Physics2DService Physics2D;
    };

    // Main-thread only. Copy the host's context into this module at load/rebind time.
    void BindFramework2DServiceContext(const Framework2DServiceContext& context);
    const Framework2DServiceContext& GetFramework2DServices();
}
