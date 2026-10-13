#include <JBro/Framework2D/Handle/Transform2D.h>

#include <JBro/Framework2D/Component/Transform2D.h>

namespace JBro::Handle
{
    Component::Transform2D* Transform2D::Resolve(const char* operation) const
    {
        return static_cast<Component::Transform2D*>(ResolveComponent(StaticTypeName(), operation));
    }

    Vector2 Transform2D::GetPosition() const
    {
        const Component::Transform2D* component = Resolve("GetPosition");
        if (component == nullptr)
        {
            return {};
        }
        return component->position;
    }

    void Transform2D::SetPosition(Vector2 value)
    {
        Component::Transform2D* component = Resolve("SetPosition");
        if (component == nullptr)
        {
            return;
        }
        component->position = value;
    }

    Degree Transform2D::GetRotation() const
    {
        const Component::Transform2D* component = Resolve("GetRotation");
        if (component == nullptr)
        {
            return {};
        }
        return component->GetRotation();
    }

    void Transform2D::SetRotation(Degree value)
    {
        Component::Transform2D* component = Resolve("SetRotation");
        if (component == nullptr)
        {
            return;
        }
        component->SetRotation(value);
    }

    Vector2 Transform2D::GetScale() const
    {
        const Component::Transform2D* component = Resolve("GetScale");
        if (component == nullptr)
        {
            return {};
        }
        return component->scale;
    }

    void Transform2D::SetScale(Vector2 value)
    {
        Component::Transform2D* component = Resolve("SetScale");
        if (component == nullptr)
        {
            return;
        }
        component->scale = value;
    }

    Vector2 Transform2D::GetAnchor() const
    {
        const Component::Transform2D* component = Resolve("GetAnchor");
        if (component == nullptr)
        {
            return {};
        }
        return component->anchor;
    }

    void Transform2D::SetAnchor(Vector2 value)
    {
        Component::Transform2D* component = Resolve("SetAnchor");
        if (component == nullptr)
        {
            return;
        }
        component->anchor = value;
    }

    Matrix3x2 Transform2D::GetWorld() const
    {
        const Component::Transform2D* component = Resolve("GetWorld");
        if (component == nullptr)
        {
            return {};
        }
        return component->world;
    }

    Vector2 Transform2D::GetWorldPosition() const
    {
        const Component::Transform2D* component = Resolve("GetWorldPosition");
        if (component == nullptr)
        {
            return {};
        }
        return component->worldPosition;
    }

    Degree Transform2D::GetWorldRotation() const
    {
        const Component::Transform2D* component = Resolve("GetWorldRotation");
        if (component == nullptr)
        {
            return {};
        }
        return Degree(component->worldRotation);
    }

    Vector2 Transform2D::GetWorldScale() const
    {
        const Component::Transform2D* component = Resolve("GetWorldScale");
        if (component == nullptr)
        {
            return {};
        }
        return component->worldScale;
    }

    Bool Transform2D::GetWorldValid() const
    {
        const Component::Transform2D* component = Resolve("GetWorldValid");
        if (component == nullptr)
        {
            return {};
        }
        return component->worldValid;
    }
}
