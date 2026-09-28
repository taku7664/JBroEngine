#pragma once

#include <JBro/Core/StableTypeId.h>
#include <JBro/Reflection/PropertyRegistry.h>
#include <JBro/Runtime/ScriptRegistry.h>
#include <JBro/Types/NameTable.h>
#include <JBro/Internal/InstanceRegistry.h>
#include <JBro/Runtime/ServiceContext.h>
#include <JBro/Runtime/SystemContext.h>
#include <JBro/Runtime/TextStore.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace JBro
{
    using ScriptContextTypeId = std::uint64_t;

    inline constexpr std::uint32_t ScriptModuleAbiVersion = 1;
    // 5: 호스트의 `TextStore` 를 넘긴다(D-211 의 남은 일, text-plan §7).
    // 6: 호스트의 스크립트 프로퍼티 표를 넘긴다(cpp-script-plan §3.1).
    inline constexpr std::uint32_t ScriptModuleLoadContextAbiVersion = 6;
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
        // 호스트의 인스턴스 레지스트리. DLL 은 이것을 자기 사본의 접근점에 1회 바인딩한다(D-44).
        Internal::InstanceRegistry* Registry = nullptr;
        // 호스트의 이름표. 바인딩하지 않으면 DLL 이 호스트가 지은 태그의 원문을
        // 되찾지 못한다 — 레지스트리와 같은 함정이다(D-44, D-51).
        NameTable* Names = nullptr;
        // 호스트의 스크립트 타입 표. DLL 이 여기에 자기 타입을 등록하고,
        // 호스트는 그 이름으로 스크립트를 붙인다(H5). 바인딩하지 않으면
        // DLL 이 자기 사본에 등록하고 호스트는 아무것도 보지 못한다.
        ScriptRegistry* Scripts = nullptr;
        // 호스트의 글자 저장소(D-211). 스크립트 타입의 리플렉션 필드가 `TextId` 면 DLL 쪽 코덱이 이 저장소로 글자를 오간다.
        // 바인딩하지 않으면 DLL 의 빈 저장소를 보고, 파일에서 읽은 글자가 호스트에는 없다.
        TextStore* Texts = nullptr;
        // 호스트의 스크립트 프로퍼티 표(cpp-script-plan §3.1). DLL 이 스크립트 타입마다 필드 표를 여기에 등록하고,
        // 인스펙터·캔버스 파일·되돌리기가 그 표로 스크립트 값을 읽고 쓴다. 바인딩하지 않으면 DLL 이 자기 사본에 등록하고
        // 호스트는 스크립트를 저장하지 못한다("this component type never registered its properties").
        PropertyRegistry* Properties = nullptr;
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
    // 크기는 64 비트에서 잰 값이다. 웹(wasm32)은 포인터가 4 바이트이고 스크립트 DLL 경계가 없다(D-206).
    static_assert(sizeof(void*) != 8 || sizeof(ScriptContextBlock) == 24);
    static_assert(sizeof(void*) != 8 || sizeof(ScriptContextRequirement) == 16);
    static_assert(sizeof(void*) != 8 || sizeof(ScriptModuleLoadContext) == 80);
    static_assert(sizeof(void*) != 8 || sizeof(ScriptModuleApi) == 40);
    static_assert(offsetof(ScriptModuleApi, AbiVersion) == 0);
}
