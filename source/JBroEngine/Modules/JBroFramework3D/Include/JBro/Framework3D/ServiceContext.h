#pragma once

#include <JBro/Framework3D/Service/DebugDraw3DService.h>
#include <JBro/Framework3D/Service/Text3DService.h>

#include <cstdint>
#include <JBro/Types/UInt.h>

namespace JBro
{
    // 3D 스크립트 DLL 과의 약속의 판번호다(D-224). 2D 의 `Framework2DServiceContext` 와 같은 모양이다.
    // 1: `Text3DService` 가 첫 서비스다.
    // 2: `DebugDraw3DService` 를 더했고(D-243) 공통 훅이 인자를 잃었다(D-242).
    inline constexpr UInt32 Framework3DServiceContextAbiVersion = 2;

    // Non-owning value services. Kept outside Runtime's dimension-independent context.
    struct Framework3DServiceContext
    {
        UInt32 AbiVersion = Framework3DServiceContextAbiVersion;
        Service::Text3DService Text3D;
        Service::DebugDraw3DService DebugDraw;
    };

    // Main-thread only. Copy the host's context into this module at load/rebind time.
    void BindFramework3DServiceContext(const Framework3DServiceContext& context);
    const Framework3DServiceContext& GetFramework3DServices();
}
