#include <JBro/Editor/Gizmo/LightGizmoModel.h>

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Int.h>

// 캔버스 뷰 라이트 기즈모의 모델 테스트(D-291 5 단계). 화면 없이 월드 숫자로 잰다.
namespace
{
    using JBro::Vector2;
    using JBro::Component::Light2D;
    using JBro::Component::Light2DType;
    using JBro::LightGizmoModel::Handle;

    void Check(JBro::Bool condition, const char* message)
    {
        if (false == condition)
        {
            std::cout << "test failure: " << message << '\n';
            throw std::runtime_error(message);
        }
    }

    JBro::Bool Near(JBro::Float got, JBro::Float want)
    {
        return std::fabs(got - want) < 0.001f;
    }

    JBro::Bool Near(Vector2 got, Vector2 want)
    {
        return Near(got.x, want.x) && Near(got.y, want.y);
    }

    // (2, 1) 에서 90 도 돌고 x 로 두 배 늘인 오브젝트다. 축은 +y 이고 늘인 크기는 축에 들지 않는다.
    JBro::Matrix3x2 Turned()
    {
        return JBro::MakeTransformMatrix2D({ 2.0f, 1.0f }, JBro::Radian(1.5707964f), { 2.0f, 1.0f });
    }

    void TestThePoseFollowsTheObject()
    {
        const JBro::LightGizmoModel::Pose pose = JBro::LightGizmoModel::PoseFromWorld(Turned());
        Check(Near(pose.center, { 2.0f, 1.0f }), "the light sits where the object is");
        Check(Near(pose.axis, { 0.0f, 1.0f }), "its axis is the object's +x, turned, without the scale");
        // 거울(크기 -1)은 축을 뒤집는다 - 렌더러가 받는 방향과 같다.
        const JBro::LightGizmoModel::Pose mirrored =
            JBro::LightGizmoModel::PoseFromWorld(JBro::MakeTransformMatrix2D({ 0.0f, 0.0f }, JBro::Radian(0.0f), { -1.0f, 1.0f }));
        Check(Near(mirrored.axis, { -1.0f, 0.0f }), "a mirrored object's light points the other way");
        const JBro::LightGizmoModel::Pose flat =
            JBro::LightGizmoModel::PoseFromWorld(JBro::MakeTransformMatrix2D({ 0.0f, 0.0f }, JBro::Radian(0.0f), { 0.0f, 1.0f }));
        Check(Near(flat.axis, { 1.0f, 0.0f }), "a zero scale leaves the axis at +x");
    }

    void TestWhichHandlesALightHas()
    {
        Light2D light;
        light.type = Light2DType::Global;
        Check(false == JBro::LightGizmoModel::HasHandles(light), "a global light has no place and no handles");
        light.type = Light2DType::Point;
        Check(JBro::LightGizmoModel::HasHandle(light, Handle::OuterRadius) && JBro::LightGizmoModel::HasHandle(light, Handle::InnerRadius),
            "a point light has both radius handles");
        Check(false == JBro::LightGizmoModel::HasHandle(light, Handle::OuterAngle) && false == JBro::LightGizmoModel::HasHandle(light, Handle::InnerAngle),
            "but no angle handles");
        light.type = Light2DType::Spot;
        Check(JBro::LightGizmoModel::HasHandle(light, Handle::OuterAngle) && JBro::LightGizmoModel::HasHandle(light, Handle::InnerAngle)
                && JBro::LightGizmoModel::HasHandle(light, Handle::OuterRadius),
            "a spot light has the angle handles too");
    }

    void TestHandlesSitOnWhatTheyChange()
    {
        const JBro::LightGizmoModel::Pose pose = JBro::LightGizmoModel::PoseFromWorld(Turned());
        Light2D light;
        light.type = Light2DType::Spot;
        light.innerRadius = 1.0f;
        light.outerRadius = 3.0f;
        light.innerAngle = 60.0f;
        light.outerAngle = 90.0f;
        Check(Near(JBro::LightGizmoModel::HandlePosition(light, pose, Handle::OuterRadius), { 2.0f, 4.0f }),
            "a spot's outer radius handle is on its axis at that radius");
        Check(Near(JBro::LightGizmoModel::HandlePosition(light, pose, Handle::InnerRadius), { 2.0f, 2.0f }),
            "and the inner one at the inner radius");
        // 축 +y 에서 반시계로 45 도(바깥 각의 반)는 (-sin 45, cos 45) 방향이다.
        const JBro::Float half = 3.0f * 0.70710678f;
        Check(Near(JBro::LightGizmoModel::HandlePosition(light, pose, Handle::OuterAngle), { 2.0f - half, 1.0f + half }),
            "the outer angle handle is on the outer circle, half the cone to the left of the axis");
        // 축에서 시계로 30 도(안쪽 각의 반)는 (sin 30, cos 30) 방향이다.
        Check(Near(JBro::LightGizmoModel::HandlePosition(light, pose, Handle::InnerAngle), { 2.0f + 1.5f, 1.0f + 3.0f * 0.8660254f }),
            "the inner angle handle is on the outer circle, half the inner cone to the right");
        light.type = Light2DType::Point;
        // 점 라이트의 반지름 손잡이는 축에서 시계로 45 도다 - 옮기기 기즈모의 손잡이 사이를 피한다.
        Check(Near(JBro::LightGizmoModel::HandlePosition(light, pose, Handle::OuterRadius), { 2.0f + half, 1.0f + half }),
            "a point light's radius handle is 45 degrees clockwise of the axis");
    }

    void TestDraggingChangesTheValue()
    {
        // 반지름은 잡은 자리에서 반지름 방향으로 간 만큼이다. 옆으로 간 것은 들지 않는다.
        const Vector2 direction{ 0.0f, 1.0f };
        Check(Near(JBro::LightGizmoModel::DragRadius(3.0f, direction, { 2.0f, 4.1f }, { 2.5f, 5.1f }), 4.0f),
            "dragging a radius handle adds what the mouse moved along the radius");
        Check(Near(JBro::LightGizmoModel::DragRadius(1.0f, direction, { 2.0f, 2.0f }, { 2.0f, -5.0f }), 0.0f),
            "a radius does not go below zero");
        Check(Near(JBro::LightGizmoModel::DragRadius(999.0f, direction, { 0.0f, 0.0f }, { 0.0f, 50.0f }), 1000.0f),
            "nor above the field's limit");

        // 각은 축과 마우스가 이루는 각의 두 배다. 축의 어느 쪽이든 같다.
        const JBro::LightGizmoModel::Pose pose = JBro::LightGizmoModel::PoseFromWorld(Turned());
        Check(Near(JBro::LightGizmoModel::AngleAt(pose, { 1.0f, 2.0f }, 10.0f).Get(), 90.0f),
            "a mouse 45 degrees off the axis makes a 90 degree cone");
        Check(Near(JBro::LightGizmoModel::AngleAt(pose, { 3.0f, 2.0f }, 10.0f).Get(), 90.0f),
            "on either side");
        Check(Near(JBro::LightGizmoModel::AngleAt(pose, { 2.0f, -3.0f }, 10.0f).Get(), 360.0f),
            "straight behind the light the cone is whole");
        Check(Near(JBro::LightGizmoModel::AngleAt(pose, { 2.0f, 1.0f }, 10.0f).Get(), 10.0f),
            "on the light itself the angle stays what it was");
    }

    void TestHandlesNameTheirFields()
    {
        Check(std::string(JBro::LightGizmoModel::FieldName(Handle::OuterRadius)) == "outerRadius"
                && std::string(JBro::LightGizmoModel::FieldName(Handle::InnerRadius)) == "innerRadius"
                && std::string(JBro::LightGizmoModel::FieldName(Handle::OuterAngle)) == "outerAngle"
                && std::string(JBro::LightGizmoModel::FieldName(Handle::InnerAngle)) == "innerAngle",
            "each handle edits the Light2D field of its name");
    }
}

JBro::Int32 RunLightGizmoModelTests()
{
    TestThePoseFollowsTheObject();
    TestWhichHandlesALightHas();
    TestHandlesSitOnWhatTheyChange();
    TestDraggingChangesTheValue();
    TestHandlesNameTheirFields();
    std::cout << "Light gizmo model tests passed.\n";
    return 0;
}
