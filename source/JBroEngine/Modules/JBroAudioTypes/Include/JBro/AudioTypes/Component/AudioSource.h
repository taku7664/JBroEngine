#pragma once

#include <JBro/AssetTypes/AssetTypesReflection.h>
#include <JBro/AudioTypes/AudioBusName.h>
#include <JBro/AudioTypes/AudioTypes.h>
#include <JBro/Reflection/CoreTypeDescriptors.h>
#include <JBro/Reflection/EnumDescriptor.h>
#include <JBro/Runtime/Component.h>

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

namespace JBro::Component
{
    // 소리가 나는 자리다(D-197). **차원과 무관하다** - 2D·3D 시스템이 각자 자기 Transform 에서 위치를 채운다.
    // 3D 에서만 뜻이 있는 필드(원뿔 감쇠처럼 소스의 방향이 필요한 것)는 여기에 두지 않는다(D-198).
    //
    // 재생 파라미터는 여기가 유일한 원천이다 - 에셋의 임포트 옵션에는 디코드 방식만 있다(D-198).
    // 재생 중에 바뀌는 값은 볼륨·피치·루프·위치다. 거리·감쇠·도플러는 다음 재생부터 적용된다(miniaudio 가 그 값을
    // 원자 변수로 들지 않는다).
    class AudioSource final : public ComponentBase
    {
    public:
        static constexpr const char* StaticTypeName()
        {
            return "Component::AudioSource";
        }

        ComponentTypeId GetTypeId() const override
        {
            return MakeStableTypeId(StaticTypeName());
        }

        // 떼일 때 제 보이스를 멈춘다. 오브젝트가 사라지면 소리도 끝나야 한다.
        void OnDetached() override;

        JBRO_REFLECT_BODY(AudioSource)

        JBRO_FIELD(AssetId,     clipId);
        JBRO_FIELD(AssetHandle, clip, NoSerialize() | ReadOnly() | Tooltip("clipId 에서 해석된 값"));
        // 프로젝트 설정의 버스 이름이다. 비우면 Master 다.
        JBRO_FIELD(AudioBusName, bus);
        JBRO_FIELD(float, volume, Range(0, 1)) = 1.0f;
        JBRO_FIELD(float, pitch, Range(0.1f, 4.0f)) = 1.0f;
        JBRO_FIELD(bool,  loop) = false;
        // 켜질 때(캔버스 시작 포함) 저절로 한 번 재생한다. 루프가 아니면 다시 켜질 때까지 다시 울리지 않는다.
        JBRO_FIELD(bool,  playOnStart) = true;
        // 켜면 듣는 자리와의 거리·방향이 소리에 든다. 끄면 배경음처럼 가운데에서 그대로 들린다.
        JBRO_FIELD(bool,  spatial) = false;
        JBRO_FIELD(AudioAttenuation, attenuation, Category("Spatial")) = AudioAttenuation::Inverse;
        JBRO_FIELD(float, minDistance, Category("Spatial")) = 1.0f;
        JBRO_FIELD(float, maxDistance, Category("Spatial")) = 50.0f;
        JBRO_FIELD(float, rolloff, Category("Spatial")) = 1.0f;
        // 0 이면 끈다. 움직이는 소스와 듣는 자리의 속도 차로 음높이가 바뀐다.
        JBRO_FIELD(float, doppler, Category("Spatial")) = 0.0f;
        // 보이스가 모자랄 때 낮은 것부터 훔친다(0..255). 배경음은 높게, 자잘한 효과음은 낮게 둔다.
        JBRO_FIELD(std::int32_t, priority, Range(0, 255)) = 128;
        // 시작할 때 이만큼 키운다(초).
        JBRO_FIELD(float, fadeIn) = 0.0f;
        // 이 소스에만 거는 필터다(Hz, 0 이면 끔). 벽 너머의 소리는 저역 통과 800 쯤, 무전기는 고역 통과 1500 쯤이다.
        // 재생 중에 바꿔도 된다. 둘 다 0 인 소스는 필터 비용이 없다.
        JBRO_FIELD(float, lowPass, Category("Filter")) = 0.0f;
        JBRO_FIELD(float, highPass, Category("Filter")) = 0.0f;
        JBRO_FIELD(AudioSourceState, state, NoSerialize() | ReadOnly()) = AudioSourceState::Idle;

        // ── 시스템 전용 ───────────────────────────────────────────────────────────────────────
        // 오디오 시스템만 쓴다. 스크립트는 `Service::AudioService` 를 거친다. 리플렉션에 나타나지 않아 저장·복사되지 않는다.
        struct Runtime
        {
            AudioVoiceHandle voice;
            // 지금 보이스가 재생하는 에셋이다. `clip` 이 바뀌면 교체한다.
            AssetHandle playingClip;
            float position[3] = {0.0f, 0.0f, 0.0f};
            float lastVolume = -1.0f;
            float lastPitch = -1.0f;
            bool lastLoop = false;
            AudioBusName lastBus;
            float lastLowPass = 0.0f;
            float lastHighPass = 0.0f;
            bool wasActive = false;
            // 이번 활성 구간에 `playOnStart` 를 이미 썼는가.
            bool playOnStartUsed = false;
            // 스크립트가 `Play` 를 불렀다. 다음 갱신이 위치를 채워 시작한다.
            bool playRequested = false;
            // 스크립트가 멈춰 둔 것이다. 시스템이 다시 켜지 않는다.
            bool stoppedByScript = false;
        };
        Runtime runtime;
    };
}
