#include <JBro/Framework2D/Handle/Light2D.h>

#include <JBro/Framework2D/Component/Light2D.h>

namespace JBro::Handle
{
    Component::Light2D* Light2D::Resolve(const char* operation) const
    {
        return static_cast<Component::Light2D*>(ResolveComponent(StaticTypeName(), operation));
    }

    Component::Light2DType Light2D::GetType() const
    {
        const Component::Light2D* component = Resolve("GetType");
        if (component == nullptr)
        {
            return {};
        }
        return component->type;
    }

    void Light2D::SetType(Component::Light2DType value)
    {
        Component::Light2D* component = Resolve("SetType");
        if (component == nullptr)
        {
            return;
        }
        component->type = value;
    }

    Color Light2D::GetColor() const
    {
        const Component::Light2D* component = Resolve("GetColor");
        if (component == nullptr)
        {
            return {};
        }
        return component->color;
    }

    void Light2D::SetColor(Color value)
    {
        Component::Light2D* component = Resolve("SetColor");
        if (component == nullptr)
        {
            return;
        }
        component->color = value;
    }

    Float Light2D::GetIntensity() const
    {
        const Component::Light2D* component = Resolve("GetIntensity");
        if (component == nullptr)
        {
            return {};
        }
        return component->intensity;
    }

    void Light2D::SetIntensity(Float value)
    {
        Component::Light2D* component = Resolve("SetIntensity");
        if (component == nullptr)
        {
            return;
        }
        component->intensity = value;
    }

    Float Light2D::GetInnerRadius() const
    {
        const Component::Light2D* component = Resolve("GetInnerRadius");
        if (component == nullptr)
        {
            return {};
        }
        return component->innerRadius;
    }

    void Light2D::SetInnerRadius(Float value)
    {
        Component::Light2D* component = Resolve("SetInnerRadius");
        if (component == nullptr)
        {
            return;
        }
        component->innerRadius = value;
    }

    Float Light2D::GetOuterRadius() const
    {
        const Component::Light2D* component = Resolve("GetOuterRadius");
        if (component == nullptr)
        {
            return {};
        }
        return component->outerRadius;
    }

    void Light2D::SetOuterRadius(Float value)
    {
        Component::Light2D* component = Resolve("SetOuterRadius");
        if (component == nullptr)
        {
            return;
        }
        component->outerRadius = value;
    }

    Degree Light2D::GetInnerAngle() const
    {
        const Component::Light2D* component = Resolve("GetInnerAngle");
        if (component == nullptr)
        {
            return {};
        }
        return component->innerAngle;
    }

    void Light2D::SetInnerAngle(Degree value)
    {
        Component::Light2D* component = Resolve("SetInnerAngle");
        if (component == nullptr)
        {
            return;
        }
        component->innerAngle = value;
    }

    Degree Light2D::GetOuterAngle() const
    {
        const Component::Light2D* component = Resolve("GetOuterAngle");
        if (component == nullptr)
        {
            return {};
        }
        return component->outerAngle;
    }

    void Light2D::SetOuterAngle(Degree value)
    {
        Component::Light2D* component = Resolve("SetOuterAngle");
        if (component == nullptr)
        {
            return;
        }
        component->outerAngle = value;
    }

    Bool Light2D::GetCastShadows() const
    {
        const Component::Light2D* component = Resolve("GetCastShadows");
        if (component == nullptr)
        {
            return {};
        }
        return component->castShadows;
    }

    void Light2D::SetCastShadows(Bool value)
    {
        Component::Light2D* component = Resolve("SetCastShadows");
        if (component == nullptr)
        {
            return;
        }
        component->castShadows = value;
    }

    Float Light2D::GetShadowSoftness() const
    {
        const Component::Light2D* component = Resolve("GetShadowSoftness");
        if (component == nullptr)
        {
            return {};
        }
        return component->shadowSoftness;
    }

    void Light2D::SetShadowSoftness(Float value)
    {
        Component::Light2D* component = Resolve("SetShadowSoftness");
        if (component == nullptr)
        {
            return;
        }
        component->shadowSoftness = value;
    }
}
