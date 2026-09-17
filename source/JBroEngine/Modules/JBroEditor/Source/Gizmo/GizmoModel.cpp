#include <JBro/Editor/Gizmo/GizmoModel.h>

#include <cmath>

namespace JBro
{
    namespace
    {
        constexpr float Pi = 3.14159265358979f;
        constexpr float Epsilon = 1.0e-6f;

        float Length2(float x, float y)
        {
            return std::sqrt(x * x + y * y);
        }

        // 점에서 선분까지의 거리.
        float DistanceToSegment(float px, float py, float x0, float y0, float x1, float y1)
        {
            const float dx = x1 - x0;
            const float dy = y1 - y0;
            const float lengthSquared = dx * dx + dy * dy;
            float t = 0.0f;
            if (lengthSquared > Epsilon)
            {
                t = ((px - x0) * dx + (py - y0) * dy) / lengthSquared;
                t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
            }
            return Length2(px - (x0 + dx * t), py - (y0 + dy * t));
        }

        float DistanceToRing(float px, float py, const GizmoHandleShape& ring)
        {
            float best = 1.0e30f;
            for (std::uint32_t index = 0; index < GizmoHandleShape::RingPoints; ++index)
            {
                const std::uint32_t next = (index + 1) % GizmoHandleShape::RingPoints;
                const float distance = DistanceToSegment(px, py, ring.ringX[index], ring.ringY[index],
                    ring.ringX[next], ring.ringY[next]);
                best = distance < best ? distance : best;
            }
            return best;
        }

        // 축에 수직인 단위 벡터 하나. 축과 가장 덜 나란한 기본 축을 골라 외적한다.
        Vec3 Perpendicular(const Vec3& axis)
        {
            const Vec3 candidate = std::fabs(axis.x) < 0.9f ? Vec3{1.0f, 0.0f, 0.0f} : Vec3{0.0f, 1.0f, 0.0f};
            return Normalize(Cross(axis, candidate));
        }

        float WrapAngle(float radians)
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
        bool ClosestAxisParameter(const Vec3& origin, const Vec3& axis, const Vec3& rayOrigin,
            const Vec3& rayDirection, float& t)
        {
            const Vec3 w = Subtract(origin, rayOrigin);
            const float b = Dot(axis, rayDirection);
            const float denominator = 1.0f - b * b;
            if (denominator < 1.0e-4f)
            {
                return false;
            }
            t = (b * Dot(rayDirection, w) - Dot(axis, w)) / denominator;
            return std::isfinite(t);
        }

        bool RayPlane(const Vec3& rayOrigin, const Vec3& rayDirection, const Vec3& planePoint,
            const Vec3& planeNormal, Vec3& hit)
        {
            const float denominator = Dot(rayDirection, planeNormal);
            if (std::fabs(denominator) < 1.0e-5f)
            {
                return false;
            }
            const float s = Dot(Subtract(planePoint, rayOrigin), planeNormal) / denominator;
            if (false == std::isfinite(s))
            {
                return false;
            }
            hit = Add(rayOrigin, Scale(rayDirection, s));
            return true;
        }

        // 화면에서 단위 월드 길이가 몇 픽셀인지, 축 `u` 방향으로 잰다.
        bool PixelsPerUnit(const GizmoCamera& camera, const Vec3& origin, const Vec3& u, float& pixels)
        {
            float cx = 0.0f;
            float cy = 0.0f;
            float ex = 0.0f;
            float ey = 0.0f;
            if (false == GizmoModel::Project(camera, origin, cx, cy)
                || false == GizmoModel::Project(camera, Add(origin, u), ex, ey))
            {
                return false;
            }
            pixels = Length2(ex - cx, ey - cy);
            return pixels > Epsilon;
        }

        bool BuildRing(const GizmoCamera& camera, const GizmoSubject& subject, GizmoAxis axis, GizmoHandleShape& out)
        {
            const Vec3 a = GizmoModel::AxisDirection(subject, axis);
            const Vec3 u = Perpendicular(a);
            const Vec3 v = Cross(a, u);
            // 고리의 월드 반지름은 화면에서 `RingRadiusPixels` 가 되게 잡는다. 두 수직 방향 중 더 길게 보이는 쪽으로 잰다.
            float pixelsU = 0.0f;
            float pixelsV = 0.0f;
            if (false == PixelsPerUnit(camera, subject.position, u, pixelsU)
                || false == PixelsPerUnit(camera, subject.position, v, pixelsV))
            {
                return false;
            }
            const float pixels = pixelsU > pixelsV ? pixelsU : pixelsV;
            const float radius = GizmoModel::RingRadiusPixels / pixels;
            out.axis = axis;
            out.ring = true;
            for (std::uint32_t index = 0; index < GizmoHandleShape::RingPoints; ++index)
            {
                const float angle = 2.0f * Pi * static_cast<float>(index) / GizmoHandleShape::RingPoints;
                const Vec3 point = Add(subject.position,
                    Add(Scale(u, radius * std::cos(angle)), Scale(v, radius * std::sin(angle))));
                if (false == GizmoModel::Project(camera, point, out.ringX[index], out.ringY[index]))
                {
                    return false;
                }
            }
            return true;
        }

        bool BuildAxisHandle(const GizmoCamera& camera, const GizmoSubject& subject, GizmoAxis axis,
            GizmoHandleShape& out)
        {
            float cx = 0.0f;
            float cy = 0.0f;
            float ex = 0.0f;
            float ey = 0.0f;
            const Vec3 a = GizmoModel::AxisDirection(subject, axis);
            if (false == GizmoModel::Project(camera, subject.position, cx, cy)
                || false == GizmoModel::Project(camera, Add(subject.position, a), ex, ey))
            {
                return false;
            }
            const float length = Length2(ex - cx, ey - cy);
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

        bool BuildCenterHandle(const GizmoCamera& camera, const GizmoSubject& subject, GizmoHandleShape& out)
        {
            float cx = 0.0f;
            float cy = 0.0f;
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

        float ScaleComponent(const Vec3& scale, GizmoAxis axis)
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

        void SetScaleComponent(Vec3& scale, GizmoAxis axis, float value)
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

    bool GizmoModel::Invert(const Matrix4x4& matrix, Matrix4x4& out)
    {
        // 여인수 전개. 행렬이 4x4 하나뿐이라 일반식으로 충분하다.
        const float* m = matrix.values;
        float inv[16];
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
        const float determinant = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
        if (false == std::isfinite(determinant) || std::fabs(determinant) < 1.0e-12f)
        {
            return false;
        }
        const float scale = 1.0f / determinant;
        for (std::uint32_t index = 0; index < 16; ++index)
        {
            out.values[index] = inv[index] * scale;
        }
        return true;
    }

    bool GizmoModel::MakeCamera(const Matrix4x4& view, const Matrix4x4& projection,
        float left, float top, float width, float height, GizmoCamera& out)
    {
        if (false == std::isfinite(width) || false == std::isfinite(height) || width <= 0.0f || height <= 0.0f)
        {
            return false;
        }
        // 열 벡터 규약: p' = P * V * p.
        Matrix4x4 viewProjection;
        for (std::uint32_t row = 0; row < 4; ++row)
        {
            for (std::uint32_t column = 0; column < 4; ++column)
            {
                float sum = 0.0f;
                for (std::uint32_t k = 0; k < 4; ++k)
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

    bool GizmoModel::Project(const GizmoCamera& camera, const Vec3& world, float& x, float& y, float* depth)
    {
        const float* m = camera.viewProjection.values;
        const float cx = m[0] * world.x + m[1] * world.y + m[2] * world.z + m[3];
        const float cy = m[4] * world.x + m[5] * world.y + m[6] * world.z + m[7];
        const float cz = m[8] * world.x + m[9] * world.y + m[10] * world.z + m[11];
        const float cw = m[12] * world.x + m[13] * world.y + m[14] * world.z + m[15];
        if (false == std::isfinite(cw) || cw <= Epsilon)
        {
            return false;
        }
        const float ndcX = cx / cw;
        const float ndcY = cy / cw;
        // NDC 의 +y 는 위, 화면의 +y 는 아래다.
        x = camera.left + (ndcX * 0.5f + 0.5f) * camera.width;
        y = camera.top + (0.5f - ndcY * 0.5f) * camera.height;
        if (depth != nullptr)
        {
            *depth = cz / cw;
        }
        return std::isfinite(x) && std::isfinite(y);
    }

    bool GizmoModel::Unproject(const GizmoCamera& camera, float x, float y, float ndcDepth, Vec3& world)
    {
        const float ndcX = (x - camera.left) / camera.width * 2.0f - 1.0f;
        const float ndcY = 1.0f - (y - camera.top) / camera.height * 2.0f;
        const float* m = camera.inverseViewProjection.values;
        const float wx = m[0] * ndcX + m[1] * ndcY + m[2] * ndcDepth + m[3];
        const float wy = m[4] * ndcX + m[5] * ndcY + m[6] * ndcDepth + m[7];
        const float wz = m[8] * ndcX + m[9] * ndcY + m[10] * ndcDepth + m[11];
        const float ww = m[12] * ndcX + m[13] * ndcY + m[14] * ndcDepth + m[15];
        if (false == std::isfinite(ww) || std::fabs(ww) < Epsilon)
        {
            return false;
        }
        world = {wx / ww, wy / ww, wz / ww};
        return std::isfinite(world.x) && std::isfinite(world.y) && std::isfinite(world.z);
    }

    bool GizmoModel::MakeRay(const GizmoCamera& camera, float x, float y, Vec3& origin, Vec3& direction)
    {
        Vec3 near;
        Vec3 far;
        if (false == Unproject(camera, x, y, 0.0f, near) || false == Unproject(camera, x, y, 1.0f, far))
        {
            return false;
        }
        const Vec3 delta = Subtract(far, near);
        const float length = Length(delta);
        if (length < Epsilon)
        {
            return false;
        }
        origin = near;
        direction = Scale(delta, 1.0f / length);
        return true;
    }

    Vec3 GizmoModel::AxisDirection(const GizmoSubject& subject, GizmoAxis axis)
    {
        Vec3 local;
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

    std::uint32_t GizmoModel::BuildHandles(GizmoMode mode, const GizmoCamera& camera, const GizmoSubject& subject,
        GizmoHandleShape* out)
    {
        std::uint32_t count = 0;
        const GizmoAxis axes[3] = {GizmoAxis::X, GizmoAxis::Y, GizmoAxis::Z};
        const std::uint32_t axisCount = subject.planar ? 2 : 3;
        if (mode == GizmoMode::Rotate)
        {
            // 2D 는 화면과 수직인 Z 고리 하나다.
            const std::uint32_t first = subject.planar ? 2 : 0;
            for (std::uint32_t index = first; index < 3; ++index)
            {
                if (BuildRing(camera, subject, axes[index], out[count]))
                {
                    ++count;
                }
            }
            return count;
        }
        for (std::uint32_t index = 0; index < axisCount; ++index)
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
        float mouseX, float mouseY)
    {
        GizmoHandleShape handles[MaxHandles];
        const std::uint32_t count = BuildHandles(mode, camera, subject, handles);
        // 가운데가 먼저다. 축 셋이 모두 거기서 시작하므로 가운데를 축으로 읽으면 가운데를 잡을 길이 없다.
        for (std::uint32_t index = 0; index < count; ++index)
        {
            if (handles[index].axis == GizmoAxis::Free
                && Length2(mouseX - handles[index].x0, mouseY - handles[index].y0) <= CenterRadiusPixels + 2.0f)
            {
                return GizmoAxis::Free;
            }
        }
        GizmoAxis best = GizmoAxis::None;
        float bestDistance = PickDistancePixels;
        for (std::uint32_t index = 0; index < count; ++index)
        {
            const GizmoHandleShape& handle = handles[index];
            if (handle.axis == GizmoAxis::Free)
            {
                continue;
            }
            const float distance = handle.ring
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

    bool GizmoModel::BeginDrag(GizmoMode mode, GizmoAxis axis, const GizmoCamera& camera,
        const GizmoSubject& subject, float mouseX, float mouseY, GizmoDrag& drag)
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
        Vec3 rayOrigin;
        Vec3 rayDirection;
        if (false == MakeRay(camera, mouseX, mouseY, rayOrigin, rayDirection))
        {
            return false;
        }
        // 화면 평면의 법선은 가운데를 지나는 광선의 방향이다. 직교면 모든 광선이 그 방향이다.
        Vec3 centerRayOrigin;
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
            const Vec3 u = Perpendicular(drag.axisDirection);
            const Vec3 v = Cross(drag.axisDirection, u);
            constexpr float probe = 0.1f;
            const Vec3 a = Add(subject.position, u);
            const Vec3 b = Add(subject.position, Add(Scale(u, std::cos(probe)), Scale(v, std::sin(probe))));
            float ax = 0.0f;
            float ay = 0.0f;
            float bx = 0.0f;
            float by = 0.0f;
            if (false == Project(camera, a, ax, ay) || false == Project(camera, b, bx, by))
            {
                return false;
            }
            const float turned = WrapAngle(std::atan2(by - drag.centerY, bx - drag.centerX)
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

    bool GizmoModel::UpdateDrag(const GizmoDrag& drag, const GizmoCamera& camera, float mouseX, float mouseY,
        GizmoSubject& result)
    {
        result = drag.start;
        Vec3 rayOrigin;
        Vec3 rayDirection;
        if (false == MakeRay(camera, mouseX, mouseY, rayOrigin, rayDirection))
        {
            return false;
        }
        switch (drag.mode)
        {
        case GizmoMode::Translate:
            if (drag.axis == GizmoAxis::Free)
            {
                Vec3 hit;
                if (false == RayPlane(rayOrigin, rayDirection, drag.start.position, drag.planeNormal, hit))
                {
                    return false;
                }
                result.position = Add(drag.start.position, Subtract(hit, drag.startHit));
                return true;
            }
            else
            {
                float t = 0.0f;
                if (false == ClosestAxisParameter(drag.start.position, drag.axisDirection, rayOrigin, rayDirection, t))
                {
                    return false;
                }
                result.position = Add(drag.start.position, Scale(drag.axisDirection, t - drag.startParameter));
                return true;
            }
        case GizmoMode::Rotate:
        {
            const float angle = std::atan2(mouseY - drag.centerY, mouseX - drag.centerX);
            const float turned = drag.angleSign * WrapAngle(angle - drag.startAngle);
            const Quaternion delta = FromAxisAngle(drag.axisDirection, turned);
            result.rotation = Normalize(Multiply(delta, drag.start.rotation));
            return true;
        }
        case GizmoMode::Scale:
            if (drag.axis == GizmoAxis::Free)
            {
                const float distance = Length2(mouseX - drag.centerX, mouseY - drag.centerY);
                const float factor = distance / drag.startDistance;
                result.scale = Scale(drag.start.scale, factor);
                return true;
            }
            else
            {
                float t = 0.0f;
                if (false == ClosestAxisParameter(drag.start.position, drag.axisDirection, rayOrigin, rayDirection, t))
                {
                    return false;
                }
                const float factor = t / drag.startParameter;
                SetScaleComponent(result.scale, drag.axis, ScaleComponent(drag.start.scale, drag.axis) * factor);
                return true;
            }
        }
        return false;
    }
}
