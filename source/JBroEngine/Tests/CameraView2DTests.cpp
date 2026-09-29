#include <JBro/Canvas/Canvas.h>
#include <JBro/Canvas/ScreenSpace.h>
#include <JBro/Framework2D/Component/Camera2D.h>
#include <JBro/Framework2D/Component/Transform2D.h>
#include <JBro/Framework2DSystem/Rendering/CameraView2D.h>
#include <JBro/Framework2DSystem/System/Camera2DSystem.h>
#include <JBro/Framework2DSystem/System/Transform2DSystem.h>

#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

// 2D 카메라가 화면에 무엇을 보이는지(D-239). 장치 없이 계산만 잰다 - 그리기와 역투영이 같은 함수를 쓰는지는
// `RendererContractTests` 의 PixelPerfect 절이 가짜 장치로 잰다.

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

    bool Close(float a, float b)
    {
        return std::fabs(a - b) < 0.0001f;
    }

    JBro::ScreenSpaceFrame Frame(float refW, float refH, float targetW, float targetH)
    {
        JBro::ScreenSpaceFrame frame;
        frame.referenceWidth = refW;
        frame.referenceHeight = refH;
        frame.targetWidth = targetW;
        frame.targetHeight = targetH;
        return frame;
    }

    void TestThePixelPerfectAreaIsAnIntegerScaleInTheMiddle()
    {
        JBro::ScreenArea area;
        float scale = 0.0f;
        Check(JBro::ComputePixelPerfectArea(Frame(320.0f, 180.0f, 1280.0f, 720.0f), area, scale)
                && Close(scale, 4.0f) && Close(area.x, 0.0f) && Close(area.y, 0.0f)
                && Close(area.width, 1280.0f) && Close(area.height, 720.0f),
            "an exact multiple fills the target at that scale");
        Check(JBro::ComputePixelPerfectArea(Frame(1280.0f, 720.0f, 1920.0f, 1080.0f), area, scale)
                && Close(scale, 1.0f) && Close(area.x, 320.0f) && Close(area.y, 180.0f)
                && Close(area.width, 1280.0f) && Close(area.height, 720.0f),
            "1.5 times rounds down to 1 with bars around it, not a blurry 1.5");
        Check(JBro::ComputePixelPerfectArea(Frame(320.0f, 180.0f, 1000.0f, 700.0f), area, scale)
                && Close(scale, 3.0f) && Close(area.x, 20.0f) && Close(area.y, 80.0f)
                && Close(area.width, 960.0f) && Close(area.height, 540.0f),
            "the smaller axis decides the scale and the rest is split evenly");
        Check(JBro::ComputePixelPerfectArea(Frame(320.0f, 180.0f, 1001.0f, 701.0f), area, scale)
                && Close(area.x, 20.0f) && Close(area.y, 80.0f),
            "the corner lands on a whole pixel when the leftover is odd");
        // 대상이 기준보다 작으면 들어가는 만큼 줄인다(에디터의 작은 게임 뷰).
        Check(JBro::ComputePixelPerfectArea(Frame(320.0f, 180.0f, 200.0f, 100.0f), area, scale)
                && Close(scale, 100.0f / 180.0f) && Close(area.height, 100.0f) && area.width <= 200.0f
                && Close(area.y, 0.0f) && Close(area.x, std::floor((200.0f - area.width) * 0.5f)),
            "a target smaller than the reference is shrunk to fit");
        // float 로 100 x (54 / 100) 은 54.0000038 이다 - 잘라 두지 않으면 뷰포트가 대상을 한 올 넘어 렌더러가 프레임을 거절한다.
        Check(JBro::ComputePixelPerfectArea(Frame(100.0f, 50.0f, 54.0f, 40.0f), area, scale)
                && area.x >= 0.0f && area.y >= 0.0f && area.x + area.width <= 54.0f && area.y + area.height <= 40.0f,
            "a shrunk rectangle never reaches past the target by rounding");
        const JBro::ScreenArea before = area;
        Check(false == JBro::ComputePixelPerfectArea(Frame(0.0f, 180.0f, 200.0f, 100.0f), area, scale)
                && Close(area.width, before.width),
            "no reference size, no area, and the result is left alone");
        Check(false == JBro::ComputePixelPerfectArea(Frame(320.0f, 180.0f, 0.0f, 100.0f), area, scale),
            "no target size, no area");
    }

    void TestTheScreenAreaDrivesExtentsAndPointers()
    {
        JBro::ScreenSpaceFrame frame = Frame(320.0f, 180.0f, 1000.0f, 700.0f);
        JBro::ScreenArea area;
        Check(JBro::GetScreenArea(frame, area) && Close(area.width, 1000.0f) && Close(area.height, 700.0f)
                && Close(area.x, 0.0f),
            "without an area the whole target is drawn");
        frame.areaX = 20.0f;
        frame.areaY = 80.0f;
        frame.areaWidth = 960.0f;
        frame.areaHeight = 540.0f;
        Check(JBro::GetScreenArea(frame, area) && Close(area.x, 20.0f) && Close(area.width, 960.0f),
            "with an area that rectangle is drawn");

        // 화면 레이어의 크기는 그 사각형의 가로세로비를 따른다(1000x700 이 아니라 960x540).
        JBro::ScreenExtent extent;
        Check(JBro::ComputeScreenExtent(JBro::ScreenScaleMode::FixedHeight, frame, extent)
                && Close(extent.halfHeight, 90.0f) && Close(extent.halfWidth, 160.0f),
            "screen layers measure the drawn rectangle, not the whole target");
        Check(JBro::ComputeScreenExtent(JBro::ScreenScaleMode::ConstantPixel, frame, extent)
                && Close(extent.halfWidth, 480.0f) && Close(extent.halfHeight, 270.0f),
            "one pixel per unit counts the drawn rectangle's pixels");

        float x = 0.0f;
        float y = 0.0f;
        Check(JBro::ScreenPixelToNormalized(frame, 500.0f, 350.0f, x, y) && Close(x, 0.0f) && Close(y, 0.0f),
            "the middle of the rectangle is the middle of the view");
        Check(JBro::ScreenPixelToNormalized(frame, 20.0f, 80.0f, x, y) && Close(x, -1.0f) && Close(y, 1.0f),
            "its top-left corner is (-1, 1)");
        Check(JBro::ScreenPixelToNormalized(frame, 10.0f, 350.0f, x, y) && x < -1.0f,
            "a pixel on the bar lies outside the view");
        float pixelX = 0.0f;
        float pixelY = 0.0f;
        Check(JBro::NormalizedToScreenPixel(frame, 1.0f, -1.0f, pixelX, pixelY)
                && Close(pixelX, 980.0f) && Close(pixelY, 620.0f),
            "and back: (1, -1) is the rectangle's bottom-right corner");
    }

    JBro::RenderCamera2D Camera(JBro::Component::CameraProjection2D projection)
    {
        JBro::RenderCamera2D camera;
        camera.projection = projection;
        camera.orthographicSize = 5.0f;
        camera.pixelsPerUnit = 16.0f;
        camera.nearPlane = -10.0f;
        camera.farPlane = 10.0f;
        return camera;
    }

    void TestOnlyDrawableCamerasAreDrawable()
    {
        using JBro::Component::CameraProjection2D;
        const float nan = std::numeric_limits<float>::quiet_NaN();
        Check(JBro::IsDrawableCamera2D(Camera(CameraProjection2D::Orthographic)), "a sane orthographic camera draws");
        Check(JBro::IsDrawableCamera2D(Camera(CameraProjection2D::PixelPerfect)), "a sane pixel perfect camera draws");

        JBro::RenderCamera2D camera = Camera(CameraProjection2D::Orthographic);
        camera.orthographicSize = 0.0f;
        Check(false == JBro::IsDrawableCamera2D(camera), "a zero size cannot be drawn");
        camera.orthographicSize = -3.0f;
        Check(false == JBro::IsDrawableCamera2D(camera), "nor a negative one");
        camera.orthographicSize = nan;
        Check(false == JBro::IsDrawableCamera2D(camera), "nor a NaN one");
        camera.orthographicSize = 5.0f;
        camera.pixelsPerUnit = 0.0f;
        Check(JBro::IsDrawableCamera2D(camera), "an orthographic camera does not care about pixelsPerUnit");

        camera = Camera(CameraProjection2D::PixelPerfect);
        camera.pixelsPerUnit = 0.0f;
        Check(false == JBro::IsDrawableCamera2D(camera), "pixel perfect with zero pixels per unit cannot be drawn");
        camera.pixelsPerUnit = 16.0f;
        camera.orthographicSize = 0.0f;
        Check(JBro::IsDrawableCamera2D(camera), "pixel perfect does not care about orthographicSize");

        camera = Camera(CameraProjection2D::Orthographic);
        camera.nearPlane = 10.0f;
        Check(false == JBro::IsDrawableCamera2D(camera), "nearPlane equal to farPlane is an empty depth range");
        camera.nearPlane = 20.0f;
        Check(false == JBro::IsDrawableCamera2D(camera), "nearPlane past farPlane is backwards");
        camera.nearPlane = nan;
        Check(false == JBro::IsDrawableCamera2D(camera), "a NaN plane cannot be drawn");

        camera = Camera(static_cast<CameraProjection2D>(7));
        Check(false == JBro::IsDrawableCamera2D(camera), "a projection the engine does not know is not drawn");
    }

    void TestTheCameraViewMatchesItsProjection()
    {
        using JBro::Component::CameraProjection2D;
        JBro::ScreenSpaceFrame frame = Frame(320.0f, 180.0f, 1000.0f, 700.0f);
        JBro::RenderCamera2D ortho = Camera(CameraProjection2D::Orthographic);
        ortho.view.m31 = -1.03f;
        ortho.view.m32 = -0.51f;
        JBro::CameraView2D view;
        Check(JBro::ComputeCameraView2D(ortho, frame, view) && Close(view.halfHeight, 5.0f)
                && Close(view.halfWidth, 5.0f * 1000.0f / 700.0f),
            "orthographic: half height is the size and the width follows the target");
        Check(Close(view.view.m31, -1.03f) && Close(view.view.m32, -0.51f), "an orthographic view is not snapped");

        JBro::RenderCamera2D pixel = ortho;
        pixel.projection = CameraProjection2D::PixelPerfect;
        JBro::ApplyCameraArea(&pixel, frame);
        Check(Close(frame.areaWidth, 960.0f) && Close(frame.areaX, 20.0f), "a pixel perfect camera letterboxes the frame");
        Check(JBro::ComputeCameraView2D(pixel, frame, view) && Close(view.halfWidth, 10.0f) && Close(view.halfHeight, 5.625f),
            "pixel perfect: the reference in source pixels, 320 / (2 x 16) by 180 / (2 x 16)");
        // -1.03 x 16 = -16.48 -> -16, -0.51 x 16 = -8.16 -> -8: 원본 1 픽셀 격자에 붙는다.
        Check(Close(view.view.m31, -1.0f) && Close(view.view.m32, -0.5f), "the view moves in whole source pixels");

        // 사각형은 투영을 따라 걸리고 풀린다.
        JBro::ApplyCameraArea(&ortho, frame);
        Check(Close(frame.areaWidth, 0.0f) && Close(frame.areaX, 0.0f), "an orthographic camera clears the letterbox");
        JBro::ApplyCameraArea(&pixel, frame);
        JBro::ApplyCameraArea(nullptr, frame);
        Check(Close(frame.areaWidth, 0.0f), "no camera clears it too");
        pixel.pixelsPerUnit = 0.0f;
        JBro::ApplyCameraArea(&pixel, frame);
        Check(Close(frame.areaWidth, 0.0f), "an undrawable pixel perfect camera does not letterbox");
        Check(false == JBro::ComputeCameraView2D(pixel, frame, view), "and has no view");
        Check(false == JBro::ComputeCameraView2D(ortho, Frame(320.0f, 180.0f, 0.0f, 0.0f), view), "no target, no view");
    }

    void TestUndrawableCamerasAreSkippedWhenChoosing()
    {
        JBro::Canvas canvas(JBro::CreateDefaultAllocator());
        JBro::Object::GameObject* broken = canvas.CreateObject("broken");
        JBro::Object::GameObject* spare = canvas.CreateObject("spare");
        Check(canvas.AttachComponent<JBro::Component::Transform2D>(broken) != nullptr
                && canvas.AttachComponent<JBro::Component::Transform2D>(spare) != nullptr, "the cameras need transforms");
        auto* brokenCamera = canvas.AttachComponent<JBro::Component::Camera2D>(broken);
        auto* spareCamera = canvas.AttachComponent<JBro::Component::Camera2D>(spare);
        Check(brokenCamera != nullptr && spareCamera != nullptr, "both cameras attach");
        brokenCamera->primary = true;
        brokenCamera->orthographicSize = 0.0f;
        spareCamera->orthographicSize = 3.0f;
        JBro::System::Transform2DSystem transforms;
        transforms.Update(canvas, 0.0f);

        JBro::RenderCamera2D chosen;
        std::uint32_t unusable = 0;
        Check(JBro::System::Camera2DSystem::SelectCamera(canvas, chosen, &unusable) && chosen.owner == spare
                && Close(chosen.orthographicSize, 3.0f),
            "a primary camera that cannot draw is passed over for the next one");
        Check(unusable == 1, "and counted, so the game view can say why");

        spareCamera->SetEnabled(false);
        unusable = 0;
        Check(false == JBro::System::Camera2DSystem::SelectCamera(canvas, chosen, &unusable) && unusable == 1,
            "with only the broken one left there is no camera, not a failed frame");

        brokenCamera->orthographicSize = 4.0f;
        unusable = 0;
        Check(JBro::System::Camera2DSystem::SelectCamera(canvas, chosen, &unusable) && chosen.owner == broken && unusable == 0,
            "fixed, it is chosen again");
        brokenCamera->projection = JBro::Component::CameraProjection2D::PixelPerfect;
        brokenCamera->pixelsPerUnit = 32.0f;
        Check(JBro::System::Camera2DSystem::SelectCamera(canvas, chosen) && Close(chosen.pixelsPerUnit, 32.0f)
                && chosen.projection == JBro::Component::CameraProjection2D::PixelPerfect,
            "pixelsPerUnit reaches the render camera");
    }
}

int RunCameraView2DTests()
{
    TestThePixelPerfectAreaIsAnIntegerScaleInTheMiddle();
    TestTheScreenAreaDrivesExtentsAndPointers();
    TestOnlyDrawableCamerasAreDrawable();
    TestTheCameraViewMatchesItsProjection();
    TestUndrawableCamerasAreSkippedWhenChoosing();
    std::cout << "Camera view 2D tests passed.\n";
    return 0;
}
