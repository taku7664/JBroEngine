#pragma once

#include <JBro/LocalizationTypes/Service/LocalizationService.h>

#include <cstdint>
#include <JBro/Types/UInt.h>

namespace JBro
{
    inline constexpr UInt32 LocalizationServiceContextAbiVersion = 1;

    // 값 서비스 묶음이다. 두 차원의 프렐류드가 이것을 공개한다(D-226). 소유하지 않는다 -
    // 호스트의 것을 로드·재바인딩 때 복사한다.
    struct LocalizationServiceContext
    {
        UInt32 AbiVersion = LocalizationServiceContextAbiVersion;
        Service::LocalizationService Localization;
    };

    // Main-thread only.
    void BindLocalizationServiceContext(const LocalizationServiceContext& context);
    const LocalizationServiceContext& GetLocalizationServices();
}
