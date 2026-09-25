#pragma once

#include <JBro/AssetTypes/AssetTypes.h>
#include <JBro/AudioTypes/AudioBusName.h>

namespace JBro::Component
{
    class AudioSource;
}

namespace JBro::System
{
    // 서비스가 부르는 오디오 시스템의 면이다(D-197). 구현은 엔진의 `JBroAudio` 에 있고 스크립트는 이것을 보지 않는다 -
    // `Service::AudioService` 가 `.cpp` 에서만 부른다(§5). **메인 스레드 전용이다.**
    class IAudioSystem
    {
    public:
        virtual ~IAudioSystem() = default;

        // 한 번 울리고 끝나는 소리다. 소스 컴포넌트가 필요 없다. 공간화하지 않는다.
        virtual void PlayOneShot(AssetHandle clip, AudioBusName bus, float volume, float pitch) = 0;
        // 월드의 한 자리에서 울린다. 2D 는 z 를 0 으로 둔다.
        virtual void PlayOneShotAt(AssetHandle clip, AudioBusName bus, float volume, float x, float y, float z) = 0;

        // 다음 갱신에서 시작한다(위치를 채워야 해서다). 이미 재생 중이면 처음부터 다시 한다.
        virtual void PlaySource(Component::AudioSource& source) = 0;
        virtual void StopSource(Component::AudioSource& source, float fadeOutSeconds) = 0;
        virtual void PauseSource(Component::AudioSource& source) = 0;
        virtual void ResumeSource(Component::AudioSource& source) = 0;
        virtual bool IsSourcePlaying(const Component::AudioSource& source) const = 0;
        virtual double GetSourceTime(const Component::AudioSource& source) const = 0;
        // 컴포넌트가 떼일 때 부른다. 보이스를 멈춘다.
        virtual void ReleaseSource(Component::AudioSource& source) = 0;

        virtual void SetBusVolume(AudioBusName bus, float volume) = 0;
        virtual float GetBusVolume(AudioBusName bus) const = 0;
        virtual void SetBusMuted(AudioBusName bus, bool muted) = 0;
        virtual bool IsBusMuted(AudioBusName bus) const = 0;
        // 게임의 소리를 전부 멈춘다(에디터 미리 듣기는 그대로다).
        virtual void StopAll() = 0;
    };
}
