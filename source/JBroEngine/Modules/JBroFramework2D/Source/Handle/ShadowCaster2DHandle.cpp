#include <JBro/Framework2D/Handle/ShadowCaster2D.h>

#include <JBro/Framework2D/Component/ShadowCaster2D.h>

namespace JBro::Handle
{
    Component::ShadowCaster2D* ShadowCaster2D::Resolve(const char* operation) const
    {
        return static_cast<Component::ShadowCaster2D*>(ResolveComponent(StaticTypeName(), operation));
    }

    Component::ShadowShape2D ShadowCaster2D::GetShape() const
    {
        const Component::ShadowCaster2D* component = Resolve("GetShape");
        if (component == nullptr)
        {
            return {};
        }
        return component->shape;
    }

    void ShadowCaster2D::SetShape(Component::ShadowShape2D value)
    {
        Component::ShadowCaster2D* component = Resolve("SetShape");
        if (component == nullptr)
        {
            return;
        }
        component->shape = value;
    }

    Bool ShadowCaster2D::GetSelfShadow() const
    {
        const Component::ShadowCaster2D* component = Resolve("GetSelfShadow");
        if (component == nullptr)
        {
            return {};
        }
        return component->selfShadow;
    }

    void ShadowCaster2D::SetSelfShadow(Bool value)
    {
        Component::ShadowCaster2D* component = Resolve("SetSelfShadow");
        if (component == nullptr)
        {
            return;
        }
        component->selfShadow = value;
    }
}
