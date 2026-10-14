#include <JBro/Framework2D/Handle/Collider2D.h>

#include <JBro/Framework2D/Component/Physics2D.h>

namespace JBro::Handle
{
    Component::Collider2D* Collider2D::Resolve(const char* operation) const
    {
        return static_cast<Component::Collider2D*>(ResolveComponent(StaticTypeName(), operation));
    }

    Component::ColliderShape2D Collider2D::GetShape() const
    {
        const Component::Collider2D* component = Resolve("GetShape");
        if (component == nullptr)
        {
            return {};
        }
        return component->shape;
    }

    void Collider2D::SetShape(Component::ColliderShape2D value)
    {
        Component::Collider2D* component = Resolve("SetShape");
        if (component == nullptr)
        {
            return;
        }
        component->shape = value;
    }

    Vector2 Collider2D::GetOffset() const
    {
        const Component::Collider2D* component = Resolve("GetOffset");
        if (component == nullptr)
        {
            return {};
        }
        return component->offset;
    }

    void Collider2D::SetOffset(Vector2 value)
    {
        Component::Collider2D* component = Resolve("SetOffset");
        if (component == nullptr)
        {
            return;
        }
        component->offset = value;
    }

    Vector2 Collider2D::GetSize() const
    {
        const Component::Collider2D* component = Resolve("GetSize");
        if (component == nullptr)
        {
            return {};
        }
        return component->size;
    }

    void Collider2D::SetSize(Vector2 value)
    {
        Component::Collider2D* component = Resolve("SetSize");
        if (component == nullptr)
        {
            return;
        }
        component->size = value;
    }

    Float Collider2D::GetRadius() const
    {
        const Component::Collider2D* component = Resolve("GetRadius");
        if (component == nullptr)
        {
            return {};
        }
        return component->radius;
    }

    void Collider2D::SetRadius(Float value)
    {
        Component::Collider2D* component = Resolve("SetRadius");
        if (component == nullptr)
        {
            return;
        }
        component->radius = value;
    }

    Bool Collider2D::GetIsTrigger() const
    {
        const Component::Collider2D* component = Resolve("GetIsTrigger");
        if (component == nullptr)
        {
            return {};
        }
        return component->isTrigger;
    }

    void Collider2D::SetIsTrigger(Bool value)
    {
        Component::Collider2D* component = Resolve("SetIsTrigger");
        if (component == nullptr)
        {
            return;
        }
        component->isTrigger = value;
    }

    void Collider2D::GetPoints(Array<Vector2>& points) const
    {
        const Component::Collider2D* component = Resolve("GetPoints");
        if (component == nullptr)
        {
            points.Clear();
            return;
        }
        points = component->points;
    }

    void Collider2D::SetPoints(const Array<Vector2>& points)
    {
        Component::Collider2D* component = Resolve("SetPoints");
        if (component == nullptr)
        {
            return;
        }
        component->points = points;
    }

    Bool Collider2D::GetLoop() const
    {
        const Component::Collider2D* component = Resolve("GetLoop");
        if (component == nullptr)
        {
            return {};
        }
        return component->loop;
    }

    void Collider2D::SetLoop(Bool value)
    {
        Component::Collider2D* component = Resolve("SetLoop");
        if (component == nullptr)
        {
            return;
        }
        component->loop = value;
    }

    Bool Collider2D::GetOneWay() const
    {
        const Component::Collider2D* component = Resolve("GetOneWay");
        if (component == nullptr)
        {
            return {};
        }
        return component->oneWay;
    }

    void Collider2D::SetOneWay(Bool value)
    {
        Component::Collider2D* component = Resolve("SetOneWay");
        if (component == nullptr)
        {
            return;
        }
        component->oneWay = value;
    }

    Float Collider2D::GetFriction() const
    {
        const Component::Collider2D* component = Resolve("GetFriction");
        if (component == nullptr)
        {
            return {};
        }
        return component->friction;
    }

    void Collider2D::SetFriction(Float value)
    {
        Component::Collider2D* component = Resolve("SetFriction");
        if (component == nullptr)
        {
            return;
        }
        component->friction = value;
    }

    Float Collider2D::GetRestitution() const
    {
        const Component::Collider2D* component = Resolve("GetRestitution");
        if (component == nullptr)
        {
            return {};
        }
        return component->restitution;
    }

    void Collider2D::SetRestitution(Float value)
    {
        Component::Collider2D* component = Resolve("SetRestitution");
        if (component == nullptr)
        {
            return;
        }
        component->restitution = value;
    }

    PhysicsLayerMask Collider2D::GetLayer() const
    {
        const Component::Collider2D* component = Resolve("GetLayer");
        if (component == nullptr)
        {
            return {};
        }
        return component->layer;
    }

    void Collider2D::SetLayer(PhysicsLayerMask value)
    {
        Component::Collider2D* component = Resolve("SetLayer");
        if (component == nullptr)
        {
            return;
        }
        component->layer = value;
    }

    PhysicsLayerMask Collider2D::GetMask() const
    {
        const Component::Collider2D* component = Resolve("GetMask");
        if (component == nullptr)
        {
            return {};
        }
        return component->mask;
    }

    void Collider2D::SetMask(PhysicsLayerMask value)
    {
        Component::Collider2D* component = Resolve("SetMask");
        if (component == nullptr)
        {
            return;
        }
        component->mask = value;
    }
}
