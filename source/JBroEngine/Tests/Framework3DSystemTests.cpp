#include <JBro/Canvas/Canvas.h>
#include <JBro/Core/Core.h>
#include <JBro/Framework3D/Component/Camera3D.h>
#include <JBro/Framework3D/Component/MeshRenderer3D.h>
#include <JBro/Framework3D/Component/Transform3D.h>
#include <JBro/Framework3D/Math3D.h>
#include <JBro/Framework3DSystem/Framework3D.h>
#include <JBro/Framework3DSystem/Math3DMatrix.h>
#include <JBro/Framework3DSystem/Rendering/MeshLibrary.h>
#include <JBro/Framework3DSystem/Rendering/RenderWorld3D.h>
#include <JBro/Framework3DSystem/System/Camera3DSystem.h>
#include <JBro/Framework3DSystem/System/MeshRender3DSystem.h>
#include <JBro/Framework3DSystem/System/Transform3DSystem.h>
#include <JBro/Runtime/GameObject.h>

#include <cmath>
#include <iostream>
#include <stdexcept>

// 3D 프레임워크의 수학·시스템·브리지를 렌더러 없이 잰다(framework3d-plan §2.5). 픽셀은
// `MeshPixelTests` 가 본다.
namespace JBro::Internal
{
    bool BuildCamera3D(const RenderCamera3D& source, const Extent2D& extent, CameraParams& result);
}

namespace
{
    void Check(bool condition, const char* message)
    {
        if (false == condition)
        {
            std::cout << "test failure: " << message << std::endl;
            throw std::runtime_error(message);
        }
    }

    bool Near(float left, float right, float tolerance = 0.0005f)
    {
        return std::fabs(left - right) <= tolerance;
    }

    constexpr float HalfPi = 1.57079632679f;

    // **오른손 규칙.** Y 축으로 +90도 돌리면 +X 가 -Z 로 간다. 이것이 틀리면 카메라가 뒤를 본다.
    void TestQuaternionsRotateRightHanded()
    {
        const JBro::Quaternion quarter = JBro::FromAxisAngle({0.0f, 1.0f, 0.0f}, HalfPi);
        const JBro::Vec3 turned = JBro::Rotate(quarter, {1.0f, 0.0f, 0.0f});
        Check(JBro::NearlyEqual(turned, {0.0f, 0.0f, -1.0f}),
            "a quarter turn about +Y must take +X to -Z");

        // 곱은 "먼저 b, 그 다음 a" 다. 두 4분의 1 회전은 반 회전이다.
        const JBro::Quaternion half = JBro::Multiply(quarter, quarter);
        Check(JBro::NearlyEqual(JBro::Rotate(half, {1.0f, 0.0f, 0.0f}), {-1.0f, 0.0f, 0.0f}),
            "two quarter turns must be a half turn");

        // 역은 되돌린다.
        const JBro::Vec3 back = JBro::Rotate(JBro::Conjugate(quarter), turned);
        Check(JBro::NearlyEqual(back, {1.0f, 0.0f, 0.0f}), "the conjugate must undo the turn");

        // 오일러 Y 하나만 주면 축각과 같다.
        const JBro::Quaternion euler = JBro::FromEuler({0.0f, HalfPi, 0.0f});
        Check(JBro::NearlyEqual(JBro::Rotate(euler, {1.0f, 0.0f, 0.0f}), {0.0f, 0.0f, -1.0f}),
            "a yaw-only Euler angle must match the axis-angle turn");

        Check(JBro::NearlyEqual(JBro::Normalize(JBro::Vec3{}), {}), "normalizing zero stays zero");
        Check(JBro::NearlyEqual(JBro::Cross({1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}), {0.0f, 0.0f, 1.0f}),
            "x cross y is z");
    }

    // TRS 는 스케일 → 회전 → 이동 순이고, 뷰는 카메라 TRS 의 역이다.
    void TestMatricesFollowTheRendererConvention()
    {
        const JBro::Quaternion quarter = JBro::FromAxisAngle({0.0f, 1.0f, 0.0f}, HalfPi);
        const JBro::Matrix4x4 world = JBro::MakeTransformMatrix3D({1.0f, 2.0f, 3.0f}, quarter, {2.0f, 2.0f, 2.0f});
        JBro::Vec3 moved;
        Check(JBro::TransformPoint(world, {1.0f, 0.0f, 0.0f}, moved), "a point must transform");
        // (1,0,0) → 스케일 2 → (2,0,0) → 회전 → (0,0,-2) → 이동 → (1,2,1)
        Check(JBro::NearlyEqual(moved, {1.0f, 2.0f, 1.0f}), "TRS must scale, then rotate, then move");

        const JBro::Matrix4x4 view = JBro::MakeViewMatrix({0.0f, 0.0f, 3.0f}, {});
        JBro::Vec3 seen;
        Check(JBro::TransformPoint(view, {0.0f, 0.0f, 0.0f}, seen), "the origin must transform");
        Check(JBro::NearlyEqual(seen, {0.0f, 0.0f, -3.0f}),
            "a camera at z=3 looking down -Z must see the origin 3 units in front");
        // 돌아선 카메라. Y 로 +90도 돌면 -X 를 보므로 (3,0,0) 에서 원점이 정면 3 앞이다.
        const JBro::Matrix4x4 turnedView = JBro::MakeViewMatrix({3.0f, 0.0f, 0.0f}, quarter);
        Check(JBro::TransformPoint(turnedView, {0.0f, 0.0f, 0.0f}, seen), "the origin must transform again");
        Check(JBro::NearlyEqual(seen, {0.0f, 0.0f, -3.0f}),
            "a camera turned to face -X must also see the origin straight ahead - the translation turns too");

        JBro::Matrix4x4 projection;
        Check(JBro::MakePerspectiveMatrix(HalfPi, 1.0f, 1.0f, 11.0f, projection), "the projection must build");
        JBro::Vec3 nearPoint;
        JBro::Vec3 farPoint;
        Check(JBro::TransformPoint(projection, {0.0f, 0.0f, -1.0f}, nearPoint)
                && JBro::TransformPoint(projection, {0.0f, 0.0f, -11.0f}, farPoint),
            "points on the planes must project");
        Check(Near(nearPoint.z, 0.0f) && Near(farPoint.z, 1.0f), "depth must run 0 at near to 1 at far");
        JBro::Vec3 edge;
        Check(JBro::TransformPoint(projection, {1.0f, 1.0f, -1.0f}, edge), "an edge point must project");
        Check(Near(edge.x, 1.0f) && Near(edge.y, 1.0f), "with a 90 degree field of view the near plane's edge is the clip edge");
        Check(false == JBro::MakePerspectiveMatrix(HalfPi, 1.0f, 5.0f, 1.0f, projection),
            "a near plane behind the far plane is refused");

        JBro::Matrix4x4 ortho;
        Check(JBro::MakeOrthographicMatrix(2.0f, 2.0f, 0.0f, 10.0f, ortho), "the orthographic projection must build");
        JBro::Vec3 corner;
        Check(JBro::TransformPoint(ortho, {4.0f, 2.0f, -10.0f}, corner), "a corner must project");
        Check(Near(corner.x, 1.0f) && Near(corner.y, 1.0f) && Near(corner.z, 1.0f),
            "orthographic half height 2 with aspect 2 spans 4 by 2, and -Z far is depth 1");
    }

    // 계층 두 단. 자식은 부모의 스케일·회전·이동을 차례로 받는다.
    void TestTransformHierarchyPropagates()
    {
        JBro::Canvas canvas(JBro::CreateDefaultAllocator());
        JBro::GameObject* parent = canvas.CreateObject("parent");
        JBro::GameObject* child = canvas.CreateObject("child");
        JBro::GameObject* loner = canvas.CreateObject("loner");
        auto* parentLocal = canvas.AttachComponent<JBro::Component::Transform3D>(parent);
        auto* childLocal = canvas.AttachComponent<JBro::Component::Transform3D>(child);
        auto* lonerLocal = canvas.AttachComponent<JBro::Component::Transform3D>(loner);
        Check(parentLocal != nullptr && childLocal != nullptr && lonerLocal != nullptr, "transforms must attach");
        parentLocal->position = {1.0f, 0.0f, 0.0f};
        parentLocal->rotation = JBro::FromAxisAngle({0.0f, 1.0f, 0.0f}, HalfPi);
        parentLocal->scale = {2.0f, 2.0f, 2.0f};
        childLocal->position = {1.0f, 0.0f, 0.0f};
        childLocal->scale = {0.5f, 0.5f, 0.5f};
        childLocal->rotation = JBro::FromAxisAngle({1.0f, 0.0f, 0.0f}, HalfPi);
        child->SetParent(parent);
        lonerLocal->position = {5.0f, 6.0f, 7.0f};

        JBro::System::Transform3DSystem system;
        system.Initialize(canvas);
        system.Update(canvas, 1.0f / 60.0f);

        Check(parentLocal->worldValid && childLocal->worldValid && lonerLocal->worldValid,
            "every active transform must get a world cache");
        Check(JBro::NearlyEqual(parentLocal->worldPosition, {1.0f, 0.0f, 0.0f}), "a root keeps its position");
        // 자식 (1,0,0) → 부모 스케일 2 → (2,0,0) → 부모 회전 → (0,0,-2) → 부모 위치 → (1,0,-2)
        Check(JBro::NearlyEqual(childLocal->worldPosition, {1.0f, 0.0f, -2.0f}),
            "the child must be scaled, turned and moved by its parent");
        Check(JBro::NearlyEqual(childLocal->worldScale, {1.0f, 1.0f, 1.0f}), "and its scale multiplies the parent's");
        // 자식의 +Y 는 자식 X 회전으로 +Z 가 되고, 부모 Y 회전으로 +X 가 된다. 순서가 뒤바뀌면 +Z 다.
        Check(JBro::NearlyEqual(JBro::Rotate(childLocal->worldRotation, {0.0f, 1.0f, 0.0f}), {1.0f, 0.0f, 0.0f}),
            "and its rotation is the parent's turn applied after its own");
        Check(JBro::NearlyEqual(lonerLocal->worldPosition, {5.0f, 6.0f, 7.0f}), "an unrelated root is untouched");

        // 부모의 transform 을 끄면 자식은 뿌리가 되지 않고 캐시가 비어야 한다 - 원점으로 튀지 않는다.
        parentLocal->SetEnabled(false);
        system.Update(canvas, 1.0f / 60.0f);
        Check(false == parentLocal->worldValid, "a disabled transform has no world cache");
        Check(false == childLocal->worldValid, "and neither does its child - it does not become a root");
    }

    // 주 카메라 하나만 뜬다. 첫 번째로 만난 것이다.
    void TestOnePrimaryCameraIsExtracted()
    {
        JBro::Canvas canvas(JBro::CreateDefaultAllocator());
        JBro::RenderWorld3D world;
        JBro::GameObject* first = canvas.CreateObject("first");
        JBro::GameObject* second = canvas.CreateObject("second");
        JBro::GameObject* eyeless = canvas.CreateObject("eyeless");
        auto* firstTransform = canvas.AttachComponent<JBro::Component::Transform3D>(first);
        canvas.AttachComponent<JBro::Component::Transform3D>(second);
        firstTransform->position = {0.0f, 0.0f, 5.0f};
        auto* firstCamera = canvas.AttachComponent<JBro::Component::Camera3D>(first);
        auto* secondCamera = canvas.AttachComponent<JBro::Component::Camera3D>(second);
        auto* eyelessCamera = canvas.AttachComponent<JBro::Component::Camera3D>(eyeless);
        firstCamera->primary = true;
        firstCamera->verticalFieldOfView = 45.0f;
        secondCamera->primary = true;
        eyelessCamera->primary = true;

        JBro::System::Transform3DSystem transforms;
        JBro::System::Camera3DSystem cameras;
        cameras.SetRenderWorld(&world);
        transforms.Initialize(canvas);
        cameras.Initialize(canvas);
        world.BeginFrame();
        transforms.Update(canvas, 1.0f / 60.0f);
        cameras.Update(canvas, 1.0f / 60.0f);

        const JBro::RenderCamera3D* camera = world.GetCamera();
        Check(camera != nullptr, "a primary camera must be extracted");
        Check(camera->owner == first, "the first primary one met, not a later one");
        Check(Near(camera->verticalFieldOfView, 45.0f), "carrying its values");

        // 트랜스폼이 없는 카메라는 뜨지 않는다. 자리가 없다.
        firstCamera->primary = false;
        secondCamera->primary = false;
        world.BeginFrame();
        transforms.Update(canvas, 1.0f / 60.0f);
        cameras.Update(canvas, 1.0f / 60.0f);
        Check(world.GetCamera() == nullptr, "a primary camera with no transform has nowhere to be");

        // 브리지가 값에서 뷰·투영을 만든다.
        JBro::RenderCamera3D source;
        source.position = {0.0f, 0.0f, 3.0f};
        source.clearColor = {0.1f, 0.2f, 0.3f, 1.0f};
        JBro::CameraParams parameters;
        Check(JBro::Internal::BuildCamera3D(source, {200, 100}, parameters), "a sane camera must build");
        Check(Near(parameters.viewport.width, 200.0f) && Near(parameters.viewport.height, 100.0f),
            "the viewport is the frame");
        Check(Near(parameters.clearColor[2], 0.3f), "the clear colour comes along");
        Check(Near(parameters.view.values[11], -3.0f), "the view moves the world 3 units down -Z");
        // 가로가 두 배이므로 x 초점이 y 의 절반이다.
        Check(Near(parameters.projection.values[0] * 2.0f, parameters.projection.values[5]),
            "the aspect ratio squeezes x");
        source.nearPlane = 10.0f;
        source.farPlane = 1.0f;
        Check(false == JBro::Internal::BuildCamera3D(source, {200, 100}, parameters),
            "a camera whose near plane is past its far plane is refused");
        Check(false == JBro::Internal::BuildCamera3D(source, {0, 100}, parameters), "and so is a zero frame");
    }

    // 메시 렌더러는 보일 때만, 트랜스폼이 있을 때만 뜬다. 빈 핸들은 아이디로 해석한다.
    void TestMeshesAreExtractedAndResolved()
    {
        JBro::Canvas canvas(JBro::CreateDefaultAllocator());
        JBro::RenderWorld3D world;
        Check(world.ReserveMeshes(4), "the render world must reserve");
        JBro::MeshLibrary library;
        Check(library.Initialize(nullptr), "a library with no renderer is empty but fine");
        Check(library.GetCount() == 0, "and holds nothing");
        const JBro::AssetHandle cube{7, 3};
        library.Register(JBro::MeshLibrary::BuiltinCubeId(), cube);
        Check(library.Resolve(JBro::MeshLibrary::BuiltinCubeId()).index == 7, "a registered id resolves");
        Check(library.Resolve(JBro::AssetId{12345}).generation == 0, "an unknown id does not");

        JBro::GameObject* shown = canvas.CreateObject("shown");
        JBro::GameObject* hidden = canvas.CreateObject("hidden");
        JBro::GameObject* placeless = canvas.CreateObject("placeless");
        JBro::GameObject* unnamed = canvas.CreateObject("unnamed");
        for (JBro::GameObject* object : {shown, hidden, unnamed})
        {
            canvas.AttachComponent<JBro::Component::Transform3D>(object);
        }
        auto* shownMesh = canvas.AttachComponent<JBro::Component::MeshRenderer3D>(shown);
        auto* hiddenMesh = canvas.AttachComponent<JBro::Component::MeshRenderer3D>(hidden);
        auto* placelessMesh = canvas.AttachComponent<JBro::Component::MeshRenderer3D>(placeless);
        auto* unnamedMesh = canvas.AttachComponent<JBro::Component::MeshRenderer3D>(unnamed);
        shownMesh->meshId = JBro::MeshLibrary::BuiltinCubeId();
        shownMesh->tint = {0.25f, 0.5f, 0.75f, 1.0f};
        hiddenMesh->meshId = JBro::MeshLibrary::BuiltinCubeId();
        hiddenMesh->visible = false;
        placelessMesh->meshId = JBro::MeshLibrary::BuiltinCubeId();
        (void)unnamedMesh;

        JBro::System::Transform3DSystem transforms;
        JBro::System::MeshRender3DSystem meshes;
        meshes.SetRenderWorld(&world);
        meshes.SetMeshLibrary(&library);
        transforms.Initialize(canvas);
        meshes.Initialize(canvas);
        world.BeginFrame();
        transforms.Update(canvas, 1.0f / 60.0f);
        meshes.Update(canvas, 1.0f / 60.0f);

        Check(world.GetMeshCount() == 1, "only the visible, placed, named mesh is extracted");
        Check(world.GetMesh(0).owner == shown, "and it is the shown one");
        Check(world.GetMesh(0).mesh.index == 7 && world.GetMesh(0).mesh.generation == 3,
            "with its handle resolved from the id");
        Check(shownMesh->mesh.index == 7, "and the resolved handle written back so it is not looked up again");
        Check(Near(world.GetMesh(0).tint.G, 0.5f), "carrying the tint");
        Check(hiddenMesh->mesh.generation == 0, "a hidden mesh is not even resolved");

        // 용량을 넘으면 세기만 한다.
        world.BeginFrame();
        for (int index = 0; index < 6; ++index)
        {
            world.SubmitMesh(world.GetMeshCount() > 0 ? world.GetMesh(0) : JBro::MeshRenderItem{});
        }
        Check(world.GetMeshCount() == 4 && world.GetDroppedMeshCount() == 2,
            "past its capacity the render world counts what it dropped");
    }

    // 렌더러 없이도 프레임워크는 뜨고 돈다. 그릴 곳이 없으니 Render 는 실패다(2D 와 같다).
    void TestTheFrameworkRunsWithoutARenderer()
    {
        JBro::Framework3D framework;
        JBro::FrameworkContext context;
        Check(framework.Initialize(context), "the 3D framework must initialize without a renderer");
        JBro::Canvas* canvas = framework.GetCanvas();
        Check(canvas != nullptr, "and own a canvas");
        JBro::GameObject* eye = canvas->CreateObject("eye");
        auto* eyeTransform = canvas->AttachComponent<JBro::Component::Transform3D>(eye);
        eyeTransform->position = {0.0f, 1.0f, 4.0f};
        canvas->AttachComponent<JBro::Component::Camera3D>(eye)->primary = true;
        JBro::GameObject* box = canvas->CreateObject("box");
        canvas->AttachComponent<JBro::Component::Transform3D>(box);
        auto* boxMesh = canvas->AttachComponent<JBro::Component::MeshRenderer3D>(box);
        boxMesh->meshId = JBro::MeshLibrary::BuiltinCubeId();

        framework.Update(1.0f / 60.0f);
        const JBro::RenderWorld3D* world = framework.GetRenderWorld();
        Check(world->GetCamera() != nullptr && world->GetCamera()->owner == eye,
            "one update must extract the camera");
        Check(world->GetMeshCount() == 0,
            "but with no renderer the builtin cube has no handle, so the mesh is not extracted");
        Check(framework.Render() == JBro::RenderResult::Failed, "rendering with no renderer fails");
        framework.Shutdown();
        Check(framework.GetCanvas() == nullptr, "shutdown drops the canvas");
    }
}

int RunFramework3DSystemTests()
{
    TestQuaternionsRotateRightHanded();
    TestMatricesFollowTheRendererConvention();
    TestTransformHierarchyPropagates();
    TestOnePrimaryCameraIsExtracted();
    TestMeshesAreExtractedAndResolved();
    TestTheFrameworkRunsWithoutARenderer();
    std::cout << "Framework3D system tests passed.\n";
    return 0;
}
