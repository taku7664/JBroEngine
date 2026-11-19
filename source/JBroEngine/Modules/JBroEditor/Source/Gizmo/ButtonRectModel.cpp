#include <JBro/Editor/Gizmo/ButtonRectModel.h>

#include <algorithm>
#include <cmath>

namespace JBro::ButtonRectModel
{
    namespace
    {
        struct Bounds
        {
            Float minX;
            Float minY;
            Float maxX;
            Float maxY;
        };

        Bounds BoundsOf(Vector2 offset, Vector2 size)
        {
            const Float halfWidth = std::fabs(size.x) * 0.5f;
            const Float halfHeight = std::fabs(size.y) * 0.5f;
            return { offset.x - halfWidth, offset.y - halfHeight, offset.x + halfWidth, offset.y + halfHeight };
        }
    }

    void Corners(Vector2 offset, Vector2 size, Vector2 corners[4])
    {
        const Bounds bounds = BoundsOf(offset, size);
        corners[0] = { bounds.minX, bounds.minY };
        corners[1] = { bounds.maxX, bounds.minY };
        corners[2] = { bounds.maxX, bounds.maxY };
        corners[3] = { bounds.minX, bounds.maxY };
    }

    Vector2 HandleLocal(Vector2 offset, Vector2 size, Handle handle)
    {
        const Bounds bounds = BoundsOf(offset, size);
        switch (handle)
        {
        case Handle::Left:
            return { bounds.minX, offset.y };
        case Handle::Right:
            return { bounds.maxX, offset.y };
        case Handle::Bottom:
            return { offset.x, bounds.minY };
        case Handle::Top:
            return { offset.x, bounds.maxY };
        case Handle::Center:
        case Handle::None:
        default:
            return offset;
        }
    }

    void Drag(Handle handle, Vector2 startOffset, Vector2 startSize, Vector2 grabLocal, Vector2 mouseLocal, Vector2& offset, Vector2& size)
    {
        const Float dx = mouseLocal.x - grabLocal.x;
        const Float dy = mouseLocal.y - grabLocal.y;
        Bounds bounds = BoundsOf(startOffset, startSize);
        switch (handle)
        {
        case Handle::Left:
            bounds.minX = std::min(bounds.minX + dx, bounds.maxX);
            break;
        case Handle::Right:
            bounds.maxX = std::max(bounds.maxX + dx, bounds.minX);
            break;
        case Handle::Bottom:
            bounds.minY = std::min(bounds.minY + dy, bounds.maxY);
            break;
        case Handle::Top:
            bounds.maxY = std::max(bounds.maxY + dy, bounds.minY);
            break;
        case Handle::Center:
            bounds.minX += dx;
            bounds.maxX += dx;
            bounds.minY += dy;
            bounds.maxY += dy;
            break;
        case Handle::None:
        default:
            break;
        }
        offset = { (bounds.minX + bounds.maxX) * 0.5f, (bounds.minY + bounds.maxY) * 0.5f };
        size = { bounds.maxX - bounds.minX, bounds.maxY - bounds.minY };
    }

    Bool FitToSprite(Vector2 spriteSize, Vector2 pivot, Vector2& size, Vector2& offset)
    {
        if (false == std::isfinite(spriteSize.x.Get()) || false == std::isfinite(spriteSize.y.Get())
            || std::fabs(spriteSize.x) < 0.001f || std::fabs(spriteSize.y) < 0.001f)
        {
            return false;
        }
        // 그림은 피벗을 원점에 두고 그려진다. 사각형은 가운데가 원점이라, 피벗이 가운데에서 벗어난 만큼 반대로 옮긴다.
        offset = { (0.5f - pivot.x) * spriteSize.x, (0.5f - pivot.y) * spriteSize.y };
        size = { std::fabs(spriteSize.x), std::fabs(spriteSize.y) };
        return true;
    }

    const char* HandleId(Handle handle)
    {
        switch (handle)
        {
        case Handle::Left:
            return "##button_left";
        case Handle::Right:
            return "##button_right";
        case Handle::Bottom:
            return "##button_bottom";
        case Handle::Top:
            return "##button_top";
        case Handle::Center:
            return "##button_center";
        case Handle::None:
        default:
            break;
        }
        return "##button_none";
    }
}
