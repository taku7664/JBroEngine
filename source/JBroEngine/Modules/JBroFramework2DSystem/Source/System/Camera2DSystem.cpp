#include <JBro/Framework2DSystem/System/Camera2DSystem.h>

#include <JBro/Canvas/Internal/CanvasAccess.h>
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
        // **`primary` 가 먼저고, 하나도 없으면 첫 활성 카메라로 그린다**(D-187).
        //
        // 그전에는 `primary` 가 아니면 아예 그리지 않았다. 카메라를 붙이고 재생을 누른
        // 사람에게는 검은 화면과 "카메라가 없습니다" 만 남는데, 카메라는 분명히 거기
        // 있으므로 무엇이 잘못됐는지 화면에서 알 길이 없다(실제 에디터에서 그랬다).
        // 기존 엔진도 지정이 없으면 첫 활성 카메라로 떨어졌다.
        bool selected = false;
        bool hasFallback = false;
        RenderCamera2D fallback;
        canvas.ForEach<Component::Camera2D>([&](Component::Camera2D& camera)
        {
            if (selected || false == camera.IsActiveComponent())
            {
                return;
            }
            GameObject* owner = Internal::CanvasAccess::GetOwner(camera);
            const auto* world = canvas.FindComponentRaw<Component::Transform2D>(owner);
            if (world == nullptr || false == world->IsActiveComponent() || false == world->worldValid)
            {
                return;
            }
            RenderCamera2D item;
            if (false == TryInvert(world->world, item.view))
            {
                return;
            }
            item.owner = owner;
            item.orthographicSize = camera.orthographicSize;
            item.projection = camera.projection;
            item.nearPlane = camera.nearPlane;
            item.farPlane = camera.farPlane;
            item.clearColor = camera.clearColor;
            if (camera.primary)
            {
                m_renderWorld->SetCamera(item);
                selected = true;
                return;
            }
            // 첫 번째 것만 든다. 뒤의 것으로 덮으면 순회 순서가 바뀔 때마다 다른
            // 카메라로 그려져, 같은 캔버스가 실행할 때마다 다르게 보인다.
            if (false == hasFallback)
            {
                fallback = item;
                hasFallback = true;
            }
        });
        if (false == selected && hasFallback)
        {
            m_renderWorld->SetCamera(fallback);
        }
    }

    void Camera2DSystem::OnUpdate(Canvas& canvas, float deltaTime)
    {
        (void)deltaTime;
        ExtractRenderWorld(canvas);
    }
}
