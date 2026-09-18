#include <JBro/D3D11RHI/D3D11RHI.h>
#include <JBro/D3D12RHI/D3D12RHI.h>
#include <JBro/VulkanRHI/VulkanRHI.h>
#include <JBro/Framework3D/Component/Camera3D.h>
#include <JBro/Framework3D/Component/MeshRenderer3D.h>
#include <JBro/Framework3D/Component/Transform3D.h>
#include <JBro/Framework3DSystem/Framework3D.h>
#include <JBro/Framework3DSystem/Math3DMatrix.h>
#include <JBro/Framework3DSystem/Rendering/MeshLibrary.h>
#include <JBro/Graphics/Renderer.h>
#include <JBro/Platform/WindowsPlatform.h>
#include <JBro/Types/Array.h>

#include <cmath>
#include <cstddef>
#include <iostream>
#include <stdexcept>

// 3D 프레임워크가 정육면체 하나를 텍스처에 실제로 그리는지 픽셀로 본다(framework3d-plan §2.5).
// 스프라이트 픽셀 테스트와 같은 준비다 - 렌더러의 메시 파이프라인, 깊이 텍스처, D3D12 깊이 첨부,
// 프레임워크의 추출·브리지가 한 줄에 서야 색이 나온다.
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

    struct Pixel
    {
        float r = 0.0f;
        float g = 0.0f;
        float b = 0.0f;
        float a = 0.0f;
    };

    Pixel ReadPixel(const JBro::Array<std::byte>& image, std::uint32_t rowPitch,
        std::uint32_t x, std::uint32_t y)
    {
        const std::size_t offset = static_cast<std::size_t>(y) * rowPitch
            + static_cast<std::size_t>(x) * 4;
        const auto* bytes = reinterpret_cast<const unsigned char*>(image.Data() + offset);
        Pixel pixel;
        pixel.b = bytes[0] / 255.0f;
        pixel.g = bytes[1] / 255.0f;
        pixel.r = bytes[2] / 255.0f;
        pixel.a = bytes[3] / 255.0f;
        return pixel;
    }

    bool Near(float a, float b, float tolerance = 0.03f)
    {
        return std::fabs(a - b) < tolerance;
    }

    constexpr std::uint32_t TargetWidth = 96;
    constexpr std::uint32_t TargetHeight = 64;

    template <typename TModule>
    struct Stage
    {
        JBro::WindowsPlatform platform;
        TModule rhi;
        JBro::JMemoryContext memory;
        JBro::WindowHandle window;
        JBro::Renderer renderer;
        JBro::TextureHandle target;
        bool ready = false;

        bool Open()
        {
            Check(platform.Initialize(memory), "the platform must initialize");
            if (false == rhi.Initialize(memory))
            {
                platform.Shutdown();
                return false;
            }
            JBro::WindowDesc windowDesc;
            constexpr char title[] = "JBro mesh probe";
            windowDesc.title = {title, sizeof(title) - 1};
            windowDesc.width = 64;
            windowDesc.height = 64;
            windowDesc.visible = false;
            window = platform.OpenPlatformWindow(windowDesc);
            Check(window.value != 0, "the probe window must open");
            JBro::RendererConfig config;
            config.api = rhi.GetApi();
            config.surface = platform.CreateSurface(window);
            config.surfaceExtent = {64, 64};
            config.maxSpriteSubmissions = 4;
            config.maxMeshSubmissions = 8;
            config.presentMode = JBro::PresentMode::Immediate;
            Check(renderer.Initialize(rhi, config), "the renderer must initialize");
            JBro::TextureDesc targetDesc;
            targetDesc.extent = {TargetWidth, TargetHeight};
            targetDesc.format = JBro::TextureFormat::BGRA8Unorm;
            targetDesc.usage = JBro::TextureUsage::RenderTarget | JBro::TextureUsage::Sampled;
            target = renderer.GetDevice()->CreateTexture(targetDesc);
            Check(target.IsValid(), "the target texture must be created");
            ready = true;
            return true;
        }

        // 프레임워크를 한 프레임 돌려 타깃에 그리고 되읽는다.
        void RenderOnce(JBro::Framework3D& framework, JBro::Array<std::byte>& image, JBro::TextureReadback& readback)
        {
            JBro::FrameTarget frameTarget;
            frameTarget.texture = target;
            frameTarget.extent = {TargetWidth, TargetHeight};
            Check(renderer.BeginFrame(frameTarget) == JBro::FrameStatus::Ready, "the frame must begin");
            framework.Update(1.0f / 60.0f);
            Check(framework.Render() == JBro::RenderResult::Submitted, "the framework must submit its view");
            Check(renderer.EndFrame() == JBro::FrameStatus::Ready, "the frame must finish");
            image.Resize(TargetWidth * TargetHeight * 4);
            Check(renderer.GetDevice()->ReadTexture(target, image.Data(), image.Size(), readback),
                "the target must read back");
        }

        void Close()
        {
            if (false == ready)
            {
                return;
            }
            renderer.GetDevice()->DestroyTexture(target);
            renderer.Shutdown();
            rhi.Shutdown();
            platform.ClosePlatformWindow(window);
            platform.PumpEvents();
            platform.Shutdown();
            ready = false;
        }
    };

    JBro::GameObject* PlaceCube(JBro::Canvas& canvas, const char* name, const JBro::Vec3& position,
        const JBro::Vec3& scale, const JBro::Color& tint)
    {
        JBro::GameObject* object = canvas.CreateObject(name);
        auto* transform = canvas.AttachComponent<JBro::Component::Transform3D>(object);
        transform->position = position;
        transform->scale = scale;
        auto* mesh = canvas.AttachComponent<JBro::Component::MeshRenderer3D>(object);
        mesh->meshId = JBro::MeshLibrary::BuiltinCubeId();
        mesh->tint = tint;
        return object;
    }

    // **정육면체가 화면에 있고, 배경은 클리어 색이다.** 가운데 픽셀이 tint 의 비율(1 : 0.5 : 0.25)을
    // 지키면 셰이더가 tint 를 읽었고 조명이 셋을 같은 만큼 깎은 것이다. 모서리는 카메라 클리어 색이다.
    template <typename TModule>
    void TestACubeIsDrawnWhereTheCameraLooks()
    {
        Stage<TModule> stage;
        if (false == stage.Open())
        {
            std::cout << "  [skip] no device for this API; mesh pixels not verified" << std::endl;
            return;
        }
        JBro::Framework3D framework;
        JBro::FrameworkContext context;
        context.renderer = &stage.renderer;
        Check(framework.Initialize(context), "the 3D framework must initialize with the renderer");
        Check(stage.renderer.GetMeshCount() == 1, "initializing must upload the builtin cube");
        JBro::Canvas& canvas = *framework.GetCanvas();

        JBro::GameObject* eye = canvas.CreateObject("eye");
        canvas.AttachComponent<JBro::Component::Transform3D>(eye)->position = {0.0f, 0.0f, 3.0f};
        auto* camera = canvas.AttachComponent<JBro::Component::Camera3D>(eye);
        camera->primary = true;
        camera->clearColor = {0.0f, 0.0f, 0.0f, 1.0f};
        PlaceCube(canvas, "box", {0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, {1.0f, 0.5f, 0.25f, 1.0f});

        JBro::Array<std::byte> image;
        JBro::TextureReadback readback;
        stage.RenderOnce(framework, image, readback);
        Check(readback.extent.width == TargetWidth && readback.extent.height == TargetHeight,
            "the readback must describe the target");
        const JBro::RendererFrameStats stats = stage.renderer.GetLastFrameStats();
        Check(stats.viewCount == 1 && stats.meshCount == 1 && stats.droppedMeshCount == 0,
            "one view with one mesh must be recorded and none dropped");

        const Pixel center = ReadPixel(image, readback.rowPitch, TargetWidth / 2, TargetHeight / 2);
        Check(center.r > 0.2f, "the cube must cover the middle of the frame");
        Check(Near(center.g / center.r, 0.5f, 0.05f) && Near(center.b / center.r, 0.25f, 0.05f),
            "and carry the tint's proportions through the lighting");
        Check(center.r < 0.999f, "lit by one directional light, the front face is not full brightness");
        // 앞면의 법선 (0,0,1) 과 고정 광원 (0.4,0.8,0.45) 의 내적은 0.449 이고 조명은 0.25 + 0.75 x 0.449 = 0.587 이다.
        // 앞면이 컬링돼 뒷면 안쪽이 보이면 내적이 음수라 앰비언트 0.25 만 남는다 - 앞면 판정이 뒤집힌 것을 여기서 잡는다.
        Check(Near(center.r, 0.587f, 0.03f),
            "the face towards the camera must carry the light's Lambert term, not just the ambient of a back face");
        const Pixel corner = ReadPixel(image, readback.rowPitch, 2, 2);
        Check(Near(corner.r, 0.0f) && Near(corner.g, 0.0f) && Near(corner.b, 0.0f),
            "the corner must stay at the camera's clear colour");
        Check(stage.renderer.GetDevice()->GetValidationErrorCount() == 0,
            "and the debug layer must have stayed quiet through the depth pass");

        framework.Shutdown();
        Check(stage.renderer.GetMeshCount() == 0, "shutting the framework down must unregister its meshes");
        stage.Close();
    }

    // **깊이 버퍼가 순서를 대신한다.** 가까운 붉은 상자를 먼저, 먼 초록 상자를 나중에 제출해도 가운데는
    // 붉어야 한다 - 깊이 없이는 나중에 그린 초록이 덮는다.
    template <typename TModule>
    void TestANearerCubeHidesAFartherOne()
    {
        Stage<TModule> stage;
        if (false == stage.Open())
        {
            std::cout << "  [skip] no device for this API; depth not verified" << std::endl;
            return;
        }
        JBro::Framework3D framework;
        JBro::FrameworkContext context;
        context.renderer = &stage.renderer;
        Check(framework.Initialize(context), "the 3D framework must initialize with the renderer");
        JBro::Canvas& canvas = *framework.GetCanvas();
        JBro::GameObject* eye = canvas.CreateObject("eye");
        canvas.AttachComponent<JBro::Component::Transform3D>(eye)->position = {0.0f, 0.0f, 4.0f};
        auto* camera = canvas.AttachComponent<JBro::Component::Camera3D>(eye);
        camera->primary = true;
        camera->clearColor = {0.0f, 0.0f, 0.0f, 1.0f};
        // 가까운 것을 먼저 붙인다 - 추출 순서가 붙인 순서라 먼저 그려진다.
        PlaceCube(canvas, "near", {0.0f, 0.0f, 1.0f}, {0.6f, 0.6f, 0.6f}, {1.0f, 0.0f, 0.0f, 1.0f});
        PlaceCube(canvas, "far", {0.0f, 0.0f, -2.0f}, {3.0f, 3.0f, 3.0f}, {0.0f, 1.0f, 0.0f, 1.0f});

        JBro::Array<std::byte> image;
        JBro::TextureReadback readback;
        stage.RenderOnce(framework, image, readback);
        Check(stage.renderer.GetLastFrameStats().meshCount == 2, "both cubes must be submitted");
        const Pixel center = ReadPixel(image, readback.rowPitch, TargetWidth / 2, TargetHeight / 2);
        Check(center.r > 0.2f && center.g < 0.05f,
            "the nearer red cube must win the middle even though the green one was drawn after it");
        // 작은 상자 밖, 큰 상자 안의 자리다. 카메라(z=4)에서 붉은 상자(z=1, 반 크기 0.3)는 세로 절반의
        // 17%(약 5px), 초록 상자(z=-2, 반 크기 1.5)는 43%(약 14px)를 덮는다 - 가운데서 10px 위는 초록만이다.
        const Pixel edge = ReadPixel(image, readback.rowPitch, TargetWidth / 2, TargetHeight / 2 - 10);
        Check(edge.g > 0.2f && edge.r < 0.05f, "away from the small cube the big green one shows");
        Check(stage.renderer.GetDevice()->GetValidationErrorCount() == 0,
            "and the debug layer must have stayed quiet");

        // 같은 렌더러로 다음 프레임을 백버퍼에 그린다. 깊이 텍스처는 크기마다 따로라 둘이 공존한다.
        Check(stage.renderer.BeginFrame() == JBro::FrameStatus::Ready, "a back buffer frame must begin");
        framework.Update(1.0f / 60.0f);
        Check(framework.Render() == JBro::RenderResult::Submitted, "the framework must submit to the back buffer too");
        Check(stage.renderer.EndFrame() == JBro::FrameStatus::Ready, "and that frame must present");
        JBro::Array<std::byte> windowImage;
        windowImage.Resize(64 * 64 * 4);
        JBro::TextureReadback windowReadback;
        Check(stage.renderer.ReadBackBuffer(windowImage.Data(), windowImage.Size(), windowReadback),
            "the back buffer must read back");
        const Pixel windowCenter = ReadPixel(windowImage, windowReadback.rowPitch, 32, 32);
        Check(windowCenter.r > 0.2f && windowCenter.g < 0.05f, "with the red cube in its middle as well");
        Check(stage.renderer.GetDevice()->GetValidationErrorCount() == 0,
            "and no validation error from switching depth targets");

        framework.Shutdown();
        stage.Close();
    }
}

namespace
{
    // **스프라이트가 메시 위에 얹힌다.** 메시가 있는 뷰는 깊이가 달린 패스라, 스프라이트 파이프라인도 그 깊이
    // 포맷을 알아야 한다(깊이는 보지도 쓰지도 않고). 포맷이 다른 파이프라인을 깊이 패스에 걸면 검증 레이어가
    // 말하고 드라이버에 따라 그림이 깨진다 - 세 백엔드에서 픽셀과 검증 오류 수를 함께 본다.
    template <typename TModule>
    void TestSpritesLayOverMeshesInTheSameView()
    {
        Stage<TModule> stage;
        if (false == stage.Open())
        {
            std::cout << "  [skip] no device for this API; sprites over meshes not verified" << std::endl;
            return;
        }
        JBro::MeshLibrary library;
        Check(library.Initialize(&stage.renderer), "the mesh library must upload its cube");
        JBro::CameraParams camera;
        camera.view = JBro::MakeViewMatrix({0.0f, 0.0f, 3.0f}, {});
        Check(JBro::MakePerspectiveMatrix(60.0f * 3.14159265f / 180.0f, 1.0f, 0.1f, 100.0f, camera.projection),
            "the perspective matrix must build");
        camera.viewport.width = static_cast<float>(TargetWidth);
        camera.viewport.height = static_cast<float>(TargetHeight);
        camera.clearColor[0] = 0.0f;
        camera.clearColor[1] = 0.0f;
        camera.clearColor[2] = 0.0f;
        camera.clearColor[3] = 1.0f;

        JBro::MeshSubmit cube;
        cube.mesh = library.Resolve(JBro::MeshLibrary::BuiltinCubeId());
        cube.tint[0] = 1.0f;
        cube.tint[1] = 0.0f;
        cube.tint[2] = 0.0f;
        // z=0 평면에서 카메라(z=3, 세로 시야 60도)가 보는 반높이는 1.73 이다. 왼쪽 위 구석에 파란 스프라이트를 놓는다.
        JBro::SpriteSubmit sprite;
        sprite.world.linear[0] = 0.8f;
        sprite.world.linear[3] = 0.8f;
        sprite.world.translation[0] = -1.2f;
        sprite.world.translation[1] = 1.2f;
        sprite.tint[0] = 0.0f;
        sprite.tint[1] = 0.0f;
        sprite.tint[2] = 1.0f;

        JBro::FrameTarget frameTarget;
        frameTarget.texture = stage.target;
        frameTarget.extent = {TargetWidth, TargetHeight};
        Check(stage.renderer.BeginFrame(frameTarget) == JBro::FrameStatus::Ready, "the frame must begin");
        Check(stage.renderer.BeginView(camera), "the view must open");
        Check(stage.renderer.SubmitMesh(cube), "the cube must submit");
        Check(stage.renderer.SubmitSprite(sprite), "the sprite must submit into the same view");
        Check(stage.renderer.EndView(), "the view must close");
        Check(stage.renderer.EndFrame() == JBro::FrameStatus::Ready, "the frame with both must record and present");

        JBro::Array<std::byte> image;
        image.Resize(TargetWidth * TargetHeight * 4);
        JBro::TextureReadback readback;
        Check(stage.renderer.GetDevice()->ReadTexture(stage.target, image.Data(), image.Size(), readback),
            "the target must read back");
        const Pixel center = ReadPixel(image, readback.rowPitch, TargetWidth / 2, TargetHeight / 2);
        Check(center.r > 0.2f && center.b < 0.05f, "the cube must still fill the middle");
        const Pixel corner = ReadPixel(image, readback.rowPitch, 8, 8);
        Check(corner.b > 0.9f && corner.r < 0.05f, "the sprite must paint the corner over the depth pass");
        Check(stage.renderer.GetDevice()->GetValidationErrorCount() == 0,
            "and the debug layer must accept the sprite pipeline inside the depth pass");

        library.Shutdown();
        stage.Close();
    }
}

int RunMeshPixelTests()
{
    TestACubeIsDrawnWhereTheCameraLooks<JBro::D3D12RHIModule>();
    TestANearerCubeHidesAFartherOne<JBro::D3D12RHIModule>();
    TestACubeIsDrawnWhereTheCameraLooks<JBro::D3D11RHIModule>();
    TestACubeIsDrawnWhereTheCameraLooks<JBro::VulkanRHIModule>();
    TestANearerCubeHidesAFartherOne<JBro::D3D11RHIModule>();
    TestANearerCubeHidesAFartherOne<JBro::VulkanRHIModule>();
    TestSpritesLayOverMeshesInTheSameView<JBro::D3D12RHIModule>();
    TestSpritesLayOverMeshesInTheSameView<JBro::D3D11RHIModule>();
    TestSpritesLayOverMeshesInTheSameView<JBro::VulkanRHIModule>();
    std::cout << "Mesh pixel tests passed.\n";
    return 0;
}
