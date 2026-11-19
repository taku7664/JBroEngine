#include <JBro/Editor/Gizmo/LightGizmo3DModel.h>

#include <cmath>

namespace JBro::LightGizmo3DModel
{
    Pose PoseFrom(const Vector3& position, const Quaternion& rotation)
    {
        Pose pose;
        pose.position = position;
        pose.direction = Normalize(Rotate(rotation, Vector3{0.0f, 0.0f, -1.0f}));
        pose.side = Normalize(Rotate(rotation, Vector3{1.0f, 0.0f, 0.0f}));
        return pose;
    }

    Bool HasHandles(const Component::Light3D& light)
    {
        return light.type == Component::Light3DType::Point || light.type == Component::Light3DType::Spot;
    }

    Bool HasHandle(const Component::Light3D& light, Handle handle)
    {
        switch (handle)
        {
        case Handle::Range:
            return HasHandles(light);
        case Handle::OuterAngle:
        case Handle::InnerAngle:
            return light.type == Component::Light3DType::Spot;
        case Handle::None:
            break;
        }
        return false;
    }

    Float ShownLength(const Component::Light3D& light)
    {
        return light.range > 0.0f ? light.range : UnboundedLength;
    }

    Float ConeRadius(Degree fullAngle, Float length)
    {
        constexpr Float DegreesToRadians = 0.017453292519943295f;
        const Float half = Float::Clamp(fullAngle.Get() * 0.5f, 0.0f, 89.5f) * DegreesToRadians;
        return std::tan(half.Get()) * length;
    }

    Vector3 HandlePosition(const Component::Light3D& light, const Pose& pose, Handle handle, const Vector3& cameraRight)
    {
        const Float length = ShownLength(light);
        if (handle == Handle::Range)
        {
            return light.type == Component::Light3DType::Point
                ? Add(pose.position, Scale(Normalize(cameraRight), length))
                : Add(pose.position, Scale(pose.direction, length));
        }
        const Vector3 end = Add(pose.position, Scale(pose.direction, length));
        if (handle == Handle::OuterAngle)
        {
            return Add(end, Scale(pose.side, ConeRadius(light.outerAngle, length)));
        }
        if (handle == Handle::InnerAngle)
        {
            return Add(end, Scale(pose.side, -ConeRadius(light.innerAngle, length)));
        }
        return pose.position;
    }

    Float DragRange(Bool alongAxis, Vector2 center, Vector2 unit, Vector2 mouse)
    {
        const Float ux = unit.x - center.x;
        const Float uy = unit.y - center.y;
        const Float unitSquared = ux * ux + uy * uy;
        if (unitSquared < 0.000001f)
        {
            return 0.0f;
        }
        const Float mx = mouse.x - center.x;
        const Float my = mouse.y - center.y;
        const Float units = alongAxis ? (mx * ux + my * uy) / unitSquared : Float(std::sqrt(((mx * mx + my * my) / unitSquared).Get()));
        return Float::Clamp(units, 0.0f, 1000.0f);
    }

    Degree DragAngle(Vector2 apex, Vector2 axisEnd, Vector2 mouse, Degree fallback)
    {
        const Float ax = axisEnd.x - apex.x;
        const Float ay = axisEnd.y - apex.y;
        const Float axisLength = std::sqrt((ax * ax + ay * ay).Get());
        if (axisLength < 0.0001f)
        {
            return fallback;
        }
        const Float dx = ax / axisLength;
        const Float dy = ay / axisLength;
        const Float mx = mouse.x - apex.x;
        const Float my = mouse.y - apex.y;
        const Float along = mx * dx + my * dy;
        const Float across = std::fabs((mx * dy - my * dx).Get());
        if (along <= 0.0001f)
        {
            return fallback;
        }
        constexpr Float RadiansToDegrees = 57.29577951308232f;
        const Float full = Float(std::atan2(across.Get(), along.Get())) * 2.0f * RadiansToDegrees;
        return Degree(Float(Float::Clamp(full, 0.0f, 179.0f)));
    }

    const char* FieldName(Handle handle)
    {
        switch (handle)
        {
        case Handle::Range:
            return "range";
        case Handle::OuterAngle:
            return "outerAngle";
        case Handle::InnerAngle:
            return "innerAngle";
        case Handle::None:
            break;
        }
        return "";
    }

    const char* HandleId(Handle handle)
    {
        switch (handle)
        {
        case Handle::Range:
            return "##light3d_range";
        case Handle::OuterAngle:
            return "##light3d_outer_angle";
        case Handle::InnerAngle:
            return "##light3d_inner_angle";
        case Handle::None:
            break;
        }
        return "##light3d_none";
    }
}
