#include <JBro/Framework3D/Handle/Rigidbody3D.h>

#include <JBro/Framework3D/Component/Physics3D.h>

namespace JBro::Handle
{
    Component::Rigidbody3D* Rigidbody3D::Resolve(const char* operation) const
    {
        return static_cast<Component::Rigidbody3D*>(ResolveComponent(StaticTypeName(), operation));
    }

    Vector3 Rigidbody3D::GetVelocity() const
    {
        const Component::Rigidbody3D* component = Resolve("GetVelocity");
        if (component == nullptr)
        {
            return {};
        }
        return component->velocity;
    }

    void Rigidbody3D::SetVelocity(Vector3 value)
    {
        Component::Rigidbody3D* component = Resolve("SetVelocity");
        if (component == nullptr)
        {
            return;
        }
        component->velocity = value;
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
