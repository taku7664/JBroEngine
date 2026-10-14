#pragma once

#include <JBro/Runtime/ScriptModule.h>

#include <type_traits>

namespace JBro
{
    // The host owns the log. Generated game DLLs only retain this versioned POD
    // callback while loaded; they never create a second static Log ring.
    inline constexpr UInt32 ScriptErrorContextAbiVersion = 1;
    inline constexpr ScriptContextTypeId ScriptErrorContextTypeId =
        MakeStableTypeId("JBro.Runtime.ScriptErrorContext");

    struct ScriptErrorContext
    {
        using ReportFunction = void (*)(void* owner, UInt64 expectedGeneration,
            const char* siteKey, const char* message) noexcept;

        UInt32 AbiVersion = ScriptErrorContextAbiVersion;
        UInt32 StructSize = static_cast<UInt32>(sizeof(ScriptErrorContext));
        void* Owner = nullptr;
        UInt64 ExpectedGeneration = 0;
        ReportFunction Report = nullptr;
    };

    inline constexpr ScriptContextRequirement ScriptErrorContextRequirement = {
        ScriptErrorContextTypeId, ScriptErrorContextAbiVersion,
        static_cast<UInt32>(sizeof(ScriptErrorContext))
    };

    ScriptContextBlock MakeScriptErrorContextBlock(const ScriptErrorContext& context) noexcept;
    const ScriptErrorContext* FindScriptErrorContext(const ScriptModuleLoadContext& context) noexcept;
    void BindScriptErrorContext(const ScriptErrorContext& context) noexcept;
    void ReportScriptError(const char* siteKey, const char* message) noexcept;

    static_assert(std::is_standard_layout_v<ScriptErrorContext>);
    static_assert(std::is_trivially_copyable_v<ScriptErrorContext>);
    static_assert(sizeof(void*) != 8 || sizeof(ScriptErrorContext) == 32);
}
