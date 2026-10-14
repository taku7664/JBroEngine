#pragma once

// 컴포넌트 데이터다. **스크립트는 이 헤더에 닿지 않는다** - 프렐류드의 핸들(`AudioSource`)로만 만진다(D-302).
// 정규화한 이름으로 써도 스크립트 대상 빌드에서는 여기서 컴파일이 실패한다.
#if defined(JBRO_SCRIPT_TARGET)
#error "A script reaches Component::AudioSource through its handle in <JBro/ScriptAPI.h>, not the component data header."
#endif

#include <JBro/AssetTypes/AssetTypesReflection.h>
#include <JBro/AudioTypes/AudioBusName.h>
#include <JBro/AudioTypes/AudioSourceTypes.h>
#include <JBro/AudioTypes/AudioTypes.h>
#include <JBro/Reflection/ContainerTypeDescriptors.h>
#include <JBro/Reflection/CoreTypeDescriptors.h>
#include <JBro/Reflection/EnumDescriptor.h>
#include <JBro/Runtime/Component.h>
#include <JBro/Types/Array.h>

#include <cstdint>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/Int.h>
#include <JBro/Types/UInt.h>

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
        // 해석된 핸들은 인스펙터에 줄을 두지 않는다(D-309) - 고르는 칸은 `clipId` 다.
        JBRO_FIELD(AssetHandle, clip, NoSerialize() | ReadOnly() | Tooltip("clipId 에서 해석된 값") | Hidden());
        // 프로젝트 설정의 버스 이름이다. 비우면 Master 다.
        JBRO_FIELD(AudioBusName, bus);
        JBRO_FIELD(Float, volume, Range(0.0f, 1.0f)) = 1.0f;
        JBRO_FIELD(Float, pitch, Range(0.1f, 4.0f)) = 1.0f;
        JBRO_FIELD(Bool,  loop) = false;
        // 켜질 때(캔버스 시작 포함) 저절로 한 번 재생한다. 루프가 아니면 다시 켜질 때까지 다시 울리지 않는다.
        JBRO_FIELD(Bool,  playOnStart) = true;
        // 켜면 듣는 자리와의 거리·방향이 소리에 든다. 끄면 배경음처럼 가운데에서 그대로 들린다. 꺼져 있으면 아래 다섯 줄은 인스펙터에 없다(D-309).
        JBRO_FIELD(Bool,  spatial) = false;
        JBRO_FIELD(AudioAttenuation, attenuation, Category("Spatial") | VisibleWhen("spatial")) = AudioAttenuation::Inverse;
        JBRO_FIELD(Float, minDistance, Category("Spatial") | VisibleWhen("spatial")) = 1.0f;
        JBRO_FIELD(Float, maxDistance, Category("Spatial") | VisibleWhen("spatial")) = 50.0f;
        JBRO_FIELD(Float, rolloff, Category("Spatial") | VisibleWhen("spatial")) = 1.0f;
        // 0 이면 끈다. 움직이는 소스와 듣는 자리의 속도 차로 음높이가 바뀐다.
        JBRO_FIELD(Float, doppler, Category("Spatial") | VisibleWhen("spatial")) = 0.0f;
        // 보이스가 모자랄 때 낮은 것부터 훔친다(0..255). 배경음은 높게, 자잘한 효과음은 낮게 둔다.
        JBRO_FIELD(Int32, priority, Range(0.0f, 255.0f)) = 128;
        // 시작할 때 이만큼 키운다(초).
        JBRO_FIELD(Float, fadeIn) = 0.0f;
        // 이 소스에만 거는 필터다(Hz, 0 이면 끔). 벽 너머의 소리는 저역 통과 800 쯤, 무전기는 고역 통과 1500 쯤이다.
        // 재생 중에 바꿔도 된다. 둘 다 0 인 소스는 필터 비용이 없다.
        JBRO_FIELD(Float, lowPass, Category("Filter")) = 0.0f;
        JBRO_FIELD(Float, highPass, Category("Filter")) = 0.0f;
        // 이 소스에만 거는 이펙트 사슬이다(D-306, 기존 `AudioPlayer.EffectGuids`). `.jfx` 에셋을 목록의 차례대로 걸고 넷까지다.
        // 재생 중에 목록이나 `.jfx` 를 고치면 다시 시작하지 않고 값만 바뀐다. 사슬을 건 소스는 그만큼 DSP 비용이 든다.
        JBRO_FIELD(Array<AssetId>, audioEffectIds, Category("Effects"));
        JBRO_FIELD(Array<AssetHandle>, audioEffects, NoSerialize() | ReadOnly() | Category("Effects") | Tooltip("audioEffectIds 에서 해석된 값") | Hidden());
        JBRO_FIELD(AudioSourceState, state, NoSerialize() | ReadOnly()) = AudioSourceState::Idle;

        // ── 시스템 전용 ───────────────────────────────────────────────────────────────────────
        // 오디오 시스템만 쓴다. 스크립트는 `Service::AudioService` 를 거친다. 리플렉션에 나타나지 않아 저장·복사되지 않는다.
        struct Runtime
        {
            AudioVoiceHandle voice;
            // 지금 보이스가 재생하는 에셋이다. `clip` 이 바뀌면 교체한다.
            AssetHandle playingClip;
            Float position[3] = {0.0f, 0.0f, 0.0f};
            Float lastVolume = -1.0f;
            Float lastPitch = -1.0f;
            Bool lastLoop = false;
            AudioBusName lastBus;
            Float lastLowPass = 0.0f;
            Float lastHighPass = 0.0f;
            Bool wasActive = false;
            // 이번 활성 구간에 `playOnStart` 를 이미 썼는가.
            Bool playOnStartUsed = false;
            // 스크립트가 `Play` 를 불렀다. 다음 갱신이 위치를 채워 시작한다.
            Bool playRequested = false;
            // 스크립트가 멈춰 둔 것이다. 시스템이 다시 켜지 않는다.
            Bool stoppedByScript = false;
            // 보이스를 이은 때의 버스 구성 세대다(D-240). 프로젝트 설정을 저장해 버스를 다시 세우면 바뀐다 - 그때 버스를 다시 잇는다.
            UInt32 busGeneration = 0;
            // 마지막으로 건 이펙트 사슬의 지문이다(D-306) - 목록·에셋·에셋의 판이 바뀌면 달라진다. 그때만 사슬 값을 다시 건다.
            UInt32 effectSignature = 0;
        };
        Runtime runtime;
    };
}
