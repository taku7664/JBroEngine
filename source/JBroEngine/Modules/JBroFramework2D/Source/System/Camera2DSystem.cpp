#include <JBro/Framework2D/System/Camera2DSystem.h>

#include <JBro/Framework2D/Canvas/Canvas.h>
#include <JBro/Framework2D/Component/Camera2D.h>
#include <JBro/Framework2D/Component/Transform2D.h>

#include <cmath>
#include <limits>

namespace JBro::System
{
    namespace
    {
        bool TryInvert(const Matrix3x2& world, Matrix3x2& view)
        {
            const double determinant = static_cast<double>(world.m11) * world.m22
                - static_cast<double>(world.m12) * world.m21;
            if (determinant == 0.0 || false == std::isfinite(determinant))
            {
                return false;
            }
            const double values[] = {
                world.m22 / determinant, -world.m12 / determinant,
                -world.m21 / determinant, world.m11 / determinant,
                (static_cast<double>(world.m32) * world.m21 - static_cast<double>(world.m31) * world.m22) / determinant,
                (static_cast<double>(world.m31) * world.m12 - static_cast<double>(world.m32) * world.m11) / determinant};
            for (double value : values)
            {
                if (false == std::isfinite(value) || std::fabs(value) > (std::numeric_limits<float>::max)())
                {
                    return false;
                }
            }
            view = {static_cast<float>(values[0]), static_cast<float>(values[1]),
                static_cast<float>(values[2]), static_cast<float>(values[3]),
                static_cast<float>(values[4]), static_cast<float>(values[5])};
            return true;
        }
    }

    int Camera2DSystem::GetExecutionOrder() const
    {
        return 300;
    }

    void Camera2DSystem::SetRenderWorld(RenderWorld2D* renderWorld)
    {
        m_renderWorld = renderWorld;
    }

    void Camera2DSystem::ExtractRenderWorld(Canvas& canvas)
    {
        if (m_renderWorld == nullptr)
        {
            return;
        }
        bool selected = false;
        canvas.ForEach<Component::Camera2D>([&](Component::Camera2D& camera)
        {
            if (selected || false == camera.primary || false == camera.IsActiveComponent())
            {
                return;
            }
            GameObject* owner = camera.GetOwner();
            const auto* world = canvas.GetComponent<Component::WorldTransform2D>(owner);
            if (world == nullptr || false == world->IsActiveComponent() || world->dirty)
            {
                return;
            }
            RenderCamera2D item;
            if (false == TryInvert(world->matrix, item.view))
            {
                return;
            }
            item.owner = owner;
            item.orthographicSize = camera.orthographicSize;
            item.projection = camera.projection;
            item.nearPlane = camera.nearPlane;
            item.farPlane = camera.farPlane;
            item.clearColor = camera.clearColor;
            m_renderWorld->SetCamera(item);
            selected = true;
        });
    }

    void Camera2DSystem::OnUpdate(Canvas& canvas, float deltaTime)
    {
        (void)deltaTime;
        ExtractRenderWorld(canvas);
    }
}
