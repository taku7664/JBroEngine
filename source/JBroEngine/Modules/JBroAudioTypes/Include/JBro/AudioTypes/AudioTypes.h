#pragma once

#include <JBro/Core/Core.h>

#include <cstdint>
#include <type_traits>

// 오디오의 값 타입이다(D-197). 스크립트가 보는 층(Tier S)이라 컴포넌트·서비스가 이것을 쓰고, 엔진의 믹서(`JBroAudio`)도
// 같은 타입으로 말한다. **장치·믹서·miniaudio 는 여기 나타나지 않는다.**

namespace JBro
{
    // 믹서가 가진 보이스 하나의 자리다. 세대가 다르면 이미 끝난 보이스다 - 무엇을 해도 아무 일이 없다.
    // 스크립트에 나가지 않는다(D-197). 컴포넌트의 시스템 전용 필드가 든다.
    struct AudioVoiceHandle
    {
        std::uint32_t index = 0;
        std::uint32_t generation = 0;

        constexpr bool IsSet() const
        {
            return generation != 0;
        }
    };

    // 믹서에 등록된 클립 하나다. 에셋의 PCM·압축 바이트를 빌려 가리킨다.
    struct AudioClipHandle
    {
        std::uint32_t index = 0;
        std::uint32_t generation = 0;

        constexpr bool IsSet() const
        {
            return generation != 0;
        }
    };

    // 믹서의 버스 번호다. 0 은 Master, 1 은 에디터 미리 듣기이고 프로젝트 버스는 그 뒤다.
    using AudioBusId = std::uint8_t;
    inline constexpr AudioBusId AudioMasterBus = 0;
    inline constexpr AudioBusId AudioEditorPreviewBus = 1;
    inline constexpr AudioBusId AudioFirstProjectBus = 2;
    // 버스는 사람이 손으로 관리하는 카테고리라 이 정도면 넉넉하다(기존 엔진 `MAX_AUDIO_BUSES` 16 + 예약 둘).
    inline constexpr std::uint32_t AudioMaxBuses = 18;
    // 예약 이름이다. 빈 이름도 Master 다.
    inline constexpr const char* AudioMasterBusName = "Master";

    // 거리에 따라 줄어드는 곡선이다. miniaudio 의 `ma_attenuation_model` 과 같은 순서다.
    enum class AudioAttenuation : std::uint8_t
    {
        // 거리와 무관하다 - 방향(팬)만 적용된다.
        None,
        // 현실의 역제곱에 가깝다.
        Inverse,
        // 최소~최대 거리 사이를 직선으로 줄인다. 게임에서 다루기 쉽다.
        Linear,
        // 가까이서 급하게 줄어든다.
        Exponential
    };

    static_assert(std::is_trivially_copyable_v<AudioVoiceHandle>);
    static_assert(std::is_trivially_copyable_v<AudioClipHandle>);
    static_assert(sizeof(AudioVoiceHandle) == 8);
}
