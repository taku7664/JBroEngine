#include <JBro/Framework2DSystem/System/Layout2DSystem.h>

#include <JBro/Canvas/Canvas.h>
#include <JBro/Canvas/Internal/CanvasAccess.h>
#include <JBro/Framework2D/Component/Layout2D.h>
#include <JBro/Framework2D/Component/Transform2D.h>
#include <JBro/Runtime/GameObject.h>

#include <JBro/Types/ValueMath.h>

#include <cmath>

namespace JBro::System
{
    Int32 Layout2DSystem::GetExecutionOrder() const
    {
        return ExecutionOrder;
    }

    void Layout2DSystem::OnUpdate(Canvas& canvas, Float deltaTime)
    {
        (void)deltaTime;
        canvas.ForEach<Component::Layout2D>([&](Component::Layout2D& layout)
        {
            Object::GameObject* owner = Internal::CanvasAccess::GetOwner(layout);
            if (owner == nullptr || false == layout.IsActiveComponent())
            {
                return;
            }
            // 1. 놓을 자식을 센다 - 살아 있고 트랜스폼이 있는 것만, 계층 차례대로.
            UInt32 count = 0;
            for (const SafePtr<Object::GameObject>& child : owner->GetChildren())
            {
                Object::GameObject* object = child.TryGet();
                if (object != nullptr && object->IsActiveInHierarchy() && canvas.FindComponentRaw<Component::Transform2D>(object) != nullptr)
                {
                    ++count;
                }
            }
            // 유한하지 않은 값이 하나라도 있으면 이번 프레임은 놓지 않는다 - 자식의 자리를 NaN 으로 만들지 않는다.
            const Float fields[] = { layout.cellSize.x, layout.cellSize.y, layout.spacing.x, layout.spacing.y,
                layout.padding.x, layout.padding.y, layout.pivot.x, layout.pivot.y };
            for (const Float field : fields)
            {
                if (false == std::isfinite(field.Get()))
                {
                    return;
                }
            }
            const Float cellWidth = std::fabs(layout.cellSize.x);
            const Float cellHeight = std::fabs(layout.cellSize.y);
            const UInt32 columns = static_cast<JBro::UInt32>(Max(Int32(1), layout.columns).Get());
            // 자식이 없으면 블록은 여백뿐이다.
            UInt32 across = 0;
            UInt32 down = 0;
            switch (layout.mode)
            {
            case Component::LayoutMode::Horizontal:
                across = count;
                down = count > 0 ? UInt32(1) : UInt32(0);
                break;
            case Component::LayoutMode::Grid:
                across = Min(count, columns);
                down = (count + columns - 1) / columns;
                break;
            case Component::LayoutMode::Vertical:
            default:
                across = count > 0 ? UInt32(1) : UInt32(0);
                down = count;
                break;
            }
            // 2. 블록의 크기와 원점. 블록의 `pivot` 이 이 오브젝트의 원점에 온다 - (0, 1) 이면 왼쪽 위다.
            const Float width = layout.padding.x * 2.0f + static_cast<JBro::Float>(across) * cellWidth
                + (across > 0 ? static_cast<JBro::Float>(across - 1) * layout.spacing.x : Float(0.0f));
            const Float height = layout.padding.y * 2.0f + static_cast<JBro::Float>(down) * cellHeight
                + (down > 0 ? static_cast<JBro::Float>(down - 1) * layout.spacing.y : Float(0.0f));
            layout.contentWidth = width;
            layout.contentHeight = height;
            const Float left = -layout.pivot.x * width;
            const Float top = (1.0f - layout.pivot.y) * height;
            // 3. 자식을 칸의 가운데에 놓는다.
            UInt32 index = 0;
            for (const SafePtr<Object::GameObject>& child : owner->GetChildren())
            {
                Object::GameObject* object = child.TryGet();
                auto* place = object != nullptr && object->IsActiveInHierarchy() ? canvas.FindComponentRaw<Component::Transform2D>(object) : nullptr;
                if (place == nullptr)
                {
                    continue;
                }
                UInt32 column = 0;
                UInt32 row = 0;
                switch (layout.mode)
                {
                case Component::LayoutMode::Horizontal:
                    column = index;
                    break;
                case Component::LayoutMode::Grid:
                    column = index % columns;
                    row = index / columns;
                    break;
                case Component::LayoutMode::Vertical:
                default:
                    row = index;
                    break;
                }
                place->position.x = left + layout.padding.x + static_cast<JBro::Float>(column) * (cellWidth + layout.spacing.x) + cellWidth * 0.5f;
                place->position.y = top - layout.padding.y - static_cast<JBro::Float>(row) * (cellHeight + layout.spacing.y) - cellHeight * 0.5f;
                ++index;
            }
        });
    }
}
