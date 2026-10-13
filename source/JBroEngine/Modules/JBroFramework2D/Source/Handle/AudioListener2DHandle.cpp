#include <JBro/Framework2D/Handle/AudioListener2D.h>

#include <JBro/Framework2D/Component/AudioListener2D.h>

namespace JBro::Handle
{
    Component::AudioListener2D* AudioListener2D::Resolve(const char* operation) const
    {
        return static_cast<Component::AudioListener2D*>(ResolveComponent(StaticTypeName(), operation));
    }

    Float AudioListener2D::GetPanDistance() const
    {
        const Component::AudioListener2D* component = Resolve("GetPanDistance");
        if (component == nullptr)
        {
            return {};
        }
        return component->panDistance;
    }

    void AudioListener2D::SetPanDistance(Float value)
    {
        Component::AudioListener2D* component = Resolve("SetPanDistance");
        if (component == nullptr)
        {
            return;
        }
        component->panDistance = value;
    }

    Float AudioListener2D::GetVolume() const
    {
        const Component::AudioListener2D* component = Resolve("GetVolume");
        if (component == nullptr)
        {
            return {};
        }
        return component->volume;
    }

    void AudioListener2D::SetVolume(Float value)
    {
        Component::AudioListener2D* component = Resolve("SetVolume");
        if (component == nullptr)
        {
            return;
        }
        component->volume = value;
    }
}
