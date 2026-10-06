#include <JBro/Editor/Gizmo/PolygonEditModel.h>

#include <cmath>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/UInt.h>

namespace JBro::PolygonEditModel
{
    Hit Pick(ArrayView<const Vector2> screen, Vector2 mouse, Bool closed)
    {
        Hit hit;
        const UInt32 count = static_cast<JBro::UInt32>(screen.Size());
        if (count < 2)
        {
            return hit;
        }

        Float bestVertex = VertexPickRadius * VertexPickRadius;
        for (UInt32 i = 0; i < count; ++i)
        {
            const Float dx = mouse.x - screen[i].x;
            const Float dy = mouse.y - screen[i].y;
            const Float distanceSquared = dx * dx + dy * dy;
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

        Float bestEdge = EdgePickDistance * EdgePickDistance;
        const UInt32 edges = closed ? count : count - 1;
        for (UInt32 i = 0; i < edges; ++i)
        {
            const Vector2 a = screen[i];
            const Vector2 b = screen[(i + 1) % count];
            const Float abx = b.x - a.x;
            const Float aby = b.y - a.y;
            const Float lengthSquared = abx * abx + aby * aby;
            if (lengthSquared <= 0.0f)
            {
                continue;
            }
            Float t = ((mouse.x - a.x) * abx + (mouse.y - a.y) * aby) / lengthSquared;
            t = t < 0.0f ? Float(0.0f) : (t > 1.0f ? Float(1.0f) : t);
            const Vector2 foot = { a.x + abx * t, a.y + aby * t };
            const Float dx = mouse.x - foot.x;
            const Float dy = mouse.y - foot.y;
            const Float distanceSquared = dx * dx + dy * dy;
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

    void SeedPoints(const Component::Collider2D& collider, Array<Vector2>& out)
    {
        out.Clear();
        if (false == collider.points.IsEmpty())
        {
            out.Append(collider.points.Data(), collider.points.Size());
            return;
        }
        const Float halfWidth = collider.size.x * 0.5f;
        const Float halfHeight = collider.size.y * 0.5f;
        if (collider.shape == Component::ColliderShape2D::Chain)
        {
            out.Add({ -halfWidth, 0.0f });
            out.Add({ halfWidth, 0.0f });
            return;
        }
        out.Add({ -halfWidth, -halfHeight });
        out.Add({ halfWidth, -halfHeight });
        out.Add({ halfWidth, halfHeight });
        out.Add({ -halfWidth, halfHeight });
    }

    Bool InsertOnEdge(Array<Vector2>& points, UInt32 edge, Vector2 point)
    {
        if (edge >= points.Size())
        {
            return false;
        }
        points.Insert(edge + 1, point);
        return true;
    }

    Bool RemoveVertex(Array<Vector2>& points, UInt32 index, UInt32 minimum)
    {
        if (index >= points.Size() || points.Size() <= minimum)
        {
            return false;
        }
        points.RemoveAt(index);
        return true;
    }

    Bool EditsPoints(const Component::Collider2D& collider)
    {
        return collider.shape == Component::ColliderShape2D::Polygon
            || collider.shape == Component::ColliderShape2D::Chain;
    }

    Bool IsClosedOutline(const Component::Collider2D& collider)
    {
        return collider.shape != Component::ColliderShape2D::Chain || collider.loop;
    }

    UInt32 MinPointCount(const Component::Collider2D& collider)
    {
        return IsClosedOutline(collider) ? MinVertexCount : UInt32(2u);
    }
}
