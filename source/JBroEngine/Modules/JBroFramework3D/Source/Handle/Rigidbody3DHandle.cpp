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
}
