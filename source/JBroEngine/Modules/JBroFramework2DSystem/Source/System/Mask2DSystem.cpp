#include <JBro/Framework2DSystem/System/Mask2DSystem.h>

#include <JBro/Canvas/Canvas.h>
#include <JBro/Canvas/Internal/CanvasAccess.h>
#include <JBro/Framework2D/Component/Mask2D.h>
#include <JBro/Framework2D/Component/Transform2D.h>
#include <JBro/Runtime/GameObject.h>

#include <algorithm>
#include <cmath>

namespace JBro::System
{
    Int32 Mask2DSystem::GetExecutionOrder() const
    {
        return ExecutionOrder;
    }

    void Mask2DSystem::OnUpdate(Canvas& canvas, Float deltaTime)
    {
        (void)deltaTime;
        canvas.ForEach<Component::Mask2D>([&](Component::Mask2D& mask)
        {
            mask.clipValid = false;
            Object::GameObject* owner = Internal::CanvasAccess::GetOwner(mask);
            const auto* transform = owner != nullptr ? canvas.FindComponentRaw<Component::Transform2D>(owner) : nullptr;
            if (false == mask.IsActiveComponent() || transform == nullptr || false == transform->worldValid)
            {
                return;
            }
            const Float halfWidth = std::fabs(mask.size.x) * 0.5f;
            const Float halfHeight = std::fabs(mask.size.y) * 0.5f;
            const Matrix3x2& world = transform->world;
            Bool first = true;
            for (UInt32 corner = 0; corner < 4; ++corner)
            {
                const Float x = mask.offset.x + Float((corner & 1u) != 0 ? halfWidth.Get() : -halfWidth.Get());
                const Float y = mask.offset.y + Float((corner & 2u) != 0 ? halfHeight.Get() : -halfHeight.Get());
                const Float wx = x * world.m11 + y * world.m21 + world.m31;
                const Float wy = x * world.m12 + y * world.m22 + world.m32;
                if (false == std::isfinite(wx.Get()) || false == std::isfinite(wy.Get()))
                {
                    return;
                }
                if (first)
                {
                    mask.clipMinX = wx;
                    mask.clipMaxX = wx;
                    mask.clipMinY = wy;
                    mask.clipMaxY = wy;
                    first = false;
                }
                else
                {
                    mask.clipMinX = std::min(mask.clipMinX, wx);
                    mask.clipMaxX = std::max(mask.clipMaxX, wx);
                    mask.clipMinY = std::min(mask.clipMinY, wy);
                    mask.clipMaxY = std::max(mask.clipMaxY, wy);
                }
            }
            mask.clipValid = true;
        });
    }
}
