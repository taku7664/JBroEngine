#include <JBro/Framework3D/Handle/MeshRenderer3D.h>

#include <JBro/Framework3D/Component/MeshRenderer3D.h>

namespace JBro::Handle
{
    Component::MeshRenderer3D* MeshRenderer3D::Resolve(const char* operation) const
    {
        return static_cast<Component::MeshRenderer3D*>(ResolveComponent(StaticTypeName(), operation));
    }

    AssetId MeshRenderer3D::GetMeshId() const
    {
        const Component::MeshRenderer3D* component = Resolve("GetMeshId");
        if (component == nullptr)
        {
            return {};
        }
        return component->meshId;
    }

    AssetHandle MeshRenderer3D::GetMesh() const
    {
        const Component::MeshRenderer3D* component = Resolve("GetMesh");
        if (component == nullptr)
        {
            return {};
        }
        return component->mesh;
    }

    void MeshRenderer3D::SetMesh(AssetHandle value)
    {
        Component::MeshRenderer3D* component = Resolve("SetMesh");
        if (component == nullptr)
        {
            return;
        }
        component->mesh = value;
    }

    AssetId MeshRenderer3D::GetMaterialId() const
    {
        const Component::MeshRenderer3D* component = Resolve("GetMaterialId");
        if (component == nullptr)
        {
            return {};
        }
        return component->materialId;
    }

    AssetHandle MeshRenderer3D::GetMaterial() const
    {
        const Component::MeshRenderer3D* component = Resolve("GetMaterial");
        if (component == nullptr)
        {
            return {};
        }
        return component->material;
    }

    void MeshRenderer3D::SetMaterial(AssetHandle value)
    {
        Component::MeshRenderer3D* component = Resolve("SetMaterial");
        if (component == nullptr)
        {
            return;
        }
        component->material = value;
    }

    Color MeshRenderer3D::GetTint() const
    {
        const Component::MeshRenderer3D* component = Resolve("GetTint");
        if (component == nullptr)
        {
            return {};
        }
        return component->tint;
    }

    void MeshRenderer3D::SetTint(Color value)
    {
        Component::MeshRenderer3D* component = Resolve("SetTint");
        if (component == nullptr)
        {
            return;
        }
        component->tint = value;
    }

    Bool MeshRenderer3D::GetVisible() const
    {
        const Component::MeshRenderer3D* component = Resolve("GetVisible");
        if (component == nullptr)
        {
            return {};
        }
        return component->visible;
    }

    void MeshRenderer3D::SetVisible(Bool value)
    {
        Component::MeshRenderer3D* component = Resolve("SetVisible");
        if (component == nullptr)
        {
            return;
        }
        component->visible = value;
    }
}
