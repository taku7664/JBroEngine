#pragma once

#include <JBro/Framework2D/Internal/SystemContext.h>
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

    inline constexpr ScriptContextTypeId Framework2DSystemContextTypeId =
        MakeStableTypeId("JBro.Framework2D.SystemContext");

    inline constexpr ScriptContextRequirement Framework2DSystemContextRequirement =
    {
        Framework2DSystemContextTypeId,
        Framework2DSystemContextAbiVersion,
        static_cast<std::uint32_t>(sizeof(Framework2DSystemContext))
    };

    ScriptContextBlock MakeFramework2DSystemContextBlock(
        const Framework2DSystemContext& context) noexcept;
    const Framework2DSystemContext* FindFramework2DSystemContext(
        const ScriptModuleLoadContext& context) noexcept;
}
