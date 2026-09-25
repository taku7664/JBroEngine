#pragma once

#include <JBro/Framework2D/Service/Physics2DService.h>
#include <JBro/Framework2D/Service/Text2DService.h>

#include <cstdint>

namespace JBro
{
    // 2: Text2D service added (D-200).
    inline constexpr std::uint32_t Framework2DServiceContextAbiVersion = 2;

    // Non-owning value services. Kept outside Runtime's dimension-independent context.
    struct Framework2DServiceContext
    {
        std::uint32_t AbiVersion = Framework2DServiceContextAbiVersion;
        Service::Physics2DService Physics2D;
        Service::Text2DService Text2D;
    };

    // Main-thread only. Copy the host's context into this module at load/rebind time.
    void BindFramework2DServiceContext(const Framework2DServiceContext& context);
    const Framework2DServiceContext& GetFramework2DServices();
}
