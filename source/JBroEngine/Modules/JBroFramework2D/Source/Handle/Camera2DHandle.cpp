#include <JBro/Framework2D/Handle/Camera2D.h>

#include <JBro/Framework2D/Component/Camera2D.h>

namespace JBro::Handle
{
    Component::Camera2D* Camera2D::Resolve(const char* operation) const
    {
        return static_cast<Component::Camera2D*>(ResolveComponent(StaticTypeName(), operation));
    }

    Component::CameraProjection2D Camera2D::GetProjection() const
    {
        const Component::Camera2D* component = Resolve("GetProjection");
        if (component == nullptr)
        {
            return {};
        }
        return component->projection;
    }

    void Camera2D::SetProjection(Component::CameraProjection2D value)
    {
        Component::Camera2D* component = Resolve("SetProjection");
        if (component == nullptr)
        {
            return;
        }
        component->projection = value;
    }

    Float Camera2D::GetOrthographicSize() const
    {
        const Component::Camera2D* component = Resolve("GetOrthographicSize");
        if (component == nullptr)
        {
            return {};
        }
        return component->orthographicSize;
    }

    void Camera2D::SetOrthographicSize(Float value)
    {
        Component::Camera2D* component = Resolve("SetOrthographicSize");
        if (component == nullptr)
        {
            return;
        }
        component->orthographicSize = value;
    }

    Float Camera2D::GetPixelsPerUnit() const
    {
        const Component::Camera2D* component = Resolve("GetPixelsPerUnit");
        if (component == nullptr)
        {
            return {};
        }
        return component->pixelsPerUnit;
    }

    void Camera2D::SetPixelsPerUnit(Float value)
    {
        Component::Camera2D* component = Resolve("SetPixelsPerUnit");
        if (component == nullptr)
        {
            return;
        }
        component->pixelsPerUnit = value;
    }

    Float Camera2D::GetNearPlane() const
    {
        const Component::Camera2D* component = Resolve("GetNearPlane");
        if (component == nullptr)
        {
            return {};
        }
        return component->nearPlane;
    }

    void Camera2D::SetNearPlane(Float value)
    {
        Component::Camera2D* component = Resolve("SetNearPlane");
        if (component == nullptr)
        {
            return;
        }
        component->nearPlane = value;
    }

    Float Camera2D::GetFarPlane() const
    {
        const Component::Camera2D* component = Resolve("GetFarPlane");
        if (component == nullptr)
        {
            return {};
        }
        return component->farPlane;
    }

    void Camera2D::SetFarPlane(Float value)
    {
        Component::Camera2D* component = Resolve("SetFarPlane");
        if (component == nullptr)
        {
            return;
        }
        component->farPlane = value;
    }

    Color Camera2D::GetClearColor() const
    {
        const Component::Camera2D* component = Resolve("GetClearColor");
        if (component == nullptr)
        {
            return {};
        }
        return component->clearColor;
    }

    void Camera2D::SetClearColor(Color value)
    {
        Component::Camera2D* component = Resolve("SetClearColor");
        if (component == nullptr)
        {
            return;
        }
        component->clearColor = value;
    }

    Bool Camera2D::GetPrimary() const
    {
        const Component::Camera2D* component = Resolve("GetPrimary");
        if (component == nullptr)
        {
            return {};
        }
        return component->primary;
    }

    void Camera2D::SetPrimary(Bool value)
    {
        Component::Camera2D* component = Resolve("SetPrimary");
        if (component == nullptr)
        {
            return;
        }
        component->primary = value;
    }

    Vector2 Camera2D::GetViewportOrigin() const
    {
        const Component::Camera2D* component = Resolve("GetViewportOrigin");
        if (component == nullptr)
        {
            return {};
        }
        return component->viewportOrigin;
    }

    void Camera2D::SetViewportOrigin(Vector2 value)
    {
        Component::Camera2D* component = Resolve("SetViewportOrigin");
        if (component == nullptr)
        {
            return;
        }
        component->viewportOrigin = value;
    }

    Vector2 Camera2D::GetViewportSize() const
    {
        const Component::Camera2D* component = Resolve("GetViewportSize");
        if (component == nullptr)
        {
            return {};
        }
        return component->viewportSize;
    }

    void Camera2D::SetViewportSize(Vector2 value)
    {
        Component::Camera2D* component = Resolve("SetViewportSize");
        if (component == nullptr)
        {
            return;
        }
        component->viewportSize = value;
    }

    Int32 Camera2D::GetDepth() const
    {
        const Component::Camera2D* component = Resolve("GetDepth");
        if (component == nullptr)
        {
            return {};
        }
        return component->depth;
    }

    void Camera2D::SetDepth(Int32 value)
    {
        Component::Camera2D* component = Resolve("SetDepth");
        if (component == nullptr)
        {
            return;
        }
        component->depth = value;
    }

    AssetHandle Camera2D::GetRenderTexture() const
    {
        const Component::Camera2D* component = Resolve("GetRenderTexture");
        return component != nullptr ? component->renderTexture : AssetHandle{};
    }

    void Camera2D::SetRenderTexture(AssetHandle value)
    {
        Component::Camera2D* component = Resolve("SetRenderTexture");
        if (component == nullptr)
        {
            return;
        }
        component->renderTexture = value;
    }

    Bool Camera2D::GetClearViewport() const
    {
        const Component::Camera2D* component = Resolve("GetClearViewport");
        if (component == nullptr)
        {
            return {};
        }
        return component->clearViewport;
    }

    void Camera2D::SetClearViewport(Bool value)
    {
        Component::Camera2D* component = Resolve("SetClearViewport");
        if (component == nullptr)
        {
            return;
        }
        component->clearViewport = value;
    }
}
