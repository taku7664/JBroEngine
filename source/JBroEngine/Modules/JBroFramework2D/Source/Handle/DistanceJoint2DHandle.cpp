#include <JBro/Framework2D/Handle/DistanceJoint2D.h>

#include <JBro/Framework2D/Component/Physics2D.h>

namespace JBro::Handle
{
    Component::DistanceJoint2D* DistanceJoint2D::Resolve(const char* operation) const
    {
        return static_cast<Component::DistanceJoint2D*>(ResolveComponent(StaticTypeName(), operation));
    }

    GameObject DistanceJoint2D::GetConnectedObject() const
    {
        const Component::DistanceJoint2D* component = Resolve("GetConnectedObject");
        if (component == nullptr)
        {
            return {};
        }
        return component->connectedObject;
    }

    void DistanceJoint2D::SetConnectedObject(GameObject value)
    {
        Component::DistanceJoint2D* component = Resolve("SetConnectedObject");
        if (component == nullptr)
        {
            return;
        }
        component->connectedObject = value;
    }

    Vector2 DistanceJoint2D::GetAnchor() const
    {
        const Component::DistanceJoint2D* component = Resolve("GetAnchor");
        if (component == nullptr)
        {
            return {};
        }
        return component->anchor;
    }

    void DistanceJoint2D::SetAnchor(Vector2 value)
    {
        Component::DistanceJoint2D* component = Resolve("SetAnchor");
        if (component == nullptr)
        {
            return;
        }
        component->anchor = value;
    }

    Vector2 DistanceJoint2D::GetConnectedAnchor() const
    {
        const Component::DistanceJoint2D* component = Resolve("GetConnectedAnchor");
        if (component == nullptr)
        {
            return {};
        }
        return component->connectedAnchor;
    }

    void DistanceJoint2D::SetConnectedAnchor(Vector2 value)
    {
        Component::DistanceJoint2D* component = Resolve("SetConnectedAnchor");
        if (component == nullptr)
        {
            return;
        }
        component->connectedAnchor = value;
    }

    Bool DistanceJoint2D::GetAutoDistance() const
    {
        const Component::DistanceJoint2D* component = Resolve("GetAutoDistance");
        if (component == nullptr)
        {
            return {};
        }
        return component->autoDistance;
    }

    void DistanceJoint2D::SetAutoDistance(Bool value)
    {
        Component::DistanceJoint2D* component = Resolve("SetAutoDistance");
        if (component == nullptr)
        {
            return;
        }
        component->autoDistance = value;
    }

    Float DistanceJoint2D::GetDistance() const
    {
        const Component::DistanceJoint2D* component = Resolve("GetDistance");
        if (component == nullptr)
        {
            return {};
        }
        return component->distance;
    }

    void DistanceJoint2D::SetDistance(Float value)
    {
        Component::DistanceJoint2D* component = Resolve("SetDistance");
        if (component == nullptr)
        {
            return;
        }
        component->distance = value;
    }

    Bool DistanceJoint2D::GetMaxDistanceOnly() const
    {
        const Component::DistanceJoint2D* component = Resolve("GetMaxDistanceOnly");
        if (component == nullptr)
        {
            return {};
        }
        return component->maxDistanceOnly;
    }

    void DistanceJoint2D::SetMaxDistanceOnly(Bool value)
    {
        Component::DistanceJoint2D* component = Resolve("SetMaxDistanceOnly");
        if (component == nullptr)
        {
            return;
        }
        component->maxDistanceOnly = value;
    }

    Float DistanceJoint2D::GetFrequency() const
    {
        const Component::DistanceJoint2D* component = Resolve("GetFrequency");
        if (component == nullptr)
        {
            return {};
        }
        return component->frequency;
    }

    void DistanceJoint2D::SetFrequency(Float value)
    {
        Component::DistanceJoint2D* component = Resolve("SetFrequency");
        if (component == nullptr)
        {
            return;
        }
        component->frequency = value;
    }

    Float DistanceJoint2D::GetDampingRatio() const
    {
        const Component::DistanceJoint2D* component = Resolve("GetDampingRatio");
        if (component == nullptr)
        {
            return {};
        }
        return component->dampingRatio;
    }

    void DistanceJoint2D::SetDampingRatio(Float value)
    {
        Component::DistanceJoint2D* component = Resolve("SetDampingRatio");
        if (component == nullptr)
        {
            return;
        }
        component->dampingRatio = value;
    }

    Bool DistanceJoint2D::GetCollideConnected() const
    {
        const Component::DistanceJoint2D* component = Resolve("GetCollideConnected");
        if (component == nullptr)
        {
            return {};
        }
        return component->collideConnected;
    }

    void DistanceJoint2D::SetCollideConnected(Bool value)
    {
        Component::DistanceJoint2D* component = Resolve("SetCollideConnected");
        if (component == nullptr)
        {
            return;
        }
        component->collideConnected = value;
    }
}
