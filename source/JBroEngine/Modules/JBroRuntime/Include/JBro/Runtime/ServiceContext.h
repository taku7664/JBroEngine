#pragma once

#include <JBro/Runtime/RandomService.h>
#include <JBro/Runtime/TimeService.h>

#include <cstdint>
#include <JBro/Types/UInt.h>

namespace JBro
{
    // 2: 시간과 난수가 들어왔고(D-242) `GameScriptBase` 의 훅이 인자를 잃었다 - 가상 함수 표가 바뀌었다.
    inline constexpr UInt32 ServiceContextAbiVersion = 2;

    // 차원과 무관한 값 서비스다. 두 프렐류드가 이것을 공개한다(§10.3).
    struct ServiceContext
    {
        UInt32 AbiVersion = ServiceContextAbiVersion;
        Service::TimeService Time;
        Service::RandomService Random;
    };

    void BindServiceContext(const ServiceContext& context);
    const ServiceContext& GetServiceContext();
}
