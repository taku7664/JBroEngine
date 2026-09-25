#pragma once

#include <JBro/AssetTypes/AssetTypes.h>
#include <JBro/AudioTypes/AudioBusName.h>
#include <JBro/AudioTypes/Component/AudioSource.h>
#include <JBro/Runtime/Ref.h>

namespace JBro::Service
{
    // 스크립트가 소리를 내는 길이다(D-197). **장치·믹서·보이스는 여기에 나타나지 않는다** - 시스템은 `.cpp` 에서만
    // 부른다(§5). 오디오가 없는 호스트(끈 설정·테스트)에서는 모두 조용히 아무 일도 하지 않는다.
    //
    // 버스는 이름이다. `"Music"` 처럼 글자로 줘도 되고(해시만 계산하고 할당하지 않는다), 자주 부르면 `AudioBusName` 을
    // 멤버로 들고 쓴다.
    //
    // 예:
    //   GetAudioServices().Audio.PlayOneShot(m_jumpClip);
    //   GetAudioServices().Audio.SetBusVolume("Music", 0.5f);
    //   GetAudioServices().Audio.Play(m_engineSound);           // Ref<Component::AudioSource>
    class AudioService
    {
    public:
        // 한 번 울린다. 가운데에서 들린다.
        void PlayOneShot(AssetHandle clip, float volume = 1.0f, float pitch = 1.0f) const;
        void PlayOneShot(AssetHandle clip, const char* bus, float volume = 1.0f, float pitch = 1.0f) const;
        void PlayOneShot(AssetHandle clip, AudioBusName bus, float volume = 1.0f, float pitch = 1.0f) const;
        // 월드의 자리에서 한 번 울린다(공간화한다). 2D 는 `PlayOneShotAt(clip, x, y)` 다.
        void PlayOneShotAt(AssetHandle clip, float x, float y, float z = 0.0f, float volume = 1.0f) const;
        void PlayOneShotAt(AssetHandle clip, AudioBusName bus, float x, float y, float z, float volume) const;

        // 소스 컴포넌트를 다룬다. 무효한 `Ref` 는 아무 일도 하지 않는다.
        void Play(Ref<Component::AudioSource> source) const;
        void Stop(Ref<Component::AudioSource> source, float fadeOutSeconds = 0.0f) const;
        void Pause(Ref<Component::AudioSource> source) const;
        void Resume(Ref<Component::AudioSource> source) const;
        // 재생 중이거나 재생을 요청해 두었으면 참이다.
        bool IsPlaying(Ref<Component::AudioSource> source) const;
        // 재생한 시간(초). 재생 중이 아니면 0 이다.
        double GetTime(Ref<Component::AudioSource> source) const;

        // 옵션 화면의 "배경음 크기" 같은 것이다. 0..1.
        void SetBusVolume(const char* bus, float volume) const;
        void SetBusVolume(AudioBusName bus, float volume) const;
        float GetBusVolume(const char* bus) const;
        float GetBusVolume(AudioBusName bus) const;
        void SetBusMuted(const char* bus, bool muted) const;
        void SetBusMuted(AudioBusName bus, bool muted) const;
        bool IsBusMuted(AudioBusName bus) const;
        void StopAll() const;
    };
}
