#pragma once

#include <JBro/AudioTypes/System/IAudioSystem.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace JBro
{
    inline constexpr std::uint32_t AudioSystemContextAbiVersion = 1;

    // 오디오 시스템 인터페이스다(D-197). 사용자에게 공개하지 않으려고 Internal 경계에 둔다. 서비스 구현과 컴포넌트의
    // `OnDetached` 만 읽으며 대상의 수명을 소유하지 않는다. 공통 `SystemContext` 에 넣지 않는다 - 공통 계층이 오디오를
    // 참조하게 된다(D-43·D-122 와 같은 이유).
    struct AudioSystemContext
    {
        std::uint32_t AbiVersion = AudioSystemContextAbiVersion;
        System::IAudioSystem* Audio = nullptr;
    };

    static_assert(std::is_standard_layout_v<AudioSystemContext>);
    static_assert(std::is_trivially_copyable_v<AudioSystemContext>);
    static_assert(offsetof(AudioSystemContext, AbiVersion) == 0);

    // Main-thread only. 호스트의 값을 이 모듈 사본에 복사한다.
    void BindAudioSystemContext(const AudioSystemContext& context);
    const AudioSystemContext& GetAudioSystems();
}
