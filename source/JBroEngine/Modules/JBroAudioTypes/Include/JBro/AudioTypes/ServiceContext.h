#pragma once

#include <JBro/AudioTypes/Service/AudioService.h>

#include <cstdint>

namespace JBro
{
    inline constexpr std::uint32_t AudioServiceContextAbiVersion = 1;

    // 오디오의 값 서비스 묶음이다(D-197). 스크립트 프렐류드가 이것을 공개한다. 소유하지 않는다 - 호스트의 것을
    // 로드·재바인딩 때 복사한다. 차원과 무관하므로 두 프렐류드가 같은 줄을 공유한다.
    struct AudioServiceContext
    {
        std::uint32_t AbiVersion = AudioServiceContextAbiVersion;
        Service::AudioService Audio;
    };

    // Main-thread only.
    void BindAudioServiceContext(const AudioServiceContext& context);
    const AudioServiceContext& GetAudioServices();
}
