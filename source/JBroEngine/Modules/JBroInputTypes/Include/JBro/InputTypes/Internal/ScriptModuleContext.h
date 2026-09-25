#pragma once

#include <JBro/Core/StableTypeId.h>
#include <JBro/InputTypes/Internal/SystemContext.h>
#include <JBro/InputTypes/ServiceContext.h>
#include <JBro/Runtime/ScriptModule.h>

// 입력 컨텍스트를 D-37 확장 블록으로 내고 찾는 도우미다. 네트워크의 것과 같은 모양이다(D-122, D-214).
// 블록은 프레임워크가 아니라 **호스트가** 만든다 - 입력 시스템은 엔진이 소유하고 두 차원이 같은 것을 쓴다.
// 스크립트 DLL 은 Load 에서 `FindInput*Context` 로 찾아 `BindInput*Context` 로 자기 사본에 묶는다.
namespace JBro
{
    inline constexpr ScriptContextTypeId InputServiceContextTypeId = MakeStableTypeId("JBro.Input.ServiceContext");

    inline constexpr ScriptContextRequirement InputServiceContextRequirement =
    {
        InputServiceContextTypeId,
        InputServiceContextAbiVersion,
        static_cast<std::uint32_t>(sizeof(InputServiceContext))
    };

    ScriptContextBlock MakeInputServiceContextBlock(const InputServiceContext& context) noexcept;
    const InputServiceContext* FindInputServiceContext(const ScriptModuleLoadContext& context) noexcept;

    inline constexpr ScriptContextTypeId InputSystemContextTypeId = MakeStableTypeId("JBro.Input.SystemContext");

    inline constexpr ScriptContextRequirement InputSystemContextRequirement =
    {
        InputSystemContextTypeId,
        InputSystemContextAbiVersion,
        static_cast<std::uint32_t>(sizeof(InputSystemContext))
    };

    ScriptContextBlock MakeInputSystemContextBlock(const InputSystemContext& context) noexcept;
    const InputSystemContext* FindInputSystemContext(const ScriptModuleLoadContext& context) noexcept;
}
