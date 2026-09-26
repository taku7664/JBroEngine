#pragma once

#include <JBro/SaveTypes/Service/SaveService.h>

#include <cstdint>

namespace JBro
{
    inline constexpr std::uint32_t SaveServiceContextAbiVersion = 1;

    // 값 서비스 묶음이다. 두 차원의 프렐류드가 이것을 공개한다(D-218). 소유하지 않는다 -
    // 호스트의 것을 로드·재바인딩 때 복사한다.
    struct SaveServiceContext
    {
        std::uint32_t AbiVersion = SaveServiceContextAbiVersion;
        Service::SaveService Save;
    };

    // Main-thread only.
    void BindSaveServiceContext(const SaveServiceContext& context);
    const SaveServiceContext& GetSaveServices();
}
