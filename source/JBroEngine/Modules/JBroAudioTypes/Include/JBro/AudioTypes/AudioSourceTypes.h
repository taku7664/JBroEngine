#pragma once

// 오디오 소스의 값 타입이다. 컴포넌트 데이터 헤더와 스크립트가 함께 본다(D-302 - 데이터 헤더는 스크립트 대상에서 막혀 있다).

#include <JBro/AudioTypes/AudioTypes.h>
#include <JBro/Reflection/EnumDescriptor.h>

#include <cstdint>

namespace JBro::Component
{
    // 소스가 지금 어디에 있는가다. 인스펙터가 읽기 전용으로 보여 준다 - "왜 안 들리지" 를 화면에서 알 수 있게.
    enum class AudioSourceState : std::uint8_t
    {
        // 재생한 적이 없다(또는 꺼졌다가 다시 무장됐다).
        Idle,
        Playing,
        Paused,
        // 루프가 아닌 소리가 끝까지 재생됐다. `playOnStart` 는 다시 켜질 때까지 다시 울리지 않는다.
        Finished,
        // 클립이 없거나 읽히지 않았다. 매 프레임 다시 시도하지 않는다.
        NoClip
    };
}

namespace JBro
{
    JBRO_DEFINE_ENUM_TYPE(AudioAttenuation, "JBro.AudioAttenuation",
        { AudioAttenuation::None,        "None" },
        { AudioAttenuation::Inverse,     "Inverse" },
        { AudioAttenuation::Linear,      "Linear" },
        { AudioAttenuation::Exponential, "Exponential" });
    JBRO_DEFINE_ENUM_TYPE(Component::AudioSourceState, "Component::AudioSourceState",
        { Component::AudioSourceState::Idle,     "Idle" },
        { Component::AudioSourceState::Playing,  "Playing" },
        { Component::AudioSourceState::Paused,   "Paused" },
        { Component::AudioSourceState::Finished, "Finished" },
        { Component::AudioSourceState::NoClip,   "NoClip" });
}
