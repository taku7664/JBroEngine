#pragma once

#include <JBro/Core/StableTypeId.h>
#include <JBro/Runtime/ScriptModule.h>
#include <JBro/SaveTypes/Internal/SystemContext.h>
#include <JBro/SaveTypes/ServiceContext.h>

// 세이브 컨텍스트를 D-37 확장 블록으로 내고 찾는 도우미다. 입력의 것과 같은 모양이다(D-214, D-218).
// 블록은 **호스트가** 만든다 - 저장소는 엔진이 소유하고 두 차원이 같은 것을 쓴다.
// 스크립트 DLL 은 Load 에서 `FindSave*Context` 로 찾아 `BindSave*Context` 로 자기 사본에 묶는다.
namespace JBro
{
    inline constexpr ScriptContextTypeId SaveServiceContextTypeId = MakeStableTypeId("JBro.Save.ServiceContext");

    inline constexpr ScriptContextRequirement SaveServiceContextRequirement =
    {
        SaveServiceContextTypeId,
        SaveServiceContextAbiVersion,
        static_cast<std::uint32_t>(sizeof(SaveServiceContext))
    };

    ScriptContextBlock MakeSaveServiceContextBlock(const SaveServiceContext& context) noexcept;
    const SaveServiceContext* FindSaveServiceContext(const ScriptModuleLoadContext& context) noexcept;

    inline constexpr ScriptContextTypeId SaveSystemContextTypeId = MakeStableTypeId("JBro.Save.SystemContext");

    inline constexpr ScriptContextRequirement SaveSystemContextRequirement =
    {
        SaveSystemContextTypeId,
        SaveSystemContextAbiVersion,
        static_cast<std::uint32_t>(sizeof(SaveSystemContext))
    };

    ScriptContextBlock MakeSaveSystemContextBlock(const SaveSystemContext& context) noexcept;
    const SaveSystemContext* FindSaveSystemContext(const ScriptModuleLoadContext& context) noexcept;
}
