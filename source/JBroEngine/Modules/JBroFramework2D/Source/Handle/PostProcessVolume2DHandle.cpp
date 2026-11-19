#include <JBro/Framework2D/Handle/PostProcessVolume2D.h>

#include <JBro/Framework2D/Component/PostProcessVolume2D.h>

namespace JBro::Handle
{
    Component::PostProcessVolume2D* PostProcessVolume2D::Resolve(const char* operation) const
    {
        return static_cast<Component::PostProcessVolume2D*>(ResolveComponent(StaticTypeName(), operation));
    }

    Component::PostProcessVolumeShape2D PostProcessVolume2D::GetShape() const
    {
        const Component::PostProcessVolume2D* component = Resolve("GetShape");
        if (component == nullptr)
        {
            return Component::PostProcessVolumeShape2D::Global;
        }
        return component->shape;
    }

    void PostProcessVolume2D::SetShape(Component::PostProcessVolumeShape2D value)
    {
        Component::PostProcessVolume2D* component = Resolve("SetShape");
        if (component == nullptr)
        {
            return;
        }
        component->shape = value;
    }

    Vector2 PostProcessVolume2D::GetSize() const
    {
        const Component::PostProcessVolume2D* component = Resolve("GetSize");
        if (component == nullptr)
        {
            return {};
        }
        return component->size;
    }

    void PostProcessVolume2D::SetSize(Vector2 value)
    {
        Component::PostProcessVolume2D* component = Resolve("SetSize");
        if (component == nullptr)
        {
            return;
        }
        component->size = value;
    }

    Float PostProcessVolume2D::GetRadius() const
    {
        const Component::PostProcessVolume2D* component = Resolve("GetRadius");
        if (component == nullptr)
        {
            return {};
        }
        return component->radius;
    }

    void PostProcessVolume2D::SetRadius(Float value)
    {
        Component::PostProcessVolume2D* component = Resolve("SetRadius");
        if (component == nullptr)
        {
            return;
        }
        component->radius = value;
    }

    Float PostProcessVolume2D::GetBlendDistance() const
    {
        const Component::PostProcessVolume2D* component = Resolve("GetBlendDistance");
        if (component == nullptr)
        {
            return {};
        }
        return component->blendDistance;
    }

    void PostProcessVolume2D::SetBlendDistance(Float value)
    {
        Component::PostProcessVolume2D* component = Resolve("SetBlendDistance");
        if (component == nullptr)
        {
            return;
        }
        component->blendDistance = value;
    }

    Int32 PostProcessVolume2D::GetPriority() const
    {
        const Component::PostProcessVolume2D* component = Resolve("GetPriority");
        if (component == nullptr)
        {
            return {};
        }
        return component->priority;
    }

    void PostProcessVolume2D::SetPriority(Int32 value)
    {
        Component::PostProcessVolume2D* component = Resolve("SetPriority");
        if (component == nullptr)
        {
            return;
        }
        component->priority = value;
    }

    Float PostProcessVolume2D::GetWeight() const
    {
        const Component::PostProcessVolume2D* component = Resolve("GetWeight");
        if (component == nullptr)
        {
            return {};
        }
        return component->weight;
    }

    void PostProcessVolume2D::SetWeight(Float value)
    {
        Component::PostProcessVolume2D* component = Resolve("SetWeight");
        if (component == nullptr)
        {
            return;
        }
        component->weight = value;
    }

    AssetId PostProcessVolume2D::GetPostProcessProfileId() const
    {
        const Component::PostProcessVolume2D* component = Resolve("GetPostProcessProfileId");
        if (component == nullptr)
        {
            return {};
        }
        return component->postProcessProfileId;
    }

    AssetHandle PostProcessVolume2D::GetPostProcessProfile() const
    {
        const Component::PostProcessVolume2D* component = Resolve("GetPostProcessProfile");
        if (component == nullptr)
        {
            return {};
        }
        return component->postProcessProfile;
    }

    void PostProcessVolume2D::SetPostProcessProfile(AssetHandle value)
    {
        Component::PostProcessVolume2D* component = Resolve("SetPostProcessProfile");
        if (component == nullptr)
        {
            return;
        }
        component->postProcessProfile = value;
    }
}
