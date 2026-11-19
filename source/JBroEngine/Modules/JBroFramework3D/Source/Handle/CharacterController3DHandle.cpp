#include <JBro/Framework3D/Handle/CharacterController3D.h>

#include <JBro/Framework3D/Component/Physics3D.h>

namespace JBro::Handle
{
    Component::CharacterController3D* CharacterController3D::Resolve(const char* operation) const
    {
        return static_cast<Component::CharacterController3D*>(ResolveComponent(StaticTypeName(), operation));
    }

    void CharacterController3D::Move(Vector3 motion)
    {
        Component::CharacterController3D* component = Resolve("Move");
        if (component == nullptr)
        {
            return;
        }
        component->Move(motion);
    }

    Bool CharacterController3D::IsGrounded() const
    {
        const Component::CharacterController3D* component = Resolve("IsGrounded");
        if (component == nullptr)
        {
            return {};
        }
        return component->IsGrounded();
    }

    Vector3 CharacterController3D::GetVelocity() const
    {
        const Component::CharacterController3D* component = Resolve("GetVelocity");
        if (component == nullptr)
        {
            return {};
        }
        return component->GetVelocity();
    }

    Vector3 CharacterController3D::GetCenter() const
    {
        const Component::CharacterController3D* component = Resolve("GetCenter");
        if (component == nullptr)
        {
            return {};
        }
        return component->center;
    }

    void CharacterController3D::SetCenter(Vector3 value)
    {
        Component::CharacterController3D* component = Resolve("SetCenter");
        if (component == nullptr)
        {
            return;
        }
        component->center = value;
    }

    Float CharacterController3D::GetRadius() const
    {
        const Component::CharacterController3D* component = Resolve("GetRadius");
        if (component == nullptr)
        {
            return {};
        }
        return component->radius;
    }

    void CharacterController3D::SetRadius(Float value)
    {
        Component::CharacterController3D* component = Resolve("SetRadius");
        if (component == nullptr)
        {
            return;
        }
        component->radius = value;
    }

    Float CharacterController3D::GetHeight() const
    {
        const Component::CharacterController3D* component = Resolve("GetHeight");
        if (component == nullptr)
        {
            return {};
        }
        return component->height;
    }

    void CharacterController3D::SetHeight(Float value)
    {
        Component::CharacterController3D* component = Resolve("SetHeight");
        if (component == nullptr)
        {
            return;
        }
        component->height = value;
    }

    Degree CharacterController3D::GetSlopeLimit() const
    {
        const Component::CharacterController3D* component = Resolve("GetSlopeLimit");
        if (component == nullptr)
        {
            return {};
        }
        return component->slopeLimit;
    }

    void CharacterController3D::SetSlopeLimit(Degree value)
    {
        Component::CharacterController3D* component = Resolve("SetSlopeLimit");
        if (component == nullptr)
        {
            return;
        }
        component->slopeLimit = value;
    }

    Float CharacterController3D::GetStepOffset() const
    {
        const Component::CharacterController3D* component = Resolve("GetStepOffset");
        if (component == nullptr)
        {
            return {};
        }
        return component->stepOffset;
    }

    void CharacterController3D::SetStepOffset(Float value)
    {
        Component::CharacterController3D* component = Resolve("SetStepOffset");
        if (component == nullptr)
        {
            return;
        }
        component->stepOffset = value;
    }
}
