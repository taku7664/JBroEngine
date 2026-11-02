#include <JBro/Framework3D/Handle/Light3D.h>

#include <JBro/Framework3D/Component/Light3D.h>

namespace JBro::Handle
{
    Component::Light3D* Light3D::Resolve(const char* operation) const
    {
        return static_cast<Component::Light3D*>(ResolveComponent(StaticTypeName(), operation));
    }

    Color Light3D::GetColor() const
    {
        const Component::Light3D* component = Resolve("GetColor");
        if (component == nullptr)
        {
            return {};
        }
        return component->color;
    }

    void Light3D::SetColor(Color value)
    {
        Component::Light3D* component = Resolve("SetColor");
        if (component == nullptr)
        {
            return;
        }
        component->color = value;
    }

    Float Light3D::GetIntensity() const
    {
        const Component::Light3D* component = Resolve("GetIntensity");
        if (component == nullptr)
        {
            return {};
        }
        return component->intensity;
    }

    void Light3D::SetIntensity(Float value)
    {
        Component::Light3D* component = Resolve("SetIntensity");
        if (component == nullptr)
        {
            return;
        }
        component->intensity = value;
    }

    Float Light3D::GetRange() const
    {
        const Component::Light3D* component = Resolve("GetRange");
        if (component == nullptr)
        {
            return {};
        }
        return component->range;
    }

    void Light3D::SetRange(Float value)
    {
        Component::Light3D* component = Resolve("SetRange");
        if (component == nullptr)
        {
            return;
        }
        component->range = value;
    }
}
