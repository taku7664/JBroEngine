#pragma once

#include <JBro/Framework3D/Internal/SystemContext.h>
#include <JBro/Framework3D/ServiceContext.h>
#include <JBro/Runtime/ScriptModule.h>

namespace JBro
{
    inline constexpr ScriptContextTypeId Framework3DServiceContextTypeId =
        MakeStableTypeId("JBro.Framework3D.ServiceContext");

    inline constexpr ScriptContextRequirement Framework3DServiceContextRequirement =
    {
        Framework3DServiceContextTypeId,
        Framework3DServiceContextAbiVersion,
        static_cast<std::uint32_t>(sizeof(Framework3DServiceContext))
    };

    ScriptContextBlock MakeFramework3DServiceContextBlock(
        const Framework3DServiceContext& context) noexcept;
    const Framework3DServiceContext* FindFramework3DServiceContext(
        const ScriptModuleLoadContext& context) noexcept;

    inline constexpr ScriptContextTypeId Framework3DSystemContextTypeId =
        MakeStableTypeId("JBro.Framework3D.SystemContext");

    inline constexpr ScriptContextRequirement Framework3DSystemContextRequirement =
    {
        Framework3DSystemContextTypeId,
        Framework3DSystemContextAbiVersion,
        static_cast<std::uint32_t>(sizeof(Framework3DSystemContext))
    };

    ScriptContextBlock MakeFramework3DSystemContextBlock(
        const Framework3DSystemContext& context) noexcept;
    const Framework3DSystemContext* FindFramework3DSystemContext(
        const ScriptModuleLoadContext& context) noexcept;
}
