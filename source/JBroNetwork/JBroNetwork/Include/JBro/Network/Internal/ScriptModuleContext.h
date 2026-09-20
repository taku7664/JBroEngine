#pragma once

#include <JBro/Core/StableTypeId.h>
#include <JBro/Network/Internal/SystemContext.h>
#include <JBro/Network/ServiceContext.h>
#include <JBro/Runtime/ScriptModule.h>

// 네트워크 컨텍스트를 D-37 확장 블록으로 내고 찾는 도우미다. `JBro::Framework2D` 의 것과 같은 모양이다.
// 블록은 프레임워크가 아니라 **호스트가** 만든다 - 트랜스포트는 캔버스보다 오래 살고 두 차원이 같은 것을 쓴다.
// 스크립트 DLL 은 Load 에서 `FindNetwork*Context` 로 찾아 `BindNetwork*Context` 로 자기 사본에 묶는다.
namespace JBro
{
    inline constexpr ScriptContextTypeId NetworkServiceContextTypeId = MakeStableTypeId("JBro.Network.ServiceContext");

    inline constexpr ScriptContextRequirement NetworkServiceContextRequirement =
    {
        NetworkServiceContextTypeId,
        NetworkServiceContextAbiVersion,
        static_cast<std::uint32_t>(sizeof(NetworkServiceContext))
    };

    ScriptContextBlock MakeNetworkServiceContextBlock(const NetworkServiceContext& context) noexcept;
    const NetworkServiceContext* FindNetworkServiceContext(const ScriptModuleLoadContext& context) noexcept;

    inline constexpr ScriptContextTypeId NetworkSystemContextTypeId = MakeStableTypeId("JBro.Network.SystemContext");

    inline constexpr ScriptContextRequirement NetworkSystemContextRequirement =
    {
        NetworkSystemContextTypeId,
        NetworkSystemContextAbiVersion,
        static_cast<std::uint32_t>(sizeof(NetworkSystemContext))
    };

    ScriptContextBlock MakeNetworkSystemContextBlock(const NetworkSystemContext& context) noexcept;
    const NetworkSystemContext* FindNetworkSystemContext(const ScriptModuleLoadContext& context) noexcept;
}
