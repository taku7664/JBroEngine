#pragma once

#include <JBro/Core/StableTypeId.h>
#include <JBro/LocalizationTypes/Internal/SystemContext.h>
#include <JBro/LocalizationTypes/ServiceContext.h>
#include <JBro/Runtime/ScriptModule.h>

// 로컬라이징 컨텍스트를 D-37 확장 블록으로 내고 찾는 도우미다. 세이브의 것과 같은 모양이다(D-218, D-226).
// 블록은 **호스트가** 만든다 - 표는 엔진이 소유하고 두 차원이 같은 것을 쓴다.
// 스크립트 DLL 은 Load 에서 `FindLocalization*Context` 로 찾아 `BindLocalization*Context` 로 자기 사본에 묶는다.
namespace JBro
{
    inline constexpr ScriptContextTypeId LocalizationServiceContextTypeId = MakeStableTypeId("JBro.Localization.ServiceContext");

    inline constexpr ScriptContextRequirement LocalizationServiceContextRequirement =
    {
        LocalizationServiceContextTypeId,
        LocalizationServiceContextAbiVersion,
        static_cast<std::uint32_t>(sizeof(LocalizationServiceContext))
    };

    ScriptContextBlock MakeLocalizationServiceContextBlock(const LocalizationServiceContext& context) noexcept;
    const LocalizationServiceContext* FindLocalizationServiceContext(const ScriptModuleLoadContext& context) noexcept;

    inline constexpr ScriptContextTypeId LocalizationSystemContextTypeId = MakeStableTypeId("JBro.Localization.SystemContext");

    inline constexpr ScriptContextRequirement LocalizationSystemContextRequirement =
    {
        LocalizationSystemContextTypeId,
        LocalizationSystemContextAbiVersion,
        static_cast<std::uint32_t>(sizeof(LocalizationSystemContext))
    };

    ScriptContextBlock MakeLocalizationSystemContextBlock(const LocalizationSystemContext& context) noexcept;
    const LocalizationSystemContext* FindLocalizationSystemContext(const ScriptModuleLoadContext& context) noexcept;
}
