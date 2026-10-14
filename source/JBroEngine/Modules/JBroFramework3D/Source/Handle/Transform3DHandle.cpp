#include <JBro/Framework3D/Handle/Transform3D.h>

#include <JBro/Framework3D/Component/Transform3D.h>

namespace JBro::Handle
{
    Component::Transform3D* Transform3D::Resolve(const char* operation) const
    {
        return static_cast<Component::Transform3D*>(ResolveComponent(StaticTypeName(), operation));
    }

    Vector3 Transform3D::GetPosition() const
    {
        const Component::Transform3D* component = Resolve("GetPosition");
        if (component == nullptr)
        {
            return {};
        }
        return component->position;
    }

    void Transform3D::SetPosition(Vector3 value)
    {
        Component::Transform3D* component = Resolve("SetPosition");
        if (component == nullptr)
        {
            return;
        }
        component->position = value;
    }

    Quaternion Transform3D::GetRotation() const
    {
        const Component::Transform3D* component = Resolve("GetRotation");
        if (component == nullptr)
        {
            return {};
        }
        return component->rotation;
    }

    void Transform3D::SetRotation(Quaternion value)
    {
        Component::Transform3D* component = Resolve("SetRotation");
        if (component == nullptr)
        {
            return;
        }
        component->rotation = value;
    }

    Vector3 Transform3D::GetScale() const
    {
        const Component::Transform3D* component = Resolve("GetScale");
        if (component == nullptr)
        {
            return {};
        }
        return component->scale;
    }

    void Transform3D::SetScale(Vector3 value)
    {
        Component::Transform3D* component = Resolve("SetScale");
        if (component == nullptr)
        {
            return;
        }
        component->scale = value;
    }

    Vector3 Transform3D::GetWorldPosition() const
    {
        const Component::Transform3D* component = Resolve("GetWorldPosition");
        if (component == nullptr)
        {
            return {};
        }
        return component->worldPosition;
    }

    Quaternion Transform3D::GetWorldRotation() const
    {
        const Component::Transform3D* component = Resolve("GetWorldRotation");
        if (component == nullptr)
        {
            return {};
        }
        return component->worldRotation;
    }

    Vector3 Transform3D::GetWorldScale() const
    {
        const Component::Transform3D* component = Resolve("GetWorldScale");
        if (component == nullptr)
        {
            return {};
        }
        return component->worldScale;
    }

    Bool Transform3D::GetWorldValid() const
    {
        const Component::Transform3D* component = Resolve("GetWorldValid");
        if (component == nullptr)
        {
            return {};
        }
        return component->worldValid;
    }
}
