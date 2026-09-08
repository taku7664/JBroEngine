#pragma once

#include <cstdint>

namespace JBro
{
    inline constexpr std::uint32_t SystemContextAbiVersion = 1;

    struct SystemContext
    {
        std::uint32_t AbiVersion = SystemContextAbiVersion;
    };

    void BindSystemContext(const SystemContext& context);
    const SystemContext& GetSystemContext();
}
