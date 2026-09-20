#pragma once

#include <JBro/Network/Service/NetworkService.h>
#include <JBro/Network/Service/NetworkSessionService.h>

#include <cstdint>

namespace JBro
{
    inline constexpr std::uint32_t NetworkServiceContextAbiVersion = 1;

    // 값 서비스 묶음. 스크립트 프렐류드가 이것을 공개한다. 소유하지 않는다 - 호스트의 것을 로드·재바인딩 때 복사한다.
    struct NetworkServiceContext
    {
        std::uint32_t AbiVersion = NetworkServiceContextAbiVersion;
        Service::NetworkSessionService Session;
        Service::NetworkService Network;
    };

    // Main-thread only.
    void BindNetworkServiceContext(const NetworkServiceContext& context);
    const NetworkServiceContext& GetNetworkServices();
}
