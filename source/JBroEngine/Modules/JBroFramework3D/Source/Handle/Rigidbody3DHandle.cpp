#include <JBro/Framework3D/Handle/Rigidbody3D.h>

#include <JBro/Framework3D/Component/Physics3D.h>

namespace JBro::Handle
{
    Component::Rigidbody3D* Rigidbody3D::Resolve(const char* operation) const
    {
        return static_cast<Component::Rigidbody3D*>(ResolveComponent(StaticTypeName(), operation));
    }

    Vector3 Rigidbody3D::GetLinearVelocity() const
    {
        const Component::Rigidbody3D* component = Resolve("GetLinearVelocity");
        if (component == nullptr)
        {
            return {};
        }
        return component->linearVelocity;
    }

    void Rigidbody3D::SetLinearVelocity(Vector3 value)
    {
        Component::Rigidbody3D* component = Resolve("SetLinearVelocity");
        if (component == nullptr)
        {
            return;
        }
        component->linearVelocity = value;
    }

    Vector3 Rigidbody3D::GetAngularVelocity() const
    {
        const Component::Rigidbody3D* component = Resolve("GetAngularVelocity");
        if (component == nullptr)
        {
            return {};
        }
        return component->angularVelocity;
    }

    void Rigidbody3D::SetAngularVelocity(Vector3 value)
    {
        Component::Rigidbody3D* component = Resolve("SetAngularVelocity");
        if (component == nullptr)
        {
            return;
        }
        component->angularVelocity = value;
    }

    Float Rigidbody3D::GetMass() const
    {
        const Component::Rigidbody3D* component = Resolve("GetMass");
        if (component == nullptr)
        {
            return {};
        }
        return component->mass;
    }

    void Rigidbody3D::SetMass(Float value)
    {
        Component::Rigidbody3D* component = Resolve("SetMass");
        if (component == nullptr)
        {
            return;
        }
        component->mass = value;
    }

    Component::BodyType3D Rigidbody3D::GetBodyType() const
    {
        const Component::Rigidbody3D* component = Resolve("GetBodyType");
        if (component == nullptr)
        {
            return {};
        }
        return component->bodyType;
    }

    void Rigidbody3D::SetBodyType(Component::BodyType3D value)
    {
        Component::Rigidbody3D* component = Resolve("SetBodyType");
        if (component == nullptr)
        {
            return;
        }
        component->bodyType = value;
    }

    Float Rigidbody3D::GetGravityScale() const
    {
        const Component::Rigidbody3D* component = Resolve("GetGravityScale");
        if (component == nullptr)
        {
            return {};
        }
        return component->gravityScale;
    }

    void Rigidbody3D::SetGravityScale(Float value)
    {
        Component::Rigidbody3D* component = Resolve("SetGravityScale");
        if (component == nullptr)
        {
            return;
        }
        component->gravityScale = value;
    }

    Float Rigidbody3D::GetLinearDamping() const
    {
        const Component::Rigidbody3D* component = Resolve("GetLinearDamping");
        if (component == nullptr)
        {
            return {};
        }
        return component->linearDamping;
    }

    void Rigidbody3D::SetLinearDamping(Float value)
    {
        Component::Rigidbody3D* component = Resolve("SetLinearDamping");
        if (component == nullptr)
        {
            return;
        }
        component->linearDamping = value;
    }

    Float Rigidbody3D::GetAngularDamping() const
    {
        const Component::Rigidbody3D* component = Resolve("GetAngularDamping");
        if (component == nullptr)
        {
            return {};
        }
        return component->angularDamping;
    }

    void Rigidbody3D::SetAngularDamping(Float value)
    {
        Component::Rigidbody3D* component = Resolve("SetAngularDamping");
        if (component == nullptr)
        {
            return;
        }
        component->angularDamping = value;
    }

    Bool Rigidbody3D::GetFreezePositionX() const
    {
        const Component::Rigidbody3D* component = Resolve("GetFreezePositionX");
        if (component == nullptr)
        {
            return {};
        }
        return component->freezePositionX;
    }

    void Rigidbody3D::SetFreezePositionX(Bool value)
    {
        Component::Rigidbody3D* component = Resolve("SetFreezePositionX");
        if (component == nullptr)
        {
            return;
        }
        component->freezePositionX = value;
    }

    Bool Rigidbody3D::GetFreezePositionY() const
    {
        const Component::Rigidbody3D* component = Resolve("GetFreezePositionY");
        if (component == nullptr)
        {
            return {};
        }
        return component->freezePositionY;
    }

    void Rigidbody3D::SetFreezePositionY(Bool value)
    {
        Component::Rigidbody3D* component = Resolve("SetFreezePositionY");
        if (component == nullptr)
        {
            return;
        }
        component->freezePositionY = value;
    }

    Bool Rigidbody3D::GetFreezePositionZ() const
    {
        const Component::Rigidbody3D* component = Resolve("GetFreezePositionZ");
        if (component == nullptr)
        {
            return {};
        }
        return component->freezePositionZ;
    }

    void Rigidbody3D::SetFreezePositionZ(Bool value)
    {
        Component::Rigidbody3D* component = Resolve("SetFreezePositionZ");
        if (component == nullptr)
        {
            return;
        }
        component->freezePositionZ = value;
    }

    Bool Rigidbody3D::GetFreezeRotationX() const
    {
        const Component::Rigidbody3D* component = Resolve("GetFreezeRotationX");
        if (component == nullptr)
        {
            return {};
        }
        return component->freezeRotationX;
    }

    void Rigidbody3D::SetFreezeRotationX(Bool value)
    {
        Component::Rigidbody3D* component = Resolve("SetFreezeRotationX");
        if (component == nullptr)
        {
            return;
        }
        component->freezeRotationX = value;
    }

    Bool Rigidbody3D::GetFreezeRotationY() const
    {
        const Component::Rigidbody3D* component = Resolve("GetFreezeRotationY");
        if (component == nullptr)
        {
            return {};
        }
        return component->freezeRotationY;
    }

    void Rigidbody3D::SetFreezeRotationY(Bool value)
    {
        Component::Rigidbody3D* component = Resolve("SetFreezeRotationY");
        if (component == nullptr)
        {
            return;
        }
        component->freezeRotationY = value;
    }

    Bool Rigidbody3D::GetFreezeRotationZ() const
    {
        const Component::Rigidbody3D* component = Resolve("GetFreezeRotationZ");
        if (component == nullptr)
        {
            return {};
        }
        return component->freezeRotationZ;
    }

    void Rigidbody3D::SetFreezeRotationZ(Bool value)
    {
        Component::Rigidbody3D* component = Resolve("SetFreezeRotationZ");
        if (component == nullptr)
        {
            return;
        }
        component->freezeRotationZ = value;
    }

    Bool Rigidbody3D::GetCanSleep() const
    {
        const Component::Rigidbody3D* component = Resolve("GetCanSleep");
        if (component == nullptr)
        {
            return {};
        }
        return component->canSleep;
    }

    void Rigidbody3D::SetCanSleep(Bool value)
    {
        Component::Rigidbody3D* component = Resolve("SetCanSleep");
        if (component == nullptr)
        {
            return;
        }
        component->canSleep = value;
    }

    void Rigidbody3D::AddForce(Vector3 force)
    {
        Component::Rigidbody3D* component = Resolve("AddForce");
        if (component == nullptr)
        {
            return;
        }
        component->AddForce(force);
    }

    void Rigidbody3D::AddForceAtPosition(Vector3 force, Vector3 worldPoint)
    {
        Component::Rigidbody3D* component = Resolve("AddForceAtPosition");
        if (component == nullptr)
        {
            return;
        }
        component->AddForceAtPosition(force, worldPoint);
    }

    void Rigidbody3D::AddTorque(Vector3 torque)
    {
        Component::Rigidbody3D* component = Resolve("AddTorque");
        if (component == nullptr)
        {
            return;
        }
        component->AddTorque(torque);
    }

    void Rigidbody3D::AddImpulse(Vector3 impulse)
    {
        Component::Rigidbody3D* component = Resolve("AddImpulse");
        if (component == nullptr)
        {
            return;
        }
        component->AddImpulse(impulse);
    }

    void Rigidbody3D::AddImpulseAtPosition(Vector3 impulse, Vector3 worldPoint)
    {
        Component::Rigidbody3D* component = Resolve("AddImpulseAtPosition");
        if (component == nullptr)
        {
            return;
        }
        component->AddImpulseAtPosition(impulse, worldPoint);
    }

    void Rigidbody3D::AddAngularImpulse(Vector3 impulse)
    {
        Component::Rigidbody3D* component = Resolve("AddAngularImpulse");
        if (component == nullptr)
        {
            return;
        }
        component->AddAngularImpulse(impulse);
    }

    void Rigidbody3D::WakeUp()
    {
        Component::Rigidbody3D* component = Resolve("WakeUp");
        if (component == nullptr)
        {
            return;
        }
        component->WakeUp();
    }

    Bool Rigidbody3D::IsSleeping() const
    {
        const Component::Rigidbody3D* component = Resolve("IsSleeping");
        return component != nullptr && component->IsSleeping();
    }
}
