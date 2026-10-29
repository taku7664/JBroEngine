#include <JBro/Framework2D/Handle/Mask2D.h>

#include <JBro/Framework2D/Component/Mask2D.h>

namespace JBro::Handle
{
    Component::Mask2D* Mask2D::Resolve(const char* operation) const
    {
        return static_cast<Component::Mask2D*>(ResolveComponent(StaticTypeName(), operation));
    }

    Vector2 Mask2D::GetSize() const
    {
        const Component::Mask2D* component = Resolve("GetSize");
        return component != nullptr ? component->size : Vector2{};
    }

    void Mask2D::SetSize(Vector2 value)
    {
        if (Component::Mask2D* component = Resolve("SetSize"))
        {
            component->size = value;
        }
    }

    Vector2 Mask2D::GetOffset() const
    {
        const Component::Mask2D* component = Resolve("GetOffset");
        return component != nullptr ? component->offset : Vector2{};
    }

    void Mask2D::SetOffset(Vector2 value)
    {
        if (Component::Mask2D* component = Resolve("SetOffset"))
        {
            component->offset = value;
        }
    }
}
