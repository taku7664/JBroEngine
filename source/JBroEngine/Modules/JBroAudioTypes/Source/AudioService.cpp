#include <JBro/AudioTypes/Service/AudioService.h>

#include <JBro/AudioTypes/Internal/SystemContext.h>
#include <JBro/AudioTypes/System/IAudioSystem.h>

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

    void AudioService::PlayOneShot(AssetHandle clip, float volume, float pitch) const
    {
        PlayOneShot(clip, AudioBusName{}, volume, pitch);
    }

    void AudioService::PlayOneShot(AssetHandle clip, const char* bus, float volume, float pitch) const
    {
        PlayOneShot(clip, Named(bus), volume, pitch);
    }

    void AudioService::PlayOneShot(AssetHandle clip, AudioBusName bus, float volume, float pitch) const
    {
        if (System::IAudioSystem* audio = Audio())
        {
            audio->PlayOneShot(clip, bus, volume, pitch);
        }
    }

    void AudioService::PlayOneShotAt(AssetHandle clip, float x, float y, float z, float volume) const
    {
        PlayOneShotAt(clip, AudioBusName{}, x, y, z, volume);
    }

    void AudioService::PlayOneShotAt(AssetHandle clip, AudioBusName bus, float x, float y, float z, float volume) const
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

    void AudioService::Stop(Ref<Component::AudioSource> source, float fadeOutSeconds) const
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

    bool AudioService::IsPlaying(Ref<Component::AudioSource> source) const
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

    void AudioService::SetBusVolume(const char* bus, float volume) const
    {
        SetBusVolume(Named(bus), volume);
    }

    void AudioService::SetBusVolume(AudioBusName bus, float volume) const
    {
        if (System::IAudioSystem* audio = Audio())
        {
            audio->SetBusVolume(bus, volume);
        }
    }

    float AudioService::GetBusVolume(const char* bus) const
    {
        return GetBusVolume(Named(bus));
    }

    float AudioService::GetBusVolume(AudioBusName bus) const
    {
        System::IAudioSystem* audio = Audio();
        return audio != nullptr ? audio->GetBusVolume(bus) : 0.0f;
    }

    void AudioService::SetBusMuted(const char* bus, bool muted) const
    {
        SetBusMuted(Named(bus), muted);
    }

    void AudioService::SetBusMuted(AudioBusName bus, bool muted) const
    {
        if (System::IAudioSystem* audio = Audio())
        {
            audio->SetBusMuted(bus, muted);
        }
    }

    bool AudioService::IsBusMuted(AudioBusName bus) const
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

    void AudioService::SetBusLowPass(const char* bus, float cutoffHz) const
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
}
