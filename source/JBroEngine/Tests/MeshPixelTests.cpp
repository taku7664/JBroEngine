#include "TestCases.h"
#include "TestClock.h"
#include <JBro/D3D11RHI/D3D11RHI.h>
#include <JBro/D3D12RHI/D3D12RHI.h>
#include <JBro/VulkanRHI/VulkanRHI.h>
#include <JBro/Canvas/Layer.h>
#include <JBro/Framework3D/Component/Camera3D.h>
#include <JBro/Framework3D/Component/Light3D.h>
#include <JBro/Canvas/Internal/CanvasAccess.h>
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
#include <JBro/Canvas/CanvasFile.h>
#include <JBro/Host/GameBuild.h>
#include <JBro/Host/ProjectFile.h>
#include <JBro/Package/PackageAssetSource.h>
#include <JBro/Package/PackageReader.h>

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

    // **3D 라이트의 셈**(D-353, `BuiltinMesh.hlsl` 의 라이트 루프). 렌더러가 `Light3DSubmit` 을 옮기는 셈(방향의 정규화, 원뿔 반각의 cos 에서 scale·offset)과
    // 셰이더의 감쇠(거리 제곱, glTF 창, 원뿔의 제곱)를 CPU 로 다시 쓴다. 환경광은 기본색 × 환경광이다.
    Pixel ExpectLit(const float base[3], JBro::Vector3 normal, JBro::Vector3 position, JBro::Vector3 camera, float metallic, float roughnessFactor,
        const float ambient[3], const JBro::Light3DSubmit* lights, int count)
    {
        constexpr float Pi = 3.14159265f;
        normal = JBro::Normalize(normal);
        const JBro::Vector3 view = JBro::Normalize(JBro::Subtract(camera, position));
        const float nv = std::max(JBro::Dot(normal, view).Get(), 0.0001f);
        const float roughness = std::min(std::max(roughnessFactor, 0.045f), 1.0f);
        const float alpha = roughness * roughness;
        const float alpha2 = alpha * alpha;
        const float k = (roughness + 1.0f) * (roughness + 1.0f) / 8.0f;
        float out[3] = {ambient[0] * base[0], ambient[1] * base[1], ambient[2] * base[2]};
        for (int index = 0; index < count; ++index)
        {
            const JBro::Light3DSubmit& light = lights[index];
            const JBro::Vector3 direction = JBro::Normalize(JBro::Vector3{light.direction[0], light.direction[1], light.direction[2]});
            JBro::Vector3 towards = JBro::Scale(direction, -1.0f);
            float attenuation = 1.0f;
            if (light.kind != JBro::Light3DKind::Directional)
            {
                const JBro::Vector3 toLight = JBro::Subtract(JBro::Vector3{light.position[0], light.position[1], light.position[2]}, position);
                const float distanceSquared = std::max(JBro::Dot(toLight, toLight).Get(), 0.0001f);
                towards = JBro::Scale(toLight, 1.0f / std::sqrt(distanceSquared));
                attenuation = 1.0f / distanceSquared;
                if (light.range > 0.0f)
                {
                    const float ratio = distanceSquared / (light.range.Get() * light.range.Get());
                    attenuation *= std::min(std::max(1.0f - ratio * ratio, 0.0f), 1.0f);
                }
                if (light.kind == JBro::Light3DKind::Spot)
                {
                    const float halfOuter = std::min(std::max(light.outerAngle.Get() * 0.5f, 0.0f), Pi * 0.5f);
                    const float halfInner = std::min(std::max(light.innerAngle.Get() * 0.5f, 0.0f), halfOuter);
                    const float difference = std::cos(halfInner) - std::cos(halfOuter);
                    const float scale = 1.0f / std::max(difference, 0.001f);
                    const float offset = -std::cos(halfOuter) * scale;
                    const float cone = std::min(std::max(JBro::Dot(direction, JBro::Scale(towards, -1.0f)).Get() * scale + offset, 0.0f), 1.0f);
                    attenuation *= cone * cone;
                }
            }
            const JBro::Vector3 halfway = JBro::Normalize(JBro::Add(towards, view));
            const float nl = std::max(0.0f, JBro::Dot(normal, towards).Get());
            const float nh = std::max(0.0f, JBro::Dot(normal, halfway).Get());
            const float vh = std::max(0.0f, JBro::Dot(view, halfway).Get());
            const float denominator = nh * nh * (alpha2 - 1.0f) + 1.0f;
            const float distribution = alpha2 / (Pi * denominator * denominator);
            const float geometry = (nv / (nv * (1.0f - k) + k)) * (nl / (nl * (1.0f - k) + k));
            for (int channel = 0; channel < 3; ++channel)
            {
                const float reflectance = 0.04f + (base[channel] - 0.04f) * metallic;
                const float fresnel = reflectance + (1.0f - reflectance) * std::pow(1.0f - vh, 5.0f);
                const float specular = distribution * geometry * fresnel / std::max(4.0f * nv * nl, 0.0001f);
                const float diffuse = (1.0f - fresnel) * (1.0f - metallic) * base[channel];
                out[channel] += (diffuse + Pi * specular) * light.color[channel].Get() * attenuation * nl;
            }
        }
        return Pixel{EncodeSrgb(out[0]), EncodeSrgb(out[1]), EncodeSrgb(out[2]), 1.0f};
    }

    // 라이트를 내지 않은 뷰의 **기본 빛**이다 - 라이트 기능 전의 고정 방향광(빛 쪽 (0.4, 0.8, 0.45), 0.75)과 주변광 0.25.
    JBro::Light3DSubmit DefaultSun()
    {
        const JBro::Vector3 towards = JBro::Normalize(JBro::Vector3{0.4f, 0.8f, 0.45f});
        JBro::Light3DSubmit sun;
        sun.kind = JBro::Light3DKind::Directional;
        sun.direction[0] = -towards.x;
        sun.direction[1] = -towards.y;
        sun.direction[2] = -towards.z;
        sun.color[0] = 0.75f;
        sun.color[1] = 0.75f;
        sun.color[2] = 0.75f;
        return sun;
    }

    Pixel ExpectStandard(const float base[3], JBro::Vector3 normal, JBro::Vector3 position, JBro::Vector3 camera, float metallic, float roughnessFactor)
    {
        const float ambient[3] = {0.25f, 0.25f, 0.25f};
        const JBro::Light3DSubmit sun = DefaultSun();
        return ExpectLit(base, normal, position, camera, metallic, roughnessFactor, ambient, &sun, 1);
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
    // **3D 게임 빌드가 에디터와 같은 그림을 낸다**(material-plan 5 단계, todo-3d 우선순위 10). 같은 그림을 내장한 두 모델의 사각형을 위에서 내려다보는 캔버스를
    // 느슨한 폴더에서 그린 것과, `BuildGame` 이 구운 패키지만으로(원본 폴더를 지운 뒤) 그린 것이 바이트까지 같다. 두 재질은 GPU 텍스처 하나를 나눠 쓰고,
    // 패키지에는 그 그림의 픽셀이 한 번만 들며, 게임의 하위 레코드는 느슨한 것과 같은 이름·같은 그림 열쇠를 갖는다.
    template <typename TModule>
    void TestA3DGameBuildDrawsWhatTheEditorDraws()
    {
        Stage<TModule> stage;
        if (false == stage.Open(true))
        {
            std::cout << "  [skip] no device for this API; the 3D game build not verified" << std::endl;
            return;
        }
        namespace fs = std::filesystem;
        const fs::path root = fs::temp_directory_path() / L"JBro3DGameBuildProbe·빌드";
        fs::remove_all(root);
        const fs::path source = root / "Source";
        const std::string quad = TestModels::SelfContainedQuadGltf("Bricks", TestModels::TinyPng());
        WriteText(source / "Assets" / "Props" / "left.gltf", quad.c_str());
        WriteText(source / "Assets" / "Props" / "right.gltf", quad.c_str());
        const fs::path projectPath = source / "Probe.jproject";
        WriteText(projectPath, "Version: 1\nEngineVersion: 0.1.0\nFramework: 3D\nRootPath: .\nAssetDirectory: Assets\nScriptOutputLibraryPath: \"\"\n"
            "Build:\n  ProductName: Probe3D\n  OutputDirectory: ../Out\n  StartupCanvas: Assets/Canvases/Main.jcanvas\n");
        const auto utf8 = [](const fs::path& path) {
            const std::u8string text = path.generic_u8string();
            return JBro::String(reinterpret_cast<const char*>(text.data()), text.size());
        };
        const JBro::String assetRoot = utf8(source / "Assets");

        JBro::AssetRegistry registry;
        JBro::AssetScanOptions options;
        options.createMissingMeta = true;
        JBro::AssetScanReport report;
        Check(registry.Scan(stage.platform, assetRoot.c_str(), options, report), "the source assets scan");
        const auto partOf = [&](const char* path, JBro::AssetType type) {
            JBro::Array<JBro::AssetId> parts;
            registry.CollectOwned(registry.FindByPath(path)->id, parts);
            JBro::AssetId found;
            for (const JBro::AssetId& id : parts)
            {
                found = registry.Find(id)->type == type ? id : found;
            }
            return found;
        };
        const JBro::AssetId leftImage = partOf("Props/left.gltf", JBro::AssetType::Texture);
        const JBro::AssetId rightImage = partOf("Props/right.gltf", JBro::AssetType::Texture);
        const JBro::UInt64 key = registry.Find(leftImage)->sharedImageKey;
        Check(key != 0 && registry.Find(rightImage)->sharedImageKey == key, "the two models' images are the same picture");

        const auto initialize = [&](JBro::AssetSystem& assets, JBro::Framework3D& framework) {
            JBro::FrameworkContext context;
            JBro::Testing::AttachClock(context);
            context.renderer = &stage.renderer;
            context.assets = &assets;
            Check(framework.Initialize(context), "the 3D framework initializes");
        };
        // 에디터 쪽: 느슨한 폴더의 캔버스를 짓고 그린다.
        JBro::Array<std::byte> looseImage;
        JBro::TextureReadback readback;
        {
            JBro::AssetSystem assets;
            Check(assets.Initialize(stage.memory), "the loose asset system initializes");
            assets.Bind(stage.platform, registry, assetRoot.c_str());
            JBro::Framework3D framework;
            initialize(assets, framework);
            JBro::Canvas& canvas = *framework.GetCanvas();
            JBro::Object::GameObject* eye = canvas.CreateObject("eye");
            auto* eyeTransform = canvas.AttachComponent<JBro::Component::Transform3D>(eye);
            eyeTransform->position = {0.0f, 3.0f, 0.0f};
            eyeTransform->rotation = JBro::FromAxisAngle({1.0f, 0.0f, 0.0f}, JBro::Radian(-1.5707963f));
            auto* camera = canvas.AttachComponent<JBro::Component::Camera3D>(eye);
            camera->primary = true;
            camera->clearColor = {0.0f, 0.0f, 0.0f, 1.0f};
            const auto place = [&](const char* name, JBro::AssetId mesh, JBro::Float x) {
                JBro::Object::GameObject* object = canvas.CreateObject(name);
                canvas.AttachComponent<JBro::Component::Transform3D>(object)->position = {x, 0.0f, -0.5f};
                auto* renderer = canvas.AttachComponent<JBro::Component::MeshRenderer3D>(object);
                renderer->meshId = mesh;
                renderer->tint = {1.0f, 1.0f, 1.0f, 1.0f};
            };
            place("left", partOf("Props/left.gltf", JBro::AssetType::Mesh), -1.25f);
            place("right", partOf("Props/right.gltf", JBro::AssetType::Mesh), 0.25f);
            framework.BindCanvasAssets();
            stage.RenderOnce(framework, looseImage, readback);
            Check(framework.GetMaterialLibrary().GetUploadedMaterialCount() == 2 && framework.GetMaterialLibrary().GetUploadedTextureCount() == 1,
                "each model's mesh draws with its own material, and the two materials share one texture for the one picture");
            const Pixel left = ReadPixel(looseImage, readback.rowPitch, 30, 27);
            const Pixel right = ReadPixel(looseImage, readback.rowPitch, 57, 27);
            std::cout << "  shared picture: left " << left.r.Get() << "," << left.g.Get() << "," << left.b.Get() << " right " << right.r.Get() << "," << right.g.Get() << "," << right.b.Get() << std::endl;
            // 둘의 빛 반사는 자리가 달라 조금 다르다(흰 정반사). 둘 다 빨강이 이기면 된다.
            Check(left.r > 0.6f && left.g < 0.25f && left.b < 0.25f && right.r > 0.6f && right.g < 0.25f && right.b < 0.25f,
                "both quads show the red texel of the shared picture");
            JBro::String text;
            JBro::CanvasFileError canvasError;
            Check(JBro::WriteCanvasText(canvas, text, canvasError), "the canvas writes");
            WriteText(source / "Assets" / "Canvases" / "Main.jcanvas", text.c_str());
            framework.Shutdown();
            // 에디터가 하듯 다시 훑어 캔버스의 메타를 세운다 - 빌드는 메타가 있는 에셋만 안다.
            Check(registry.Scan(stage.platform, assetRoot.c_str(), options, report), "the source assets scan again with the canvas");
            assets.Shutdown();
        }

        // 게임을 빌드하고 원본 폴더를 지운다.
        JBro::ProjectFile project;
        JBro::ProjectFileError projectError;
        const JBro::String projectUtf8 = utf8(projectPath);
        Check(JBro::LoadProjectFile(stage.platform, projectUtf8.c_str(), project, projectError), "the 3D project reads");
        JBro::GameBuildOptions buildOptions;
        buildOptions.physicsWorkers = 0;
        JBro::GameBuildReport built;
        const JBro::Bool ok = JBro::BuildGame(stage.platform, project, projectUtf8.c_str(), buildOptions, built);
        if (false == ok)
        {
            std::cout << "  build error: " << built.error.c_str() << std::endl;
        }
        for (const JBro::String& warning : built.warnings)
        {
            std::cout << "  build warning: " << warning.c_str() << std::endl;
        }
        Check(ok && built.warnings.IsEmpty(), "the 3D game builds cleanly");
        std::cout << "  3D game build: " << built.assets.Get() << " assets, " << built.packageBytes.Get() << " bytes" << std::endl;
        Check(built.cookedTextures == 1 && built.sharedTextures == 1, "the shared picture's pixels are cooked once");
        fs::remove_all(source);

        JBro::Package::PackageReader package;
        JBro::String error;
        Check(package.Open(stage.platform, utf8(root / "Out" / "Probe3D" / "Content" / "game.jpak").c_str(), error), "the built package opens");
        Check(package.Find(leftImage, JBro::Package::BlobKind::CookedTexture) != nullptr
                && package.Find(rightImage, JBro::Package::BlobKind::CookedTexture) == nullptr
                && package.Find(rightImage, JBro::Package::BlobKind::Record) != nullptr,
            "one image carries the pixels and the other is a record only");
        JBro::AssetRegistry packed;
        JBro::Package::FillRegistry(package, packed);
        const JBro::AssetRecord* packedRight = packed.Find(rightImage);
        Check(packedRight != nullptr && packedRight->name == "Bricks" && packedRight->sharedImageKey == key,
            "the game's sub-record has the loose name and the same picture key, filled from the model's meta");
        JBro::Package::PackageAssetSource packageSource(package);
        JBro::AssetSystem assets;
        Check(assets.Initialize(stage.memory), "the packaged asset system initializes");
        assets.Bind(stage.platform, packed, packageSource);
        // 픽셀이 없는 쪽을 먼저 싣는다 - 실린 쌍둥이가 없으니 같은 열쇠의 구운 블롭을 읽어야 선다.
        const JBro::AssetHandle twin = assets.Load(rightImage);
        Check(assets.GetTexture(twin) != nullptr && assets.GetTexture(twin)->width == 2,
            "the image shipped as a record loads first, from its twin's cooked pixels");
        assets.Release(twin);
        JBro::Framework3D framework;
        JBro::Array<std::byte> gameImage;
        initialize(assets, framework);
        JBro::Array<std::byte> canvasText;
        Check(assets.ReadSourceByPath("Canvases/Main.jcanvas", canvasText), "the startup canvas reads from the package");
        JBro::CanvasFileError canvasError;
        Check(JBro::ReadCanvasText(*framework.GetCanvas(), reinterpret_cast<const char*>(canvasText.Data()), canvasText.Size(), canvasError),
            "the packaged canvas loads");
        framework.BindCanvasAssets();
        stage.RenderOnce(framework, gameImage, readback);
        Check(framework.GetMaterialLibrary().GetUploadedMaterialCount() == 2 && framework.GetMaterialLibrary().GetUploadedTextureCount() == 1,
            "the game shares the one texture too");
        Check(gameImage.Size() == looseImage.Size() && std::memcmp(gameImage.Data(), looseImage.Data(), looseImage.Size()) == 0,
            "the game built from the package draws the very same picture as the loose project");
        Check(stage.renderer.GetDevice()->GetValidationErrorCount() == 0, "and the debug layer stayed quiet");
        framework.Shutdown();
        assets.Shutdown();
        package.Close();
        stage.Close();
        fs::remove_all(root);
    }
    // **3D 라이트가 메시를 비춘다**(D-353, lighting3d-plan 1 단계). 위에서 직교로 내려다본 바닥(흰 재질 × 회색 틴트)에 라이트를 내고, 다섯 자리의 픽셀을
    // 셰이더 식의 CPU 판(`ExpectLit`)과 견준다. 라이트가 없는 뷰는 기본 빛이고, 하나라도(환경광만이라도) 내면 낸 라이트만이다. 점광은 `range` 밖이 검고,
    // 스포트는 바깥 각 밖이 검다. 한 프레임의 두 뷰는 제 라이트를 따로 받고, 뷰 하나의 상한을 넘는 라이트는 버리고 센다.
    template <typename TModule>
    void TestLightsLightTheMeshes()
    {
        Stage<TModule> stage;
        if (false == stage.Open(true))
        {
            std::cout << "  [skip] no device for this API; 3D lights not verified" << std::endl;
            return;
        }
        JBro::Renderer& renderer = stage.renderer;
        JBro::MeshVertex corners[4];
        const float spots[4][2] = {{-8.0f, -8.0f}, {8.0f, -8.0f}, {8.0f, 8.0f}, {-8.0f, 8.0f}};
        for (int at = 0; at < 4; ++at)
        {
            corners[at].position[0] = spots[at][0];
            corners[at].position[1] = 0.0f;
            corners[at].position[2] = spots[at][1];
            corners[at].normal[0] = 0.0f;
            corners[at].normal[1] = 1.0f;
            corners[at].normal[2] = 0.0f;
        }
        const JBro::UInt32 indices[6] = {0, 1, 2, 0, 2, 3};
        const JBro::AssetHandle floor = renderer.RegisterMesh({corners, 4}, {indices, 6});
        Check(floor.generation != 0, "the floor registers");

        // 카메라는 (0, 10, 0) 에서 아래를 본다. 화면 위가 월드 -Z 이고, 화면 전체가 x -6..6, z -4..4 다.
        const JBro::Vector3 eye{0.0f, 10.0f, 0.0f};
        const auto cameraFor = [&](JBro::Float left, JBro::Float width) {
            JBro::CameraParams camera;
            camera.view = JBro::MakeViewMatrix(eye, JBro::FromAxisAngle({1.0f, 0.0f, 0.0f}, JBro::Radian(-1.5707963f)));
            Check(JBro::MakeOrthographicMatrix(4.0f, width / 64.0f, 0.1f, 100.0f, camera.projection), "the projection is made");
            camera.viewport.x = left;
            camera.viewport.width = width;
            camera.viewport.height = static_cast<float>(TargetHeight);
            return camera;
        };
        JBro::Array<std::byte> image;
        JBro::TextureReadback readback;
        struct ViewLights
        {
            JBro::Float left = 0.0f;
            JBro::Float width = 96.0f;
            const JBro::Light3DSubmit* lights = nullptr;
            int count = 0;
        };
        const auto render = [&](const ViewLights* views, int viewCount) {
            JBro::FrameTarget frameTarget;
            frameTarget.texture = stage.target;
            frameTarget.extent = {TargetWidth, TargetHeight};
            Check(renderer.BeginFrame(frameTarget) == JBro::FrameStatus::Ready, "the frame begins");
            for (int at = 0; at < viewCount; ++at)
            {
                Check(renderer.BeginView(cameraFor(views[at].left, views[at].width)), "the view begins");
                if (views[at].count != 0)
                {
                    renderer.SubmitLights3D({views[at].lights, static_cast<JBro::UInt32>(views[at].count)});
                }
                JBro::MeshSubmit item;
                item.mesh = floor;
                item.tint[0] = 0.5f;
                item.tint[1] = 0.5f;
                item.tint[2] = 0.5f;
                Check(renderer.SubmitMesh(item), "the floor is submitted");
                Check(renderer.EndView(), "the view ends");
            }
            Check(renderer.EndFrame() == JBro::FrameStatus::Ready, "the frame finishes");
            image.Resize(TargetWidth * TargetHeight * 4);
            Check(renderer.GetDevice()->ReadTexture(stage.target, image.Data(), image.Size(), readback), "the target reads back");
        };
        const float grey[3] = {0.5f, 0.5f, 0.5f};
        const float none[3] = {0.0f, 0.0f, 0.0f};
        // 픽셀 가운데의 월드 자리(뷰의 왼쪽 끝과 폭으로).
        const auto worldAt = [](JBro::UInt32 x, JBro::UInt32 y, float left, float width) {
            const float halfWidth = 4.0f * width / 64.0f;
            return JBro::Vector3{((static_cast<float>(x.Get()) + 0.5f - left) / width * 2.0f - 1.0f) * halfWidth, 0.0f,
                ((static_cast<float>(y.Get()) + 0.5f) / 64.0f * 2.0f - 1.0f) * 4.0f};
        };
        // (55, 32) 는 스포트의 안쪽 각과 바깥 각 사이(축에서 0.94 유닛)다 - 원뿔의 감쇠 곡선이 여기서 보인다.
        const JBro::UInt32 samples[][2] = {{8, 8}, {48, 32}, {55, 32}, {63, 32}, {80, 20}, {30, 50}, {44, 30}, {52, 34}};
        const auto matches = [&](const char* what, const float ambient[3], const JBro::Light3DSubmit* lights, int count) {
            for (const auto& sample : samples)
            {
                const Pixel expected = ExpectLit(grey, {0.0f, 1.0f, 0.0f}, worldAt(sample[0], sample[1], 0.0f, 96.0f), eye, 0.0f, 1.0f, ambient, lights, count);
                const Pixel actual = ReadPixel(image, readback.rowPitch, sample[0], sample[1]);
                if (false == (Near(actual.r, expected.r, 0.02f) && Near(actual.g, expected.g, 0.02f) && Near(actual.b, expected.b, 0.02f)))
                {
                    std::cout << "  at (" << sample[0].Get() << ", " << sample[1].Get() << ") got " << actual.r.Get() << ' ' << actual.g.Get() << ' ' << actual.b.Get()
                              << " expected " << expected.r.Get() << ' ' << expected.g.Get() << ' ' << expected.b.Get() << std::endl;
                    Check(false, what);
                }
            }
        };

        // 라이트가 없으면 기본 빛이다.
        const ViewLights unlit;
        render(&unlit, 1);
        const float defaultAmbient[3] = {0.25f, 0.25f, 0.25f};
        const JBro::Light3DSubmit sun = DefaultSun();
        matches("a view with no 3D light gets the built-in sun and ambient", defaultAmbient, &sun, 1);

        // 환경광만 내면 해는 없다.
        JBro::Light3DSubmit ambientLight;
        ambientLight.kind = JBro::Light3DKind::Ambient;
        ambientLight.color[0] = 0.6f;
        ambientLight.color[1] = 0.4f;
        ambientLight.color[2] = 0.2f;
        const ViewLights onlyAmbient{0.0f, 96.0f, &ambientLight, 1};
        render(&onlyAmbient, 1);
        const float ambientColour[3] = {0.6f, 0.4f, 0.2f};
        matches("an ambient light alone lights evenly and the built-in sun is gone", ambientColour, nullptr, 0);

        // 방향광은 제 방향에서 온다 - 기울면 덜 밝다.
        JBro::Light3DSubmit slanted;
        slanted.kind = JBro::Light3DKind::Directional;
        slanted.direction[0] = 0.6f;
        slanted.direction[1] = -0.8f;
        slanted.direction[2] = 0.0f;
        const ViewLights directional{0.0f, 96.0f, &slanted, 1};
        render(&directional, 1);
        matches("a directional light shades by its own direction", none, &slanted, 1);

        // 점광은 거리 제곱으로 줄고 range 에서 0 이다.
        JBro::Light3DSubmit bulb;
        bulb.kind = JBro::Light3DKind::Point;
        bulb.position[0] = 2.0f;
        bulb.position[1] = 1.0f;
        bulb.color[0] = 2.0f;
        bulb.color[1] = 2.0f;
        bulb.color[2] = 2.0f;
        bulb.range = 3.0f;
        const ViewLights point{0.0f, 96.0f, &bulb, 1};
        render(&point, 1);
        matches("a point light falls off with distance and its range window", none, &bulb, 1);
        Check(ReadPixel(image, readback.rowPitch, 64, 32).r > 0.5f && ReadPixel(image, readback.rowPitch, 8, 32).r == 0.0f,
            "under the bulb is bright and past its range is black");

        // 스포트는 원뿔 밖이 검다.
        JBro::Light3DSubmit torch;
        torch.kind = JBro::Light3DKind::Spot;
        torch.position[1] = 2.0f;
        torch.direction[0] = 0.0f;
        torch.direction[1] = -1.0f;
        torch.direction[2] = 0.0f;
        torch.color[0] = 6.0f;
        torch.color[1] = 6.0f;
        torch.color[2] = 6.0f;
        torch.innerAngle = JBro::Radian(0.6981317f);
        torch.outerAngle = JBro::Radian(1.0471976f);
        const ViewLights spot{0.0f, 96.0f, &torch, 1};
        render(&spot, 1);
        matches("a spot light is full inside its inner cone and fades to its outer cone", none, &torch, 1);
        Check(ReadPixel(image, readback.rowPitch, 48, 32).r > 0.5f && ReadPixel(image, readback.rowPitch, 63, 32).r == 0.0f,
            "the spot's centre is lit and past its outer cone is black");

        // 모두 함께: 셋의 합과 환경광.
        const JBro::Light3DSubmit all[4] = {ambientLight, slanted, bulb, torch};
        const ViewLights together{0.0f, 96.0f, all, 4};
        render(&together, 1);
        matches("lights add up", ambientColour, all + 1, 3);

        // 한 프레임의 두 뷰: 왼쪽은 환경광만, 오른쪽은 라이트 없음(기본 빛). 그다음 셋째 뷰가 왼쪽과 같은 라이트를 다시 낸다.
        const ViewLights split[2] = {{0.0f, 48.0f, &ambientLight, 1}, {48.0f, 48.0f, nullptr, 0}};
        render(split, 2);
        const Pixel leftExpected = ExpectLit(grey, {0.0f, 1.0f, 0.0f}, worldAt(20, 20, 0.0f, 48.0f), eye, 0.0f, 1.0f, ambientColour, nullptr, 0);
        const Pixel rightExpected = ExpectLit(grey, {0.0f, 1.0f, 0.0f}, worldAt(70, 20, 48.0f, 48.0f), eye, 0.0f, 1.0f, defaultAmbient, &sun, 1);
        const Pixel leftActual = ReadPixel(image, readback.rowPitch, 20, 20);
        const Pixel rightActual = ReadPixel(image, readback.rowPitch, 70, 20);
        Check(Near(leftActual.r, leftExpected.r, 0.02f) && Near(leftActual.b, leftExpected.b, 0.02f)
                && Near(rightActual.r, rightExpected.r, 0.02f) && Near(rightActual.b, rightExpected.b, 0.02f),
            "each view of a frame has its own lights - one view's lights do not reach the next");
        const ViewLights again[2] = {{0.0f, 48.0f, &ambientLight, 1}, {48.0f, 48.0f, &ambientLight, 1}};
        render(again, 2);
        Check(Near(ReadPixel(image, readback.rowPitch, 70, 20).r, leftExpected.r, 0.02f) && Near(ReadPixel(image, readback.rowPitch, 70, 20).b, leftExpected.b, 0.02f),
            "a view with the same lights as the one before draws the same");

        // 뷰 하나의 상한을 넘는 라이트는 버리고 센다.
        JBro::Light3DSubmit many[70];
        for (JBro::Light3DSubmit& one : many)
        {
            one.kind = JBro::Light3DKind::Point;
            one.position[1] = 50.0f;
            one.range = 0.1f;
        }
        const ViewLights crowded{0.0f, 96.0f, many, 70};
        render(&crowded, 1);
        const JBro::RendererFrameStats stats = renderer.GetLastFrameStats();
        Check(stats.light3DCount == 70 && stats.droppedLight3DCount == 70 - JBro::Renderer::MaxLights3DPerView,
            "lights past the per-view limit are dropped and counted");
        Check(renderer.GetDevice()->GetValidationErrorCount() == 0, "and the debug layer stayed quiet");
        renderer.UnregisterMesh(floor);
        stage.Close();
    }
    // **`Light3D` 컴포넌트가 캔버스를 비춘다**(D-353, lighting3d-plan 2 단계). 위에서 직교로 내려다본 바닥(빌트인 평면)을 프레임워크로 그린다.
    // (1) 새 프로젝트의 시작 라이트와 같은 방향광(오브젝트 회전으로 돌린 -Z)·환경광은 라이트가 없을 때의 기본 빛과 같은 그림이다 - 회전이 방향이 된다.
    // (2) 감춘 레이어의 라이트는 꺼진다. 점광과 스포트(회전으로 아래를 본다)가 셰이더 식의 CPU 판과 같다.
    // (3) 캔버스 파일로 쓰고 다시 읽어도 라이트의 값과 그림이 같다.
    template <typename TModule>
    void TestLightComponentsLightTheCanvas()
    {
        Stage<TModule> stage;
        if (false == stage.Open(true))
        {
            std::cout << "  [skip] no device for this API; light components not verified" << std::endl;
            return;
        }
        JBro::Framework3D framework;
        JBro::FrameworkContext context;
        JBro::Testing::AttachClock(context);
        context.renderer = &stage.renderer;
        Check(framework.Initialize(context), "the 3D framework initializes");
        JBro::Canvas& canvas = *framework.GetCanvas();
        const JBro::Vector3 eyeAt{0.0f, 10.0f, 0.0f};
        const auto build = [&](JBro::Canvas& target) {
            JBro::Object::GameObject* eye = target.CreateObject("eye");
            auto* eyeTransform = target.AttachComponent<JBro::Component::Transform3D>(eye);
            eyeTransform->position = eyeAt;
            eyeTransform->rotation = JBro::FromAxisAngle({1.0f, 0.0f, 0.0f}, JBro::Radian(-1.5707963f));
            auto* camera = target.AttachComponent<JBro::Component::Camera3D>(eye);
            camera->primary = true;
            camera->projection = JBro::Component::CameraProjection3D::Orthographic;
            camera->orthographicSize = 4.0f;
            camera->clearColor = {0.0f, 0.0f, 0.0f, 1.0f};
            JBro::Object::GameObject* floor = target.CreateObject("floor");
            // 바닥은 원점보다 반 유닛 아래다 - 원점에 놓인 라이트(자리가 없어 원점으로 잘못 친 것)가 바닥을 스치지 않고 비추게 한다.
            auto* floorTransform = target.AttachComponent<JBro::Component::Transform3D>(floor);
            floorTransform->position = {0.0f, -0.5f, 0.0f};
            floorTransform->scale = {2.0f, 1.0f, 2.0f};
            auto* mesh = target.AttachComponent<JBro::Component::MeshRenderer3D>(floor);
            for (JBro::UInt32 at = 0; at < JBro::MeshLibrary::GetBuiltinCount(); ++at)
            {
                if (std::string(JBro::MeshLibrary::GetBuiltin(at).name) == "Plane")
                {
                    mesh->meshId = JBro::MeshLibrary::GetBuiltin(at).id;
                }
            }
            mesh->tint = {0.5f, 0.5f, 0.5f, 1.0f};
        };
        build(canvas);
        JBro::Array<std::byte> image;
        JBro::TextureReadback readback;
        const auto renderTwice = [&]() {
            // 첫 프레임은 트랜스폼의 월드 값이 서는 프레임이다. 둘째를 본다.
            stage.RenderOnce(framework, image, readback);
            stage.RenderOnce(framework, image, readback);
        };
        renderTwice();
        const JBro::Array<std::byte> builtIn = image;

        // (1) 시작 라이트와 같은 한 쌍: 회전으로 돌린 방향광 0.75 와 환경광 0.25.
        JBro::Object::GameObject* lamp = canvas.CreateObject("Light");
        {
            auto* transform = canvas.AttachComponent<JBro::Component::Transform3D>(lamp);
            const JBro::Vector3 from{0.0f, 0.0f, -1.0f};
            const JBro::Vector3 to = JBro::Normalize(JBro::Vector3{-0.4f, -0.8f, -0.45f});
            const JBro::Vector3 axis = JBro::Cross(from, to);
            transform->position = {2.0f, 4.0f, 2.0f};
            transform->rotation = JBro::Normalize(JBro::Quaternion{axis.x, axis.y, axis.z, 1.0f + JBro::Dot(from, to)});
            auto* sun = canvas.AttachComponent<JBro::Component::Light3D>(lamp);
            sun->type = JBro::Component::Light3DType::Directional;
            sun->intensity = 0.75f;
            auto* ambient = canvas.AttachComponent<JBro::Component::Light3D>(lamp);
            ambient->type = JBro::Component::Light3DType::Ambient;
            ambient->intensity = 0.25f;
        }
        renderTwice();
        Check(stage.renderer.GetLastFrameStats().light3DCount == 2, "both lights of the lamp reach the renderer");
        int largest = 0;
        for (std::size_t at = 0; at < image.Size(); ++at)
        {
            largest = std::max(largest, std::abs(static_cast<int>(image[at]) - static_cast<int>(builtIn[at])));
        }
        Check(largest <= 2, "a sun turned by the object's rotation and an ambient light draw what the built-in light draws");

        const float grey[3] = {0.5f, 0.5f, 0.5f};
        const float none[3] = {0.0f, 0.0f, 0.0f};
        const auto worldAt = [](JBro::UInt32 x, JBro::UInt32 y) {
            return JBro::Vector3{((static_cast<float>(x.Get()) + 0.5f) / 96.0f * 2.0f - 1.0f) * 6.0f, -0.5f,
                ((static_cast<float>(y.Get()) + 0.5f) / 64.0f * 2.0f - 1.0f) * 4.0f};
        };
        const auto matches = [&](const char* what, const JBro::Light3DSubmit& light) {
            const JBro::UInt32 samples[][2] = {{8, 8}, {48, 32}, {55, 32}, {63, 32}, {80, 20}, {30, 50}};
            for (const auto& sample : samples)
            {
                const Pixel expected = ExpectLit(grey, {0.0f, 1.0f, 0.0f}, worldAt(sample[0], sample[1]), eyeAt, 0.0f, 1.0f, none, &light, 1);
                const Pixel actual = ReadPixel(image, readback.rowPitch, sample[0], sample[1]);
                if (false == (Near(actual.r, expected.r, 0.02f) && Near(actual.g, expected.g, 0.02f) && Near(actual.b, expected.b, 0.02f)))
                {
                    std::cout << "  at (" << sample[0].Get() << ", " << sample[1].Get() << ") got " << actual.r.Get() << " expected " << expected.r.Get() << std::endl;
                    Check(false, what);
                }
            }
        };

        // (2) 시작 라이트를 감춘 레이어로 옮기고 점광 하나를 둔다.
        JBro::Layer& lamps = canvas.CreateLayer("Lamps");
        Check(canvas.SetObjectLayer(lamp, lamps.GetId()), "the lamp goes on its own layer");
        lamps.SetVisible(false);
        JBro::Object::GameObject* bulbObject = canvas.CreateObject("bulb");
        canvas.AttachComponent<JBro::Component::Transform3D>(bulbObject)->position = {2.0f, 1.0f, 0.0f};
        auto* bulb = canvas.AttachComponent<JBro::Component::Light3D>(bulbObject);
        bulb->type = JBro::Component::Light3DType::Point;
        bulb->intensity = 2.0f;
        bulb->range = 3.0f;
        renderTwice();
        JBro::Light3DSubmit expectedBulb;
        expectedBulb.kind = JBro::Light3DKind::Point;
        expectedBulb.position[0] = 2.0f;
        expectedBulb.position[1] = 1.0f;
        expectedBulb.color[0] = 2.0f;
        expectedBulb.color[1] = 2.0f;
        expectedBulb.color[2] = 2.0f;
        expectedBulb.range = 3.0f;
        matches("a point light component lights by its place and range, and a hidden layer's lights are off", expectedBulb);

        // 스포트: 회전으로 아래를 본다. 점광도 감춘 레이어로.
        Check(canvas.SetObjectLayer(bulbObject, lamps.GetId()), "the bulb goes on the hidden layer");
        JBro::Object::GameObject* torchObject = canvas.CreateObject("torch");
        auto* torchTransform = canvas.AttachComponent<JBro::Component::Transform3D>(torchObject);
        torchTransform->position = {0.0f, 2.0f, 0.0f};
        torchTransform->rotation = JBro::FromAxisAngle({1.0f, 0.0f, 0.0f}, JBro::Radian(-1.5707963f));
        auto* torch = canvas.AttachComponent<JBro::Component::Light3D>(torchObject);
        torch->type = JBro::Component::Light3DType::Spot;
        torch->intensity = 6.0f;
        torch->range = 0.0f;
        torch->innerAngle = 40.0f;
        torch->outerAngle = 60.0f;
        // 자리가 없는(트랜스폼 없는) 점광은 빛을 내지 않는다 - 원점에 있는 것으로 치지 않는다.
        JBro::Object::GameObject* loose = canvas.CreateObject("loose");
        auto* looseLight = canvas.AttachComponent<JBro::Component::Light3D>(loose);
        looseLight->type = JBro::Component::Light3DType::Point;
        looseLight->intensity = 50.0f;
        looseLight->range = 0.0f;
        renderTwice();
        JBro::Light3DSubmit expectedTorch;
        expectedTorch.kind = JBro::Light3DKind::Spot;
        expectedTorch.position[1] = 2.0f;
        expectedTorch.direction[0] = 0.0f;
        expectedTorch.direction[1] = -1.0f;
        expectedTorch.direction[2] = 0.0f;
        expectedTorch.color[0] = 6.0f;
        expectedTorch.color[1] = 6.0f;
        expectedTorch.color[2] = 6.0f;
        expectedTorch.innerAngle = JBro::Radian(0.6981317f);
        expectedTorch.outerAngle = JBro::Radian(1.0471976f);
        matches("a spot light component points along the object's -Z, and a light with no transform gives no light", expectedTorch);
        const JBro::Array<std::byte> spotImage = image;

        // (3) 캔버스 파일 왕복.
        JBro::String text;
        JBro::CanvasFileError error;
        Check(JBro::WriteCanvasText(canvas, text, error), "the canvas writes");
        // 파일은 빈 캔버스에만 읽힌다 - 프레임워크를 다시 세워 새 캔버스에 읽는다.
        framework.Shutdown();
        Check(framework.Initialize(context), "the 3D framework initializes again");
        JBro::Canvas& reread = *framework.GetCanvas();
        Check(JBro::ReadCanvasText(reread, text.c_str(), text.size(), error), "the canvas reads back");
        JBro::Object::GameObject* readTorch = nullptr;
        reread.ForEach<JBro::Component::Light3D>([&](JBro::Component::Light3D& light) {
            if (light.type == JBro::Component::Light3DType::Spot)
            {
                readTorch = JBro::Internal::CanvasAccess::GetOwner(light);
                Check(light.intensity == 6.0f && light.range == 0.0f && Near(light.innerAngle.Get(), 40.0f, 0.001f) && Near(light.outerAngle.Get(), 60.0f, 0.001f),
                    "the spot's values survive the canvas file");
            }
        });
        Check(readTorch != nullptr, "the spot light is read back");
        renderTwice();
        Check(image.Size() == spotImage.Size() && std::memcmp(image.Data(), spotImage.Data(), image.Size()) == 0,
            "the canvas read back draws the very same lights");
        Check(stage.renderer.GetDevice()->GetValidationErrorCount() == 0, "and the debug layer stayed quiet");
        framework.Shutdown();
        stage.Close();
    }
}

JBro::Int32 RunMeshPixelTests()
{
    JBRO_CASE(TestLightComponentsLightTheCanvas<JBro::D3D12RHIModule>());
    JBRO_CASE(TestLightComponentsLightTheCanvas<JBro::D3D11RHIModule>());
    JBRO_CASE(TestLightComponentsLightTheCanvas<JBro::VulkanRHIModule>());
    JBRO_CASE(TestLightsLightTheMeshes<JBro::D3D12RHIModule>());
    JBRO_CASE(TestLightsLightTheMeshes<JBro::D3D11RHIModule>());
    JBRO_CASE(TestLightsLightTheMeshes<JBro::VulkanRHIModule>());
    JBRO_CASE(TestA3DGameBuildDrawsWhatTheEditorDraws<JBro::D3D12RHIModule>());
    JBRO_CASE(TestA3DGameBuildDrawsWhatTheEditorDraws<JBro::D3D11RHIModule>());
    JBRO_CASE(TestA3DGameBuildDrawsWhatTheEditorDraws<JBro::VulkanRHIModule>());
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
