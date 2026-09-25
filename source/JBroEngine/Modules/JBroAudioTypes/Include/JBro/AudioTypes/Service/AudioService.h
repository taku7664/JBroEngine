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
        // 버스의 이펙트 사슬 전부(D-202). 한 칸만 바꾸려면 읽어서 고쳐 쓴다.
        void SetBusEffects(AudioBusName bus, const AudioBusEffects& effects) const;
        AudioBusEffects GetBusEffects(AudioBusName bus) const;
        // 자주 쓰는 하나: 이 위를 깎는다(Hz). 0 이면 끈다. 일시 정지 화면에서 `SetBusLowPass("Music", 800)`.
        void SetBusLowPass(const char* bus, float cutoffHz) const;
        // 버스 음량을 `seconds` 에 걸쳐 옮긴다(D-205). `FadeBusVolume("Music", 0.2f, 1.5f)` - 스냅숏 대신 이것 몇 줄이다.
        void FadeBusVolume(const char* bus, float volume, float seconds) const;
        void FadeBusVolume(AudioBusName bus, float volume, float seconds) const;
        void StopAll() const;

        // 옵션 화면의 "출력 장치" 다(D-203). 목록을 열 때 `GetOutputDeviceCount` 를 한 번 부르고(몇 ms 걸린다) 그 뒤
        // 이름을 읽는다. 없는 이름을 주면 시스템 기본으로 연다.
        std::uint32_t GetOutputDeviceCount() const;
        const char* GetOutputDeviceName(std::uint32_t index) const;
        const char* GetOutputDevice() const;
        bool SetOutputDevice(const char* name) const;
        // 웹에서 브라우저가 첫 입력 전까지 소리를 막고 있다. 참이면 "눌러서 시작" 같은 안내를 띄운다.
        bool IsWaitingForUserGesture() const;
        // 창이 포커스를 잃었을 때 소리를 끌지다(옵션 화면의 "백그라운드에서 음소거").
        void SetMuteWhenUnfocused(bool mute) const;
        bool IsMuteWhenUnfocused() const;
    };
}
