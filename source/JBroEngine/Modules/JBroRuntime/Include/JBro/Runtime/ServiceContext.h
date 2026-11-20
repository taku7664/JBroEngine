#pragma once

#include <JBro/Runtime/MaterialService.h>
#include <JBro/Runtime/ObjectService.h>
#include <JBro/Runtime/PostProcessService.h>
#include <JBro/Runtime/RandomService.h>
#include <JBro/Runtime/TimeService.h>
#include <JBro/Runtime/ViewportService.h>

#include <cstdint>
#include <JBro/Types/UInt.h>

namespace JBro
{
    // 2: 시간과 난수가 들어왔고(D-242) `GameScriptBase` 의 훅이 인자를 잃었다 - 가상 함수 표가 바뀌었다.
    // 3: 스크립트가 컴포넌트에서 떨어졌다(D-302) - `GameScriptBase` 가 `ComponentBase` 를 상속하지 않아 가상 함수 표가 바뀌었다.
    // 5: 후처리 서비스가 들어왔다(D-346).
    // 6: 머티리얼 서비스가 들어왔다(D-348).
    // 7: 뷰포트 서비스가 들어왔다(D-438).
    inline constexpr UInt32 ServiceContextAbiVersion = 7;

    // 차원과 무관한 값 서비스다. 두 프렐류드가 이것을 공개한다(§10.3).
    struct ServiceContext
    {
        UInt32 AbiVersion = ServiceContextAbiVersion;
        Service::TimeService Time;
        Service::RandomService Random;
        Service::ObjectService Objects;
        Service::PostProcessService PostProcess;
        Service::MaterialService Material;
        // 캔버스의 뷰포트 목록(D-438).
        Service::ViewportService Viewports;
    };

    void BindServiceContext(const ServiceContext& context);
    const ServiceContext& GetServiceContext();
}
