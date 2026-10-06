#include <JBro/AudioTypes/Service/AudioService.h>

#include <JBro/AudioTypes/Internal/SystemContext.h>
#include <JBro/AudioTypes/System/IAudioSystem.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/UInt.h>

namespace JBro::Service
{
    namespace
    {
        System::IAudioSystem* Audio()
        {
            return GetAudioSystems().Audio;
        }

        // 글자는 해시만 계산한다 - 인턴하지 않으므로 할당이 없다. 버스 표는 같은 해시로 찾는다.
        AudioBusName Named(const char* bus)
        {
            AudioBusName name;
            if (bus != nullptr && bus[0] != '\0')
            {
                name.id = MakeNameId(bus);
            }
            return name;
        }
    }

    void AudioService::PlayOneShot(AssetHandle clip, Float volume, Float pitch) const
    {
        PlayOneShot(clip, AudioBusName{}, volume, pitch);
    }

    void AudioService::PlayOneShot(AssetHandle clip, const char* bus, Float volume, Float pitch) const
    {
        PlayOneShot(clip, Named(bus), volume, pitch);
    }

    void AudioService::PlayOneShot(AssetHandle clip, AudioBusName bus, Float volume, Float pitch) const
    {
        if (System::IAudioSystem* audio = Audio())
        {
            audio->PlayOneShot(clip, bus, volume, pitch);
        }
    }

    void AudioService::PlayOneShotAt(AssetHandle clip, Float x, Float y, Float z, Float volume) const
    {
        PlayOneShotAt(clip, AudioBusName{}, x, y, z, volume);
    }

    void AudioService::PlayOneShotAt(AssetHandle clip, AudioBusName bus, Float x, Float y, Float z, Float volume) const
    {
        if (System::IAudioSystem* audio = Audio())
        {
            audio->PlayOneShotAt(clip, bus, volume, x, y, z);
        }
    }

    void AudioService::Play(Ref<Component::AudioSource> source) const
    {
        System::IAudioSystem* audio = Audio();
        if (Component::AudioSource* target = source.Get(); audio != nullptr && target != nullptr)
        {
            audio->PlaySource(*target);
        }
    }

    void AudioService::Stop(Ref<Component::AudioSource> source, Float fadeOutSeconds) const
    {
        System::IAudioSystem* audio = Audio();
        if (Component::AudioSource* target = source.Get(); audio != nullptr && target != nullptr)
        {
            audio->StopSource(*target, fadeOutSeconds);
        }
    }

    void AudioService::Pause(Ref<Component::AudioSource> source) const
    {
        System::IAudioSystem* audio = Audio();
        if (Component::AudioSource* target = source.Get(); audio != nullptr && target != nullptr)
        {
            audio->PauseSource(*target);
        }
    }

    void AudioService::Resume(Ref<Component::AudioSource> source) const
    {
        System::IAudioSystem* audio = Audio();
        if (Component::AudioSource* target = source.Get(); audio != nullptr && target != nullptr)
        {
            audio->ResumeSource(*target);
        }
    }

    Bool AudioService::IsPlaying(Ref<Component::AudioSource> source) const
    {
        System::IAudioSystem* audio = Audio();
        const Component::AudioSource* target = source.Get();
        return audio != nullptr && target != nullptr && audio->IsSourcePlaying(*target);
    }

    double AudioService::GetTime(Ref<Component::AudioSource> source) const
    {
        System::IAudioSystem* audio = Audio();
        const Component::AudioSource* target = source.Get();
        return audio != nullptr && target != nullptr ? audio->GetSourceTime(*target) : 0.0;
    }

    void AudioService::SetBusVolume(const char* bus, Float volume) const
    {
        SetBusVolume(Named(bus), volume);
    }

    void AudioService::SetBusVolume(AudioBusName bus, Float volume) const
    {
        if (System::IAudioSystem* audio = Audio())
        {
            audio->SetBusVolume(bus, volume);
        }
    }

    Float AudioService::GetBusVolume(const char* bus) const
    {
        return GetBusVolume(Named(bus));
    }

    Float AudioService::GetBusVolume(AudioBusName bus) const
    {
        System::IAudioSystem* audio = Audio();
        return audio != nullptr ? audio->GetBusVolume(bus) : Float(0.0f);
    }

    void AudioService::SetBusMuted(const char* bus, Bool muted) const
    {
        SetBusMuted(Named(bus), muted);
    }

    void AudioService::SetBusMuted(AudioBusName bus, Bool muted) const
    {
        if (System::IAudioSystem* audio = Audio())
        {
            audio->SetBusMuted(bus, muted);
        }
    }

    Bool AudioService::IsBusMuted(AudioBusName bus) const
    {
        System::IAudioSystem* audio = Audio();
        return audio != nullptr && audio->IsBusMuted(bus);
    }

    void AudioService::SetBusEffects(AudioBusName bus, const AudioBusEffects& effects) const
    {
        if (System::IAudioSystem* audio = Audio())
        {
            audio->SetBusEffects(bus, effects);
        }
    }

    AudioBusEffects AudioService::GetBusEffects(AudioBusName bus) const
    {
        System::IAudioSystem* audio = Audio();
        return audio != nullptr ? audio->GetBusEffects(bus) : AudioBusEffects{};
    }

    void AudioService::SetBusLowPass(const char* bus, Float cutoffHz) const
    {
        if (System::IAudioSystem* audio = Audio())
        {
            AudioBusEffects effects = audio->GetBusEffects(Named(bus));
            effects.lowPassHz = cutoffHz;
            audio->SetBusEffects(Named(bus), effects);
        }
    }

    void AudioService::StopAll() const
    {
        if (System::IAudioSystem* audio = Audio())
        {
            audio->StopAll();
        }
    }

    void AudioService::FadeBusVolume(const char* bus, Float volume, Float seconds) const
    {
        FadeBusVolume(Named(bus), volume, seconds);
    }

    void AudioService::FadeBusVolume(AudioBusName bus, Float volume, Float seconds) const
    {
        if (System::IAudioSystem* audio = Audio())
        {
            audio->FadeBusVolume(bus, volume, seconds);
        }
    }

    UInt32 AudioService::GetOutputDeviceCount() const
    {
        System::IAudioSystem* audio = Audio();
        return audio != nullptr ? audio->GetOutputDeviceCount() : UInt32(0);
    }

    const char* AudioService::GetOutputDeviceName(UInt32 index) const
    {
        System::IAudioSystem* audio = Audio();
        return audio != nullptr ? audio->GetOutputDeviceName(index) : "";
    }

    const char* AudioService::GetOutputDevice() const
    {
        System::IAudioSystem* audio = Audio();
        return audio != nullptr ? audio->GetOutputDevice() : "";
    }

    Bool AudioService::SetOutputDevice(const char* name) const
    {
        System::IAudioSystem* audio = Audio();
        return audio != nullptr && audio->SetOutputDevice(name);
    }

    Bool AudioService::IsWaitingForUserGesture() const
    {
        System::IAudioSystem* audio = Audio();
        return audio != nullptr && audio->IsWaitingForUserGesture();
    }

    void AudioService::SetMuteWhenUnfocused(Bool mute) const
    {
        if (System::IAudioSystem* audio = Audio())
        {
            audio->SetMuteWhenUnfocused(mute);
        }
    }

    Bool AudioService::IsMuteWhenUnfocused() const
    {
        System::IAudioSystem* audio = Audio();
        return audio != nullptr && audio->IsMuteWhenUnfocused();
    }
}
