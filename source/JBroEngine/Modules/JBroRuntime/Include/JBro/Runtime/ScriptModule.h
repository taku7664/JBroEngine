#pragma once

#include <JBro/Core/StableTypeId.h>
#include <JBro/Runtime/ServiceContext.h>
#include <JBro/Runtime/SystemContext.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace JBro
{
    using ScriptContextTypeId = std::uint64_t;

    inline constexpr std::uint32_t ScriptModuleAbiVersion = 1;
    inline constexpr std::uint32_t ScriptModuleLoadContextAbiVersion = 1;
    inline constexpr std::uint32_t MaxScriptContextBlocks = 64;
    inline constexpr char ScriptModuleEntryPointName[] = "JBroScriptModule_GetApi";

    struct ScriptContextBlock
    {
        ScriptContextTypeId TypeId = 0;
        std::uint32_t AbiVersion = 0;
        std::uint32_t Size = 0;
        const void* Data = nullptr;
    };

    struct ScriptContextRequirement
    {
        ScriptContextTypeId TypeId = 0;
        std::uint32_t AbiVersion = 0;
        std::uint32_t Size = 0;
    };

    struct ScriptModuleLoadContext
    {
        std::uint32_t AbiVersion = ScriptModuleLoadContextAbiVersion;
        std::uint32_t StructSize = sizeof(ScriptModuleLoadContext);
        const SystemContext* Systems = nullptr;
        const ServiceContext* Services = nullptr;
        const ScriptContextBlock* Extensions = nullptr;
        std::uint32_t ExtensionCount = 0;
        std::uint32_t Reserved = 0;
    };

    using ScriptModuleLoadFunction =
        bool (*)(const ScriptModuleLoadContext* context) noexcept;
    using ScriptModuleUnloadFunction = void (*)() noexcept;

    struct ScriptModuleApi
    {
        std::uint32_t AbiVersion = ScriptModuleAbiVersion;
        std::uint32_t StructSize = sizeof(ScriptModuleApi);
        const ScriptContextRequirement* RequiredContexts = nullptr;
        std::uint32_t RequiredContextCount = 0;
        std::uint32_t Reserved = 0;
        ScriptModuleLoadFunction Load = nullptr;
        ScriptModuleUnloadFunction Unload = nullptr;
    };

    using GetScriptModuleApiFunction = const ScriptModuleApi* (*)(
        std::uint32_t hostAbiVersion,
        std::uint32_t hostApiSize) noexcept;

    const ScriptContextBlock* FindScriptContextBlock(
        const ScriptModuleLoadContext& context,
        ScriptContextTypeId typeId) noexcept;

    bool ValidateScriptModuleLoadContext(
        const ScriptModuleLoadContext& context) noexcept;

    // Called inside the script DLL so its statically linked Runtime copy receives host values.
    bool BindScriptModuleContexts(
        const ScriptModuleLoadContext& context) noexcept;

    static_assert(std::is_standard_layout_v<ScriptContextBlock>);
    static_assert(std::is_trivially_copyable_v<ScriptContextBlock>);
    static_assert(std::is_standard_layout_v<ScriptContextRequirement>);
    static_assert(std::is_trivially_copyable_v<ScriptContextRequirement>);
    static_assert(std::is_standard_layout_v<ScriptModuleLoadContext>);
    static_assert(std::is_trivially_copyable_v<ScriptModuleLoadContext>);
    static_assert(std::is_standard_layout_v<ScriptModuleApi>);
    static_assert(std::is_trivially_copyable_v<ScriptModuleApi>);
    static_assert(sizeof(ScriptContextBlock) == 24);
    static_assert(sizeof(ScriptContextRequirement) == 16);
    static_assert(sizeof(ScriptModuleLoadContext) == 40);
    static_assert(sizeof(ScriptModuleApi) == 40);
    static_assert(offsetof(ScriptModuleApi, AbiVersion) == 0);
}
