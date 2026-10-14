#include <JBro/Framework2D/Handle/Rigidbody2D.h>

#include <JBro/Framework2D/Component/Physics2D.h>

namespace JBro::Handle
{
    Component::Rigidbody2D* Rigidbody2D::Resolve(const char* operation) const
    {
        return static_cast<Component::Rigidbody2D*>(ResolveComponent(StaticTypeName(), operation));
    }

    Component::BodyType2D Rigidbody2D::GetBodyType() const
    {
        const Component::Rigidbody2D* component = Resolve("GetBodyType");
        if (component == nullptr)
        {
            return {};
        }
        return component->bodyType;
    }

    void Rigidbody2D::SetBodyType(Component::BodyType2D value)
    {
        Component::Rigidbody2D* component = Resolve("SetBodyType");
        if (component == nullptr)
        {
            return;
        }
        component->bodyType = value;
    }

    Vector2 Rigidbody2D::GetLinearVelocity() const
    {
        const Component::Rigidbody2D* component = Resolve("GetLinearVelocity");
        if (component == nullptr)
        {
            return {};
        }
        return component->linearVelocity;
    }

    void Rigidbody2D::SetLinearVelocity(Vector2 value)
    {
        Component::Rigidbody2D* component = Resolve("SetLinearVelocity");
        if (component == nullptr)
        {
            return;
        }
        component->linearVelocity = value;
    }

    Float Rigidbody2D::GetAngularVelocity() const
    {
        const Component::Rigidbody2D* component = Resolve("GetAngularVelocity");
        if (component == nullptr)
        {
            return {};
        }
        return component->angularVelocity;
    }

    void Rigidbody2D::SetAngularVelocity(Float value)
    {
        Component::Rigidbody2D* component = Resolve("SetAngularVelocity");
        if (component == nullptr)
        {
            return;
        }
        component->angularVelocity = value;
    }

    Float Rigidbody2D::GetMass() const
    {
        const Component::Rigidbody2D* component = Resolve("GetMass");
        if (component == nullptr)
        {
            return {};
        }
        return component->mass;
    }

    void Rigidbody2D::SetMass(Float value)
    {
        Component::Rigidbody2D* component = Resolve("SetMass");
        if (component == nullptr)
        {
            return;
        }
        component->mass = value;
    }

    Float Rigidbody2D::GetGravityScale() const
    {
        const Component::Rigidbody2D* component = Resolve("GetGravityScale");
        if (component == nullptr)
        {
            return {};
        }
        return component->gravityScale;
    }

    void Rigidbody2D::SetGravityScale(Float value)
    {
        Component::Rigidbody2D* component = Resolve("SetGravityScale");
        if (component == nullptr)
        {
            return;
        }
        component->gravityScale = value;
    }

    Float Rigidbody2D::GetLinearDamping() const
    {
        const Component::Rigidbody2D* component = Resolve("GetLinearDamping");
        if (component == nullptr)
        {
            return {};
        }
        return component->linearDamping;
    }

    void Rigidbody2D::SetLinearDamping(Float value)
    {
        Component::Rigidbody2D* component = Resolve("SetLinearDamping");
        if (component == nullptr)
        {
            return;
        }
        component->linearDamping = value;
    }

    Float Rigidbody2D::GetAngularDamping() const
    {
        const Component::Rigidbody2D* component = Resolve("GetAngularDamping");
        if (component == nullptr)
        {
            return {};
        }
        return component->angularDamping;
    }

    void Rigidbody2D::SetAngularDamping(Float value)
    {
        Component::Rigidbody2D* component = Resolve("SetAngularDamping");
        if (component == nullptr)
        {
            return;
        }
        component->angularDamping = value;
    }

    Bool Rigidbody2D::GetFixedRotation() const
    {
        const Component::Rigidbody2D* component = Resolve("GetFixedRotation");
        if (component == nullptr)
        {
            return {};
        }
        return component->fixedRotation;
    }

    void Rigidbody2D::SetFixedRotation(Bool value)
    {
        Component::Rigidbody2D* component = Resolve("SetFixedRotation");
        if (component == nullptr)
        {
            return;
        }
        component->fixedRotation = value;
    }

    Bool Rigidbody2D::GetFreezePositionX() const
    {
        const Component::Rigidbody2D* component = Resolve("GetFreezePositionX");
        if (component == nullptr)
        {
            return {};
        }
        return component->freezePositionX;
    }

    void Rigidbody2D::SetFreezePositionX(Bool value)
    {
        Component::Rigidbody2D* component = Resolve("SetFreezePositionX");
        if (component == nullptr)
        {
            return;
        }
        component->freezePositionX = value;
    }

    Bool Rigidbody2D::GetFreezePositionY() const
    {
        const Component::Rigidbody2D* component = Resolve("GetFreezePositionY");
        if (component == nullptr)
        {
            return {};
        }
        return component->freezePositionY;
    }

    void Rigidbody2D::SetFreezePositionY(Bool value)
    {
        Component::Rigidbody2D* component = Resolve("SetFreezePositionY");
        if (component == nullptr)
        {
            return;
        }
        component->freezePositionY = value;
    }

    Bool Rigidbody2D::GetCanSleep() const
    {
        const Component::Rigidbody2D* component = Resolve("GetCanSleep");
        if (component == nullptr)
        {
            return {};
        }
        return component->canSleep;
    }

    void Rigidbody2D::SetCanSleep(Bool value)
    {
        Component::Rigidbody2D* component = Resolve("SetCanSleep");
        if (component == nullptr)
        {
            return;
        }
        component->canSleep = value;
    }

    void Rigidbody2D::AddForce(Vector2 force)
    {
        Component::Rigidbody2D* component = Resolve("AddForce");
        if (component == nullptr)
        {
            return;
        }
        component->AddForce(force);
    }

    void Rigidbody2D::AddForceAtPosition(Vector2 force, Vector2 worldPoint)
    {
        Component::Rigidbody2D* component = Resolve("AddForceAtPosition");
        if (component == nullptr)
        {
            return;
        }
        component->AddForceAtPosition(force, worldPoint);
    }

    void Rigidbody2D::AddTorque(Float torque)
    {
        Component::Rigidbody2D* component = Resolve("AddTorque");
        if (component == nullptr)
        {
            return;
        }
        component->AddTorque(torque);
    }

    void Rigidbody2D::AddImpulse(Vector2 impulse)
    {
        Component::Rigidbody2D* component = Resolve("AddImpulse");
        if (component == nullptr)
        {
            return;
        }
        component->AddImpulse(impulse);
    }

    void Rigidbody2D::AddImpulseAtPosition(Vector2 impulse, Vector2 worldPoint)
    {
        Component::Rigidbody2D* component = Resolve("AddImpulseAtPosition");
        if (component == nullptr)
        {
            return;
        }
        component->AddImpulseAtPosition(impulse, worldPoint);
    }

    void Rigidbody2D::AddAngularImpulse(Float impulse)
    {
        Component::Rigidbody2D* component = Resolve("AddAngularImpulse");
        if (component == nullptr)
        {
            return;
        }
        component->AddAngularImpulse(impulse);
    }

    void Rigidbody2D::WakeUp()
    {
        Component::Rigidbody2D* component = Resolve("WakeUp");
        if (component == nullptr)
        {
            return;
        }
        component->WakeUp();
    }

    Bool Rigidbody2D::IsSleeping() const
    {
        const Component::Rigidbody2D* component = Resolve("IsSleeping");
        if (component == nullptr)
        {
            return {};
        }
        return component->IsSleeping();
    }
}
