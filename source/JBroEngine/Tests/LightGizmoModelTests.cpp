#include "TestCases.h"
#include <JBro/Editor/Gizmo/LightGizmo3DModel.h>
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
        // **맞은편 손잡이는 축에 비친 자리다**(D-298). 원뿔의 양쪽 가장자리를 다 잡을 수 있다.
        Check(Near(JBro::LightGizmoModel::HandlePosition(light, pose, Handle::OuterAngleMirror), { 2.0f + half, 1.0f + half }),
            "the outer angle has a mirrored handle on the other edge of the cone");
        Check(Near(JBro::LightGizmoModel::HandlePosition(light, pose, Handle::InnerAngleMirror), { 2.0f - 1.5f, 1.0f + 3.0f * 0.8660254f }),
            "and so does the inner angle");
        Check(JBro::LightGizmoModel::HasHandle(light, Handle::OuterAngleMirror) && JBro::LightGizmoModel::IsAngle(Handle::InnerAngleMirror)
                && false == JBro::LightGizmoModel::IsAngle(Handle::OuterRadius),
            "a spot has the mirrored handles and they are angle handles");
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
                && std::string(JBro::LightGizmoModel::FieldName(Handle::InnerAngle)) == "innerAngle"
                && std::string(JBro::LightGizmoModel::FieldName(Handle::OuterAngleMirror)) == "outerAngle"
                && std::string(JBro::LightGizmoModel::FieldName(Handle::InnerAngleMirror)) == "innerAngle",
            "each handle edits the Light2D field of its name, a mirrored one the same field as its twin");
        Check(std::string(JBro::LightGizmoModel::HandleId(Handle::OuterAngle)) != JBro::LightGizmoModel::HandleId(Handle::OuterAngleMirror),
            "but the twins are separate items");
    }
    // **3D 라이트 기즈모의 모델**(D-353 3 단계). 손잡이는 월드에 놓이고, 끌기는 화면 좌표로 잰다.
    void TestThe3DLightHandles()
    {
        using JBro::LightGizmo3DModel::Handle;
        JBro::Component::Light3D light;
        light.type = JBro::Component::Light3DType::Spot;
        light.range = 4.0f;
        light.innerAngle = 30.0f;
        light.outerAngle = 90.0f;
        // 아래를 보는 스포트: 로컬 -Z 가 -Y 로, 로컬 +X 는 그대로 옆이다.
        const JBro::LightGizmo3DModel::Pose pose = JBro::LightGizmo3DModel::PoseFrom({1.0f, 5.0f, 0.0f},
            JBro::FromAxisAngle({1.0f, 0.0f, 0.0f}, JBro::Radian(-1.5707963f)));
        const auto near3 = [](JBro::Vector3 a, JBro::Vector3 b) {
            return std::fabs(a.x - b.x) < 0.001f && std::fabs(a.y - b.y) < 0.001f && std::fabs(a.z - b.z) < 0.001f;
        };
        Check(near3(pose.direction, {0.0f, -1.0f, 0.0f}) && near3(pose.side, {1.0f, 0.0f, 0.0f}), "the pose shines along the object's -Z and keeps its +X as the side");
        const JBro::Vector3 right{0.0f, 0.0f, 1.0f};
        Check(near3(JBro::LightGizmo3DModel::HandlePosition(light, pose, Handle::Range, right), {1.0f, 1.0f, 0.0f}),
            "a spot's range handle is at the end of its axis");
        // 바깥 90 도는 반각 45 도라 끝 원의 반지름이 길이와 같다.
        Check(near3(JBro::LightGizmo3DModel::HandlePosition(light, pose, Handle::OuterAngle, right), {5.0f, 1.0f, 0.0f}),
            "the outer angle handle is on the end circle on the side");
        Check(JBro::LightGizmo3DModel::HandlePosition(light, pose, Handle::InnerAngle, right).x < 1.0f,
            "and the inner one across from it");
        Check(JBro::LightGizmo3DModel::HasHandle(light, Handle::OuterAngle) && JBro::LightGizmo3DModel::HasHandle(light, Handle::Range),
            "a spot has range and angle handles");
        light.type = JBro::Component::Light3DType::Point;
        Check(near3(JBro::LightGizmo3DModel::HandlePosition(light, pose, Handle::Range, right), {1.0f, 5.0f, 4.0f}),
            "a point light's range handle is on its sphere toward the screen's right");
        Check(false == JBro::LightGizmo3DModel::HasHandle(light, Handle::OuterAngle), "a point light has no angle handles");
        light.type = JBro::Component::Light3DType::Directional;
        Check(false == JBro::LightGizmo3DModel::HasHandles(light), "a directional light has no handles");
        light.type = JBro::Component::Light3DType::Spot;
        light.range = 0.0f;
        Check(near3(JBro::LightGizmo3DModel::HandlePosition(light, pose, Handle::Range, right), {1.0f, 0.0f, 0.0f}),
            "an unbounded light is drawn and grabbed at the shown length");

        // 끌기: 점광은 화면 거리, 스포트는 축에 내린 거리를 유닛으로 바꾼다.
        Check(Near(JBro::LightGizmo3DModel::DragRange(false, {100.0f, 100.0f}, {120.0f, 100.0f}, {100.0f, 160.0f}), 3.0f),
            "a point light's range is the mouse's screen distance in units, whichever way");
        Check(Near(JBro::LightGizmo3DModel::DragRange(true, {100.0f, 100.0f}, {100.0f, 120.0f}, {130.0f, 160.0f}), 3.0f),
            "a spot's range is how far the mouse went along the axis");
        Check(Near(JBro::LightGizmo3DModel::DragRange(true, {100.0f, 100.0f}, {100.0f, 120.0f}, {100.0f, 50.0f}), 0.0f),
            "and not below zero");
        // 각: 축에서 45 도 비낀 마우스는 90 도 원뿔이다. 꼭짓점 뒤면 그대로다.
        Check(Near(JBro::LightGizmo3DModel::DragAngle({0.0f, 0.0f}, {0.0f, 10.0f}, {5.0f, 5.0f}, 20.0f).Get(), 90.0f),
            "a mouse 45 degrees off the axis makes a 90 degree cone");
        Check(Near(JBro::LightGizmo3DModel::DragAngle({0.0f, 0.0f}, {0.0f, 10.0f}, {-5.0f, 5.0f}, 20.0f).Get(), 90.0f), "on either side");
        Check(Near(JBro::LightGizmo3DModel::DragAngle({0.0f, 0.0f}, {0.0f, 10.0f}, {3.0f, -4.0f}, 20.0f).Get(), 20.0f),
            "behind the apex the angle stays what it was");
        Check(std::string(JBro::LightGizmo3DModel::FieldName(Handle::Range)) == "range"
                && std::string(JBro::LightGizmo3DModel::FieldName(Handle::OuterAngle)) == "outerAngle"
                && std::string(JBro::LightGizmo3DModel::FieldName(Handle::InnerAngle)) == "innerAngle",
            "each handle edits the Light3D field of its name");
    }
}

JBro::Int32 RunLightGizmoModelTests()
{
    JBRO_CASE(TestThePoseFollowsTheObject());
    JBRO_CASE(TestWhichHandlesALightHas());
    JBRO_CASE(TestHandlesSitOnWhatTheyChange());
    JBRO_CASE(TestDraggingChangesTheValue());
    JBRO_CASE(TestHandlesNameTheirFields());
    JBRO_CASE(TestThe3DLightHandles());
    std::cout << "Light gizmo model tests passed.\n";
    return 0;
}
