#include <JBro/Editor/Gizmo/GizmoModel.h>
#include <JBro/Framework3DSystem/Math3DMatrix.h>

#include <cmath>
#include <iostream>
#include <stdexcept>

// 기즈모 수학을 창 없이 잰다(D-109). 직교 2D 카메라와 원근 3D 카메라에서 투영·집기·끌기가 맞는지,
// 회전의 부호가 카메라 위치에 따라 뒤집히지 않는지 본다.
namespace
{
    void Check(bool condition, const char* message)
    {
        if (false == condition)
        {
            std::cout << "test failure: " << message << '\n';
            throw std::runtime_error(message);
        }
    }

    bool Near(float a, float b, float tolerance = 1.0e-3f)
    {
        return std::fabs(a - b) <= tolerance;
    }

    constexpr float Pi = 3.14159265358979f;

    // 반높이 1, 종횡비 1 의 직교 카메라가 200x200 화면 사각형에 앉는다. 월드 1 = 100 픽셀이다.
    JBro::GizmoCamera OrthoCamera()
    {
        JBro::Matrix4x4 projection;
        Check(JBro::MakeOrthographicMatrix(1.0f, 1.0f, -10.0f, 10.0f, projection), "the orthographic matrix must build");
        JBro::GizmoCamera camera;
        Check(JBro::GizmoModel::MakeCamera(JBro::Matrix4x4{}, projection, 0.0f, 0.0f, 200.0f, 200.0f, camera),
            "the gizmo camera must build from a view and a projection");
        return camera;
    }

    JBro::GizmoCamera PerspectiveCamera(const JBro::Vec3& eye, const JBro::Quaternion& rotation)
    {
        JBro::Matrix4x4 projection;
        Check(JBro::MakePerspectiveMatrix(60.0f * Pi / 180.0f, 1.0f, 0.1f, 100.0f, projection),
            "the perspective matrix must build");
        JBro::GizmoCamera camera;
        Check(JBro::GizmoModel::MakeCamera(JBro::MakeViewMatrix(eye, rotation), projection, 0.0f, 0.0f, 400.0f, 400.0f,
                  camera),
            "the perspective gizmo camera must build");
        return camera;
    }

    JBro::GizmoSubject PlanarSubject()
    {
        JBro::GizmoSubject subject;
        subject.planar = true;
        return subject;
    }

    void TestProjectionMapsTheWorldOntoTheRectangle()
    {
        const JBro::GizmoCamera camera = OrthoCamera();
        float x = 0.0f;
        float y = 0.0f;
        Check(JBro::GizmoModel::Project(camera, {0.0f, 0.0f, 0.0f}, x, y) && Near(x, 100.0f) && Near(y, 100.0f),
            "the origin must land in the middle of the rectangle");
        Check(JBro::GizmoModel::Project(camera, {1.0f, 0.0f, 0.0f}, x, y) && Near(x, 200.0f) && Near(y, 100.0f),
            "+x must go right");
        Check(JBro::GizmoModel::Project(camera, {0.0f, 1.0f, 0.0f}, x, y) && Near(x, 100.0f) && Near(y, 0.0f),
            "+y must go up the screen, which is towards smaller y");
        JBro::Vec3 world;
        Check(JBro::GizmoModel::Unproject(camera, 150.0f, 50.0f, 0.5f, world) && Near(world.x, 0.5f) && Near(world.y, 0.5f),
            "unprojecting a pixel must give the world point back");

        JBro::Matrix4x4 singular;
        for (float& value : singular.values)
        {
            value = 0.0f;
        }
        JBro::Matrix4x4 inverse;
        Check(false == JBro::GizmoModel::Invert(singular, inverse), "a singular matrix has no inverse");
        JBro::GizmoCamera broken;
        Check(false == JBro::GizmoModel::MakeCamera(JBro::Matrix4x4{}, JBro::Matrix4x4{}, 0.0f, 0.0f, 0.0f, 10.0f, broken),
            "a rectangle without width is refused");
    }

    void TestHandlesAreBuiltAndPickedInPixels()
    {
        const JBro::GizmoCamera camera = OrthoCamera();
        const JBro::GizmoSubject subject = PlanarSubject();
        JBro::GizmoHandleShape handles[JBro::GizmoModel::MaxHandles];
        const std::uint32_t count = JBro::GizmoModel::BuildHandles(JBro::GizmoMode::Translate, camera, subject, handles);
        Check(count == 3, "a planar subject has two axes and the centre");
        Check(handles[0].axis == JBro::GizmoAxis::X && Near(handles[0].x1, 100.0f + JBro::GizmoModel::AxisLengthPixels)
                && Near(handles[0].y1, 100.0f),
            "the x handle must reach right by the fixed pixel length");
        Check(handles[1].axis == JBro::GizmoAxis::Y && Near(handles[1].x1, 100.0f)
                && Near(handles[1].y1, 100.0f - JBro::GizmoModel::AxisLengthPixels),
            "the y handle must reach up by the fixed pixel length");
        Check(handles[2].axis == JBro::GizmoAxis::Free, "the centre handle comes last");

        Check(JBro::GizmoModel::Pick(JBro::GizmoMode::Translate, camera, subject, 150.0f, 100.0f) == JBro::GizmoAxis::X,
            "a point on the x arrow picks x");
        Check(JBro::GizmoModel::Pick(JBro::GizmoMode::Translate, camera, subject, 100.0f, 50.0f) == JBro::GizmoAxis::Y,
            "a point on the y arrow picks y");
        Check(JBro::GizmoModel::Pick(JBro::GizmoMode::Translate, camera, subject, 102.0f, 98.0f) == JBro::GizmoAxis::Free,
            "the centre wins over the axes that start there");
        Check(JBro::GizmoModel::Pick(JBro::GizmoMode::Translate, camera, subject, 150.0f, 150.0f) == JBro::GizmoAxis::None,
            "empty space picks nothing");

        const std::uint32_t rings = JBro::GizmoModel::BuildHandles(JBro::GizmoMode::Rotate, camera, subject, handles);
        Check(rings == 1 && handles[0].ring && handles[0].axis == JBro::GizmoAxis::Z,
            "a planar subject rotates about z only");
        Check(JBro::GizmoModel::Pick(JBro::GizmoMode::Rotate, camera, subject, 100.0f + JBro::GizmoModel::RingRadiusPixels,
                  100.0f) == JBro::GizmoAxis::Z,
            "a point on the ring picks z");
        Check(JBro::GizmoModel::Pick(JBro::GizmoMode::Rotate, camera, subject, 100.0f, 100.0f) == JBro::GizmoAxis::None,
            "the middle of the ring is not the ring");

        // 카메라를 정면으로 가리키는 축은 그릴 방향이 없어 빠진다.
        JBro::GizmoSubject facing;
        const std::uint32_t solid = JBro::GizmoModel::BuildHandles(JBro::GizmoMode::Translate, camera, facing, handles);
        Check(solid == 3, "the z axis of a 3D subject points at an orthographic camera and is left out");
    }

    void TestTranslationFollowsTheMouseAlongTheAxis()
    {
        const JBro::GizmoCamera camera = OrthoCamera();
        const JBro::GizmoSubject subject = PlanarSubject();
        JBro::GizmoDrag drag;
        Check(JBro::GizmoModel::BeginDrag(JBro::GizmoMode::Translate, JBro::GizmoAxis::X, camera, subject, 150.0f, 100.0f, drag),
            "a drag on the x handle must begin");
        JBro::GizmoSubject result;
        Check(JBro::GizmoModel::UpdateDrag(drag, camera, 190.0f, 130.0f, result), "the drag must update");
        Check(Near(result.position.x, 0.4f) && Near(result.position.y, 0.0f),
            "40 pixels along x are 0.4 world units, and the sideways part of the mouse is ignored");

        Check(JBro::GizmoModel::BeginDrag(JBro::GizmoMode::Translate, JBro::GizmoAxis::Free, camera, subject, 100.0f, 100.0f, drag),
            "a drag on the centre must begin");
        Check(JBro::GizmoModel::UpdateDrag(drag, camera, 120.0f, 80.0f, result), "the free drag must update");
        Check(Near(result.position.x, 0.2f) && Near(result.position.y, 0.2f),
            "the centre moves in the screen plane, both axes");

        // 대상이 돌아 있으면 손잡이도 돌아 있다. 90도 돈 x 축은 화면 위를 가리킨다.
        JBro::GizmoSubject turned = PlanarSubject();
        turned.rotation = JBro::FromAxisAngle({0.0f, 0.0f, 1.0f}, Pi * 0.5f);
        Check(JBro::GizmoModel::Pick(JBro::GizmoMode::Translate, camera, turned, 100.0f, 50.0f) == JBro::GizmoAxis::X,
            "the x handle of a turned subject lies along its own x");
        Check(JBro::GizmoModel::BeginDrag(JBro::GizmoMode::Translate, JBro::GizmoAxis::X, camera, turned, 100.0f, 50.0f, drag)
                && JBro::GizmoModel::UpdateDrag(drag, camera, 100.0f, 20.0f, result),
            "dragging the turned x handle must work");
        Check(Near(result.position.x, 0.0f) && Near(result.position.y, 0.3f), "and it moves along world +y");
    }

    void TestRotationTurnsRightHandedAboutTheAxis()
    {
        const JBro::GizmoCamera camera = OrthoCamera();
        const JBro::GizmoSubject subject = PlanarSubject();
        JBro::GizmoDrag drag;
        const float ring = JBro::GizmoModel::RingRadiusPixels;
        Check(JBro::GizmoModel::BeginDrag(JBro::GizmoMode::Rotate, JBro::GizmoAxis::Z, camera, subject, 100.0f + ring, 100.0f, drag),
            "a drag on the ring must begin");
        JBro::GizmoSubject result;
        Check(JBro::GizmoModel::UpdateDrag(drag, camera, 100.0f, 100.0f - ring, result), "the ring drag must update");
        // 오른쪽(+x)에서 위(+y)로 끌었다. 오른손 규약에서 z 축을 도는 +90도다.
        const JBro::Quaternion expected = JBro::FromAxisAngle({0.0f, 0.0f, 1.0f}, Pi * 0.5f);
        Check(Near(result.rotation.z, expected.z, 1.0e-3f) && Near(result.rotation.w, expected.w, 1.0e-3f),
            "right to top on screen is +90 degrees about z for a camera looking down -z");

        // 같은 손짓을 뒤에서 보는 카메라는 반대로 읽는다 - 부호가 카메라를 따라간다.
        const JBro::GizmoCamera behind = PerspectiveCamera({0.0f, 0.0f, -3.0f}, JBro::FromAxisAngle({0.0f, 1.0f, 0.0f}, Pi));
        JBro::GizmoSubject solid;
        float cx = 0.0f;
        float cy = 0.0f;
        Check(JBro::GizmoModel::Project(behind, solid.position, cx, cy), "the subject must be in front of the camera");
        Check(JBro::GizmoModel::BeginDrag(JBro::GizmoMode::Rotate, JBro::GizmoAxis::Z, behind, solid, cx + ring, cy, drag),
            "a ring drag from behind must begin");
        Check(JBro::GizmoModel::UpdateDrag(drag, behind, cx, cy - ring, result), "and update");
        const JBro::Quaternion mirrored = JBro::FromAxisAngle({0.0f, 0.0f, 1.0f}, -Pi * 0.5f);
        Check(Near(result.rotation.z, mirrored.z, 1.0e-3f) && Near(result.rotation.w, mirrored.w, 1.0e-3f),
            "seen from the other side, the same screen motion is -90 degrees");
    }

    void TestScaleIsARatioAlongTheHandle()
    {
        const JBro::GizmoCamera camera = OrthoCamera();
        const JBro::GizmoSubject subject = PlanarSubject();
        JBro::GizmoDrag drag;
        Check(JBro::GizmoModel::BeginDrag(JBro::GizmoMode::Scale, JBro::GizmoAxis::X, camera, subject, 150.0f, 100.0f, drag),
            "a scale drag on x must begin");
        JBro::GizmoSubject result;
        Check(JBro::GizmoModel::UpdateDrag(drag, camera, 200.0f, 100.0f, result), "the scale drag must update");
        Check(Near(result.scale.x, 2.0f) && Near(result.scale.y, 1.0f), "twice as far from the centre is twice the scale");
        Check(JBro::GizmoModel::UpdateDrag(drag, camera, 125.0f, 100.0f, result) && Near(result.scale.x, 0.5f),
            "half as far is half the scale");

        Check(JBro::GizmoModel::BeginDrag(JBro::GizmoMode::Scale, JBro::GizmoAxis::Free, camera, subject, 130.0f, 100.0f, drag),
            "a uniform scale drag must begin");
        Check(JBro::GizmoModel::UpdateDrag(drag, camera, 100.0f, 160.0f, result), "and update");
        Check(Near(result.scale.x, 2.0f) && Near(result.scale.y, 2.0f), "the centre scales every axis by the distance ratio");

        Check(false == JBro::GizmoModel::BeginDrag(JBro::GizmoMode::Scale, JBro::GizmoAxis::X, camera, subject, 100.0f, 100.0f, drag),
            "a scale drag that starts on the centre has no ratio and is refused");
        Check(false == JBro::GizmoModel::BeginDrag(JBro::GizmoMode::Rotate, JBro::GizmoAxis::Free, camera, subject, 100.0f, 100.0f, drag),
            "there is no centre handle for rotation");
        Check(false == JBro::GizmoModel::BeginDrag(JBro::GizmoMode::Translate, JBro::GizmoAxis::Z, camera, subject, 100.0f, 100.0f, drag),
            "a planar subject has no z handle to drag");
    }

    void TestPerspectiveDragKeepsTheHandleUnderTheMouse()
    {
        const JBro::GizmoCamera camera = PerspectiveCamera({1.0f, 2.0f, 4.0f}, {});
        JBro::GizmoSubject subject;
        subject.position = {0.5f, -0.5f, 0.0f};
        subject.rotation = JBro::FromAxisAngle({0.0f, 1.0f, 0.0f}, 0.4f);
        JBro::GizmoHandleShape handles[JBro::GizmoModel::MaxHandles];
        const std::uint32_t count = JBro::GizmoModel::BuildHandles(JBro::GizmoMode::Translate, camera, subject, handles);
        Check(count == 4, "a 3D subject in perspective shows three axes and the centre");
        // 손잡이의 중간을 잡아 30 픽셀 옆으로 끈다.
        const float grabX = (handles[0].x0 + handles[0].x1) * 0.5f;
        const float grabY = (handles[0].y0 + handles[0].y1) * 0.5f;
        Check(JBro::GizmoModel::Pick(JBro::GizmoMode::Translate, camera, subject, grabX, grabY) == JBro::GizmoAxis::X,
            "the middle of the x arrow picks x in perspective too");
        JBro::GizmoDrag drag;
        Check(JBro::GizmoModel::BeginDrag(JBro::GizmoMode::Translate, JBro::GizmoAxis::X, camera, subject, grabX, grabY, drag),
            "the perspective drag must begin");
        JBro::GizmoSubject result;
        const float mouseX = grabX + 30.0f;
        const float mouseY = grabY + 5.0f;
        Check(JBro::GizmoModel::UpdateDrag(drag, camera, mouseX, mouseY, result), "and update");
        // 잡은 점은 축 위의 점이다. 그 점이 마우스 광선 위에 있으려면 다시 투영했을 때 마우스 자리여야 한다 -
        // 마우스가 축에서 벗어난 만큼은 축에 가장 가까운 점으로 흡수된다.
        const JBro::Vec3 grabbed = JBro::Add(result.position, JBro::Scale(drag.axisDirection, drag.startParameter));
        float px = 0.0f;
        float py = 0.0f;
        Check(JBro::GizmoModel::Project(camera, grabbed, px, py), "the grabbed point must stay in front of the camera");
        const float dx = px - mouseX;
        const float dy = py - mouseY;
        // 축의 화면 방향에 수직인 성분은 버려지므로, 축 방향 성분만 비교한다.
        const float ax = handles[0].x1 - handles[0].x0;
        const float ay = handles[0].y1 - handles[0].y0;
        const float along = (dx * ax + dy * ay) / std::sqrt(ax * ax + ay * ay);
        Check(std::fabs(along) < 0.5f, "along the axis the grabbed point follows the mouse to within half a pixel");
        Check(Near(JBro::Length(JBro::Cross(JBro::Subtract(result.position, subject.position), drag.axisDirection)), 0.0f, 1.0e-4f),
            "the subject moved along its own x axis and nowhere else");
    }
}

int RunGizmoModelTests()
{
    TestProjectionMapsTheWorldOntoTheRectangle();
    TestHandlesAreBuiltAndPickedInPixels();
    TestTranslationFollowsTheMouseAlongTheAxis();
    TestRotationTurnsRightHandedAboutTheAxis();
    TestScaleIsARatioAlongTheHandle();
    TestPerspectiveDragKeepsTheHandleUnderTheMouse();
    std::cout << "Gizmo model tests passed.\n";
    return 0;
}
