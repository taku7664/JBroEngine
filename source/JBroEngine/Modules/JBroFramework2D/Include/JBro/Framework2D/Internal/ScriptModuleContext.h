#pragma once

#include <JBro/Framework2D/ServiceContext.h>
#include <JBro/Runtime/ScriptModule.h>

namespace JBro
{
    inline constexpr ScriptContextTypeId Framework2DServiceContextTypeId =
        MakeStableTypeId("JBro.Framework2D.ServiceContext");

    inline constexpr ScriptContextRequirement Framework2DServiceContextRequirement =
    {
        Framework2DServiceContextTypeId,
        Framework2DServiceContextAbiVersion,
        static_cast<std::uint32_t>(sizeof(Framework2DServiceContext))
    };

    ScriptContextBlock MakeFramework2DServiceContextBlock(
        const Framework2DServiceContext& context) noexcept;
    const Framework2DServiceContext* FindFramework2DServiceContext(
        const ScriptModuleLoadContext& context) noexcept;
}
