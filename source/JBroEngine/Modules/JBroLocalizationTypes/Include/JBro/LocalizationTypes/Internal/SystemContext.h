#pragma once

#include <JBro/LocalizationTypes/System/ILocalization.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <JBro/Types/UInt.h>

namespace JBro
{
    inline constexpr UInt32 LocalizationSystemContextAbiVersion = 1;

    // 게임 문자열 조회 인터페이스 묶음이다(D-226). 호스트가 소유한 구현을 가리킨다.
    // 세이브처럼 D-37 확장 블록으로 넘긴다 - Runtime 이 로컬라이징 모듈을 알 필요가 없다. 서비스 구현과 텍스트 시스템만 읽는다.
    struct LocalizationSystemContext
    {
        UInt32 AbiVersion = LocalizationSystemContextAbiVersion;
        System::ILocalization* Localization = nullptr;
    };

    static_assert(std::is_standard_layout_v<LocalizationSystemContext>);
    static_assert(std::is_trivially_copyable_v<LocalizationSystemContext>);
    static_assert(offsetof(LocalizationSystemContext, AbiVersion) == 0);

    // Main-thread only. 호스트의 값을 이 모듈 사본에 복사한다.
    void BindLocalizationSystemContext(const LocalizationSystemContext& context);
    const LocalizationSystemContext& GetLocalizationSystems();
}
