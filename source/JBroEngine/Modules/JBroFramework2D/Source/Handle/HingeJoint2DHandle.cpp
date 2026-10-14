#include <JBro/Framework2D/Handle/HingeJoint2D.h>

#include <JBro/Framework2D/Component/Physics2D.h>

namespace JBro::Handle
{
    Component::HingeJoint2D* HingeJoint2D::Resolve(const char* operation) const
    {
        return static_cast<Component::HingeJoint2D*>(ResolveComponent(StaticTypeName(), operation));
    }

    GameObject HingeJoint2D::GetConnectedObject() const
    {
        const Component::HingeJoint2D* component = Resolve("GetConnectedObject");
        if (component == nullptr)
        {
            return {};
        }
        return component->connectedObject;
    }

    void HingeJoint2D::SetConnectedObject(GameObject value)
    {
        Component::HingeJoint2D* component = Resolve("SetConnectedObject");
        if (component == nullptr)
        {
            return;
        }
        component->connectedObject = value;
    }

    Vector2 HingeJoint2D::GetAnchor() const
    {
        const Component::HingeJoint2D* component = Resolve("GetAnchor");
        if (component == nullptr)
        {
            return {};
        }
        return component->anchor;
    }

    void HingeJoint2D::SetAnchor(Vector2 value)
    {
        Component::HingeJoint2D* component = Resolve("SetAnchor");
        if (component == nullptr)
        {
            return;
        }
        component->anchor = value;
    }

    Vector2 HingeJoint2D::GetConnectedAnchor() const
    {
        const Component::HingeJoint2D* component = Resolve("GetConnectedAnchor");
        if (component == nullptr)
        {
            return {};
        }
        return component->connectedAnchor;
    }

    void HingeJoint2D::SetConnectedAnchor(Vector2 value)
    {
        Component::HingeJoint2D* component = Resolve("SetConnectedAnchor");
        if (component == nullptr)
        {
            return;
        }
        component->connectedAnchor = value;
    }

    Bool HingeJoint2D::GetAutoConnectedAnchor() const
    {
        const Component::HingeJoint2D* component = Resolve("GetAutoConnectedAnchor");
        if (component == nullptr)
        {
            return {};
        }
        return component->autoConnectedAnchor;
    }

    void HingeJoint2D::SetAutoConnectedAnchor(Bool value)
    {
        Component::HingeJoint2D* component = Resolve("SetAutoConnectedAnchor");
        if (component == nullptr)
        {
            return;
        }
        component->autoConnectedAnchor = value;
    }

    Bool HingeJoint2D::GetUseLimits() const
    {
        const Component::HingeJoint2D* component = Resolve("GetUseLimits");
        if (component == nullptr)
        {
            return {};
        }
        return component->useLimits;
    }

    void HingeJoint2D::SetUseLimits(Bool value)
    {
        Component::HingeJoint2D* component = Resolve("SetUseLimits");
        if (component == nullptr)
        {
            return;
        }
        component->useLimits = value;
    }

    Degree HingeJoint2D::GetLowerAngle() const
    {
        const Component::HingeJoint2D* component = Resolve("GetLowerAngle");
        if (component == nullptr)
        {
            return {};
        }
        return component->lowerAngle;
    }

    void HingeJoint2D::SetLowerAngle(Degree value)
    {
        Component::HingeJoint2D* component = Resolve("SetLowerAngle");
        if (component == nullptr)
        {
            return;
        }
        component->lowerAngle = value;
    }

    Degree HingeJoint2D::GetUpperAngle() const
    {
        const Component::HingeJoint2D* component = Resolve("GetUpperAngle");
        if (component == nullptr)
        {
            return {};
        }
        return component->upperAngle;
    }

    void HingeJoint2D::SetUpperAngle(Degree value)
    {
        Component::HingeJoint2D* component = Resolve("SetUpperAngle");
        if (component == nullptr)
        {
            return;
        }
        component->upperAngle = value;
    }

    Bool HingeJoint2D::GetUseMotor() const
    {
        const Component::HingeJoint2D* component = Resolve("GetUseMotor");
        if (component == nullptr)
        {
            return {};
        }
        return component->useMotor;
    }

    void HingeJoint2D::SetUseMotor(Bool value)
    {
        Component::HingeJoint2D* component = Resolve("SetUseMotor");
        if (component == nullptr)
        {
            return;
        }
        component->useMotor = value;
    }

    Degree HingeJoint2D::GetMotorSpeed() const
    {
        const Component::HingeJoint2D* component = Resolve("GetMotorSpeed");
        if (component == nullptr)
        {
            return {};
        }
        return component->motorSpeed;
    }

    void HingeJoint2D::SetMotorSpeed(Degree value)
    {
        Component::HingeJoint2D* component = Resolve("SetMotorSpeed");
        if (component == nullptr)
        {
            return;
        }
        component->motorSpeed = value;
    }

    Float HingeJoint2D::GetMaxMotorTorque() const
    {
        const Component::HingeJoint2D* component = Resolve("GetMaxMotorTorque");
        if (component == nullptr)
        {
            return {};
        }
        return component->maxMotorTorque;
    }

    void HingeJoint2D::SetMaxMotorTorque(Float value)
    {
        Component::HingeJoint2D* component = Resolve("SetMaxMotorTorque");
        if (component == nullptr)
        {
            return;
        }
        component->maxMotorTorque = value;
    }

    Bool HingeJoint2D::GetCollideConnected() const
    {
        const Component::HingeJoint2D* component = Resolve("GetCollideConnected");
        if (component == nullptr)
        {
            return {};
        }
        return component->collideConnected;
    }

    void HingeJoint2D::SetCollideConnected(Bool value)
    {
        Component::HingeJoint2D* component = Resolve("SetCollideConnected");
        if (component == nullptr)
        {
            return;
        }
        component->collideConnected = value;
    }
}
