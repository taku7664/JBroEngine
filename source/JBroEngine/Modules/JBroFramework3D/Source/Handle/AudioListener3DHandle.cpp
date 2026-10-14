#include <JBro/Framework3D/Handle/AudioListener3D.h>

#include <JBro/Framework3D/Component/AudioListener3D.h>

namespace JBro::Handle
{
    Component::AudioListener3D* AudioListener3D::Resolve(const char* operation) const
    {
        return static_cast<Component::AudioListener3D*>(ResolveComponent(StaticTypeName(), operation));
    }

    Float AudioListener3D::GetVolume() const
    {
        const Component::AudioListener3D* component = Resolve("GetVolume");
        if (component == nullptr)
        {
            return {};
        }
        return component->volume;
    }

    void AudioListener3D::SetVolume(Float value)
    {
        Component::AudioListener3D* component = Resolve("SetVolume");
        if (component == nullptr)
        {
            return;
        }
        component->volume = value;
    }
}
