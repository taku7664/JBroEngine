#include <JBro/Editor/Gizmo/LightGizmoModel.h>

#include <cmath>

namespace JBro::LightGizmoModel
{
    namespace
    {
        // `Light2D` 의 반지름 필드의 상한이다(`Range(0, 1000)`).
        constexpr Float MaxRadius = 1000.0f;
        constexpr Float RadiansToDegrees = 57.29577951308232f;
        constexpr Float DegreesToRadians = 0.017453292519943295f;

        Vector2 Rotate(Vector2 vector, Float radians)
        {
            const Float cosine = std::cos(radians.Get());
            const Float sine = std::sin(radians.Get());
            return { vector.x * cosine - vector.y * sine, vector.x * sine + vector.y * cosine };
        }
    }

    Pose PoseFromWorld(const Matrix3x2& world)
    {
        // 행 벡터 규약이라 첫 행이 오브젝트의 +x 다(`Light2DSystem` 과 같은 셈).
        Pose pose;
        pose.center = { world.m31, world.m32 };
        const Float length = std::sqrt(world.m11 * world.m11 + world.m12 * world.m12);
        if (length > 0.00001f)
        {
            pose.axis = { world.m11 / length, world.m12 / length };
        }
        return pose;
    }

    Bool HasHandles(const Component::Light2D& light)
    {
        return light.type == Component::Light2DType::Point || light.type == Component::Light2DType::Spot;
    }

    Bool HasHandle(const Component::Light2D& light, Handle handle)
    {
        switch (handle)
        {
        case Handle::InnerRadius:
        case Handle::OuterRadius:
            return HasHandles(light);
        case Handle::InnerAngle:
        case Handle::OuterAngle:
            return light.type == Component::Light2DType::Spot;
        case Handle::None:
            break;
        }
        return false;
    }

    Vector2 RadiusDirection(const Component::Light2D& light, const Pose& pose)
    {
        if (light.type == Component::Light2DType::Spot)
        {
            return pose.axis;
        }
        return Rotate(pose.axis, -45.0f * DegreesToRadians);
    }

    Vector2 HandlePosition(const Component::Light2D& light, const Pose& pose, Handle handle)
    {
        Float radius = 0.0f;
        Vector2 direction = RadiusDirection(light, pose);
        switch (handle)
        {
        case Handle::InnerRadius:
            radius = light.innerRadius;
            break;
        case Handle::OuterRadius:
            radius = light.outerRadius;
            break;
        case Handle::InnerAngle:
            radius = light.outerRadius;
            direction = Rotate(pose.axis, -0.5f * light.innerAngle.Get() * DegreesToRadians);
            break;
        case Handle::OuterAngle:
            radius = light.outerRadius;
            direction = Rotate(pose.axis, 0.5f * light.outerAngle.Get() * DegreesToRadians);
            break;
        case Handle::None:
            return pose.center;
        }
        return { pose.center.x + direction.x * radius, pose.center.y + direction.y * radius };
    }

    Float DragRadius(Float startRadius, Vector2 direction, Vector2 grabWorld, Vector2 mouseWorld)
    {
        const Float moved = (mouseWorld.x - grabWorld.x) * direction.x + (mouseWorld.y - grabWorld.y) * direction.y;
        return Float(startRadius + moved).Clamp(0.0f, MaxRadius);
    }

    Degree AngleAt(const Pose& pose, Vector2 mouseWorld, Degree fallback)
    {
        const Float dx = mouseWorld.x - pose.center.x;
        const Float dy = mouseWorld.y - pose.center.y;
        if (dx * dx + dy * dy < 0.0000001f)
        {
            return fallback;
        }
        const Float cross = pose.axis.x * dy - pose.axis.y * dx;
        const Float dot = pose.axis.x * dx + pose.axis.y * dy;
        const Float half = std::fabs(std::atan2(cross.Get(), dot.Get())) * RadiansToDegrees;
        return Degree(Float(half * 2.0f).Clamp(0.0f, 360.0f));
    }

    const char* FieldName(Handle handle)
    {
        switch (handle)
        {
        case Handle::InnerRadius:
            return "innerRadius";
        case Handle::OuterRadius:
            return "outerRadius";
        case Handle::InnerAngle:
            return "innerAngle";
        case Handle::OuterAngle:
            return "outerAngle";
        case Handle::None:
            break;
        }
        return "";
    }

    const char* HandleId(Handle handle)
    {
        switch (handle)
        {
        case Handle::InnerRadius:
            return "##light_inner_radius";
        case Handle::OuterRadius:
            return "##light_outer_radius";
        case Handle::InnerAngle:
            return "##light_inner_angle";
        case Handle::OuterAngle:
            return "##light_outer_angle";
        case Handle::None:
            break;
        }
        return "##light_none";
    }
}
