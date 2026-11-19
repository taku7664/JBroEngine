#include <JBro/Framework3D/Handle/HingeJoint3D.h>

#include <JBro/Framework3D/Component/Physics3D.h>

namespace JBro::Handle
{
    Component::HingeJoint3D* HingeJoint3D::Resolve(const char* operation) const
    {
        return static_cast<Component::HingeJoint3D*>(ResolveComponent(StaticTypeName(), operation));
    }

    GameObject HingeJoint3D::GetConnectedObject() const
    {
        const Component::HingeJoint3D* component = Resolve("GetConnectedObject");
        if (component == nullptr)
        {
            return {};
        }
        return component->connectedObject;
    }

    void HingeJoint3D::SetConnectedObject(GameObject value)
    {
        Component::HingeJoint3D* component = Resolve("SetConnectedObject");
        if (component == nullptr)
        {
            return;
        }
        component->connectedObject = value;
    }

    Vector3 HingeJoint3D::GetAnchor() const
    {
        const Component::HingeJoint3D* component = Resolve("GetAnchor");
        if (component == nullptr)
        {
            return {};
        }
        return component->anchor;
    }

    void HingeJoint3D::SetAnchor(Vector3 value)
    {
        Component::HingeJoint3D* component = Resolve("SetAnchor");
        if (component == nullptr)
        {
            return;
        }
        component->anchor = value;
    }

    Vector3 HingeJoint3D::GetConnectedAnchor() const
    {
        const Component::HingeJoint3D* component = Resolve("GetConnectedAnchor");
        if (component == nullptr)
        {
            return {};
        }
        return component->connectedAnchor;
    }

    void HingeJoint3D::SetConnectedAnchor(Vector3 value)
    {
        Component::HingeJoint3D* component = Resolve("SetConnectedAnchor");
        if (component == nullptr)
        {
            return;
        }
        component->connectedAnchor = value;
    }

    Bool HingeJoint3D::GetAutoConnectedAnchor() const
    {
        const Component::HingeJoint3D* component = Resolve("GetAutoConnectedAnchor");
        if (component == nullptr)
        {
            return {};
        }
        return component->autoConnectedAnchor;
    }

    void HingeJoint3D::SetAutoConnectedAnchor(Bool value)
    {
        Component::HingeJoint3D* component = Resolve("SetAutoConnectedAnchor");
        if (component == nullptr)
        {
            return;
        }
        component->autoConnectedAnchor = value;
    }

    Vector3 HingeJoint3D::GetAxis() const
    {
        const Component::HingeJoint3D* component = Resolve("GetAxis");
        if (component == nullptr)
        {
            return {};
        }
        return component->axis;
    }

    void HingeJoint3D::SetAxis(Vector3 value)
    {
        Component::HingeJoint3D* component = Resolve("SetAxis");
        if (component == nullptr)
        {
            return;
        }
        component->axis = value;
    }

    Bool HingeJoint3D::GetUseLimits() const
    {
        const Component::HingeJoint3D* component = Resolve("GetUseLimits");
        if (component == nullptr)
        {
            return {};
        }
        return component->useLimits;
    }

    void HingeJoint3D::SetUseLimits(Bool value)
    {
        Component::HingeJoint3D* component = Resolve("SetUseLimits");
        if (component == nullptr)
        {
            return;
        }
        component->useLimits = value;
    }

    Degree HingeJoint3D::GetLowerAngle() const
    {
        const Component::HingeJoint3D* component = Resolve("GetLowerAngle");
        if (component == nullptr)
        {
            return {};
        }
        return component->lowerAngle;
    }

    void HingeJoint3D::SetLowerAngle(Degree value)
    {
        Component::HingeJoint3D* component = Resolve("SetLowerAngle");
        if (component == nullptr)
        {
            return;
        }
        component->lowerAngle = value;
    }

    Degree HingeJoint3D::GetUpperAngle() const
    {
        const Component::HingeJoint3D* component = Resolve("GetUpperAngle");
        if (component == nullptr)
        {
            return {};
        }
        return component->upperAngle;
    }

    void HingeJoint3D::SetUpperAngle(Degree value)
    {
        Component::HingeJoint3D* component = Resolve("SetUpperAngle");
        if (component == nullptr)
        {
            return;
        }
        component->upperAngle = value;
    }

    Bool HingeJoint3D::GetUseMotor() const
    {
        const Component::HingeJoint3D* component = Resolve("GetUseMotor");
        if (component == nullptr)
        {
            return {};
        }
        return component->useMotor;
    }

    void HingeJoint3D::SetUseMotor(Bool value)
    {
        Component::HingeJoint3D* component = Resolve("SetUseMotor");
        if (component == nullptr)
        {
            return;
        }
        component->useMotor = value;
    }

    Degree HingeJoint3D::GetMotorSpeed() const
    {
        const Component::HingeJoint3D* component = Resolve("GetMotorSpeed");
        if (component == nullptr)
        {
            return {};
        }
        return component->motorSpeed;
    }

    void HingeJoint3D::SetMotorSpeed(Degree value)
    {
        Component::HingeJoint3D* component = Resolve("SetMotorSpeed");
        if (component == nullptr)
        {
            return;
        }
        component->motorSpeed = value;
    }

    Float HingeJoint3D::GetMaxMotorTorque() const
    {
        const Component::HingeJoint3D* component = Resolve("GetMaxMotorTorque");
        if (component == nullptr)
        {
            return {};
        }
        return component->maxMotorTorque;
    }

    void HingeJoint3D::SetMaxMotorTorque(Float value)
    {
        Component::HingeJoint3D* component = Resolve("SetMaxMotorTorque");
        if (component == nullptr)
        {
            return;
        }
        component->maxMotorTorque = value;
    }

    Bool HingeJoint3D::GetCollideConnected() const
    {
        const Component::HingeJoint3D* component = Resolve("GetCollideConnected");
        if (component == nullptr)
        {
            return {};
        }
        return component->collideConnected;
    }

    void HingeJoint3D::SetCollideConnected(Bool value)
    {
        Component::HingeJoint3D* component = Resolve("SetCollideConnected");
        if (component == nullptr)
        {
            return;
        }
        component->collideConnected = value;
    }
}
