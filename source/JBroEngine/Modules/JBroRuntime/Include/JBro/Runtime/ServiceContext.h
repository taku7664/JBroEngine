#pragma once

#include <cstdint>

namespace JBro
{
    inline constexpr std::uint32_t ServiceContextAbiVersion = 1;

    struct ServiceContext
    {
        std::uint32_t AbiVersion = ServiceContextAbiVersion;
    };

    void BindServiceContext(const ServiceContext& context);
    const ServiceContext& GetServiceContext();
}
