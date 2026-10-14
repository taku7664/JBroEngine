#include <JBro/AudioTypes/Handle/AudioSource.h>

#include <JBro/AudioTypes/Component/AudioSource.h>

namespace JBro::Handle
{
    Component::AudioSource* AudioSource::Resolve(const char* operation) const
    {
        return static_cast<Component::AudioSource*>(ResolveComponent(StaticTypeName(), operation));
    }

    AssetId AudioSource::GetClipId() const
    {
        const Component::AudioSource* component = Resolve("GetClipId");
        if (component == nullptr)
        {
            return {};
        }
        return component->clipId;
    }

    AssetHandle AudioSource::GetClip() const
    {
        const Component::AudioSource* component = Resolve("GetClip");
        if (component == nullptr)
        {
            return {};
        }
        return component->clip;
    }

    void AudioSource::SetClip(AssetHandle value)
    {
        Component::AudioSource* component = Resolve("SetClip");
        if (component == nullptr)
        {
            return;
        }
        component->clip = value;
    }

    AudioBusName AudioSource::GetBus() const
    {
        const Component::AudioSource* component = Resolve("GetBus");
        if (component == nullptr)
        {
            return {};
        }
        return component->bus;
    }

    void AudioSource::SetBus(AudioBusName value)
    {
        Component::AudioSource* component = Resolve("SetBus");
        if (component == nullptr)
        {
            return;
        }
        component->bus = value;
    }

    Float AudioSource::GetVolume() const
    {
        const Component::AudioSource* component = Resolve("GetVolume");
        if (component == nullptr)
        {
            return {};
        }
        return component->volume;
    }

    void AudioSource::SetVolume(Float value)
    {
        Component::AudioSource* component = Resolve("SetVolume");
        if (component == nullptr)
        {
            return;
        }
        component->volume = value;
    }

    Float AudioSource::GetPitch() const
    {
        const Component::AudioSource* component = Resolve("GetPitch");
        if (component == nullptr)
        {
            return {};
        }
        return component->pitch;
    }

    void AudioSource::SetPitch(Float value)
    {
        Component::AudioSource* component = Resolve("SetPitch");
        if (component == nullptr)
        {
            return;
        }
        component->pitch = value;
    }

    Bool AudioSource::GetLoop() const
    {
        const Component::AudioSource* component = Resolve("GetLoop");
        if (component == nullptr)
        {
            return {};
        }
        return component->loop;
    }

    void AudioSource::SetLoop(Bool value)
    {
        Component::AudioSource* component = Resolve("SetLoop");
        if (component == nullptr)
        {
            return;
        }
        component->loop = value;
    }

    Bool AudioSource::GetPlayOnStart() const
    {
        const Component::AudioSource* component = Resolve("GetPlayOnStart");
        if (component == nullptr)
        {
            return {};
        }
        return component->playOnStart;
    }

    void AudioSource::SetPlayOnStart(Bool value)
    {
        Component::AudioSource* component = Resolve("SetPlayOnStart");
        if (component == nullptr)
        {
            return;
        }
        component->playOnStart = value;
    }

    Bool AudioSource::GetSpatial() const
    {
        const Component::AudioSource* component = Resolve("GetSpatial");
        if (component == nullptr)
        {
            return {};
        }
        return component->spatial;
    }

    void AudioSource::SetSpatial(Bool value)
    {
        Component::AudioSource* component = Resolve("SetSpatial");
        if (component == nullptr)
        {
            return;
        }
        component->spatial = value;
    }

    AudioAttenuation AudioSource::GetAttenuation() const
    {
        const Component::AudioSource* component = Resolve("GetAttenuation");
        if (component == nullptr)
        {
            return {};
        }
        return component->attenuation;
    }

    void AudioSource::SetAttenuation(AudioAttenuation value)
    {
        Component::AudioSource* component = Resolve("SetAttenuation");
        if (component == nullptr)
        {
            return;
        }
        component->attenuation = value;
    }

    Float AudioSource::GetMinDistance() const
    {
        const Component::AudioSource* component = Resolve("GetMinDistance");
        if (component == nullptr)
        {
            return {};
        }
        return component->minDistance;
    }

    void AudioSource::SetMinDistance(Float value)
    {
        Component::AudioSource* component = Resolve("SetMinDistance");
        if (component == nullptr)
        {
            return;
        }
        component->minDistance = value;
    }

    Float AudioSource::GetMaxDistance() const
    {
        const Component::AudioSource* component = Resolve("GetMaxDistance");
        if (component == nullptr)
        {
            return {};
        }
        return component->maxDistance;
    }

    void AudioSource::SetMaxDistance(Float value)
    {
        Component::AudioSource* component = Resolve("SetMaxDistance");
        if (component == nullptr)
        {
            return;
        }
        component->maxDistance = value;
    }

    Float AudioSource::GetRolloff() const
    {
        const Component::AudioSource* component = Resolve("GetRolloff");
        if (component == nullptr)
        {
            return {};
        }
        return component->rolloff;
    }

    void AudioSource::SetRolloff(Float value)
    {
        Component::AudioSource* component = Resolve("SetRolloff");
        if (component == nullptr)
        {
            return;
        }
        component->rolloff = value;
    }

    Float AudioSource::GetDoppler() const
    {
        const Component::AudioSource* component = Resolve("GetDoppler");
        if (component == nullptr)
        {
            return {};
        }
        return component->doppler;
    }

    void AudioSource::SetDoppler(Float value)
    {
        Component::AudioSource* component = Resolve("SetDoppler");
        if (component == nullptr)
        {
            return;
        }
        component->doppler = value;
    }

    Int32 AudioSource::GetPriority() const
    {
        const Component::AudioSource* component = Resolve("GetPriority");
        if (component == nullptr)
        {
            return {};
        }
        return component->priority;
    }

    void AudioSource::SetPriority(Int32 value)
    {
        Component::AudioSource* component = Resolve("SetPriority");
        if (component == nullptr)
        {
            return;
        }
        component->priority = value;
    }

    Float AudioSource::GetFadeIn() const
    {
        const Component::AudioSource* component = Resolve("GetFadeIn");
        if (component == nullptr)
        {
            return {};
        }
        return component->fadeIn;
    }

    void AudioSource::SetFadeIn(Float value)
    {
        Component::AudioSource* component = Resolve("SetFadeIn");
        if (component == nullptr)
        {
            return;
        }
        component->fadeIn = value;
    }

    Float AudioSource::GetLowPass() const
    {
        const Component::AudioSource* component = Resolve("GetLowPass");
        if (component == nullptr)
        {
            return {};
        }
        return component->lowPass;
    }

    void AudioSource::SetLowPass(Float value)
    {
        Component::AudioSource* component = Resolve("SetLowPass");
        if (component == nullptr)
        {
            return;
        }
        component->lowPass = value;
    }

    Float AudioSource::GetHighPass() const
    {
        const Component::AudioSource* component = Resolve("GetHighPass");
        if (component == nullptr)
        {
            return {};
        }
        return component->highPass;
    }

    void AudioSource::SetHighPass(Float value)
    {
        Component::AudioSource* component = Resolve("SetHighPass");
        if (component == nullptr)
        {
            return;
        }
        component->highPass = value;
    }

    void AudioSource::GetAudioEffectIds(Array<AssetId>& ids) const
    {
        const Component::AudioSource* component = Resolve("GetAudioEffectIds");
        if (component == nullptr)
        {
            ids.Clear();
            return;
        }
        ids = component->audioEffectIds;
    }

    Component::AudioSourceState AudioSource::GetState() const
    {
        const Component::AudioSource* component = Resolve("GetState");
        if (component == nullptr)
        {
            return {};
        }
        return component->state;
    }
}
