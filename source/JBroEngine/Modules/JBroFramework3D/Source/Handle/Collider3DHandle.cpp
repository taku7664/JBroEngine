#include <JBro/Framework3D/Handle/Collider3D.h>

#include <JBro/Framework3D/Component/Physics3D.h>

namespace JBro::Handle
{
    Component::Collider3D* Collider3D::Resolve(const char* operation) const
    {
        return static_cast<Component::Collider3D*>(ResolveComponent(StaticTypeName(), operation));
    }

    Vector3 Collider3D::GetSize() const
    {
        const Component::Collider3D* component = Resolve("GetSize");
        if (component == nullptr)
        {
            return {};
        }
        return component->size;
    }

    void Collider3D::SetSize(Vector3 value)
    {
        Component::Collider3D* component = Resolve("SetSize");
        if (component == nullptr)
        {
            return;
        }
        component->size = value;
    }
}
