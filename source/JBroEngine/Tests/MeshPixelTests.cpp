#include "TestCases.h"
#include "TestClock.h"
#include <JBro/D3D11RHI/D3D11RHI.h>
#include <JBro/D3D12RHI/D3D12RHI.h>
#include <JBro/VulkanRHI/VulkanRHI.h>
#include <JBro/Canvas/Layer.h>
#include <JBro/Framework3D/Component/Camera3D.h>
#include <JBro/Framework3D/Component/MeshRenderer3D.h>
#include <JBro/Framework3D/Component/Transform3D.h>
#include <JBro/Framework3DSystem/Framework3D.h>
#include <JBro/Framework3DSystem/Math3DMatrix.h>
#include <JBro/Framework3DSystem/Rendering/MeshLibrary.h>
#include <JBro/Graphics/Renderer.h>
#include <JBro/Platform/WindowsPlatform.h>
#include <JBro/Types/Array.h>
#include <JBro/Types/Math3D.h>
#include <JBro/Asset/Asset.h>
#include <JBro/Asset/AssetRegistry.h>
#include "TestModels.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/Int.h>
#include <JBro/Types/UInt.h>
#include <JBro/Types/ValueMath.h>

// 3D 프레임워크가 정육면체 하나를 텍스처에 실제로 그리는지 픽셀로 본다(framework3d-plan §2.5).
// 스프라이트 픽셀 테스트와 같은 준비다 - 렌더러의 메시 파이프라인, 깊이 텍스처, D3D12 깊이 첨부,
// 프레임워크의 추출·브리지가 한 줄에 서야 색이 나온다.
namespace
{
    void Check(JBro::Bool condition, const char* message)
    {
        if (false == condition)
        {
            std::cout << "test failure: " << message << '\n';
            throw std::runtime_error(message);
        }
    }

    struct Pixel
    {
        JBro::Float r = 0.0f;
        JBro::Float g = 0.0f;
        JBro::Float b = 0.0f;
        JBro::Float a = 0.0f;
    };

    // **`Standard` 셰이더의 식을 CPU 로 다시 쓴 것**(material-plan 3 단계, `BuiltinMesh.hlsl` 의 `PSMain`). 그림 칸이 없는 재질 한 점의 기대색이다 -
    // 선형 기본색(재질 × 틴트)·법선·월드 위치·카메라 위치에서 고정 방향광의 GGX·Lambert 와 주변광을 더하고 sRGB 로 싼다.
    // 손으로 적은 숫자 대신 이것과 견주어, 셰이더의 항 하나가 틀리면 그 픽셀이 어긋난다.
    float EncodeSrgb(float linear)
    {
        const float c = linear < 0.0f ? 0.0f : (linear > 1.0f ? 1.0f : linear);
        return c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
    }

    Pixel ExpectStandard(const float base[3], JBro::Vector3 normal, JBro::Vector3 position, JBro::Vector3 camera, float metallic, float roughnessFactor)
    {
        constexpr float Pi = 3.14159265f;
        const JBro::Vector3 light = JBro::Normalize(JBro::Vector3{0.4f, 0.8f, 0.45f});
        normal = JBro::Normalize(normal);
        const JBro::Vector3 view = JBro::Normalize(JBro::Subtract(camera, position));
        const JBro::Vector3 halfway = JBro::Normalize(JBro::Add(light, view));
        const float nl = std::max(0.0f, JBro::Dot(normal, light).Get());
        const float nv = std::max(JBro::Dot(normal, view).Get(), 0.0001f);
        const float nh = std::max(0.0f, JBro::Dot(normal, halfway).Get());
        const float vh = std::max(0.0f, JBro::Dot(view, halfway).Get());
        const float roughness = std::min(std::max(roughnessFactor, 0.045f), 1.0f);
        const float alpha = roughness * roughness;
        const float alpha2 = alpha * alpha;
        const float denominator = nh * nh * (alpha2 - 1.0f) + 1.0f;
        const float distribution = alpha2 / (Pi * denominator * denominator);
        const float k = (roughness + 1.0f) * (roughness + 1.0f) / 8.0f;
        const float geometry = (nv / (nv * (1.0f - k) + k)) * (nl / (nl * (1.0f - k) + k));
        float out[3] = {};
        for (int channel = 0; channel < 3; ++channel)
        {
            const float reflectance = 0.04f + (base[channel] - 0.04f) * metallic;
            const float fresnel = reflectance + (1.0f - reflectance) * std::pow(1.0f - vh, 5.0f);
            const float specular = distribution * geometry * fresnel / std::max(4.0f * nv * nl, 0.0001f);
            const float diffuse = (1.0f - fresnel) * (1.0f - metallic) * base[channel];
            const float direct = (diffuse + Pi * specular) * 0.75f * nl;
            out[channel] = EncodeSrgb(0.25f * base[channel] + direct);
        }
        return Pixel{out[0], out[1], out[2], 1.0f};
    }

    Pixel ReadPixel(const JBro::Array<std::byte>& image, JBro::UInt32 rowPitch,
        JBro::UInt32 x, JBro::UInt32 y)
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

    JBro::Bool Near(JBro::Float a, JBro::Float b, JBro::Float tolerance = 0.03f)
    {
        return std::fabs(a - b) < tolerance;
    }

    constexpr JBro::UInt32 TargetWidth = 96;
    constexpr JBro::UInt32 TargetHeight = 64;

    template <typename TModule>
    struct Stage
    {
        JBro::WindowsPlatform platform;
        TModule rhi;
        JBro::JMemoryContext memory;
        JBro::WindowHandle window;
        JBro::Renderer renderer;
        JBro::TextureHandle target;
        JBro::Bool ready = false;

        JBro::Bool Open(JBro::Bool validation = false)
        {
            Check(platform.Initialize(memory), "the platform must initialize");
            if (false == rhi.Initialize(memory))
            {
                platform.Shutdown();
                return false;
            }
            JBro::WindowDesc windowDesc;
            constexpr char title[] = "JBro mesh probe";
            windowDesc.title = {title, static_cast<JBro::UInt32>(sizeof(title) - 1)};
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
            config.validation = validation;
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
            // **호스트와 같은 차례다** - 갱신(Update)이 먼저, 렌더 프레임이 나중이다(`EngineInstance::Tick`). 렌더러는 프레임 안의 등록을 거절하므로
            // 갱신에서 처음 만난 모델 메시를 올리는 길(D-342)은 이 차례에서만 선다. 전에는 거꾸로 불러도 빌트인만 있어 드러나지 않았다.
            JBro::Testing::Tick(framework, 1.0f / 60.0f);
            Check(renderer.BeginFrame(frameTarget) == JBro::FrameStatus::Ready, "the frame must begin");
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

    JBro::Object::GameObject* PlaceCube(JBro::Canvas& canvas, const char* name, const JBro::Vector3& position,
        const JBro::Vector3& scale, const JBro::Color& tint)
    {
        JBro::Object::GameObject* object = canvas.CreateObject(name);
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
        JBro::Testing::AttachClock(context);
        context.renderer = &stage.renderer;
        Check(framework.Initialize(context), "the 3D framework must initialize with the renderer");
        Check(stage.renderer.GetMeshCount() == JBro::MeshLibrary::GetBuiltinCount(), "initializing must upload every builtin shape");
        JBro::Canvas& canvas = *framework.GetCanvas();

        JBro::Object::GameObject* eye = canvas.CreateObject("eye");
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
        // 재질이 없는 메시는 흰 기본 재질(metallic 0, roughness 1)에 틴트를 곱한 `Standard` 다(material-plan 3 단계). 앞면 한가운데 (0, 0, 0.5) 를
        // 카메라 (0, 0, 3) 에서 본 기대색과 채널마다 견준다. 앞면이 컬링돼 뒷면 안쪽이 보이면 빛의 항이 빠져 주변광만 남는다 - 앞면 판정이 뒤집힌 것도 여기서 잡힌다.
        const float tint[3] = {1.0f, 0.5f, 0.25f};
        const Pixel expected = ExpectStandard(tint, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.5f}, {0.0f, 0.0f, 3.0f}, 0.0f, 1.0f);
        std::cout << "  front face: " << center.r.Get() << " " << center.g.Get() << " " << center.b.Get()
                  << " expected " << expected.r.Get() << " " << expected.g.Get() << " " << expected.b.Get() << std::endl;
        Check(Near(center.r, expected.r, 0.02f) && Near(center.g, expected.g, 0.02f) && Near(center.b, expected.b, 0.02f),
            "the face towards the camera carries the Standard shading of the default material, tinted and encoded to sRGB");
        Check(center.r < 0.999f, "lit by one directional light, the front face is not full brightness");
        Check(center.r > EncodeSrgb(0.25f) + 0.05f, "and is lit by the light, not just the ambient of a back face");
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
        JBro::Testing::AttachClock(context);
        context.renderer = &stage.renderer;
        Check(framework.Initialize(context), "the 3D framework must initialize with the renderer");
        JBro::Canvas& canvas = *framework.GetCanvas();
        JBro::Object::GameObject* eye = canvas.CreateObject("eye");
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
        // 메시 픽셀은 "다른 채널이 0" 이 아니라 "어느 색이 이기는가" 로 본다 - 유전체의 반사광은 흰색이라(F0 0.04, material-plan 3 단계) 빨간 정육면체에도 초록이 조금 든다.
        Check(center.r > 0.2f && center.r > center.g * 2.0f,
            "the nearer red cube must win the middle even though the green one was drawn after it");
        // 작은 상자 밖, 큰 상자 안의 자리다. 카메라(z=4)에서 붉은 상자(z=1, 반 크기 0.3)는 세로 절반의
        // 17%(약 5px), 초록 상자(z=-2, 반 크기 1.5)는 43%(약 14px)를 덮는다 - 가운데서 10px 위는 초록만이다.
        const Pixel edge = ReadPixel(image, readback.rowPitch, TargetWidth / 2, TargetHeight / 2 - 10);
        Check(edge.g > 0.2f && edge.g > edge.r * 2.0f, "away from the small cube the big green one shows");
        Check(stage.renderer.GetDevice()->GetValidationErrorCount() == 0,
            "and the debug layer must have stayed quiet");

        // 같은 렌더러로 다음 프레임을 백버퍼에 그린다. 깊이 텍스처는 크기마다 따로라 둘이 공존한다.
        Check(stage.renderer.BeginFrame() == JBro::FrameStatus::Ready, "a back buffer frame must begin");
        JBro::Testing::Tick(framework, 1.0f / 60.0f);
        Check(framework.Render() == JBro::RenderResult::Submitted, "the framework must submit to the back buffer too");
        Check(stage.renderer.EndFrame() == JBro::FrameStatus::Ready, "and that frame must present");
        JBro::Array<std::byte> windowImage;
        windowImage.Resize(64 * 64 * 4);
        JBro::TextureReadback windowReadback;
        Check(stage.renderer.ReadBackBuffer(windowImage.Data(), windowImage.Size(), windowReadback),
            "the back buffer must read back");
        const Pixel windowCenter = ReadPixel(windowImage, windowReadback.rowPitch, 32, 32);
        Check(windowCenter.r > 0.2f && windowCenter.r > windowCenter.g * 2.0f, "with the red cube in its middle as well");
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
        camera.viewport.width = static_cast<JBro::Float>(TargetWidth);
        camera.viewport.height = static_cast<JBro::Float>(TargetHeight);
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
        Check(center.r > 0.2f && center.r > center.b * 2.0f, "the cube must still fill the middle");
        const Pixel corner = ReadPixel(image, readback.rowPitch, 8, 8);
        Check(corner.b > 0.9f && corner.r < 0.05f, "the sprite must paint the corner over the depth pass");
        Check(stage.renderer.GetDevice()->GetValidationErrorCount() == 0,
            "and the debug layer must accept the sprite pipeline inside the depth pass");

        library.Shutdown();
        stage.Close();
    }
}

namespace
{
    // **월드 텍스트는 메시에 가려지고 어느 방향이든 향한다**(D-222). 카메라(z=3, 세로 60도, 가로세로 1)는 96 x 64 타깃을 본다.
    // - 붉은 상자(원점, 크기 1) 뒤의 큰 초록 판(z=-1, 3 x 3)은 상자 자리에서 가려지고 상자 위쪽에서만 보인다.
    // - 상자 앞의 작은 파랑 판(z=1.5, 0.3)은 상자 위에 그려진다. Y 로 60 도 돌리면 가로가 절반이다 - 2D 아핀으로는 못 하는 방향이다.
    // - SDF 판 둘: 거리값 1 은 채우기 색, 0.25 는 외곽선(문턱 0.1) 색이다.
    // - 메시가 없는 뷰에도 깊이가 달려 파랑 판이 그려진다.
    template <typename TModule>
    void TestWorldTextHidesBehindMeshesAndFacesAnyWay()
    {
        Stage<TModule> stage;
        if (false == stage.Open(true))
        {
            std::cout << "  [skip] no device for this API; world text not verified" << std::endl;
            return;
        }
        JBro::MeshLibrary library;
        Check(library.Initialize(&stage.renderer), "the mesh library must upload its cube");
        const std::byte solid[4] = {std::byte{255}, std::byte{255}, std::byte{255}, std::byte{255}};
        const std::byte quarter[4] = {std::byte{255}, std::byte{255}, std::byte{255}, std::byte{64}};
        const JBro::AssetHandle inside = stage.renderer.RegisterTexture({1, 1}, {solid, 4});
        const JBro::AssetHandle outside = stage.renderer.RegisterTexture({1, 1}, {quarter, 4});
        Check(inside.generation != 0 && outside.generation != 0, "the distance textures register");

        JBro::CameraParams camera;
        camera.view = JBro::MakeViewMatrix({0.0f, 0.0f, 3.0f}, {});
        Check(JBro::MakePerspectiveMatrix(60.0f * 3.14159265f / 180.0f, 1.0f, 0.1f, 100.0f, camera.projection),
            "the perspective matrix must build");
        camera.viewport.width = static_cast<JBro::Float>(TargetWidth);
        camera.viewport.height = static_cast<JBro::Float>(TargetHeight);
        camera.clearColor[3] = 1.0f;

        JBro::MeshSubmit cube;
        cube.mesh = library.Resolve(JBro::MeshLibrary::BuiltinCubeId());
        cube.tint[1] = 0.0f;
        cube.tint[2] = 0.0f;
        const auto quad = [](const JBro::Vector3& position, JBro::Float size, const JBro::Quaternion& rotation, JBro::Float r, JBro::Float g, JBro::Float b) {
            JBro::WorldTextSubmit text;
            text.world = JBro::MakeTransformMatrix3D(position, rotation, {size, size, 1.0f});
            text.tint[0] = r;
            text.tint[1] = g;
            text.tint[2] = b;
            return text;
        };
        const JBro::WorldTextSubmit behind = quad({0.0f, 0.0f, -1.0f}, 3.0f, {}, 0.0f, 1.0f, 0.0f);
        const JBro::WorldTextSubmit front = quad({0.0f, 0.0f, 1.5f}, 0.3f, {}, 0.0f, 0.0f, 1.0f);
        const JBro::Float half = 30.0f * 3.14159265f / 180.0f;
        JBro::Quaternion turned;
        turned.y = std::sin(half);
        turned.w = std::cos(half);
        const JBro::WorldTextSubmit turnedFront = quad({0.0f, 0.0f, 1.5f}, 0.3f, turned, 0.0f, 0.0f, 1.0f);
        JBro::WorldTextSubmit fill = quad({-0.6f, 0.0f, 1.5f}, 0.2f, {}, 1.0f, 1.0f, 0.0f);
        fill.texture = inside;
        fill.sdf = true;
        fill.outlineEdge = static_cast<std::uint16_t>(0.1f * 65535.0f);
        fill.outlineColor[0] = 255;
        fill.outlineColor[2] = 255;
        fill.outlineColor[3] = 255;
        JBro::WorldTextSubmit outline = fill;
        outline.world = JBro::MakeTransformMatrix3D({0.6f, 0.0f, 1.5f}, {}, {0.2f, 0.2f, 1.0f});
        outline.texture = outside;

        JBro::Array<std::byte> image;
        image.Resize(TargetWidth * TargetHeight * 4);
        JBro::TextureReadback readback;
        const auto draw = [&](JBro::Bool withCube, JBro::JArrayView<JBro::WorldTextSubmit> texts) {
            JBro::FrameTarget frameTarget;
            frameTarget.texture = stage.target;
            frameTarget.extent = {TargetWidth, TargetHeight};
            Check(stage.renderer.BeginFrame(frameTarget) == JBro::FrameStatus::Ready, "the frame must begin");
            Check(stage.renderer.BeginView(camera), "the view must open");
            if (withCube)
            {
                Check(stage.renderer.SubmitMesh(cube), "the cube must submit");
            }
            Check(stage.renderer.SubmitWorldTexts(texts), "the world texts must submit");
            Check(stage.renderer.EndView(), "the view must close");
            Check(stage.renderer.EndFrame() == JBro::FrameStatus::Ready, "the frame must record and present");
            Check(stage.renderer.GetDevice()->ReadTexture(stage.target, image.Data(), image.Size(), readback),
                "the target must read back");
        };

        const JBro::WorldTextSubmit scene[] = {behind, front, fill, outline};
        draw(true, {scene, 4});
        Check(stage.renderer.GetLastFrameStats().worldTextCount == 4, "four world texts are recorded");
        const Pixel center = ReadPixel(image, readback.rowPitch, TargetWidth / 2, TargetHeight / 2);
        Check(center.b > 0.9f && center.r < 0.05f, "the blue plate in front of the cube draws over it");
        const Pixel onCube = ReadPixel(image, readback.rowPitch, TargetWidth / 2 + 11, TargetHeight / 2);
        Check(onCube.r > 0.2f && onCube.r > onCube.g * 2.0f && onCube.r > onCube.b * 2.0f, "beside it the cube hides the green plate behind");
        const Pixel aboveCube = ReadPixel(image, readback.rowPitch, TargetWidth / 2, TargetHeight / 2 - 16);
        Check(aboveCube.g > 0.9f && aboveCube.r < 0.05f, "above the cube the green plate shows");
        const Pixel nearBlueEdge = ReadPixel(image, readback.rowPitch, TargetWidth / 2 + 6, TargetHeight / 2);
        Check(nearBlueEdge.b > 0.9f, "the square plate is blue six pixels right of the middle");
        const Pixel fillPixel = ReadPixel(image, readback.rowPitch, TargetWidth / 2 - 33, TargetHeight / 2);
        Check(fillPixel.r > 0.9f && fillPixel.g > 0.9f && fillPixel.b < 0.05f, "a distance of 1 is the fill colour");
        const Pixel outlinePixel = ReadPixel(image, readback.rowPitch, TargetWidth / 2 + 33, TargetHeight / 2);
        Check(outlinePixel.r > 0.9f && outlinePixel.g < 0.05f && outlinePixel.b > 0.9f, "a distance of 0.25 is the outline colour");
        Check(stage.renderer.GetDevice()->GetValidationErrorCount() == 0, "the debug layer accepts the world text pass");

        const JBro::WorldTextSubmit turnedScene[] = {behind, turnedFront};
        draw(true, {turnedScene, 2});
        const Pixel turnedEdge = ReadPixel(image, readback.rowPitch, TargetWidth / 2 + 6, TargetHeight / 2);
        Check(turnedEdge.r > 0.2f && turnedEdge.r > turnedEdge.b * 2.0f, "turned 60 degrees about Y the plate is narrower and the cube shows there");
        const Pixel turnedCenter = ReadPixel(image, readback.rowPitch, TargetWidth / 2, TargetHeight / 2);
        Check(turnedCenter.b > 0.9f, "and it still covers the middle");

        // 메시 없이 글자만 있는 뷰도 깊이 패스다.
        draw(false, {&front, 1});
        const Pixel alone = ReadPixel(image, readback.rowPitch, TargetWidth / 2, TargetHeight / 2);
        Check(alone.b > 0.9f, "a view with only world text draws it");
        Check(stage.renderer.GetDevice()->GetValidationErrorCount() == 0, "and the text-only depth pass is valid");

        stage.renderer.UnregisterTexture(inside);
        stage.renderer.UnregisterTexture(outside);
        library.Shutdown();
        stage.Close();
    }
}

namespace
{
    // **3D 레이어는 포토샵의 레이어다**(D-280). 레이어마다 따로 그린 한 장이고 차례대로 쌓인다 - 위 레이어의 먼 초록 상자가 아래 레이어의
    // 가까운 붉은 상자를 덮는다(한 깊이 버퍼였다면 붉은 것이 이긴다, `TestANearerCubeHidesAFartherOne`). 블렌드와 불투명도는 2D 와 같은 규칙으로
    // 레이어 한 장에 걸린다. 기대값은 같은 장면을 한 레이어씩만 켜고 잰 두 색에서 낸다 - 조명이 어떻든 레이어 셈만 본다:
    // 표준 50% 는 (위 + 아래) / 2, 더하기는 위 + 아래, 곱하기는 위 x 아래. 블렌드가 걸린 레이어는 둘째 프레임부터 얹힌다(텍스처가 그때 선다).
    template <typename TModule>
    void TestLayersStackAndBlendIn3D()
    {
        Stage<TModule> stage;
        if (false == stage.Open())
        {
            std::cout << "  [skip] no device for this API; 3D layers not verified" << std::endl;
            return;
        }
        JBro::Framework3D framework;
        JBro::FrameworkContext context;
        JBro::Testing::AttachClock(context);
        context.renderer = &stage.renderer;
        Check(framework.Initialize(context), "the 3D framework must initialize with the renderer");
        JBro::Canvas& canvas = *framework.GetCanvas();
        JBro::Object::GameObject* eye = canvas.CreateObject("eye");
        canvas.AttachComponent<JBro::Component::Transform3D>(eye)->position = {0.0f, 0.0f, 4.0f};
        auto* camera = canvas.AttachComponent<JBro::Component::Camera3D>(eye);
        camera->primary = true;
        camera->clearColor = {0.0f, 0.0f, 0.0f, 1.0f};
        PlaceCube(canvas, "near", {0.0f, 0.0f, 1.0f}, {0.6f, 0.6f, 0.6f}, {1.0f, 0.0f, 0.0f, 1.0f});
        JBro::Layer& top = canvas.CreateLayer("Top");
        JBro::Object::GameObject* farCube = PlaceCube(canvas, "far", {0.0f, 0.0f, -2.0f}, {3.0f, 3.0f, 3.0f}, {0.0f, 1.0f, 0.0f, 1.0f});
        Check(canvas.SetObjectLayer(farCube, top.GetId()), "the far cube goes on the top layer");
        JBro::Layer* base = canvas.FindLayer(canvas.GetDefaultLayer());

        JBro::Array<std::byte> image;
        JBro::TextureReadback readback;
        const auto center = [&]() {
            stage.RenderOnce(framework, image, readback);
            stage.RenderOnce(framework, image, readback);
            return ReadPixel(image, readback.rowPitch, TargetWidth / 2, TargetHeight / 2);
        };
        const auto matches = [](const Pixel& a, JBro::Float r, JBro::Float g, JBro::Float b) {
            return Near(a.r, r, 0.03f) && Near(a.g, g, 0.03f) && Near(a.b, b, 0.03f);
        };

        const Pixel stacked = center();
        Check(stage.renderer.GetLastFrameStats().viewCount == 2, "two layers are two views");
        // 유전체의 반사광은 흰색이라(F0 0.04 가 세 채널에 같다) 초록 정육면체에도 빨강이 조금 든다 - 빨강이 0 인지가 아니라 어느 정육면체인지를 본다.
        Check(stacked.g > 0.2f && stacked.g > stacked.r * 2.0f,
            "the top layer's far green cube covers the bottom layer's near red one - layers stack, depth does not cross them");
        Check(stage.renderer.GetLastFrameStats().compositedLayerCount == 0, "plain layers draw straight to the target");

        top.SetVisible(false);
        const Pixel below = center();
        top.SetVisible(true);
        base->SetVisible(false);
        const Pixel above = center();
        base->SetVisible(true);
        Check(below.r > 0.2f && above.g > 0.2f, "each layer alone shows its own cube");
        Check(stage.renderer.GetLastFrameStats().viewCount == 1, "a frame with one layer drawn is one view");

        top.SetOpacity(0.5f);
        Pixel mixed = center();
        Check(stage.renderer.GetLastFrameStats().compositedLayerCount == 1, "the faded layer is composited");
        Check(matches(mixed, (below.r + above.r) * 0.5f, (below.g + above.g) * 0.5f, (below.b + above.b) * 0.5f),
            "a half-opacity layer lies over the one below at half strength");
        top.SetOpacity(1.0f);
        top.SetBlend(JBro::LayerBlend::Additive);
        mixed = center();
        Check(matches(mixed, JBro::Min(1.0f, below.r + above.r), JBro::Min(1.0f, below.g + above.g), JBro::Min(1.0f, below.b + above.b)),
            "an additive layer adds itself to the one below");
        top.SetBlend(JBro::LayerBlend::Multiply);
        mixed = center();
        Check(matches(mixed, below.r * above.r, below.g * above.g, below.b * above.b), "a multiply layer multiplies the one below");

        // **레이어 썸네일**(D-288): 그 레이어만, 블렌드(지금 Multiply)·불투명도 없이 불투명한 썸네일 바탕에. 크기는 대상의 절반이다.
        // 두 번 그린다 - 썸네일이 합성한다면 합성 대상이 선 둘째 프레임에서 드러난다.
        top.SetOpacity(0.5f);
        JBro::TextureDesc thumbnailDesc;
        thumbnailDesc.extent = {TargetWidth / 2, TargetHeight / 2};
        thumbnailDesc.format = JBro::TextureFormat::BGRA8Unorm;
        thumbnailDesc.usage = JBro::TextureUsage::RenderTarget | JBro::TextureUsage::Sampled;
        const JBro::TextureHandle thumbnailTarget = stage.renderer.GetDevice()->CreateTexture(thumbnailDesc);
        Check(thumbnailTarget.IsValid(), "the thumbnail target is created");
        JBro::Array<std::byte> thumbnailImage;
        thumbnailImage.Resize(static_cast<std::size_t>(TargetWidth / 2) * (TargetHeight / 2) * 4);
        JBro::TextureReadback thumbnailReadback;
        // 첫 프레임의 깊이 없는 뷰 수다. 처음 보는 크기는 그 프레임에 메시 없이 그리고 프레임은 버리지 않는다.
        JBro::UInt32 firstWithoutDepth = 0xFFFFFFFFu;
        Pixel firstCentre;
        const auto thumbnail = [&](JBro::LayerId layer) {
            for (JBro::Int32 frame = 0; frame < 2; ++frame)
            {
                JBro::FrameTarget frameTarget;
                frameTarget.texture = stage.target;
                frameTarget.extent = {TargetWidth, TargetHeight};
                Check(stage.renderer.BeginFrame(frameTarget) == JBro::FrameStatus::Ready, "the frame must begin");
                JBro::Testing::Tick(framework, 1.0f / 60.0f);
                Check(framework.Render() == JBro::RenderResult::Submitted, "the framework must submit its view");
                JBro::LayerThumbnailDesc desc;
                desc.target = thumbnailTarget;
                desc.extent = thumbnailDesc.extent;
                desc.layer = layer;
                Check(framework.RenderLayerThumbnail(desc) == JBro::RenderResult::Submitted, "the thumbnail must submit");
                Check(stage.renderer.EndFrame() == JBro::FrameStatus::Ready, "the frame must finish");
                if (firstWithoutDepth == 0xFFFFFFFFu)
                {
                    firstWithoutDepth = stage.renderer.GetLastFrameStats().viewsWithoutDepthCount;
                    Check(stage.renderer.GetDevice()->ReadTexture(thumbnailTarget, thumbnailImage.Data(), thumbnailImage.Size(), thumbnailReadback),
                        "the first thumbnail must read back");
                    firstCentre = ReadPixel(thumbnailImage, thumbnailReadback.rowPitch, TargetWidth / 4, TargetHeight / 4);
                }
            }
            Check(stage.renderer.GetDevice()->ReadTexture(thumbnailTarget, thumbnailImage.Data(), thumbnailImage.Size(), thumbnailReadback),
                "the thumbnail must read back");
        };
        thumbnail(top.GetId());
        Check(firstWithoutDepth == 1 && matches(firstCentre, 0.08f, 0.09f, 0.11f),
            "a thumbnail of a new size is drawn without meshes on its first frame, and the frame goes on");
        Check(stage.renderer.GetLastFrameStats().viewsWithoutDepthCount == 0, "the thumbnail's depth is there from its second frame");
        const Pixel topCentre = ReadPixel(thumbnailImage, thumbnailReadback.rowPitch, TargetWidth / 4, TargetHeight / 4);
        const Pixel corner = ReadPixel(thumbnailImage, thumbnailReadback.rowPitch, 0, 0);
        Check(matches(topCentre, above.r, above.g, above.b), "the top layer's thumbnail is that layer alone, blend and opacity aside");
        Check(matches(corner, 0.08f, 0.09f, 0.11f), "on the opaque thumbnail background");
        thumbnail(canvas.GetDefaultLayer());
        Check(matches(ReadPixel(thumbnailImage, thumbnailReadback.rowPitch, TargetWidth / 4, TargetHeight / 4), below.r, below.g, below.b),
            "the bottom layer's thumbnail is its own cube");
        stage.renderer.GetDevice()->DestroyTexture(thumbnailTarget);
        Check(stage.renderer.GetDevice()->GetValidationErrorCount() == 0, "and the debug layer must have stayed quiet");

        framework.Shutdown();
        stage.Close();
    }
}

namespace
{
    // **빌트인 도형마다 카메라를 향한 면이 빛을 받는다**(D-335 우선순위 2). 흰 도형 하나를 가운데 두고 가운데 픽셀이 고정 광원의
    // 램버트 값인지 본다. 감김이 뒤집히면 바깥 면이 컬링되고 안쪽 면(앰비언트 0.25)이 보인다 - 정육면체 때 실제로 그랬다(D-108).
    // 기대색은 흰 기본 재질의 `Standard` 식(`ExpectStandard`)이다 - 구·캡슐·원기둥은 옆에서(+Z) 본 앞 법선 (0,0,1), 평면은 위에서 내려다본 법선 (0,1,0).
    template <typename TModule>
    void TestEveryBuiltinShapeShowsItsLitFrontFace()
    {
        Stage<TModule> stage;
        if (false == stage.Open())
        {
            std::cout << "  [skip] no device for this API; builtin shapes not verified" << std::endl;
            return;
        }
        JBro::Framework3D framework;
        JBro::FrameworkContext context;
        JBro::Testing::AttachClock(context);
        context.renderer = &stage.renderer;
        Check(framework.Initialize(context), "the 3D framework must initialize with the renderer");
        Check(JBro::MeshLibrary::GetBuiltinCount() == 5, "the builtin shapes are the cube, sphere, capsule, cylinder and plane");
        JBro::Canvas& canvas = *framework.GetCanvas();

        JBro::Object::GameObject* eye = canvas.CreateObject("eye");
        auto* eyeTransform = canvas.AttachComponent<JBro::Component::Transform3D>(eye);
        auto* camera = canvas.AttachComponent<JBro::Component::Camera3D>(eye);
        camera->primary = true;
        camera->clearColor = {0.0f, 0.0f, 0.0f, 1.0f};
        JBro::Object::GameObject* shape = canvas.CreateObject("shape");
        canvas.AttachComponent<JBro::Component::Transform3D>(shape);
        auto* mesh = canvas.AttachComponent<JBro::Component::MeshRenderer3D>(shape);
        mesh->tint = {1.0f, 1.0f, 1.0f, 1.0f};

        for (JBro::UInt32 index = 0; index < JBro::MeshLibrary::GetBuiltinCount(); ++index)
        {
            const JBro::MeshLibrary::BuiltinMesh builtin = JBro::MeshLibrary::GetBuiltin(index);
            Check(builtin.name != nullptr && false == builtin.id.IsNull(), "every builtin has a name and an id");
            Check(false == (JBro::MeshLibrary::GetBuiltin(index).id == JBro::MeshLibrary::GetBuiltin((index + 1) % JBro::MeshLibrary::GetBuiltinCount()).id),
                "and no two builtins share an id");
            const JBro::Bool plane = std::strcmp(builtin.name, "Plane") == 0;
            if (plane)
            {
                // 위에서 곧장 내려다본다. 카메라는 -Z 를 보므로 X 축으로 -90도 돌린다.
                eyeTransform->position = {0.0f, 3.0f, 0.0f};
                eyeTransform->rotation = JBro::FromAxisAngle({1.0f, 0.0f, 0.0f}, JBro::Radian(-1.5707963f));
            }
            else
            {
                eyeTransform->position = {0.0f, 0.0f, 3.0f};
                eyeTransform->rotation = {};
            }
            mesh->meshId = builtin.id;
            // 아이디를 바꾸면 해석된 핸들도 비운다 - 에디터의 커맨드 뒤 해석 패스가 하는 일이다. 비우지 않으면 첫 도형(정육면체)의 핸들이
            // 남아 다섯 번 모두 정육면체를 그렸고, 그래서 감김을 뒤집은 뮤테이션을 이 시험이 놓쳤다.
            mesh->mesh = {};
            JBro::Array<std::byte> image;
            JBro::TextureReadback readback;
            stage.RenderOnce(framework, image, readback);
            Check(stage.renderer.GetLastFrameStats().meshCount == 1, "the shape must be submitted");
            const Pixel center = ReadPixel(image, readback.rowPitch, TargetWidth / 2, TargetHeight / 2);
            const float white[3] = {1.0f, 1.0f, 1.0f};
            const JBro::Float expected = plane
                ? ExpectStandard(white, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 3.0f, 0.0f}, 0.0f, 1.0f).r
                : ExpectStandard(white, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.5f}, {0.0f, 0.0f, 3.0f}, 0.0f, 1.0f).r;
            std::cout << "  " << builtin.name << " front face " << center.r.Get() << " (expected " << expected.Get() << ")" << std::endl;
            Check(Near(center.r, expected, 0.05f), "the face towards the camera must be lit, not a culled shape's inside");
            const Pixel corner = ReadPixel(image, readback.rowPitch, 2, 2);
            if (false == plane)
            {
                Check(Near(corner.r, 0.0f), "the shape is no bigger than it should be - the corner stays clear");
            }
        }
        Check(stage.renderer.GetDevice()->GetValidationErrorCount() == 0, "and the debug layer must have stayed quiet");
        framework.Shutdown();
        Check(stage.renderer.GetMeshCount() == 0, "shutting the framework down must unregister every builtin");
        stage.Close();
    }
}

namespace
{
    // 카메라(z=3, 세로 화각 60°)에서 보이는 평면 z=0 의 삼각형 하나다. 위(+Z)에서 볼 때 반시계라 앞면이 카메라를 본다.
    const char* const UpperLeftTriangle = "v -2.2 0.6 0\nv -1.0 0.6 0\nv -1.6 1.5 0\nf 1 2 3\n";
    const char* const LowerRightTriangle = "v 1.0 -1.5 0\nv 2.2 -1.5 0\nv 1.6 -0.6 0\nf 1 2 3\n";

    void WriteText(const std::filesystem::path& path, const char* text)
    {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream file(path, std::ios::binary);
        file << text;
    }

    // **모델의 메시가 그려진다**(D-338·D-342, model-import-plan 4 단계). 레지스트리 스캔 → 해석 패스(`BindCanvasAssets`)가 `meshId` 로 에셋 핸들을 채움 →
    // `MeshLibrary` 가 CPU 자료를 처음 만날 때 렌더러에 올림. 삼각형은 화면 왼쪽 위에 있어, 정육면체 대체(가운데)와 갈린다.
    // 파일을 고쳐 다시 읽으면(in-place 재로드, 자료 세대가 오른다) 라이브러리가 다시 올려 그림이 오른쪽 아래로 옮겨진다.
    template <typename TModule>
    void TestAModelMeshIsDrawnAndFollowsAReload()
    {
        Stage<TModule> stage;
        if (false == stage.Open())
        {
            std::cout << "  [skip] no device for this API; model meshes not verified" << std::endl;
            return;
        }
        namespace fs = std::filesystem;
        const fs::path root = fs::temp_directory_path() / L"JBroModelMeshProbe·모델";
        fs::remove_all(root);
        WriteText(root / "props" / "shard.obj", UpperLeftTriangle);
        const std::u8string rootText = root.u8string();
        const JBro::String rootPath(reinterpret_cast<const char*>(rootText.data()), rootText.size());

        JBro::AssetRegistry registry;
        JBro::AssetScanOptions options;
        options.createMissingMeta = true;
        JBro::AssetScanReport report;
        Check(registry.Scan(stage.platform, rootPath.c_str(), options, report), "the probe folder scans");
        const JBro::AssetRecord* model = registry.FindByPath("props/shard.obj");
        Check(model != nullptr, "the model is registered");
        JBro::Array<JBro::AssetId> parts;
        registry.CollectOwned(model->id, parts);
        Check(parts.Size() == 1 && registry.Find(parts[0])->type == JBro::AssetType::Mesh, "with its one mesh");
        const JBro::AssetId meshId = parts[0];
        JBro::AssetSystem assets;
        Check(assets.Initialize(stage.memory), "the asset system initializes");
        assets.Bind(stage.platform, registry, rootPath.c_str());

        JBro::Framework3D framework;
        JBro::FrameworkContext context;
        JBro::Testing::AttachClock(context);
        context.renderer = &stage.renderer;
        context.assets = &assets;
        Check(framework.Initialize(context), "the 3D framework initializes with the renderer and the assets");
        JBro::Canvas& canvas = *framework.GetCanvas();
        JBro::Object::GameObject* eye = canvas.CreateObject("eye");
        canvas.AttachComponent<JBro::Component::Transform3D>(eye)->position = {0.0f, 0.0f, 3.0f};
        auto* camera = canvas.AttachComponent<JBro::Component::Camera3D>(eye);
        camera->primary = true;
        camera->clearColor = {0.0f, 0.0f, 0.0f, 1.0f};
        JBro::Object::GameObject* shard = canvas.CreateObject("shard");
        canvas.AttachComponent<JBro::Component::Transform3D>(shard);
        auto* mesh = canvas.AttachComponent<JBro::Component::MeshRenderer3D>(shard);
        mesh->meshId = meshId;
        mesh->tint = {1.0f, 1.0f, 1.0f, 1.0f};
        framework.BindCanvasAssets();
        Check(mesh->mesh.generation != 0 && assets.GetMesh(mesh->mesh) != nullptr, "the resolve pass fills the component with the mesh asset");

        JBro::Array<std::byte> image;
        JBro::TextureReadback readback;
        stage.RenderOnce(framework, image, readback);
        Check(framework.GetMeshLibrary().GetUploadedAssetCount() == 1, "the library uploads the model's mesh once");
        const Pixel upperLeft = ReadPixel(image, readback.rowPitch, 18, 16);
        const Pixel center = ReadPixel(image, readback.rowPitch, TargetWidth / 2, TargetHeight / 2);
        const Pixel lowerRight = ReadPixel(image, readback.rowPitch, 77, 53);
        std::cout << "  model mesh: upper-left " << upperLeft.r.Get() << ", centre " << center.r.Get() << ", lower-right " << lowerRight.r.Get() << std::endl;
        const float white[3] = {1.0f, 1.0f, 1.0f};
        const Pixel litFront = ExpectStandard(white, {0.0f, 0.0f, 1.0f}, {-1.6f, 0.9f, 0.0f}, {0.0f, 0.0f, 3.0f}, 0.0f, 1.0f);
        Check(Near(upperLeft.r, litFront.r, 0.05f), "the triangle is drawn where the file put it, lit from the front");
        Check(Near(center.r, 0.0f) && Near(lowerRight.r, 0.0f), "and nowhere else - it is not the cube fallback");

        // 파일을 고치고 다시 읽는다. 핸들은 그대로이고 자료 세대가 올라 라이브러리가 다시 올린다.
        const JBro::AssetHandle before = mesh->mesh;
        WriteText(root / "props" / "shard.obj", LowerRightTriangle);
        Check(assets.ReloadInPlace(meshId), "the mesh reloads in place");
        Check(mesh->mesh.index == before.index && mesh->mesh.generation == before.generation, "keeping its handle");
        stage.RenderOnce(framework, image, readback);
        const Pixel movedUpperLeft = ReadPixel(image, readback.rowPitch, 18, 16);
        const Pixel movedLowerRight = ReadPixel(image, readback.rowPitch, 77, 53);
        const Pixel movedLit = ExpectStandard(white, {0.0f, 0.0f, 1.0f}, {1.6f, -0.9f, 0.0f}, {0.0f, 0.0f, 3.0f}, 0.0f, 1.0f);
        Check(Near(movedLowerRight.r, movedLit.r, 0.05f) && Near(movedUpperLeft.r, 0.0f), "after the reload the triangle is drawn where the file now puts it");
        Check(framework.GetMeshLibrary().GetUploadedAssetCount() == 1, "the old upload was replaced, not added to");
        // 한 번 더 고친다. 이제 에셋 시스템은 방금 읽은 모델을 들고 있다 - 다시 읽기가 그것을 버리지 않으면 낡은 자리를 그린다
        // (첫 다시 읽기는 해석 패스의 `CollectUnused` 가 비워 둔 뒤라 이것을 드러내지 못했다 - 뮤테이션이 지나갔다).
        WriteText(root / "props" / "shard.obj", UpperLeftTriangle);
        Check(assets.ReloadInPlace(meshId), "the mesh reloads in place again");
        stage.RenderOnce(framework, image, readback);
        Check(Near(ReadPixel(image, readback.rowPitch, 18, 16).r, litFront.r, 0.05f) && Near(ReadPixel(image, readback.rowPitch, 77, 53).r, 0.0f),
            "a second reload reads the file again rather than the model it last decoded");
        Check(stage.renderer.GetDevice()->GetValidationErrorCount() == 0, "and the debug layer stayed quiet");

        framework.Shutdown();
        assets.Shutdown();
        stage.Close();
        fs::remove_all(root);
    }
}

namespace
{
    std::string IdText(JBro::AssetId id)
    {
        char text[JBro::Uuid::TextCapacity] = {};
        id.ToText(text, sizeof(text));
        return text;
    }

    // **재질이 그림과 값으로 그려진다**(material-plan 3 단계). 위에서 내려다본 1 x 1 사각형(모델, 내장 그림 2x2 와 재질 `Brick` - 기본색 (1, 0.5, 0.25),
    // metallic 0.2, roughness 0.7)이 칸을 비워 둔 채로도 **파일의 재질**로 그려지고, 사분면마다 그림의 텍셀 × 기본색을 `Standard` 식으로 낸다
    // (UV 의 방향까지 본다 - 왼쪽 위 빨강, 오른쪽 위 초록, 왼쪽 아래 파랑). 칸에 `.jmat` 을 넣으면 그것이 이긴다: `Unlit` 에 가운데 회색(128) 그림이면
    // sRGB 뷰가 선형 0.216 으로 풀고 출력이 다시 싸서 0.502 가 나온다(sRGB 뷰가 없으면 0.735). 컷아웃 재질은 반투명 텍셀(오른쪽 아래)을 버린다.
    template <typename TModule>
    void TestMaterialsDrawTheirPicturesAndValues()
    {
        Stage<TModule> stage;
        if (false == stage.Open())
        {
            std::cout << "  [skip] no device for this API; materials not verified" << std::endl;
            return;
        }
        namespace fs = std::filesystem;
        const fs::path root = fs::temp_directory_path() / L"JBroMaterialPixelProbe·재질";
        fs::remove_all(root);
        const std::string quad = TestModels::SelfContainedQuadGltf("Bricks", TestModels::TinyPng());
        WriteText(root / "props" / "quad.gltf", quad.c_str());
        {
            const std::string grey = TestModels::SolidTga(128, 128, 128, 255);
            std::filesystem::create_directories(root / "art");
            std::ofstream file(root / "art" / "grey.tga", std::ios::binary);
            file.write(grey.data(), static_cast<std::streamsize>(grey.size()));
        }
        const std::u8string rootText = root.u8string();
        const JBro::String rootPath(reinterpret_cast<const char*>(rootText.data()), rootText.size());

        JBro::AssetRegistry registry;
        JBro::AssetScanOptions options;
        options.createMissingMeta = true;
        JBro::AssetScanReport report;
        Check(registry.Scan(stage.platform, rootPath.c_str(), options, report), "the probe folder scans");
        const JBro::AssetRecord* model = registry.FindByPath("props/quad.gltf");
        Check(model != nullptr, "the quad model is registered");
        JBro::AssetId meshId;
        JBro::AssetId imageId;
        JBro::Array<JBro::AssetId> parts;
        registry.CollectOwned(model->id, parts);
        for (const JBro::AssetId& id : parts)
        {
            meshId = registry.Find(id)->type == JBro::AssetType::Mesh ? id : meshId;
            imageId = registry.Find(id)->type == JBro::AssetType::Texture ? id : imageId;
        }
        const JBro::AssetId greyId = registry.FindByPath("art/grey.tga")->id;
        const std::string flat = "Version: 1\nMaterial:\n  shader: Unlit\n  baseColorTextureId: " + IdText(greyId) + "\n";
        const std::string cut = "Version: 1\nMaterial:\n  alpha: Cutout\n  alphaCutoff: 0.75\n  baseColorTextureId: " + IdText(imageId) + "\n";
        WriteText(root / "art" / "flat.jmat", flat.c_str());
        WriteText(root / "art" / "cut.jmat", cut.c_str());
        Check(registry.Scan(stage.platform, rootPath.c_str(), options, report), "the folder scans again with the material files");
        const JBro::AssetId flatId = registry.FindByPath("art/flat.jmat")->id;
        const JBro::AssetId cutId = registry.FindByPath("art/cut.jmat")->id;

        JBro::AssetSystem assets;
        Check(assets.Initialize(stage.memory), "the asset system initializes");
        assets.Bind(stage.platform, registry, rootPath.c_str());
        JBro::Framework3D framework;
        JBro::FrameworkContext context;
        JBro::Testing::AttachClock(context);
        context.renderer = &stage.renderer;
        context.assets = &assets;
        Check(framework.Initialize(context), "the 3D framework initializes with the renderer and the assets");
        JBro::Canvas& canvas = *framework.GetCanvas();
        JBro::Object::GameObject* eye = canvas.CreateObject("eye");
        auto* eyeTransform = canvas.AttachComponent<JBro::Component::Transform3D>(eye);
        // 위에서 곧장 내려다본다. 화면 위가 -Z, 오른쪽이 +X 다.
        eyeTransform->position = {0.0f, 3.0f, 0.0f};
        eyeTransform->rotation = JBro::FromAxisAngle({1.0f, 0.0f, 0.0f}, JBro::Radian(-1.5707963f));
        auto* camera = canvas.AttachComponent<JBro::Component::Camera3D>(eye);
        camera->primary = true;
        camera->clearColor = {0.0f, 0.0f, 0.0f, 1.0f};
        JBro::Object::GameObject* object = canvas.CreateObject("quad");
        // 사각형은 (0..1, 0, 0..1) 이라 가운데로 옮긴다.
        canvas.AttachComponent<JBro::Component::Transform3D>(object)->position = {-0.5f, 0.0f, -0.5f};
        auto* mesh = canvas.AttachComponent<JBro::Component::MeshRenderer3D>(object);
        mesh->meshId = meshId;
        mesh->tint = {1.0f, 1.0f, 1.0f, 1.0f};
        framework.BindCanvasAssets();
        const JBro::MeshData* quadMesh = assets.GetMesh(mesh->mesh);
        Check(quadMesh != nullptr && false == quadMesh->material.IsNull() && registry.Find(quadMesh->material) != nullptr,
            "the resolve pass loads the model's mesh, which knows the material it uses in the file");

        // 사각형은 화면에서 한 변 약 18 픽셀이다(거리 3, 세로 화각 60°, 높이 64). 사분면 가운데는 화면 가운데에서 ±4.6 픽셀이다.
        const JBro::UInt32 left = TargetWidth / 2 - 5;
        const JBro::UInt32 right = TargetWidth / 2 + 5;
        const JBro::UInt32 top = TargetHeight / 2 - 5;
        const JBro::UInt32 bottom = TargetHeight / 2 + 5;
        JBro::Array<std::byte> image;
        JBro::TextureReadback readback;
        stage.RenderOnce(framework, image, readback);
        Check(framework.GetMaterialLibrary().GetUploadedMaterialCount() == 1 &&framework.GetMaterialLibrary().GetUploadedTextureCount() == 1,
            "with the slot empty the mesh's own material is uploaded, with its one picture");
        const auto expect = [](float r, float g, float b, float x, float z) {
            const float base[3] = {r, g, b};
            return ExpectStandard(base, {0.0f, 1.0f, 0.0f}, {x, 0.0f, z}, {0.0f, 3.0f, 0.0f}, 0.2f, 0.7f);
        };
        const auto near = [](const Pixel& got, const Pixel& want) {
            return Near(got.r, want.r, 0.04f) && Near(got.g, want.g, 0.04f) && Near(got.b, want.b, 0.04f);
        };
        const Pixel red = ReadPixel(image, readback.rowPitch, left, top);
        const Pixel green = ReadPixel(image, readback.rowPitch, right, top);
        const Pixel blue = ReadPixel(image, readback.rowPitch, left, bottom);
        const Pixel wantRed = expect(1.0f, 0.0f, 0.0f, -0.25f, -0.25f);
        const Pixel wantGreen = expect(0.0f, 0.5f, 0.0f, 0.25f, -0.25f);
        const Pixel wantBlue = expect(0.0f, 0.0f, 0.25f, -0.25f, 0.25f);
        std::cout << "  material quad: red " << red.r.Get() << "/" << wantRed.r.Get() << ", green " << green.g.Get() << "/" << wantGreen.g.Get()
                  << ", blue " << blue.b.Get() << "/" << wantBlue.b.Get() << std::endl;
        Check(near(red, wantRed), "the top-left quarter is the red texel times the material's base colour, shaded by Standard");
        Check(near(green, wantGreen), "the top-right quarter is the green texel times the base colour's half green");
        Check(near(blue, wantBlue), "the bottom-left quarter is the blue texel times the base colour's quarter blue");

        // 칸에 재질을 넣으면 그것이다. `Unlit` 과 가운데 회색 그림 - sRGB 뷰가 선형으로 풀고 출력이 다시 싼다.
        mesh->materialId = flatId;
        mesh->material = {};
        framework.BindCanvasAssets();
        stage.RenderOnce(framework, image, readback);
        const Pixel grey = ReadPixel(image, readback.rowPitch, TargetWidth / 2, TargetHeight / 2);
        std::cout << "  unlit grey: " << grey.r.Get() << std::endl;
        Check(Near(grey.r, 128.0f / 255.0f, 0.02f) && Near(grey.g, grey.r, 0.004f) && Near(grey.b, grey.r, 0.004f),
            "an Unlit material in the slot wins, and a grey picture read through an sRGB view comes back as the same grey");

        // 컷아웃: 반투명 텍셀(오른쪽 아래)은 버려져 바탕이 보이고, 불투명 텍셀은 남는다.
        mesh->materialId = cutId;
        mesh->material = {};
        framework.BindCanvasAssets();
        stage.RenderOnce(framework, image, readback);
        const Pixel kept = ReadPixel(image, readback.rowPitch, left, top);
        const Pixel dropped = ReadPixel(image, readback.rowPitch, right, bottom);
        Check(kept.r > 0.3f && Near(dropped.r, 0.0f) && Near(dropped.g, 0.0f) && Near(dropped.b, 0.0f),
            "a cutout material drops the texel whose alpha is under its cutoff and keeps the opaque ones");
        Check(stage.renderer.GetDevice()->GetValidationErrorCount() == 0, "and the debug layer stayed quiet");

        framework.Shutdown();
        Check(stage.renderer.GetMaterialCount() == 0, "shutting the framework down unregisters its materials");
        assets.Shutdown();
        stage.Close();
        fs::remove_all(root);
    }
}

JBro::Int32 RunMeshPixelTests()
{
    JBRO_CASE(TestMaterialsDrawTheirPicturesAndValues<JBro::D3D12RHIModule>());
    JBRO_CASE(TestMaterialsDrawTheirPicturesAndValues<JBro::D3D11RHIModule>());
    JBRO_CASE(TestMaterialsDrawTheirPicturesAndValues<JBro::VulkanRHIModule>());
    JBRO_CASE(TestAModelMeshIsDrawnAndFollowsAReload<JBro::D3D12RHIModule>());
    JBRO_CASE(TestAModelMeshIsDrawnAndFollowsAReload<JBro::D3D11RHIModule>());
    JBRO_CASE(TestAModelMeshIsDrawnAndFollowsAReload<JBro::VulkanRHIModule>());
    JBRO_CASE(TestEveryBuiltinShapeShowsItsLitFrontFace<JBro::D3D12RHIModule>());
    JBRO_CASE(TestEveryBuiltinShapeShowsItsLitFrontFace<JBro::D3D11RHIModule>());
    JBRO_CASE(TestEveryBuiltinShapeShowsItsLitFrontFace<JBro::VulkanRHIModule>());
    JBRO_CASE(TestLayersStackAndBlendIn3D<JBro::D3D12RHIModule>());
    JBRO_CASE(TestLayersStackAndBlendIn3D<JBro::D3D11RHIModule>());
    JBRO_CASE(TestLayersStackAndBlendIn3D<JBro::VulkanRHIModule>());
    JBRO_CASE(TestACubeIsDrawnWhereTheCameraLooks<JBro::D3D12RHIModule>());
    JBRO_CASE(TestANearerCubeHidesAFartherOne<JBro::D3D12RHIModule>());
    JBRO_CASE(TestACubeIsDrawnWhereTheCameraLooks<JBro::D3D11RHIModule>());
    JBRO_CASE(TestACubeIsDrawnWhereTheCameraLooks<JBro::VulkanRHIModule>());
    JBRO_CASE(TestANearerCubeHidesAFartherOne<JBro::D3D11RHIModule>());
    JBRO_CASE(TestANearerCubeHidesAFartherOne<JBro::VulkanRHIModule>());
    JBRO_CASE(TestSpritesLayOverMeshesInTheSameView<JBro::D3D12RHIModule>());
    JBRO_CASE(TestSpritesLayOverMeshesInTheSameView<JBro::D3D11RHIModule>());
    JBRO_CASE(TestSpritesLayOverMeshesInTheSameView<JBro::VulkanRHIModule>());
    JBRO_CASE(TestWorldTextHidesBehindMeshesAndFacesAnyWay<JBro::D3D12RHIModule>());
    JBRO_CASE(TestWorldTextHidesBehindMeshesAndFacesAnyWay<JBro::D3D11RHIModule>());
    JBRO_CASE(TestWorldTextHidesBehindMeshesAndFacesAnyWay<JBro::VulkanRHIModule>());
    std::cout << "Mesh pixel tests passed.\n";
    return 0;
}
