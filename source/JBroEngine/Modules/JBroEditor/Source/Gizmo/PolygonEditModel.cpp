#include <JBro/Editor/Gizmo/PolygonEditModel.h>

#include <cmath>

namespace JBro::PolygonEditModel
{
    Hit Pick(ArrayView<const Vec2> screen, Vec2 mouse)
    {
        Hit hit;
        const std::uint32_t count = static_cast<std::uint32_t>(screen.Size());
        if (count < 2)
        {
            return hit;
        }

        float bestVertex = VertexPickRadius * VertexPickRadius;
        for (std::uint32_t i = 0; i < count; ++i)
        {
            const float dx = mouse.x - screen[i].x;
            const float dy = mouse.y - screen[i].y;
            const float distanceSquared = dx * dx + dy * dy;
            if (distanceSquared <= bestVertex)
            {
                bestVertex = distanceSquared;
                hit.kind = HitKind::Vertex;
                hit.index = i;
            }
        }
        if (hit.kind == HitKind::Vertex)
        {
            return hit;
        }

        float bestEdge = EdgePickDistance * EdgePickDistance;
        for (std::uint32_t i = 0; i < count; ++i)
        {
            const Vec2 a = screen[i];
            const Vec2 b = screen[(i + 1) % count];
            const float abx = b.x - a.x;
            const float aby = b.y - a.y;
            const float lengthSquared = abx * abx + aby * aby;
            if (lengthSquared <= 0.0f)
            {
                continue;
            }
            float t = ((mouse.x - a.x) * abx + (mouse.y - a.y) * aby) / lengthSquared;
            t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
            const Vec2 foot = { a.x + abx * t, a.y + aby * t };
            const float dx = mouse.x - foot.x;
            const float dy = mouse.y - foot.y;
            const float distanceSquared = dx * dx + dy * dy;
            if (distanceSquared <= bestEdge)
            {
                bestEdge = distanceSquared;
                hit.kind = HitKind::Edge;
                hit.index = i;
                hit.point = foot;
            }
        }
        return hit;
    }

    void SeedPoints(const Component::Collider2D& collider, Array<Vec2>& out)
    {
        out.Clear();
        if (false == collider.points.IsEmpty())
        {
            out.Append(collider.points.Data(), collider.points.Size());
            return;
        }
        const float halfWidth = collider.size.x * 0.5f;
        const float halfHeight = collider.size.y * 0.5f;
        out.Add({ -halfWidth, -halfHeight });
        out.Add({ halfWidth, -halfHeight });
        out.Add({ halfWidth, halfHeight });
        out.Add({ -halfWidth, halfHeight });
    }

    bool InsertOnEdge(Array<Vec2>& points, std::uint32_t edge, Vec2 point)
    {
        if (edge >= points.Size())
        {
            return false;
        }
        points.Insert(edge + 1, point);
        return true;
    }

    bool RemoveVertex(Array<Vec2>& points, std::uint32_t index)
    {
        if (index >= points.Size() || points.Size() <= MinVertexCount)
        {
            return false;
        }
        points.RemoveAt(index);
        return true;
    }
}
