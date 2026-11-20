#include <JBro/Framework3D/Handle/DistanceJoint3D.h>

#include <JBro/Framework3D/Component/Physics3D.h>

namespace JBro::Handle
{
    Component::DistanceJoint3D* DistanceJoint3D::Resolve(const char* operation) const
    {
        return static_cast<Component::DistanceJoint3D*>(ResolveComponent(StaticTypeName(), operation));
    }

    GameObject DistanceJoint3D::GetConnectedObject() const
    {
        const Component::DistanceJoint3D* component = Resolve("GetConnectedObject");
        if (component == nullptr)
        {
            return {};
        }
        return component->connectedObject;
    }

    void DistanceJoint3D::SetConnectedObject(GameObject value)
    {
        Component::DistanceJoint3D* component = Resolve("SetConnectedObject");
        if (component == nullptr)
        {
            return;
        }
        component->connectedObject = value;
    }

    Vector3 DistanceJoint3D::GetAnchor() const
    {
        const Component::DistanceJoint3D* component = Resolve("GetAnchor");
        if (component == nullptr)
        {
            return {};
        }
        return component->anchor;
    }

    void DistanceJoint3D::SetAnchor(Vector3 value)
    {
        Component::DistanceJoint3D* component = Resolve("SetAnchor");
        if (component == nullptr)
        {
            return;
        }
        component->anchor = value;
    }

    Vector3 DistanceJoint3D::GetConnectedAnchor() const
    {
        const Component::DistanceJoint3D* component = Resolve("GetConnectedAnchor");
        if (component == nullptr)
        {
            return {};
        }
        return component->connectedAnchor;
    }

    void DistanceJoint3D::SetConnectedAnchor(Vector3 value)
    {
        Component::DistanceJoint3D* component = Resolve("SetConnectedAnchor");
        if (component == nullptr)
        {
            return;
        }
        component->connectedAnchor = value;
    }

    Bool DistanceJoint3D::GetAutoDistance() const
    {
        const Component::DistanceJoint3D* component = Resolve("GetAutoDistance");
        if (component == nullptr)
        {
            return {};
        }
        return component->autoDistance;
    }

    void DistanceJoint3D::SetAutoDistance(Bool value)
    {
        Component::DistanceJoint3D* component = Resolve("SetAutoDistance");
        if (component == nullptr)
        {
            return;
        }
        component->autoDistance = value;
    }

    Float DistanceJoint3D::GetDistance() const
    {
        const Component::DistanceJoint3D* component = Resolve("GetDistance");
        if (component == nullptr)
        {
            return {};
        }
        return component->distance;
    }

    void DistanceJoint3D::SetDistance(Float value)
    {
        Component::DistanceJoint3D* component = Resolve("SetDistance");
        if (component == nullptr)
        {
            return;
        }
        component->distance = value;
    }

    Bool DistanceJoint3D::GetMaxDistanceOnly() const
    {
        const Component::DistanceJoint3D* component = Resolve("GetMaxDistanceOnly");
        if (component == nullptr)
        {
            return {};
        }
        return component->maxDistanceOnly;
    }

    void DistanceJoint3D::SetMaxDistanceOnly(Bool value)
    {
        Component::DistanceJoint3D* component = Resolve("SetMaxDistanceOnly");
        if (component == nullptr)
        {
            return;
        }
        component->maxDistanceOnly = value;
    }

    Float DistanceJoint3D::GetFrequency() const
    {
        const Component::DistanceJoint3D* component = Resolve("GetFrequency");
        if (component == nullptr)
        {
            return {};
        }
        return component->frequency;
    }

    void DistanceJoint3D::SetFrequency(Float value)
    {
        Component::DistanceJoint3D* component = Resolve("SetFrequency");
        if (component == nullptr)
        {
            return;
        }
        component->frequency = value;
    }

    Float DistanceJoint3D::GetDampingRatio() const
    {
        const Component::DistanceJoint3D* component = Resolve("GetDampingRatio");
        if (component == nullptr)
        {
            return {};
        }
        return component->dampingRatio;
    }

    void DistanceJoint3D::SetDampingRatio(Float value)
    {
        Component::DistanceJoint3D* component = Resolve("SetDampingRatio");
        if (component == nullptr)
        {
            return;
        }
        component->dampingRatio = value;
    }

    Bool DistanceJoint3D::GetCollideConnected() const
    {
        const Component::DistanceJoint3D* component = Resolve("GetCollideConnected");
        if (component == nullptr)
        {
            return {};
        }
        return component->collideConnected;
    }

    void DistanceJoint3D::SetCollideConnected(Bool value)
    {
        Component::DistanceJoint3D* component = Resolve("SetCollideConnected");
        if (component == nullptr)
        {
            return;
        }
        component->collideConnected = value;
    }

    Float DistanceJoint3D::GetBreakForce() const
    {
        const Component::DistanceJoint3D* component = Resolve("GetBreakForce");
        if (component == nullptr)
        {
            return {};
        }
        return component->breakForce;
    }

    void DistanceJoint3D::SetBreakForce(Float value)
    {
        Component::DistanceJoint3D* component = Resolve("SetBreakForce");
        if (component == nullptr)
        {
            return;
        }
        component->breakForce = value;
    }

    Float DistanceJoint3D::GetBreakTorque() const
    {
        const Component::DistanceJoint3D* component = Resolve("GetBreakTorque");
        if (component == nullptr)
        {
            return {};
        }
        return component->breakTorque;
    }

    void DistanceJoint3D::SetBreakTorque(Float value)
    {
        Component::DistanceJoint3D* component = Resolve("SetBreakTorque");
        if (component == nullptr)
        {
            return;
        }
        component->breakTorque = value;
    }
}
