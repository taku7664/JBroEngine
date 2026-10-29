#include <JBro/Framework2DSystem/Rendering/ClipRect2D.h>

#include <JBro/Canvas/Canvas.h>
#include <JBro/Framework2D/Component/Mask2D.h>
#include <JBro/Runtime/GameObject.h>

#include <algorithm>

namespace JBro
{
    Bool ComputeClipRect2D(Canvas& canvas, Object::GameObject* object, Float rect[4])
    {
        Bool any = false;
        for (Object::GameObject* at = object; at != nullptr; at = at->GetParent())
        {
            const auto* mask = canvas.FindComponentRaw<Component::Mask2D>(at);
            if (mask == nullptr || false == mask->IsActiveComponent() || false == mask->clipValid)
            {
                continue;
            }
            if (false == any)
            {
                rect[0] = mask->clipMinX;
                rect[1] = mask->clipMinY;
                rect[2] = mask->clipMaxX;
                rect[3] = mask->clipMaxY;
                any = true;
                continue;
            }
            // 겹치면 교집합이다. 비면(어긋난 마스크) 넓이 0 의 사각형으로 남는다 - 아무것도 그리지 않는다.
            rect[0] = std::max(rect[0], mask->clipMinX);
            rect[1] = std::max(rect[1], mask->clipMinY);
            rect[2] = std::max(rect[0], std::min(rect[2], mask->clipMaxX));
            rect[3] = std::max(rect[1], std::min(rect[3], mask->clipMaxY));
        }
        return any;
    }

    Bool ClipRectContains(const Float rect[4], Vector2 point)
    {
        // 넓이 0 의 사각형(어긋난 마스크의 교집합)은 아무것도 담지 않는다 - 경계 위의 점도 밖이다.
        if (rect[2] <= rect[0] || rect[3] <= rect[1])
        {
            return false;
        }
        return point.x >= rect[0] && point.x <= rect[2] && point.y >= rect[1] && point.y <= rect[3];
    }
}
