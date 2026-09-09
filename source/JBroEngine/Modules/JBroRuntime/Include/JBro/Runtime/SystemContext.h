#pragma once

#include <cstdint>

namespace JBro
{
    namespace System
    {
        class IPhysics2DSystem;
    }

    inline constexpr std::uint32_t SystemContextAbiVersion = 2;

    struct SystemContext
    {
        std::uint32_t AbiVersion = SystemContextAbiVersion;
        System::IPhysics2DSystem* Physics2D = nullptr;
    };

    void BindSystemContext(const SystemContext& context);
    const SystemContext& GetSystemContext();
}
