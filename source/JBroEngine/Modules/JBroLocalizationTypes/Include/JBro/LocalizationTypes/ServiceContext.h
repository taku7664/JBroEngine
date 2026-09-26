#pragma once

#include <JBro/LocalizationTypes/Service/LocalizationService.h>

#include <cstdint>

namespace JBro
{
    inline constexpr std::uint32_t LocalizationServiceContextAbiVersion = 1;

    // 값 서비스 묶음이다. 두 차원의 프렐류드가 이것을 공개한다(D-226). 소유하지 않는다 -
    // 호스트의 것을 로드·재바인딩 때 복사한다.
    struct LocalizationServiceContext
    {
        std::uint32_t AbiVersion = LocalizationServiceContextAbiVersion;
        Service::LocalizationService Localization;
    };

    // Main-thread only.
    void BindLocalizationServiceContext(const LocalizationServiceContext& context);
    const LocalizationServiceContext& GetLocalizationServices();
}
