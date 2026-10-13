#include <JBro/Framework3D/Handle/Camera3D.h>

#include <JBro/Framework3D/Component/Camera3D.h>

namespace JBro::Handle
{
    Component::Camera3D* Camera3D::Resolve(const char* operation) const
    {
        return static_cast<Component::Camera3D*>(ResolveComponent(StaticTypeName(), operation));
    }

    Component::CameraProjection3D Camera3D::GetProjection() const
    {
        const Component::Camera3D* component = Resolve("GetProjection");
        if (component == nullptr)
        {
            return {};
        }
        return component->projection;
    }

    void Camera3D::SetProjection(Component::CameraProjection3D value)
    {
        Component::Camera3D* component = Resolve("SetProjection");
        if (component == nullptr)
        {
            return;
        }
        component->projection = value;
    }

    Float Camera3D::GetVerticalFieldOfView() const
    {
        const Component::Camera3D* component = Resolve("GetVerticalFieldOfView");
        if (component == nullptr)
        {
            return {};
        }
        return component->verticalFieldOfView;
    }

    void Camera3D::SetVerticalFieldOfView(Float value)
    {
        Component::Camera3D* component = Resolve("SetVerticalFieldOfView");
        if (component == nullptr)
        {
            return;
        }
        component->verticalFieldOfView = value;
    }

    Float Camera3D::GetOrthographicSize() const
    {
        const Component::Camera3D* component = Resolve("GetOrthographicSize");
        if (component == nullptr)
        {
            return {};
        }
        return component->orthographicSize;
    }

    void Camera3D::SetOrthographicSize(Float value)
    {
        Component::Camera3D* component = Resolve("SetOrthographicSize");
        if (component == nullptr)
        {
            return;
        }
        component->orthographicSize = value;
    }

    Float Camera3D::GetNearPlane() const
    {
        const Component::Camera3D* component = Resolve("GetNearPlane");
        if (component == nullptr)
        {
            return {};
        }
        return component->nearPlane;
    }

    void Camera3D::SetNearPlane(Float value)
    {
        Component::Camera3D* component = Resolve("SetNearPlane");
        if (component == nullptr)
        {
            return;
        }
        component->nearPlane = value;
    }

    Float Camera3D::GetFarPlane() const
    {
        const Component::Camera3D* component = Resolve("GetFarPlane");
        if (component == nullptr)
        {
            return {};
        }
        return component->farPlane;
    }

    void Camera3D::SetFarPlane(Float value)
    {
        Component::Camera3D* component = Resolve("SetFarPlane");
        if (component == nullptr)
        {
            return;
        }
        component->farPlane = value;
    }

    Color Camera3D::GetClearColor() const
    {
        const Component::Camera3D* component = Resolve("GetClearColor");
        if (component == nullptr)
        {
            return {};
        }
        return component->clearColor;
    }

    void Camera3D::SetClearColor(Color value)
    {
        Component::Camera3D* component = Resolve("SetClearColor");
        if (component == nullptr)
        {
            return;
        }
        component->clearColor = value;
    }

    Bool Camera3D::GetPrimary() const
    {
        const Component::Camera3D* component = Resolve("GetPrimary");
        if (component == nullptr)
        {
            return {};
        }
        return component->primary;
    }

    void Camera3D::SetPrimary(Bool value)
    {
        Component::Camera3D* component = Resolve("SetPrimary");
        if (component == nullptr)
        {
            return;
        }
        component->primary = value;
    }
}
