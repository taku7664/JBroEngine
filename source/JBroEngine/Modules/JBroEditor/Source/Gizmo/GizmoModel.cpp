#include <JBro/Editor/Gizmo/GizmoModel.h>

#include <algorithm>
#include <cmath>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/Int.h>
#include <JBro/Types/UInt.h>

namespace JBro
{
    namespace
    {
        constexpr Float Epsilon = 1.0e-6f;

        Float Length2(Float x, Float y)
        {
            return std::sqrt(x * x + y * y);
        }

        // 점에서 선분까지의 거리.
        Float DistanceToSegment(Float px, Float py, Float x0, Float y0, Float x1, Float y1)
        {
            const Float dx = x1 - x0;
            const Float dy = y1 - y0;
            const Float lengthSquared = dx * dx + dy * dy;
            Float t = 0.0f;
            if (lengthSquared > Epsilon)
            {
                t = ((px - x0) * dx + (py - y0) * dy) / lengthSquared;
                t = t < 0.0f ? Float(0.0f) : (t > 1.0f ? Float(1.0f) : t);
            }
            return Length2(px - (x0 + dx * t), py - (y0 + dy * t));
        }

        Float DistanceToRing(Float px, Float py, const GizmoHandleShape& ring)
        {
            Float best = 1.0e30f;
            for (UInt32 index = 0; index < GizmoHandleShape::RingPoints; ++index)
            {
                const UInt32 next = (index + 1) % GizmoHandleShape::RingPoints;
                const Float distance = DistanceToSegment(px, py, ring.ringX[index], ring.ringY[index],
                    ring.ringX[next], ring.ringY[next]);
                best = distance < best ? distance : best;
            }
            return best;
        }

        // 축에 수직인 단위 벡터 하나. 축과 가장 덜 나란한 기본 축을 골라 외적한다.
        Vector3 Perpendicular(const Vector3& axis)
        {
            const Vector3 candidate = std::fabs(axis.x) < 0.9f ? Vector3{1.0f, 0.0f, 0.0f} : Vector3{0.0f, 1.0f, 0.0f};
            return Normalize(Cross(axis, candidate));
        }

        Float WrapAngle(Float radians)
        {
            if (false == std::isfinite(radians))
            {
                return 0.0f;
            }
            while (radians > Pi)
            {
                radians -= 2.0f * Pi;
            }
            while (radians < -Pi)
            {
                radians += 2.0f * Pi;
            }
            return radians;
        }

        // 축 직선(o + a t)에서 광선(r + d s)에 가장 가까운 점의 t. 둘이 나란하면 거짓이다.
        Bool ClosestAxisParameter(const Vector3& origin, const Vector3& axis, const Vector3& rayOrigin,
            const Vector3& rayDirection, Float& t)
        {
            const Vector3 w = Subtract(origin, rayOrigin);
            const Float b = Dot(axis, rayDirection);
            const Float denominator = 1.0f - b * b;
            if (denominator < 1.0e-4f)
            {
                return false;
            }
            t = (b * Dot(rayDirection, w) - Dot(axis, w)) / denominator;
            return std::isfinite(t);
        }

        Bool RayPlane(const Vector3& rayOrigin, const Vector3& rayDirection, const Vector3& planePoint,
            const Vector3& planeNormal, Vector3& hit)
        {
            const Float denominator = Dot(rayDirection, planeNormal);
            if (std::fabs(denominator) < 1.0e-5f)
            {
                return false;
            }
            const Float s = Dot(Subtract(planePoint, rayOrigin), planeNormal) / denominator;
            if (false == std::isfinite(s))
            {
                return false;
            }
            hit = Add(rayOrigin, Scale(rayDirection, s));
            return true;
        }

        // 화면에서 단위 월드 길이가 몇 픽셀인지, 축 `u` 방향으로 잰다.
        Bool PixelsPerUnit(const GizmoCamera& camera, const Vector3& origin, const Vector3& u, Float& pixels)
        {
            Float cx = 0.0f;
            Float cy = 0.0f;
            Float ex = 0.0f;
            Float ey = 0.0f;
            if (false == GizmoModel::Project(camera, origin, cx, cy)
                || false == GizmoModel::Project(camera, Add(origin, u), ex, ey))
            {
                return false;
            }
            pixels = Length2(ex - cx, ey - cy);
            return pixels > Epsilon;
        }

        Bool BuildRing(const GizmoCamera& camera, const GizmoSubject& subject, GizmoAxis axis, GizmoHandleShape& out)
        {
            const Vector3 a = GizmoModel::AxisDirection(subject, axis);
            const Vector3 u = Perpendicular(a);
            const Vector3 v = Cross(a, u);
            // 고리의 월드 반지름은 화면에서 `RingRadiusPixels` 가 되게 잡는다. 두 수직 방향 중 더 길게 보이는 쪽으로 잰다.
            Float pixelsU = 0.0f;
            Float pixelsV = 0.0f;
            if (false == PixelsPerUnit(camera, subject.position, u, pixelsU)
                || false == PixelsPerUnit(camera, subject.position, v, pixelsV))
            {
                return false;
            }
            const Float pixels = pixelsU > pixelsV ? pixelsU : pixelsV;
            const Float radius = GizmoModel::RingRadiusPixels / pixels;
            out.axis = axis;
            out.ring = true;
            for (UInt32 index = 0; index < GizmoHandleShape::RingPoints; ++index)
            {
                const Float angle = 2.0f * Pi * static_cast<JBro::Float>(index) / GizmoHandleShape::RingPoints;
                const Vector3 point = Add(subject.position,
                    Add(Scale(u, radius * std::cos(angle)), Scale(v, radius * std::sin(angle))));
                if (false == GizmoModel::Project(camera, point, out.ringX[index], out.ringY[index]))
                {
                    return false;
                }
            }
            return true;
        }

        Bool BuildAxisHandle(const GizmoCamera& camera, const GizmoSubject& subject, GizmoAxis axis,
            GizmoHandleShape& out)
        {
            Float cx = 0.0f;
            Float cy = 0.0f;
            Float ex = 0.0f;
            Float ey = 0.0f;
            const Vector3 a = GizmoModel::AxisDirection(subject, axis);
            if (false == GizmoModel::Project(camera, subject.position, cx, cy)
                || false == GizmoModel::Project(camera, Add(subject.position, a), ex, ey))
            {
                return false;
            }
            const Float length = Length2(ex - cx, ey - cy);
            if (length < 1.0e-3f)
            {
                // 축이 카메라를 정면으로 가리킨다. 그릴 방향이 없다.
                return false;
            }
            out.axis = axis;
            out.ring = false;
            out.x0 = cx;
            out.y0 = cy;
            out.x1 = cx + (ex - cx) / length * GizmoModel::AxisLengthPixels;
            out.y1 = cy + (ey - cy) / length * GizmoModel::AxisLengthPixels;
            return true;
        }

        Bool BuildCenterHandle(const GizmoCamera& camera, const GizmoSubject& subject, GizmoHandleShape& out)
        {
            Float cx = 0.0f;
            Float cy = 0.0f;
            if (false == GizmoModel::Project(camera, subject.position, cx, cy))
            {
                return false;
            }
            out.axis = GizmoAxis::Free;
            out.ring = false;
            out.x0 = cx;
            out.y0 = cy;
            out.x1 = cx;
            out.y1 = cy;
            return true;
        }

        Float ScaleComponent(const Vector3& scale, GizmoAxis axis)
        {
            switch (axis)
            {
            case GizmoAxis::X:
                return scale.x;
            case GizmoAxis::Y:
                return scale.y;
            case GizmoAxis::Z:
                return scale.z;
            default:
                return 1.0f;
            }
        }

        void SetScaleComponent(Vector3& scale, GizmoAxis axis, Float value)
        {
            switch (axis)
            {
            case GizmoAxis::X:
                scale.x = value;
                break;
            case GizmoAxis::Y:
                scale.y = value;
                break;
            case GizmoAxis::Z:
                scale.z = value;
                break;
            default:
                break;
            }
        }
    }

    Bool GizmoModel::Invert(const Matrix4x4& matrix, Matrix4x4& out)
    {
        // 여인수 전개. 행렬이 4x4 하나뿐이라 일반식으로 충분하다.
        const Float* m = matrix.values;
        Float inv[16];
        inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14]
            + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
        inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14]
            - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
        inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13]
            + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
        inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13]
            - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
        inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14]
            - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
        inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14]
            + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
        inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13]
            - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
        inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13]
            + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
        inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14]
            + m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
        inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14]
            - m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
        inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13]
            + m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
        inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13]
            - m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
        inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10]
            - m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
        inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10]
            + m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
        inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9]
            - m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
        inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9]
            + m[8] * m[1] * m[6] - m[8] * m[2] * m[5];
        const Float determinant = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
        if (false == std::isfinite(determinant) || std::fabs(determinant) < 1.0e-12f)
        {
            return false;
        }
        const Float scale = 1.0f / determinant;
        for (UInt32 index = 0; index < 16; ++index)
        {
            out.values[index] = inv[index] * scale;
        }
        return true;
    }

    Bool GizmoModel::MakeCamera(const Matrix4x4& view, const Matrix4x4& projection,
        Float left, Float top, Float width, Float height, GizmoCamera& out)
    {
        if (false == std::isfinite(width) || false == std::isfinite(height) || width <= 0.0f || height <= 0.0f)
        {
            return false;
        }
        // 열 벡터 규약: p' = P * V * p.
        Matrix4x4 viewProjection;
        for (UInt32 row = 0; row < 4; ++row)
        {
            for (UInt32 column = 0; column < 4; ++column)
            {
                Float sum = 0.0f;
                for (UInt32 k = 0; k < 4; ++k)
                {
                    sum += projection.values[row * 4 + k] * view.values[k * 4 + column];
                }
                viewProjection.values[row * 4 + column] = sum;
            }
        }
        if (false == Invert(viewProjection, out.inverseViewProjection))
        {
            return false;
        }
        out.viewProjection = viewProjection;
        out.left = left;
        out.top = top;
        out.width = width;
        out.height = height;
        return true;
    }

    Bool GizmoModel::Project(const GizmoCamera& camera, const Vector3& world, Float& x, Float& y, Float* depth)
    {
        const Float* m = camera.viewProjection.values;
        const Float cx = m[0] * world.x + m[1] * world.y + m[2] * world.z + m[3];
        const Float cy = m[4] * world.x + m[5] * world.y + m[6] * world.z + m[7];
        const Float cz = m[8] * world.x + m[9] * world.y + m[10] * world.z + m[11];
        const Float cw = m[12] * world.x + m[13] * world.y + m[14] * world.z + m[15];
        if (false == std::isfinite(cw) || cw <= Epsilon)
        {
            return false;
        }
        const Float ndcX = cx / cw;
        const Float ndcY = cy / cw;
        // NDC 의 +y 는 위, 화면의 +y 는 아래다.
        x = camera.left + (ndcX * 0.5f + 0.5f) * camera.width;
        y = camera.top + (0.5f - ndcY * 0.5f) * camera.height;
        if (depth != nullptr)
        {
            *depth = cz / cw;
        }
        return std::isfinite(x) && std::isfinite(y);
    }

    Bool GizmoModel::ProjectPlaneRect(const GizmoCamera& camera, const Vector3& origin, const Vector3& axisX, const Vector3& axisY,
        Float minX, Float minY, Float maxX, Float maxY, Float& screenMinX, Float& screenMinY, Float& screenMaxX,
        Float& screenMaxY)
    {
        const Float cornersX[4] = {minX, maxX, maxX, minX};
        const Float cornersY[4] = {minY, minY, maxY, maxY};
        for (Int32 corner = 0; corner < 4; ++corner)
        {
            const Vector3 world = Add(origin, Add(Scale(axisX, cornersX[corner]), Scale(axisY, cornersY[corner])));
            Float x = 0.0f;
            Float y = 0.0f;
            if (false == Project(camera, world, x, y))
            {
                return false;
            }
            screenMinX = corner == 0 ? x : std::min(screenMinX, x);
            screenMinY = corner == 0 ? y : std::min(screenMinY, y);
            screenMaxX = corner == 0 ? x : std::max(screenMaxX, x);
            screenMaxY = corner == 0 ? y : std::max(screenMaxY, y);
        }
        return true;
    }

    Bool GizmoModel::Unproject(const GizmoCamera& camera, Float x, Float y, Float ndcDepth, Vector3& world)
    {
        const Float ndcX = (x - camera.left) / camera.width * 2.0f - 1.0f;
        const Float ndcY = 1.0f - (y - camera.top) / camera.height * 2.0f;
        const Float* m = camera.inverseViewProjection.values;
        const Float wx = m[0] * ndcX + m[1] * ndcY + m[2] * ndcDepth + m[3];
        const Float wy = m[4] * ndcX + m[5] * ndcY + m[6] * ndcDepth + m[7];
        const Float wz = m[8] * ndcX + m[9] * ndcY + m[10] * ndcDepth + m[11];
        const Float ww = m[12] * ndcX + m[13] * ndcY + m[14] * ndcDepth + m[15];
        if (false == std::isfinite(ww) || std::fabs(ww) < Epsilon)
        {
            return false;
        }
        world = {wx / ww, wy / ww, wz / ww};
        return std::isfinite(world.x) && std::isfinite(world.y) && std::isfinite(world.z);
    }

    Bool GizmoModel::MakeRay(const GizmoCamera& camera, Float x, Float y, Vector3& origin, Vector3& direction)
    {
        Vector3 near;
        Vector3 far;
        if (false == Unproject(camera, x, y, 0.0f, near) || false == Unproject(camera, x, y, 1.0f, far))
        {
            return false;
        }
        const Vector3 delta = Subtract(far, near);
        const Float length = Length(delta);
        if (length < Epsilon)
        {
            return false;
        }
        origin = near;
        direction = Scale(delta, 1.0f / length);
        return true;
    }

    Vector3 GizmoModel::AxisDirection(const GizmoSubject& subject, GizmoAxis axis)
    {
        Vector3 local;
        switch (axis)
        {
        case GizmoAxis::X:
            local = {1.0f, 0.0f, 0.0f};
            break;
        case GizmoAxis::Y:
            local = {0.0f, 1.0f, 0.0f};
            break;
        case GizmoAxis::Z:
            local = {0.0f, 0.0f, 1.0f};
            break;
        default:
            return {};
        }
        return Normalize(Rotate(Normalize(subject.rotation), local));
    }

    UInt32 GizmoModel::BuildHandles(GizmoMode mode, const GizmoCamera& camera, const GizmoSubject& subject,
        GizmoHandleShape* out)
    {
        UInt32 count = 0;
        const GizmoAxis axes[3] = {GizmoAxis::X, GizmoAxis::Y, GizmoAxis::Z};
        const UInt32 axisCount = subject.planar ? 2 : 3;
        if (mode == GizmoMode::Rotate)
        {
            // 2D 는 화면과 수직인 Z 고리 하나다.
            const UInt32 first = subject.planar ? 2 : 0;
            for (UInt32 index = first; index < 3; ++index)
            {
                if (BuildRing(camera, subject, axes[index], out[count]))
                {
                    ++count;
                }
            }
            return count;
        }
        for (UInt32 index = 0; index < axisCount; ++index)
        {
            if (BuildAxisHandle(camera, subject, axes[index], out[count]))
            {
                ++count;
            }
        }
        if (BuildCenterHandle(camera, subject, out[count]))
        {
            ++count;
        }
        return count;
    }

    GizmoAxis GizmoModel::Pick(GizmoMode mode, const GizmoCamera& camera, const GizmoSubject& subject,
        Float mouseX, Float mouseY)
    {
        GizmoHandleShape handles[MaxHandles];
        const UInt32 count = BuildHandles(mode, camera, subject, handles);
        // 가운데가 먼저다. 축 셋이 모두 거기서 시작하므로 가운데를 축으로 읽으면 가운데를 잡을 길이 없다.
        for (UInt32 index = 0; index < count; ++index)
        {
            if (handles[index].axis == GizmoAxis::Free
                && Length2(mouseX - handles[index].x0, mouseY - handles[index].y0) <= CenterRadiusPixels + 2.0f)
            {
                return GizmoAxis::Free;
            }
        }
        GizmoAxis best = GizmoAxis::None;
        Float bestDistance = PickDistancePixels;
        for (UInt32 index = 0; index < count; ++index)
        {
            const GizmoHandleShape& handle = handles[index];
            if (handle.axis == GizmoAxis::Free)
            {
                continue;
            }
            const Float distance = handle.ring
                ? DistanceToRing(mouseX, mouseY, handle)
                : DistanceToSegment(mouseX, mouseY, handle.x0, handle.y0, handle.x1, handle.y1);
            if (distance <= bestDistance)
            {
                bestDistance = distance;
                best = handle.axis;
            }
        }
        return best;
    }

    Bool GizmoModel::BeginDrag(GizmoMode mode, GizmoAxis axis, const GizmoCamera& camera,
        const GizmoSubject& subject, Float mouseX, Float mouseY, GizmoDrag& drag)
    {
        drag = {};
        if (axis == GizmoAxis::None || (axis == GizmoAxis::Free && mode == GizmoMode::Rotate)
            || (axis == GizmoAxis::Z && subject.planar && mode != GizmoMode::Rotate))
        {
            return false;
        }
        drag.mode = mode;
        drag.axis = axis;
        drag.start = subject;
        if (false == Project(camera, subject.position, drag.centerX, drag.centerY))
        {
            return false;
        }
        Vector3 rayOrigin;
        Vector3 rayDirection;
        if (false == MakeRay(camera, mouseX, mouseY, rayOrigin, rayDirection))
        {
            return false;
        }
        // 화면 평면의 법선은 가운데를 지나는 광선의 방향이다. 직교면 모든 광선이 그 방향이다.
        Vector3 centerRayOrigin;
        if (false == MakeRay(camera, drag.centerX, drag.centerY, centerRayOrigin, drag.planeNormal))
        {
            return false;
        }
        switch (mode)
        {
        case GizmoMode::Translate:
        case GizmoMode::Scale:
            if (axis == GizmoAxis::Free)
            {
                if (mode == GizmoMode::Translate)
                {
                    return RayPlane(rayOrigin, rayDirection, subject.position, drag.planeNormal, drag.startHit);
                }
                drag.startDistance = Length2(mouseX - drag.centerX, mouseY - drag.centerY);
                return drag.startDistance > 1.0f;
            }
            drag.axisDirection = AxisDirection(subject, axis);
            if (false == ClosestAxisParameter(subject.position, drag.axisDirection, rayOrigin, rayDirection,
                    drag.startParameter))
            {
                return false;
            }
            // 크기는 비율이라 시작점이 가운데면 나눌 수 없다.
            return mode == GizmoMode::Translate || std::fabs(drag.startParameter) > 1.0e-3f;
        case GizmoMode::Rotate:
        {
            drag.axisDirection = AxisDirection(subject, axis);
            drag.startAngle = std::atan2(mouseY - drag.centerY, mouseX - drag.centerX);
            // 축을 도는 양의 회전이 화면에서 어느 쪽으로 보이는지 작은 회전 하나로 잰다. 카메라가 축의 어느
            // 쪽에 있든 부호가 맞는다.
            const Vector3 u = Perpendicular(drag.axisDirection);
            const Vector3 v = Cross(drag.axisDirection, u);
            constexpr Float probe = 0.1f;
            const Vector3 a = Add(subject.position, u);
            const Vector3 b = Add(subject.position, Add(Scale(u, std::cos(probe)), Scale(v, std::sin(probe))));
            Float ax = 0.0f;
            Float ay = 0.0f;
            Float bx = 0.0f;
            Float by = 0.0f;
            if (false == Project(camera, a, ax, ay) || false == Project(camera, b, bx, by))
            {
                return false;
            }
            const Float turned = WrapAngle(std::atan2(by - drag.centerY, bx - drag.centerX)
                - std::atan2(ay - drag.centerY, ax - drag.centerX));
            if (std::fabs(turned) < 1.0e-4f)
            {
                // 축이 화면과 나란하다. 고리가 선으로 보이고 돌릴 방향이 없다.
                return false;
            }
            drag.angleSign = turned > 0.0f ? 1.0f : -1.0f;
            return true;
        }
        }
        return false;
    }

    Bool GizmoModel::UpdateDrag(const GizmoDrag& drag, const GizmoCamera& camera, Float mouseX, Float mouseY,
        GizmoSubject& result)
    {
        result = drag.start;
        Vector3 rayOrigin;
        Vector3 rayDirection;
        if (false == MakeRay(camera, mouseX, mouseY, rayOrigin, rayDirection))
        {
            return false;
        }
        switch (drag.mode)
        {
        case GizmoMode::Translate:
            if (drag.axis == GizmoAxis::Free)
            {
                Vector3 hit;
                if (false == RayPlane(rayOrigin, rayDirection, drag.start.position, drag.planeNormal, hit))
                {
                    return false;
                }
                result.position = Add(drag.start.position, Subtract(hit, drag.startHit));
                return true;
            }
            else
            {
                Float t = 0.0f;
                if (false == ClosestAxisParameter(drag.start.position, drag.axisDirection, rayOrigin, rayDirection, t))
                {
                    return false;
                }
                result.position = Add(drag.start.position, Scale(drag.axisDirection, t - drag.startParameter));
                return true;
            }
        case GizmoMode::Rotate:
        {
            const Float angle = std::atan2(mouseY - drag.centerY, mouseX - drag.centerX);
            const Float turned = drag.angleSign * WrapAngle(angle - drag.startAngle);
            const Quaternion delta = FromAxisAngle(drag.axisDirection, Radian(turned));
            result.rotation = Normalize(Multiply(delta, drag.start.rotation));
            return true;
        }
        case GizmoMode::Scale:
            if (drag.axis == GizmoAxis::Free)
            {
                const Float distance = Length2(mouseX - drag.centerX, mouseY - drag.centerY);
                const Float factor = distance / drag.startDistance;
                result.scale = Scale(drag.start.scale, factor);
                return true;
            }
            else
            {
                Float t = 0.0f;
                if (false == ClosestAxisParameter(drag.start.position, drag.axisDirection, rayOrigin, rayDirection, t))
                {
                    return false;
                }
                const Float factor = t / drag.startParameter;
                SetScaleComponent(result.scale, drag.axis, ScaleComponent(drag.start.scale, drag.axis) * factor);
                return true;
            }
        }
        return false;
    }

    void GizmoModel::SnapTranslation(const GizmoDrag& drag, Float step, GizmoSubject& result)
    {
        if (drag.mode != GizmoMode::Translate || false == std::isfinite(step) || false == (step > 0.0f))
        {
            return;
        }
        // 번호에 간격을 곱한다 - 격자의 선과 같은 셈이라(D-162) 0 은 정확히 0 이고 숫자가 붙은 선에 정확히 선다.
        const auto snap = [step](Float value) {
            return std::round(value / step) * step;
        };
        if (drag.axis == GizmoAxis::Free)
        {
            result.position.x = snap(result.position.x);
            result.position.y = snap(result.position.y);
            if (false == drag.start.planar)
            {
                result.position.z = snap(result.position.z);
            }
            return;
        }
        // 축이 월드 축과 나란한가. 0.9999 는 0.8 도쯤이다 - 그보다 돌아간 축에서 한 성분만 붙이면 손잡이 축을 벗어난다.
        constexpr Float Aligned = 0.9999f;
        const Vector3& axis = drag.axisDirection;
        if (std::fabs(axis.x) >= Aligned)
        {
            result.position.x = snap(result.position.x);
            return;
        }
        if (std::fabs(axis.y) >= Aligned)
        {
            result.position.y = snap(result.position.y);
            return;
        }
        if (std::fabs(axis.z) >= Aligned)
        {
            result.position.z = snap(result.position.z);
            return;
        }
        const Float travel = Dot(Subtract(result.position, drag.start.position), axis);
        result.position = Add(drag.start.position, Scale(axis, snap(travel)));
    }

    void GizmoModel::SnapRotation(const GizmoDrag& drag, Float stepRadians, GizmoSubject& result)
    {
        if (drag.mode != GizmoMode::Rotate || false == std::isfinite(stepRadians) || false == (stepRadians > 0.0f))
        {
            return;
        }
        // `UpdateDrag` 는 시작 회전에 축 둘레의 회전 하나를 앞에 곱했다. 그 하나를 되찾는다 - (축 x sin(t/2), cos(t/2)) 이고,
        // 끌기는 한 번에 반 바퀴까지라(`WrapAngle`) cos(t/2) 가 음이 아니어서 atan2 가 t 를 그대로 돌려준다.
        const Quaternion delta = Multiply(result.rotation, Conjugate(drag.start.rotation));
        const Vector3 axis = drag.axisDirection;
        const Float sine = delta.x * axis.x + delta.y * axis.y + delta.z * axis.z;
        const Float turned = 2.0f * std::atan2(sine, delta.w);
        const Float snapped = std::round(turned / stepRadians) * stepRadians;
        result.rotation = Normalize(Multiply(FromAxisAngle(axis, Radian(snapped)), drag.start.rotation));
    }
}
