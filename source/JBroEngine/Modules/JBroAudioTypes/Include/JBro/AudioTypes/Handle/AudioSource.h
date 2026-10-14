#pragma once

#include <JBro/AssetTypes/AssetTypes.h>
#include <JBro/AudioTypes/AudioBusName.h>
#include <JBro/AudioTypes/AudioSourceTypes.h>
#include <JBro/Runtime/ComponentHandle.h>
#include <JBro/Runtime/ComponentHandleReflection.h>
#include <JBro/Types/Array.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/Int.h>
#include <JBro/Types/UInt.h>

#include <type_traits>

namespace JBro::Component
{
    class AudioSource;
}

namespace JBro::Handle
{
    // 스크립트가 보는 `AudioSource` 다(D-302). 데이터는 엔진의 `Component::AudioSource` 에 있고, 이것은 그 자리를 가리키는 24B 값이다.
    // 소리가 나는 자리다(D-197). 재생·멈춤은 `GetAudioServices().Audio` 의 `Play`·`Stop` 등에 이 핸들을 넘긴다.
    // 무효한 핸들의 멤버는 경고를 남기고 아무것도 하지 않으며, 값을 돌려주는 멤버는 그 타입의 기본값을 준다.
    class AudioSource final : public Internal::ComponentHandle
    {
    public:
        // 데이터 클래스와 같은 글자다 - 그래서 타입 id 가 같고 `GetComponent<AudioSource>()` 가 이것으로 찾는다.
        static constexpr const char* StaticTypeName()
        {
            return "Component::AudioSource";
        }

        // 저장되는 클립 id 다.
        AssetId GetClipId() const;

        // 재생할 클립이다. 저장되는 `ClipId` 는 바뀌지 않는다.
        AssetHandle GetClip() const;
        void SetClip(AssetHandle value);

        // 프로젝트 설정의 버스 이름이다. 비우면 Master 다.
        AudioBusName GetBus() const;
        void SetBus(AudioBusName value);
        Float GetVolume() const;
        void SetVolume(Float value);
        Float GetPitch() const;
        void SetPitch(Float value);
        Bool GetLoop() const;
        void SetLoop(Bool value);
        Bool GetPlayOnStart() const;
        void SetPlayOnStart(Bool value);
        Bool GetSpatial() const;
        void SetSpatial(Bool value);

        // 아래 다섯은 다음 재생부터 적용된다.
        AudioAttenuation GetAttenuation() const;
        void SetAttenuation(AudioAttenuation value);
        Float GetMinDistance() const;
        void SetMinDistance(Float value);
        Float GetMaxDistance() const;
        void SetMaxDistance(Float value);
        Float GetRolloff() const;
        void SetRolloff(Float value);
        Float GetDoppler() const;
        void SetDoppler(Float value);
        Int32 GetPriority() const;
        void SetPriority(Int32 value);
        Float GetFadeIn() const;
        void SetFadeIn(Float value);
        Float GetLowPass() const;
        void SetLowPass(Float value);
        Float GetHighPass() const;
        void SetHighPass(Float value);

        // 이 소스에 건 이펙트 에셋(`.jfx`) 목록이다. `ids` 를 덮어쓴다 - 무효면 비운다.
        void GetAudioEffectIds(Array<AssetId>& ids) const;
        Component::AudioSourceState GetState() const;

    private:
        Component::AudioSource* Resolve(const char* operation) const;
    };

    static_assert(sizeof(AudioSource) == 24, "a component handle is a 24-byte script value");
    static_assert(std::is_standard_layout_v<AudioSource>, "a component handle must stay standard layout");
    static_assert(std::is_trivially_copyable_v<AudioSource>, "a component handle must stay trivially copyable");
}

// 스크립트 필드로 저장한다(`JBRO_FIELD(AudioSource, target)`).
JBRO_DEFINE_COMPONENT_HANDLE_TYPE(JBro::Handle::AudioSource);
