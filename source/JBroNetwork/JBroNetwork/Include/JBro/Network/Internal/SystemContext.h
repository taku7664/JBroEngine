#pragma once

#include <JBro/Network/System/INetworkSystem.h>

#include <cstdint>
#include <type_traits>

namespace JBro
{
    inline constexpr std::uint32_t NetworkSystemContextAbiVersion = 1;

    // 네트워크 시스템 인터페이스 묶음. 차원과 무관하지만 `JBroRuntime::SystemContext` 에 넣지 않는다 - 거기 넣으면 네트워크를
    // 쓰지 않는 게임 바이너리까지 의존을 끌고 간다(D-43 과 같은 이유). D-37 확장 블록으로 전달되고 서비스 구현만 읽는다.
    struct NetworkSystemContext
    {
        std::uint32_t AbiVersion = NetworkSystemContextAbiVersion;
        System::INetworkSystem* Network = nullptr;
    };

    static_assert(std::is_standard_layout_v<NetworkSystemContext>);
    static_assert(std::is_trivially_copyable_v<NetworkSystemContext>);
    static_assert(offsetof(NetworkSystemContext, AbiVersion) == 0);

    // Main-thread only. 호스트의 값을 이 모듈 사본에 복사한다.
    void BindNetworkSystemContext(const NetworkSystemContext& context);
    const NetworkSystemContext& GetNetworkSystems();
}
