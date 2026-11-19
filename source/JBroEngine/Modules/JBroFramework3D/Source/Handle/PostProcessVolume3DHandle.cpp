#include <JBro/Framework3D/Handle/PostProcessVolume3D.h>

#include <JBro/Framework3D/Component/PostProcessVolume3D.h>

namespace JBro::Handle
{
    Component::PostProcessVolume3D* PostProcessVolume3D::Resolve(const char* operation) const
    {
        return static_cast<Component::PostProcessVolume3D*>(ResolveComponent(StaticTypeName(), operation));
    }

    Component::PostProcessVolumeShape3D PostProcessVolume3D::GetShape() const
    {
        const Component::PostProcessVolume3D* component = Resolve("GetShape");
        if (component == nullptr)
        {
            return Component::PostProcessVolumeShape3D::Global;
        }
        return component->shape;
    }

    void PostProcessVolume3D::SetShape(Component::PostProcessVolumeShape3D value)
    {
        Component::PostProcessVolume3D* component = Resolve("SetShape");
        if (component == nullptr)
        {
            return;
        }
        component->shape = value;
    }

    Vector3 PostProcessVolume3D::GetSize() const
    {
        const Component::PostProcessVolume3D* component = Resolve("GetSize");
        if (component == nullptr)
        {
            return {};
        }
        return component->size;
    }

    void PostProcessVolume3D::SetSize(Vector3 value)
    {
        Component::PostProcessVolume3D* component = Resolve("SetSize");
        if (component == nullptr)
        {
            return;
        }
        component->size = value;
    }

    Float PostProcessVolume3D::GetRadius() const
    {
        const Component::PostProcessVolume3D* component = Resolve("GetRadius");
        if (component == nullptr)
        {
            return {};
        }
        return component->radius;
    }

    void PostProcessVolume3D::SetRadius(Float value)
    {
        Component::PostProcessVolume3D* component = Resolve("SetRadius");
        if (component == nullptr)
        {
            return;
        }
        component->radius = value;
    }

    Float PostProcessVolume3D::GetBlendDistance() const
    {
        const Component::PostProcessVolume3D* component = Resolve("GetBlendDistance");
        if (component == nullptr)
        {
            return {};
        }
        return component->blendDistance;
    }

    void PostProcessVolume3D::SetBlendDistance(Float value)
    {
        Component::PostProcessVolume3D* component = Resolve("SetBlendDistance");
        if (component == nullptr)
        {
            return;
        }
        component->blendDistance = value;
    }

    Int32 PostProcessVolume3D::GetPriority() const
    {
        const Component::PostProcessVolume3D* component = Resolve("GetPriority");
        if (component == nullptr)
        {
            return {};
        }
        return component->priority;
    }

    void PostProcessVolume3D::SetPriority(Int32 value)
    {
        Component::PostProcessVolume3D* component = Resolve("SetPriority");
        if (component == nullptr)
        {
            return;
        }
        component->priority = value;
    }

    Float PostProcessVolume3D::GetWeight() const
    {
        const Component::PostProcessVolume3D* component = Resolve("GetWeight");
        if (component == nullptr)
        {
            return {};
        }
        return component->weight;
    }

    void PostProcessVolume3D::SetWeight(Float value)
    {
        Component::PostProcessVolume3D* component = Resolve("SetWeight");
        if (component == nullptr)
        {
            return;
        }
        component->weight = value;
    }

    AssetId PostProcessVolume3D::GetPostProcessProfileId() const
    {
        const Component::PostProcessVolume3D* component = Resolve("GetPostProcessProfileId");
        if (component == nullptr)
        {
            return {};
        }
        return component->postProcessProfileId;
    }

    AssetHandle PostProcessVolume3D::GetPostProcessProfile() const
    {
        const Component::PostProcessVolume3D* component = Resolve("GetPostProcessProfile");
        if (component == nullptr)
        {
            return {};
        }
        return component->postProcessProfile;
    }

    void PostProcessVolume3D::SetPostProcessProfile(AssetHandle value)
    {
        Component::PostProcessVolume3D* component = Resolve("SetPostProcessProfile");
        if (component == nullptr)
        {
            return;
        }
        component->postProcessProfile = value;
    }
}
