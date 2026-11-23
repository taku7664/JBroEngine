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
#include <JBro/Framework3DSystem/Rendering/EnvironmentLibrary.h>
#include <JBro/Task/TaskManager.h>
#include <JBro/Framework2D/Component/Button2D.h>
#include <JBro/Framework2D/Component/SpriteRenderer2D.h>
#include <JBro/Framework2D/Component/Transform2D.h>
#include <JBro/Input/InputSystem.h>
#include <JBro/Canvas/ComponentRegistry.h>
#include <JBro/Framework3D/Scripting/GameScript.h>
#include <JBro/InputTypes/InputHandler.h>
#include <JBro/Graphics/Renderer.h>
#include <JBro/Platform/WindowsPlatform.h>
#include <JBro/Types/Array.h>
#include <JBro/Types/Math3D.h>
#include <JBro/Asset/Asset.h>
#include <JBro/Asset/AssetMetaFile.h>
#include <JBro/Asset/AssetRegistry.h>
#include <JBro/Asset/RenderTextureFile.h>
#include "TestModels.h"
#include <JBro/Canvas/CanvasFile.h>
#include <JBro/Host/GameBuild.h>
#include <JBro/Host/ProjectFile.h>
#include <JBro/Package/PackageAssetSource.h>
#include <JBro/Package/PackageReader.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
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

    // **3D 라이트의 셈**(D-407, `BuiltinMesh.hlsl` 의 라이트 루프). 렌더러가 `Light3DSubmit` 을 옮기는 셈(방향의 정규화, 원뿔 반각의 cos 에서 scale·offset)과
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
            // 갱신에서 처음 만난 모델 메시를 올리는 길(D-395)은 이 차례에서만 선다. 전에는 거꾸로 불러도 빌트인만 있어 드러나지 않았다.
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

    // **3D 도 뷰포트마다 제 사각형에 그린다**(D-438). 왼쪽 절반은 카메라 A(검은 바탕)가 붉은 상자를, 오른쪽 절반은 카메라 B(초록 바탕)가
    // 멀리 있는 푸른 상자를 본다. 둘째 뷰포트는 제 사각형을 제 바탕색으로 칠한다(렌더러는 대상을 첫 뷰에서만 지운다). 레이어 필터와 칠하기 끄기도 잰다.
    template <typename TModule>
    void TestViewportsSplitTheScreenIn3D()
    {
        Stage<TModule> stage;
        if (false == stage.Open())
        {
            std::cout << "  [skip] no device for this API; 3D viewports not verified" << std::endl;
            return;
        }
        JBro::Framework3D framework;
        JBro::FrameworkContext context;
        JBro::Testing::AttachClock(context);
        context.renderer = &stage.renderer;
        Check(framework.Initialize(context), "the 3D framework must initialize with the renderer");
        JBro::Canvas& canvas = *framework.GetCanvas();

        JBro::Object::GameObject* eyeA = canvas.CreateObject("eyeA");
        canvas.AttachComponent<JBro::Component::Transform3D>(eyeA)->position = {0.0f, 0.0f, 3.0f};
        canvas.AttachComponent<JBro::Component::Camera3D>(eyeA)->clearColor = {0.0f, 0.0f, 0.0f, 1.0f};
        JBro::Object::GameObject* eyeB = canvas.CreateObject("eyeB");
        canvas.AttachComponent<JBro::Component::Transform3D>(eyeB)->position = {100.0f, 0.0f, 3.0f};
        canvas.AttachComponent<JBro::Component::Camera3D>(eyeB)->clearColor = {0.0f, 1.0f, 0.0f, 1.0f};
        PlaceCube(canvas, "red", {0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, {1.0f, 0.0f, 0.0f, 1.0f});
        JBro::Object::GameObject* blue = PlaceCube(canvas, "blue", {100.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 1.0f, 1.0f});
        JBro::Layer& far = canvas.CreateLayer("Far");
        Check(canvas.SetObjectLayer(blue, far.GetId()), "the blue cube goes on its own layer");
        {
            JBro::Array<JBro::CanvasViewport> halves;
            JBro::CanvasViewport left;
            left.camera = eyeA->GetInstanceId();
            left.size = {0.5f, 1.0f};
            JBro::CanvasViewport right;
            right.camera = eyeB->GetInstanceId();
            right.origin = {0.5f, 0.0f};
            right.size = {0.5f, 1.0f};
            halves.Add(left);
            halves.Add(right);
            canvas.SetViewports(halves);
        }

        JBro::Array<std::byte> image;
        JBro::TextureReadback readback;
        stage.RenderOnce(framework, image, readback);
        const Pixel leftMiddle = ReadPixel(image, readback.rowPitch, TargetWidth / 4, TargetHeight / 2);
        const Pixel rightMiddle = ReadPixel(image, readback.rowPitch, TargetWidth * 3 / 4, TargetHeight / 2);
        // 상자의 다른 두 채널은 주변광만큼(약 0.06) 남는다.
        Check(leftMiddle.r > 0.5f && leftMiddle.b < 0.15f && leftMiddle.g < 0.15f, "the left half shows camera A's red cube");
        Check(rightMiddle.b > 0.5f && rightMiddle.r < 0.15f && rightMiddle.g < 0.15f, "the right half shows camera B's blue cube, a hundred units away");
        const Pixel leftCorner = ReadPixel(image, readback.rowPitch, 2, 2);
        const Pixel rightCorner = ReadPixel(image, readback.rowPitch, TargetWidth - 3, 2);
        Check(Near(leftCorner.g, 0.0f) && Near(leftCorner.r, 0.0f), "the left half keeps camera A's black clear");
        Check(rightCorner.g > 0.9f && rightCorner.r < 0.05f, "the second viewport paints its rectangle with camera B's green clear");
        Check(stage.renderer.GetLastFrameStats().viewCount >= 2, "two cameras, at least two views");

        // 오른쪽 뷰포트가 기본 레이어만 그리면 "Far" 의 푸른 상자가 빠지고 초록 바탕이 보인다.
        canvas.GetViewportAt(1)->layers.Add(canvas.GetDefaultLayer());
        stage.RenderOnce(framework, image, readback);
        const Pixel filtered = ReadPixel(image, readback.rowPitch, TargetWidth * 3 / 4, TargetHeight / 2);
        Check(filtered.g > 0.9f && filtered.b < 0.05f, "a viewport's layer filter leaves the other layer's cube out");

        // 칠하기를 끄면 둘째 뷰포트는 첫 뷰의 검은 바탕 위에 그린다.
        canvas.GetViewportAt(1)->clearViewport = false;
        stage.RenderOnce(framework, image, readback);
        const Pixel unpainted = ReadPixel(image, readback.rowPitch, TargetWidth - 3, 2);
        Check(Near(unpainted.g, 0.0f), "with its clear off the second viewport leaves the first view's clear");
        Check(stage.renderer.GetDevice()->GetValidationErrorCount() == 0, "and the debug layer stayed quiet");

        framework.Shutdown();
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
    // 3D 스크립트 런타임(D-363)의 손잡이다. 수명 훅과 화면 레이어 버튼의 UI 훅을 센다.
    class UiProbe3D final : public JBro::GameScript3D
    {
    public:
        static constexpr const char* StaticTypeName()
        {
            return "Tests::UiProbe3D";
        }

        JBro::ComponentTypeId GetTypeId() const override
        {
            return JBro::MakeStableTypeId(StaticTypeName());
        }

        void OnStart() override
        {
            ++starts;
        }

        void OnUpdate() override
        {
            ++updates;
        }

        void OnPointerDown() override
        {
            ++downs;
        }

        void OnClick() override
        {
            ++clicks;
        }

        JBro::Int32 starts = 0;
        JBro::Int32 updates = 0;
        JBro::Int32 downs = 0;
        JBro::Int32 clicks = 0;
    };

    // `"Game"` 레이어의 3D 스크립트 핸들러다. `"UI"` 의 위젯이 마우스를 쓰면 빈 마우스를 본다.
    class GameInputProbe3D final : public JBro::GameScript3D, public JBro::InputHandler<"Game", 0>
    {
    public:
        static constexpr const char* StaticTypeName()
        {
            return "Tests::GameInputProbe3D";
        }

        JBro::ComponentTypeId GetTypeId() const override
        {
            return JBro::MakeStableTypeId(StaticTypeName());
        }

        JBro::InputResult OnInput(JBro::InputView& input) override
        {
            ++calls;
            sawMouse = input.Mouse().IsDown(JBro::MouseButton::Left);
            return JBro::InputResult::Pass;
        }

        JBro::Int32 calls = 0;
        JBro::Bool sawMouse = false;
    };

    // **3D 캔버스의 화면 레이어는 화면 UI 다**(D-362). 정육면체는 빛을 받고, 화면 레이어의 흰 사각형은 3D 뷰 위에 빛 없이 흰색 그대로다. 화면 레이어의
    // 정육면체는 그려지지 않는다. 버튼은 3D 에도 입력을 받아 누르면 색이 바뀐다 - 스크립트의 입력 체인이 없는 3D 는 프레임워크가 위젯에 직접 넘긴다.
    template <typename TModule>
    void TestScreenLayersDrawScreenUiOverThe3DView()
    {
        Stage<TModule> stage;
        if (false == stage.Open())
        {
            std::cout << "  [skip] no device for this API; 3D screen UI not verified" << std::endl;
            return;
        }
        JBro::System::InputSystem input;
        JBro::Framework3D framework;
        JBro::FrameworkContext context;
        JBro::Testing::AttachClock(context);
        context.renderer = &stage.renderer;
        context.input = &input;
        Check(framework.Initialize(context), "the 3D framework must initialize with the renderer");
        Check(JBro::ComponentRegistry::Get().Find(JBro::Component::Button2D::StaticTypeName()) != nullptr
                && JBro::ComponentRegistry::Get().Find(JBro::Component::Transform2D::StaticTypeName()) != nullptr,
            "a 3D framework opens the screen UI components by name");
        JBro::ScreenSpaceFrame screen;
        screen.referenceWidth = static_cast<JBro::Float>(TargetWidth);
        screen.referenceHeight = static_cast<JBro::Float>(TargetHeight);
        screen.targetWidth = static_cast<JBro::Float>(TargetWidth);
        screen.targetHeight = static_cast<JBro::Float>(TargetHeight);
        framework.SetScreenSpace(screen);
        JBro::Canvas& canvas = *framework.GetCanvas();

        JBro::Object::GameObject* eye = canvas.CreateObject("eye");
        canvas.AttachComponent<JBro::Component::Transform3D>(eye)->position = {0.0f, 0.0f, 3.0f};
        auto* camera = canvas.AttachComponent<JBro::Component::Camera3D>(eye);
        camera->clearColor = {0.0f, 0.0f, 0.0f, 1.0f};
        PlaceCube(canvas, "box", {0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, {1.0f, 1.0f, 1.0f, 1.0f});

        JBro::Layer& hud = canvas.CreateLayer("HUD");
        hud.SetSpace(JBro::LayerSpace::Screen);
        // 화면 레이어에 둔 정육면체는 3D 카메라로 그리지 않는다 - 왼쪽(-1.5) 자리가 비어 있어야 한다.
        JBro::Object::GameObject* stray = PlaceCube(canvas, "stray", {-1.5f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, {1.0f, 1.0f, 1.0f, 1.0f});
        Check(canvas.SetObjectLayer(stray, hud.GetId()), "the stray cube goes on the screen layer");
        // 오른쪽 위 구석의 12 x 12 사각형: 화면 픽셀 x 78..89, y 2..13.
        JBro::Object::GameObject* panel = canvas.CreateObject("panel");
        Check(canvas.SetObjectLayer(panel, hud.GetId()), "the panel goes on the screen layer");
        auto* place = canvas.AttachComponent<JBro::Component::Transform2D>(panel);
        place->anchor = {1.0f, 1.0f};
        place->position = {-12.0f, -8.0f};
        auto* sprite = canvas.AttachComponent<JBro::Component::SpriteRenderer2D>(panel);
        sprite->sizeMode = JBro::Component::SpriteSizeMode::Custom;
        sprite->size = {12.0f, 12.0f};
        auto* button = canvas.AttachComponent<JBro::Component::Button2D>(panel);
        button->size = {12.0f, 12.0f};
        // 3D 스크립트도 돈다(D-363): 버튼의 오브젝트에 붙은 스크립트가 수명 훅과 UI 훅을 받고, 아래 레이어의 핸들러는 위젯이 쓴 마우스를 보지 못한다.
        UiProbe3D* probe = canvas.AttachScript<UiProbe3D>(panel);
        GameInputProbe3D* gameInput = canvas.AttachScript<GameInputProbe3D>(eye);

        JBro::Array<std::byte> image;
        JBro::TextureReadback readback;
        input.BeginFrame({});
        stage.RenderOnce(framework, image, readback);
        const Pixel cube = ReadPixel(image, readback.rowPitch, TargetWidth / 2, TargetHeight / 2);
        Check(cube.r > 0.2f && cube.r < 0.999f, "the cube is drawn and lit");
        const Pixel hudPixel = ReadPixel(image, readback.rowPitch, 84, 8);
        Check(Near(hudPixel.r, 1.0f) && Near(hudPixel.g, 1.0f) && Near(hudPixel.b, 1.0f), "the screen square is white - over the 3D view and unlit");
        const Pixel outside = ReadPixel(image, readback.rowPitch, 84, 20);
        Check(outside.r < 0.05f, "nothing is drawn below the square");
        const Pixel strayPixel = ReadPixel(image, readback.rowPitch, 12, TargetHeight / 2);
        Check(strayPixel.r < 0.05f, "a cube on the screen layer is not drawn by the 3D camera");
        Check(probe->starts == 1 && probe->updates == 1, "a 3D script starts and updates in its first frame");
        // 입력 체인은 시작 훅보다 먼저 돌고 시작한 스크립트만 부른다(2D 와 같다) - 첫 프레임에는 아직 부르지 않는다.
        Check(gameInput->calls == 0, "the input chain waits for the script to start");

        // 마우스를 사각형 위에서 누르면 버튼이 눌리고 색이 pressedTint(0.7) 가 된다.
        JBro::InputEvent move;
        move.kind = JBro::InputEventKind::MouseMove;
        move.x = 84.0f;
        move.y = 8.0f;
        JBro::InputEvent down;
        down.kind = JBro::InputEventKind::MouseButtonDown;
        down.button = JBro::MouseButton::Left;
        const JBro::InputEvent press[] = {move, down};
        input.BeginFrame({press, 2});
        stage.RenderOnce(framework, image, readback);
        Check(button->pressed, "the button takes the press through the 3D framework");
        Check(probe->downs == 1 && probe->clicks == 0, "the 3D script on the button hears the press");
        Check(gameInput->calls == 1 && false == gameInput->sawMouse, "the game layer below sees no mouse the button took");
        const Pixel pressed = ReadPixel(image, readback.rowPitch, 84, 8);
        Check(Near(pressed.r, 0.7f, 0.02f) && Near(pressed.b, 0.7f, 0.02f), "the pressed tint shows on the screen square");

        // 멈추면 위젯도 입력을 받지 않는다(2D 와 같다).
        JBro::InputEvent up;
        up.kind = JBro::InputEventKind::MouseButtonUp;
        up.button = JBro::MouseButton::Left;
        framework.SetSimulationEnabled(false);
        const JBro::Int32 updatesBeforePause = probe->updates;
        input.BeginFrame({&up, 1});
        stage.RenderOnce(framework, image, readback);
        Check(button->pressed, "paused, the button does not see the release");
        Check(probe->updates == updatesBeforePause, "paused, the 3D script does not update");
        framework.SetSimulationEnabled(true);
        input.BeginFrame({});
        stage.RenderOnce(framework, image, readback);
        Check(false == button->pressed, "running again, the button sees the mouse is up");
        Check(probe->updates == updatesBeforePause + 1 && probe->clicks == 0, "and the script runs again; a release seen only after the pause is no click");

        // 누르고 떼면 `OnClick` 이다.
        input.BeginFrame({press, 2});
        stage.RenderOnce(framework, image, readback);
        input.BeginFrame({&up, 1});
        stage.RenderOnce(framework, image, readback);
        Check(probe->clicks == 1, "pressing and releasing on the button clicks it for the 3D script");

        framework.Shutdown();
        stage.Close();
    }

    // **빌트인 도형마다 카메라를 향한 면이 빛을 받는다**(D-388 우선순위 2). 흰 도형 하나를 가운데 두고 가운데 픽셀이 고정 광원의
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

    // **모델의 메시가 그려진다**(D-391·D-395, model-import-plan 4 단계). 레지스트리 스캔 → 해석 패스(`BindCanvasAssets`)가 `meshId` 로 에셋 핸들을 채움 →
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
            // 같은 회색을 `colorSpace: Linear` 로 둔 그림(todo-3d 다음 순서 4, D-442). 메타를 손으로 적어 옵션을 준다.
            std::ofstream linear(root / "art" / "linear.tga", std::ios::binary);
            linear.write(grey.data(), static_cast<std::streamsize>(grey.size()));
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
        // 스캐너가 지은 메타에서 옵션만 `Linear` 로 바꿔 다시 쓴다 - 에디터의 임포트 옵션 편집과 같은 파일이다.
        {
            const fs::path metaPath = root / "art" / "linear.tga.jmeta";
            std::ifstream in(metaPath, std::ios::binary);
            const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            in.close();
            JBro::AssetMetaFile meta;
            JBro::AssetMetaError metaError;
            Check(JBro::ParseAssetMetaFile(text.c_str(), text.size(), meta, metaError), "the scanner wrote a meta for the linear grey picture");
            meta.hasTextureOptions = true;
            meta.textureOptions.colorSpace = JBro::TextureColorSpace::Linear;
            WriteText(metaPath, JBro::FormatAssetMetaFile(meta).c_str());
        }
        const JBro::AssetRecord* linearRecord = registry.FindByPath("art/linear.tga");
        Check(linearRecord != nullptr, "the linear grey picture is registered");
        const std::string flatLinear = "Version: 1\nMaterial:\n  shader: Unlit\n  baseColorTextureId: " + IdText(linearRecord->id) + "\n";
        WriteText(root / "art" / "flat_linear.jmat", flatLinear.c_str());
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
        Check(framework.GetMeshMaterialLibrary().GetUploadedMaterialCount() == 1 &&framework.GetMeshMaterialLibrary().GetUploadedTextureCount() == 1,
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

        // **그림의 `colorSpace` 가 칸의 기본을 이긴다**(D-442). 같은 회색을 선형으로 두면 0.502 를 빛의 양으로 읽고 출력이 sRGB 로 싸 0.735 가 된다.
        mesh->materialId = registry.FindByPath("art/flat_linear.jmat")->id;
        mesh->material = {};
        framework.BindCanvasAssets();
        stage.RenderOnce(framework, image, readback);
        const Pixel linearGrey = ReadPixel(image, readback.rowPitch, TargetWidth / 2, TargetHeight / 2);
        std::cout << "  unlit linear grey: " << linearGrey.r.Get() << std::endl;
        Check(Near(linearGrey.r, EncodeSrgb(128.0f / 255.0f), 0.02f),
            "a picture imported as Linear is not decoded through an sRGB view - the same grey comes out lighter");

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
        Check(stage.renderer.GetMeshMaterialCount() == 0, "shutting the framework down unregisters its materials");
        assets.Shutdown();
        stage.Close();
        fs::remove_all(root);
    }
    // **그림자**(D-445). 위에서 내려다보는 바닥(빌트인 평면, 흰 기본 재질) 위에 정육면체가 떠 있고((0, 1.5, 0), 한 변 1), 해는 +X 아래쪽으로 45 도 기울어
    // 비춘다 - 바닥의 (1.5, 0, 0) 은 정육면체의 그림자 안이고 (-1.5, 0, 0) 은 빛을 받는다. 환경광이 없어 그림자 안은 검다.
    // 짙기 0.5 면 그림자 안이 빛의 절반(선형)이고, 정육면체가 드리우지 않거나 바닥이 받지 않거나 해가 드리우지 않으면 그 자리도 밝다.
    // 끝으로 해를 끄고 (-2, 3.5, 0) 에서 같은 쪽으로 비추는 스포트가 같은 자리에 그림자를 드리운다 - (1.5, 0, 1.5) 는 그 원뿔 안에서 빛을 받는다.
    template <typename TModule>
    void TestShadowsFallBehindMeshes()
    {
        Stage<TModule> stage;
        if (false == stage.Open())
        {
            std::cout << "  [skip] no device for this API; shadows not verified" << std::endl;
            return;
        }
        namespace fs = std::filesystem;
        const fs::path root = fs::temp_directory_path() / L"JBroShadowProbe·그림자";
        fs::remove_all(root);
        fs::create_directories(root);
        const std::u8string rootText = root.u8string();
        const JBro::String rootPath(reinterpret_cast<const char*>(rootText.data()), rootText.size());
        JBro::AssetRegistry registry;
        JBro::AssetScanOptions options;
        options.createMissingMeta = true;
        JBro::AssetScanReport report;
        Check(registry.Scan(stage.platform, rootPath.c_str(), options, report), "the probe folder scans");
        JBro::AssetSystem assets;
        Check(assets.Initialize(stage.memory), "the asset system initializes");
        assets.Bind(stage.platform, registry, rootPath.c_str());
        JBro::Framework3D framework;
        JBro::FrameworkContext context;
        JBro::Testing::AttachClock(context);
        context.renderer = &stage.renderer;
        context.assets = &assets;
        Check(framework.Initialize(context), "the 3D framework initializes");
        JBro::Canvas& canvas = *framework.GetCanvas();
        JBro::Object::GameObject* eye = canvas.CreateObject("eye");
        auto* eyeTransform = canvas.AttachComponent<JBro::Component::Transform3D>(eye);
        eyeTransform->position = {0.0f, 4.0f, 0.0f};
        eyeTransform->rotation = JBro::FromAxisAngle({1.0f, 0.0f, 0.0f}, JBro::Radian(-1.5707963f));
        canvas.AttachComponent<JBro::Component::Camera3D>(eye)->clearColor = {0.0f, 0.0f, 0.0f, 1.0f};
        const auto builtin = [](const char* name) {
            for (JBro::UInt32 index = 0; index < JBro::MeshLibrary::GetBuiltinCount(); ++index)
            {
                if (std::strcmp(JBro::MeshLibrary::GetBuiltin(index).name, name) == 0)
                {
                    return JBro::MeshLibrary::GetBuiltin(index).id;
                }
            }
            return JBro::AssetId{};
        };
        JBro::Object::GameObject* floorObject = canvas.CreateObject("floor");
        canvas.AttachComponent<JBro::Component::Transform3D>(floorObject);
        auto* floor = canvas.AttachComponent<JBro::Component::MeshRenderer3D>(floorObject);
        floor->meshId = builtin("Plane");
        JBro::Object::GameObject* boxObject = canvas.CreateObject("box");
        canvas.AttachComponent<JBro::Component::Transform3D>(boxObject)->position = {0.0f, 1.5f, 0.0f};
        auto* box = canvas.AttachComponent<JBro::Component::MeshRenderer3D>(boxObject);
        box->meshId = builtin("Cube");
        // 앞(-Z)을 X 축으로 -90 도 돌려 아래(-Y)로, 다시 Z 축으로 45 도 돌려 (1, -1, 0) 쪽으로.
        const JBro::Quaternion slanted = JBro::Multiply(JBro::FromAxisAngle({0.0f, 0.0f, 1.0f}, JBro::Radian(0.7853982f)),
            JBro::FromAxisAngle({1.0f, 0.0f, 0.0f}, JBro::Radian(-1.5707963f)));
        JBro::Object::GameObject* sunObject = canvas.CreateObject("sun");
        canvas.AttachComponent<JBro::Component::Transform3D>(sunObject)->rotation = slanted;
        auto* sun = canvas.AttachComponent<JBro::Component::Light3D>(sunObject);
        sun->type = JBro::Component::Light3DType::Directional;
        sun->castShadows = true;

        // 바닥 위 (x, 0, z) 의 화면 자리: 높이 4, 세로 시야 60 도, 96 x 64 다. 화면 위가 -Z, 오른쪽이 +X 다.
        const auto floorPixel = [&](JBro::Float x, JBro::Float z) {
            const float halfHeight = 4.0f * std::tan(0.5235988f);
            const float halfWidth = halfHeight * static_cast<float>(TargetWidth.Get()) / static_cast<float>(TargetHeight.Get());
            const JBro::UInt32 column = static_cast<JBro::UInt32>(std::lround(TargetWidth.Get() * 0.5f * (1.0f + x.Get() / halfWidth)));
            const JBro::UInt32 row = static_cast<JBro::UInt32>(std::lround(TargetHeight.Get() * 0.5f * (1.0f + z.Get() / halfHeight)));
            return std::pair<JBro::UInt32, JBro::UInt32>{column, row};
        };
        JBro::Array<std::byte> image;
        JBro::TextureReadback readback;
        const auto read = [&](JBro::Float x, JBro::Float z) {
            const auto at = floorPixel(x, z);
            return ReadPixel(image, readback.rowPitch, at.first, at.second).r;
        };
        const auto render = [&]() {
            framework.BindCanvasAssets();
            stage.RenderOnce(framework, image, readback);
        };
        const auto linear = [](JBro::Float encoded) {
            const float value = encoded.Get();
            return value <= 0.04045f ? value / 12.92f : std::pow((value + 0.055f) / 1.055f, 2.4f);
        };

        // 아틀라스는 그림자를 처음 바란 프레임 다음에 선다(장치는 프레임 안에서 자원을 만들지 않는다) - 첫 프레임은 그림자 없이 그린다.
        render();
        Check(read(1.5f, 0.0f) > 0.5f, "the first frame that wants shadows draws without them while the atlas is made");
        render();
        const JBro::Float lit = read(-1.5f, 0.0f);
        const JBro::Float shaded = read(1.5f, 0.0f);
        std::cout << "  sun shadow: lit " << lit.Get() << ", in the box's shadow " << shaded.Get() << std::endl;
        Check(lit > 0.5f, "the floor away from the box is lit by the sun");
        Check(shaded < 0.05f, "the floor behind the box from the sun is in its shadow - no ambient light, so it is black");

        sun->shadowStrength = 0.5f;
        render();
        const JBro::Float half = read(1.5f, 0.0f);
        std::cout << "  half-strength shadow: " << half.Get() << std::endl;
        Check(Near(linear(half), linear(lit) * 0.5f, 0.03f), "a shadow strength of 0.5 lets half the sun's light through");
        sun->shadowStrength = 1.0f;

        box->castShadows = false;
        render();
        Check(read(1.5f, 0.0f) > 0.5f, "a mesh that does not cast leaves no shadow");
        box->castShadows = true;

        floor->receiveShadows = false;
        render();
        Check(read(1.5f, 0.0f) > 0.5f, "a mesh that does not receive shadows stays lit");
        floor->receiveShadows = true;

        sun->castShadows = false;
        render();
        Check(read(1.5f, 0.0f) > 0.5f, "a light that does not cast shadows lights the floor behind the box");

        // 스포트: 해를 끄고 비스듬히 위에서 같은 쪽으로 비춘다.
        sun->intensity = 0.0f;
        JBro::Object::GameObject* spotObject = canvas.CreateObject("spot");
        auto* spotTransform = canvas.AttachComponent<JBro::Component::Transform3D>(spotObject);
        spotTransform->position = {-2.0f, 3.5f, 0.0f};
        spotTransform->rotation = slanted;
        auto* spot = canvas.AttachComponent<JBro::Component::Light3D>(spotObject);
        spot->type = JBro::Component::Light3DType::Spot;
        spot->intensity = 30.0f;
        spot->range = 20.0f;
        spot->innerAngle = 80.0f;
        spot->outerAngle = 90.0f;
        spot->castShadows = true;
        render();
        const JBro::Float spotLit = read(1.5f, 1.5f);
        const JBro::Float spotShaded = read(1.5f, 0.0f);
        std::cout << "  spot shadow: lit " << spotLit.Get() << ", in the box's shadow " << spotShaded.Get() << std::endl;
        Check(spotLit > 0.5f && spotShaded < 0.05f, "a spot light casts the box's shadow on the floor and lights the floor beside it");

        // **점광**(D-446): 스포트를 끄고 (0, 2.5, 0) 의 점광 아래에 납작한 판((0, 1.5, 0), 2.4 x 0.2 x 0.6)을 둔다. 판의 그림자는 x 로 ±3.33, z 로 ±0.83 까지다.
        // 바닥의 x = 2.5 는 라이트에서 45 도 - 큐브의 -Y 면과 +X 면의 경계다. 그 양쪽(2.3 은 -Y 면, 2.7 은 +X 면)과 경계 위가 그림자이고,
        // 같은 x 의 z = 1.5 는 판에 가리지 않아 밝다 - 면 사이 이음매에서 그림자가 끊기거나 엉뚱한 그림자가 서지 않는다.
        spot->castShadows = false;
        spot->intensity = 0.0f;
        canvas.FindComponentRaw<JBro::Component::Transform3D>(boxObject)->scale = {2.4f, 0.2f, 0.6f};
        JBro::Object::GameObject* pointObject = canvas.CreateObject("point");
        canvas.AttachComponent<JBro::Component::Transform3D>(pointObject)->position = {0.0f, 2.5f, 0.0f};
        auto* point = canvas.AttachComponent<JBro::Component::Light3D>(pointObject);
        point->type = JBro::Component::Light3DType::Point;
        point->intensity = 30.0f;
        point->range = 20.0f;
        point->castShadows = true;
        render();
        const JBro::Float edges[3] = {2.3f, 2.5f, 2.7f};
        JBro::Bool allShaded = true;
        JBro::Bool allLit = true;
        for (const JBro::Float x : edges)
        {
            const JBro::Float shadedAt = read(x, 0.0f);
            const JBro::Float litAt = read(x, 1.5f);
            std::cout << "  point shadow at x " << x.Get() << ": behind the slab " << shadedAt.Get() << ", beside it " << litAt.Get() << std::endl;
            allShaded = allShaded && shadedAt < 0.05f;
            allLit = allLit && litAt > 0.5f;
        }
        Check(allShaded, "a point light casts the slab's shadow on both sides of the seam between two cube faces and on the seam itself");
        Check(allLit, "and the floor the slab does not hide stays lit across the same seam");

        point->shadowStrength = 0.5f;
        render();
        const JBro::Float pointHalf = read(2.7f, 0.0f);
        const JBro::Float pointFull = read(2.7f, 1.5f);
        std::cout << "  half-strength point shadow: " << pointHalf.Get() << std::endl;
        Check(pointHalf > 0.2f && pointHalf < pointFull, "a point light's shadow strength lets part of its light through");
        point->shadowStrength = 1.0f;

        point->castShadows = false;
        render();
        Check(read(2.7f, 0.0f) > 0.5f, "a point light that does not cast shadows lights the floor behind the slab");
        Check(stage.renderer.GetDevice()->GetValidationErrorCount() == 0, "and the debug layer stayed quiet");

        framework.Shutdown();
        assets.Shutdown();
        stage.Close();
        fs::remove_all(root);
    }

    // RGBE 한 텍셀이다(Radiance `.hdr`). 하늘 맵 시험이 그림을 손으로 짓는다.
    void PushRgbe(std::string& bytes, float r, float g, float b)
    {
        const float largest = std::max(r, std::max(g, b));
        if (largest < 1e-32f)
        {
            bytes.append(4, '\0');
            return;
        }
        int exponent = 0;
        const float mantissa = std::frexp(largest, &exponent);
        const float scale = mantissa * 256.0f / largest;
        bytes.push_back(static_cast<char>(static_cast<unsigned char>(r * scale)));
        bytes.push_back(static_cast<char>(static_cast<unsigned char>(g * scale)));
        bytes.push_back(static_cast<char>(static_cast<unsigned char>(b * scale)));
        bytes.push_back(static_cast<char>(static_cast<unsigned char>(exponent + 128)));
    }

    // 셰이더의 `EnvironmentBrdf` 와 같은 식이다(Karis 2014, 표 없는 split-sum).
    float EnvironmentBrdf(float reflectance, float roughness, float normalDotView)
    {
        const float r[4] = {roughness * -1.0f + 1.0f, roughness * -0.0275f + 0.0425f, roughness * -0.572f + 1.04f, roughness * 0.022f - 0.04f};
        const float a004 = std::min(r[0] * r[0], std::exp2(-9.28f * normalDotView)) * r[0] + r[1];
        return reflectance * (-1.04f * a004 + r[2]) + (1.04f * a004 + r[3]);
    }

    // **캔버스 환경이 표준 재질을 비춘다**(D-447). 위에서 내려다보는 흰 바닥(기본 재질: metallic 0, roughness 1)에 직접광 없이(세기 0 의 `Ambient` 하나 - 라이트가 있으니
    // 렌더러의 기본 빛이 꺼진다) 환경만 비춘다. `Color` 는 그 색 x (확산 1 + 거친 반사), 세기 2 는 두 배다. `Gradient` 는 위를 보는 바닥이 하늘 색이다.
    // `Sky` 는 손으로 지은 `.hdr`(위 반구 파랑, 아래 반구 빨강)이다: 바닥은 파랗고, 세기 0 이면 검다. 거울 같은 금속 바닥은 반사로만 파랗고 반사 세기 0 이면 검다.
    // 위를 보는 카메라의 배경이 `Sky` 면 하늘(파랑)이 지우기 색(검정) 위에 그려진다. 하늘은 한 번만 굽는다(세기를 바꿔도 다시 굽지 않는다).
    template <typename TModule>
    void TestEnvironmentLightsMeshes()
    {
        Stage<TModule> stage;
        if (false == stage.Open())
        {
            std::cout << "  [skip] no device for this API; the environment not verified" << std::endl;
            return;
        }
        namespace fs = std::filesystem;
        const fs::path root = fs::temp_directory_path() / L"JBroEnvironmentProbe·환경";
        fs::remove_all(root);
        fs::create_directories(root / "sky");
        {
            std::string file = "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 32 +X 64\n";
            for (int y = 0; y < 32; ++y)
            {
                for (int x = 0; x < 64; ++x)
                {
                    PushRgbe(file, y < 16 ? 0.0f : 1.0f, 0.0f, y < 16 ? 1.0f : 0.0f);
                }
            }
            // RGBE 에는 0 바이트가 있다 - 글자로 쓰면 첫 0 에서 잘린다. 길이째 쓴다.
            std::ofstream out(root / "sky" / "split.hdr", std::ios::binary);
            out.write(file.data(), static_cast<std::streamsize>(file.size()));
        }
        const std::u8string rootText = root.u8string();
        const JBro::String rootPath(reinterpret_cast<const char*>(rootText.data()), rootText.size());
        JBro::AssetRegistry registry;
        JBro::AssetScanOptions options;
        options.createMissingMeta = true;
        JBro::AssetScanReport report;
        Check(registry.Scan(stage.platform, rootPath.c_str(), options, report), "the probe folder scans");
        const JBro::AssetRecord* skyRecord = registry.FindByPath("sky/split.hdr");
        Check(skyRecord != nullptr && skyRecord->type == JBro::AssetType::EnvironmentMap, "the .hdr file is registered as a sky map");
        // 다시 훑으면 레지스트리의 레코드가 옮겨진다 - 아이디는 값으로 떠 둔다.
        const JBro::AssetId skyId = skyRecord->id;
        WriteText(root / "sky" / "mirror.jmat", "Version: 1\nMaterial:\n  shader: Standard\n  metallic: 1\n  roughness: 0\n");
        Check(registry.Scan(stage.platform, rootPath.c_str(), options, report), "the folder scans again with the material");
        const JBro::AssetId mirrorId = registry.FindByPath("sky/mirror.jmat")->id;
        const JBro::UInt32 texturesBefore = stage.renderer.GetTextureCount();
        JBro::AssetSystem assets;
        Check(assets.Initialize(stage.memory), "the asset system initializes");
        assets.Bind(stage.platform, registry, rootPath.c_str());
        JBro::Framework3D framework;
        JBro::FrameworkContext context;
        JBro::Testing::AttachClock(context);
        context.renderer = &stage.renderer;
        context.assets = &assets;
        Check(framework.Initialize(context), "the 3D framework initializes");
        JBro::Canvas& canvas = *framework.GetCanvas();
        JBro::Object::GameObject* eye = canvas.CreateObject("eye");
        canvas.AttachComponent<JBro::Component::Transform3D>(eye)->position = {0.0f, 4.0f, 0.0f};
        canvas.FindComponentRaw<JBro::Component::Transform3D>(eye)->rotation = JBro::FromAxisAngle({1.0f, 0.0f, 0.0f}, JBro::Radian(-1.5707963f));
        canvas.AttachComponent<JBro::Component::Camera3D>(eye)->clearColor = {0.0f, 0.0f, 0.0f, 1.0f};
        JBro::Object::GameObject* floorObject = canvas.CreateObject("floor");
        canvas.AttachComponent<JBro::Component::Transform3D>(floorObject);
        auto* floor = canvas.AttachComponent<JBro::Component::MeshRenderer3D>(floorObject);
        for (JBro::UInt32 index = 0; index < JBro::MeshLibrary::GetBuiltinCount(); ++index)
        {
            if (std::strcmp(JBro::MeshLibrary::GetBuiltin(index).name, "Plane") == 0)
            {
                floor->meshId = JBro::MeshLibrary::GetBuiltin(index).id;
            }
        }
        // 세기 0 의 환경광 하나 - 라이트가 있으니 기본 빛(고정 방향광)이 꺼지고 환경만 남는다.
        JBro::Object::GameObject* darkObject = canvas.CreateObject("dark");
        auto* dark = canvas.AttachComponent<JBro::Component::Light3D>(darkObject);
        dark->type = JBro::Component::Light3DType::Ambient;
        dark->intensity = 0.0f;

        JBro::Array<std::byte> image;
        JBro::TextureReadback readback;
        const auto render = [&]() {
            framework.BindCanvasAssets();
            stage.RenderOnce(framework, image, readback);
            return ReadPixel(image, readback.rowPitch, TargetWidth / 2, TargetHeight / 2);
        };
        const auto encode = [](float linear) {
            return linear <= 0.0031308f ? linear * 12.92f : 1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f;
        };
        const auto setEnvironment = [&](auto change) {
            JBro::CanvasEnvironment environment = canvas.GetEnvironment();
            change(environment);
            canvas.SetEnvironment(environment);
        };

        const Pixel before = render();
        Check(Near(before.r, 0.0f, 0.01f) && Near(before.g, 0.0f, 0.01f), "the default environment adds no light - the floor is black with no direct light");

        // 색: 확산 c 와 거친 반사 c x 환경 BRDF(0.04, 거칠기 1, 바로 위에서 봄).
        setEnvironment([](JBro::CanvasEnvironment& environment) {
            environment.ambientSource = JBro::EnvironmentAmbientSource::Color;
            environment.ambientColor = JBro::Color{0.2f, 0.3f, 0.4f, 1.0f};
        });
        const float lift = 1.0f + EnvironmentBrdf(0.04f, 1.0f, 1.0f);
        const Pixel colored = render();
        std::cout << "  colour ambient: " << colored.r.Get() << " " << colored.g.Get() << " " << colored.b.Get() << " (want "
                  << encode(0.2f * lift) << " " << encode(0.3f * lift) << " " << encode(0.4f * lift) << ")" << std::endl;
        Check(Near(colored.r, encode(0.2f * lift), 0.02f) && Near(colored.g, encode(0.3f * lift), 0.02f) && Near(colored.b, encode(0.4f * lift), 0.02f),
            "a Color environment lights the white floor with its colour");
        setEnvironment([](JBro::CanvasEnvironment& environment) { environment.ambientIntensity = 2.0f; });
        const Pixel doubled = render();
        Check(Near(doubled.r, encode(0.4f * lift), 0.02f) && Near(doubled.b, encode(0.8f * lift), 0.02f), "the ambient intensity scales it");

        // 그라디언트: 위를 보는 바닥은 하늘 색만 받는다.
        setEnvironment([](JBro::CanvasEnvironment& environment) {
            environment.ambientSource = JBro::EnvironmentAmbientSource::Gradient;
            environment.ambientIntensity = 1.0f;
            environment.skyColor = JBro::Color{0.5f, 0.0f, 0.0f, 1.0f};
            environment.equatorColor = JBro::Color{0.0f, 0.5f, 0.0f, 1.0f};
            environment.groundColor = JBro::Color{0.0f, 0.0f, 0.5f, 1.0f};
        });
        const Pixel gradient = render();
        std::cout << "  gradient ambient: " << gradient.r.Get() << " " << gradient.g.Get() << " " << gradient.b.Get() << std::endl;
        Check(Near(gradient.r, encode(0.5f * lift), 0.02f) && gradient.g < 0.1f && gradient.b < 0.1f,
            "a Gradient environment lights an upward face with the sky colour, not the equator or the ground");

        // 하늘: 위 반구가 파랑, 아래가 빨강이다.
        setEnvironment([&](JBro::CanvasEnvironment& environment) {
            environment.ambientSource = JBro::EnvironmentAmbientSource::Sky;
            environment.skyId = skyId;
        });
        const Pixel sky = render();
        std::cout << "  sky ambient: " << sky.r.Get() << " " << sky.g.Get() << " " << sky.b.Get() << std::endl;
        // 확산만(반사 0)에 하늘 세기 0.1: 위 반구가 1 인 하늘은 위를 보는 흰 면을 1 로 비추므로(SH 의 정확한 값) 0.1 이 그대로 나와야 한다.
        setEnvironment([](JBro::CanvasEnvironment& environment) {
            environment.reflectionIntensity = 0.0f;
            environment.skyIntensity = 0.1f;
        });
        const Pixel dim = render();
        std::cout << "  sky 0.1, diffuse only: " << dim.r.Get() << " " << dim.g.Get() << " " << dim.b.Get() << " (want " << encode(0.1f) << ")" << std::endl;
        Check(Near(dim.b, encode(0.1f), 0.02f) && dim.r < 0.02f, "the sky's diffuse light is the sky's own brightness on a face looking into it");
        setEnvironment([](JBro::CanvasEnvironment& environment) {
            environment.reflectionIntensity = 1.0f;
            environment.skyIntensity = 1.0f;
        });
        Check(framework.GetEnvironmentLibrary().GetUploadedMap().generation != 0, "the sky map is baked and uploaded");
        Check(sky.b > 0.8f && sky.r < sky.b * 0.5f && sky.g < 0.05f, "a Sky environment lights the floor with the sky above it, not the ground below");
        setEnvironment([](JBro::CanvasEnvironment& environment) { environment.skyIntensity = 0.0f; });
        const Pixel unlit = render();
        Check(unlit.b < 0.02f && unlit.r < 0.02f, "a sky intensity of 0 takes its light away");
        setEnvironment([](JBro::CanvasEnvironment& environment) { environment.skyIntensity = 1.0f; });

        // 거울 금속: 확산이 없고 반사로만 위의 하늘(파랑)을 비춘다.
        floor->materialId = mirrorId;
        floor->material = {};
        const Pixel mirror = render();
        std::cout << "  mirror floor: " << mirror.r.Get() << " " << mirror.g.Get() << " " << mirror.b.Get() << std::endl;
        Check(mirror.b > 0.9f && mirror.r < 0.1f, "a mirror metal reflects the sky above it");
        setEnvironment([](JBro::CanvasEnvironment& environment) { environment.reflectionIntensity = 0.0f; });
        const Pixel dull = render();
        Check(dull.b < 0.02f, "a reflection intensity of 0 leaves a metal with nothing to show");
        setEnvironment([](JBro::CanvasEnvironment& environment) { environment.reflectionIntensity = 1.0f; });
        Check(framework.GetEnvironmentLibrary().GetBuildCount() == 1, "the sky was baked once - changing intensities does not bake it again");

        // 하늘 배경: 위를 보는 카메라. 배경이 `Color` 면 지우기 색(검정), `Sky` 면 위 반구(파랑)다.
        canvas.FindComponentRaw<JBro::Component::Transform3D>(eye)->rotation = JBro::FromAxisAngle({1.0f, 0.0f, 0.0f}, JBro::Radian(1.5707963f));
        const Pixel cleared = render();
        Check(cleared.b < 0.02f, "a camera whose background is Color keeps its clear colour");
        canvas.FindComponentRaw<JBro::Component::Camera3D>(eye)->background = JBro::Component::CameraBackground3D::Sky;
        const Pixel background = render();
        std::cout << "  sky background: " << background.r.Get() << " " << background.g.Get() << " " << background.b.Get() << std::endl;
        Check(background.b > 0.95f && background.r < 0.05f, "a camera whose background is Sky draws the sky map behind everything");
        Check(stage.renderer.GetDevice()->GetValidationErrorCount() == 0, "and the debug layer stayed quiet");

        framework.Shutdown();
        Check(stage.renderer.GetTextureCount() == texturesBefore, "shutting the framework down unregisters every texture it made, the sky map with them");
        assets.Shutdown();
        stage.Close();
        fs::remove_all(root);
    }

    // **하늘 회전은 하늘을 오브젝트처럼 +Y 둘레로 돌린다**(D-448). 손으로 지은 `.hdr` 는 방위마다 색이 다르다: 정면(-Z)이 빨강, 오른쪽(+X)이 초록, 왼쪽(-X)이 파랑,
    // 뒤(+Z)가 검정이다. 회전 90 도는 Y 축 90 도로 돈 오브젝트처럼 +X 에 있던 것을 -Z 로 가져온다 - 정면을 보는 카메라의 `Sky` 배경이 초록, -90 도면 파랑이다.
    // 같은 회전을 확산(SH)과 반사도 쓴다: 카메라를 보는(+Z) 벽은 회전 90 도에서 원래 -X 에 있던 파랑을 받고, -90 도에서 초록을 받는다.
    template <typename TModule>
    void TestSkyRotationTurnsTheSky()
    {
        Stage<TModule> stage;
        if (false == stage.Open())
        {
            std::cout << "  [skip] no device for this API; the sky rotation not verified" << std::endl;
            return;
        }
        namespace fs = std::filesystem;
        const fs::path root = fs::temp_directory_path() / L"JBroSkyRotationProbe·회전";
        fs::remove_all(root);
        fs::create_directories(root / "sky");
        {
            std::string file = "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 32 +X 64\n";
            for (int y = 0; y < 32; ++y)
            {
                for (int x = 0; x < 64; ++x)
                {
                    // u = 0.5 가 -Z, 0.75 가 +X, 0.25 가 -X 다(BuiltinEnvironment.hlsli).
                    const float u = (static_cast<float>(x) + 0.5f) / 64.0f;
                    const bool front = u >= 0.375f && u < 0.625f;
                    const bool right = u >= 0.625f && u < 0.875f;
                    const bool left = u >= 0.125f && u < 0.375f;
                    PushRgbe(file, front ? 1.0f : 0.0f, right ? 1.0f : 0.0f, left ? 1.0f : 0.0f);
                }
            }
            std::ofstream out(root / "sky" / "compass.hdr", std::ios::binary);
            out.write(file.data(), static_cast<std::streamsize>(file.size()));
        }
        WriteText(root / "sky" / "mirror.jmat", "Version: 1\nMaterial:\n  shader: Standard\n  metallic: 1\n  roughness: 0\n");
        const std::u8string rootText = root.u8string();
        const JBro::String rootPath(reinterpret_cast<const char*>(rootText.data()), rootText.size());
        JBro::AssetRegistry registry;
        JBro::AssetScanOptions options;
        options.createMissingMeta = true;
        JBro::AssetScanReport report;
        Check(registry.Scan(stage.platform, rootPath.c_str(), options, report), "the probe folder scans");
        const JBro::AssetId skyId = registry.FindByPath("sky/compass.hdr")->id;
        const JBro::AssetId mirrorId = registry.FindByPath("sky/mirror.jmat")->id;
        JBro::AssetSystem assets;
        Check(assets.Initialize(stage.memory), "the asset system initializes");
        assets.Bind(stage.platform, registry, rootPath.c_str());
        JBro::Framework3D framework;
        JBro::FrameworkContext context;
        JBro::Testing::AttachClock(context);
        context.renderer = &stage.renderer;
        context.assets = &assets;
        Check(framework.Initialize(context), "the 3D framework initializes");
        JBro::Canvas& canvas = *framework.GetCanvas();
        // 카메라는 +Z 에서 정면(-Z)을 본다.
        JBro::Object::GameObject* eye = canvas.CreateObject("eye");
        canvas.AttachComponent<JBro::Component::Transform3D>(eye)->position = {0.0f, 0.0f, 4.0f};
        auto* camera = canvas.AttachComponent<JBro::Component::Camera3D>(eye);
        camera->clearColor = {0.0f, 0.0f, 0.0f, 1.0f};
        camera->background = JBro::Component::CameraBackground3D::Sky;
        // 벽: 평면을 X 둘레로 90 도 세워 법선이 +Z(카메라 쪽)다. 처음에는 꺼 두고 배경만 본다.
        JBro::Object::GameObject* wallObject = canvas.CreateObject("wall");
        canvas.AttachComponent<JBro::Component::Transform3D>(wallObject)->rotation = JBro::FromAxisAngle({1.0f, 0.0f, 0.0f}, JBro::Radian(1.5707963f));
        auto* wall = canvas.AttachComponent<JBro::Component::MeshRenderer3D>(wallObject);
        for (JBro::UInt32 index = 0; index < JBro::MeshLibrary::GetBuiltinCount(); ++index)
        {
            if (std::strcmp(JBro::MeshLibrary::GetBuiltin(index).name, "Plane") == 0)
            {
                wall->meshId = JBro::MeshLibrary::GetBuiltin(index).id;
            }
        }
        wall->visible = false;
        JBro::Object::GameObject* darkObject = canvas.CreateObject("dark");
        auto* dark = canvas.AttachComponent<JBro::Component::Light3D>(darkObject);
        dark->type = JBro::Component::Light3DType::Ambient;
        dark->intensity = 0.0f;
        const auto setRotation = [&](float degrees, float reflection) {
            JBro::CanvasEnvironment environment = canvas.GetEnvironment();
            environment.ambientSource = JBro::EnvironmentAmbientSource::Sky;
            environment.skyId = skyId;
            environment.skyRotation = degrees;
            environment.reflectionIntensity = reflection;
            canvas.SetEnvironment(environment);
        };

        JBro::Array<std::byte> image;
        JBro::TextureReadback readback;
        const auto render = [&]() {
            framework.BindCanvasAssets();
            stage.RenderOnce(framework, image, readback);
            return ReadPixel(image, readback.rowPitch, TargetWidth / 2, TargetHeight / 2);
        };
        const auto print = [](const char* what, const Pixel& pixel) {
            std::cout << "  " << what << ": " << pixel.r.Get() << " " << pixel.g.Get() << " " << pixel.b.Get() << std::endl;
        };

        // 배경.
        setRotation(0.0f, 1.0f);
        const Pixel straight = render();
        print("background, rotation 0", straight);
        Check(straight.r > 0.9f && straight.g < 0.05f && straight.b < 0.05f, "with no rotation the camera looking down -Z sees the sky's -Z (red)");
        setRotation(90.0f, 1.0f);
        const Pixel turned = render();
        print("background, rotation 90", turned);
        Check(turned.g > 0.9f && turned.r < 0.05f && turned.b < 0.05f, "a rotation of 90 brings the sky's +X (green) to -Z, as a 90 degree yaw turns an object's +X to -Z");
        setRotation(-90.0f, 1.0f);
        const Pixel back = render();
        print("background, rotation -90", back);
        Check(back.b > 0.9f && back.r < 0.05f && back.g < 0.05f, "a rotation of -90 brings the sky's -X (blue) to -Z");

        // 확산: 반사를 끄고 +Z 를 보는 흰 벽.
        wall->visible = true;
        setRotation(90.0f, 0.0f);
        const Pixel diffuseTurned = render();
        print("diffuse wall, rotation 90", diffuseTurned);
        Check(diffuseTurned.b > diffuseTurned.g + 0.1f && diffuseTurned.b > diffuseTurned.r + 0.1f,
            "at 90 the wall facing +Z takes the light of the sky's -X (blue), which the rotation has brought round to +Z");
        setRotation(-90.0f, 0.0f);
        const Pixel diffuseBack = render();
        print("diffuse wall, rotation -90", diffuseBack);
        Check(diffuseBack.g > diffuseBack.b + 0.1f && diffuseBack.g > diffuseBack.r + 0.1f, "at -90 it takes the sky's +X (green)");
        setRotation(0.0f, 0.0f);
        const Pixel diffuseStraight = render();
        print("diffuse wall, rotation 0", diffuseStraight);
        setRotation(180.0f, 0.0f);
        const Pixel diffuseAround = render();
        print("diffuse wall, rotation 180", diffuseAround);
        Check(diffuseAround.r > diffuseStraight.r + 0.2f, "at 180 the wall facing +Z takes the sky's -Z (red) it had at its back");

        // 반사: 거울 벽은 -Z 로 보는 눈을 +Z 로 되비춘다.
        wall->materialId = mirrorId;
        wall->material = {};
        setRotation(90.0f, 1.0f);
        const Pixel mirrorTurned = render();
        print("mirror wall, rotation 90", mirrorTurned);
        Check(mirrorTurned.b > 0.9f && mirrorTurned.g < 0.05f && mirrorTurned.r < 0.05f, "at 90 the mirror facing +Z reflects the sky's -X (blue)");
        setRotation(-90.0f, 1.0f);
        const Pixel mirrorBack = render();
        print("mirror wall, rotation -90", mirrorBack);
        Check(mirrorBack.g > 0.9f && mirrorBack.b < 0.05f && mirrorBack.r < 0.05f, "at -90 it reflects the sky's +X (green)");
        Check(framework.GetEnvironmentLibrary().GetBuildCount() == 1, "turning the sky does not bake it again");
        Check(stage.renderer.GetDevice()->GetValidationErrorCount() == 0, "and the debug layer stayed quiet");

        framework.Shutdown();
        assets.Shutdown();
        stage.Close();
        fs::remove_all(root);
    }

    // **하늘은 워커에서 굽는다**(D-449). 하늘을 고르는 `Resolve` 는 원본을 떠 넘기고 곧장 돌아오며, 그동안은 앞 하늘(처음이면 하늘 없이)을 그린다.
    // 굽는 도중 하늘이 또 바뀌면 앞 굽기는 멈추고 결과를 버린다. 내려갈 때는 굽기를 멈추게 해 다 굽기를 기다리지 않는다. 태스크 관리자가 없으면 그 자리에서 굽는다.
    template <typename TModule>
    void TestSkyBakesOnWorkers()
    {
        Stage<TModule> stage;
        if (false == stage.Open())
        {
            std::cout << "  [skip] no device for this API; the sky bake on workers not verified" << std::endl;
            return;
        }
        namespace fs = std::filesystem;
        const fs::path root = fs::temp_directory_path() / L"JBroSkyBakeProbe·굽기";
        fs::remove_all(root);
        fs::create_directories(root / "sky");
        // 한 색의 하늘 둘이다. 1024 x 512 라 디버그에서 한 번 굽는 데 2 초쯤 걸린다 - 도중에 바꾸고 멈출 틈이 있다.
        const auto writeSky = [&](const char* name, float r, float b) {
            std::string file = "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 512 +X 1024\n";
            for (int at = 0; at < 1024 * 512; ++at)
            {
                PushRgbe(file, r, 0.0f, b);
            }
            std::ofstream out(root / "sky" / name, std::ios::binary);
            out.write(file.data(), static_cast<std::streamsize>(file.size()));
        };
        writeSky("red.hdr", 1.0f, 0.0f);
        writeSky("blue.hdr", 0.0f, 1.0f);
        const std::u8string rootText = root.u8string();
        const JBro::String rootPath(reinterpret_cast<const char*>(rootText.data()), rootText.size());
        JBro::AssetRegistry registry;
        JBro::AssetScanOptions options;
        options.createMissingMeta = true;
        JBro::AssetScanReport report;
        Check(registry.Scan(stage.platform, rootPath.c_str(), options, report), "the probe folder scans");
        JBro::AssetSystem assets;
        Check(assets.Initialize(stage.memory), "the asset system initializes");
        assets.Bind(stage.platform, registry, rootPath.c_str());
        const JBro::AssetHandle red = assets.Load(registry.FindByPath("sky/red.hdr")->id);
        const JBro::AssetHandle blue = assets.Load(registry.FindByPath("sky/blue.hdr")->id);
        Check(red.generation != 0 && blue.generation != 0, "both skies load");

        JBro::TaskManager tasks;
        JBro::TaskManagerDesc desc;
        desc.workerCount = 2;
        Check(tasks.Initialize(desc) && tasks.UsesWorkers(), "the task manager runs workers");
        JBro::EnvironmentLibrary library;
        library.Initialize(&stage.renderer, &assets, &tasks);
        JBro::CanvasEnvironment environment;
        environment.ambientSource = JBro::EnvironmentAmbientSource::Sky;
        environment.ambientIntensity = 1.0f;
        environment.skyIntensity = 1.0f;
        JBro::Environment3D result;
        using Clock = std::chrono::steady_clock;
        const auto seconds = [](Clock::time_point from) {
            return std::chrono::duration<double>(Clock::now() - from).count();
        };
        const auto settle = [&]() {
            const auto from = Clock::now();
            while (library.IsBaking() && seconds(from) < 120.0)
            {
                tasks.Update();
                library.Resolve(environment, result);
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            return seconds(from);
        };

        // 빨강을 고른다. 곧장 돌아오고 아직 하늘이 없다.
        environment.sky = red;
        auto from = Clock::now();
        library.Resolve(environment, result);
        const double handOff = seconds(from);
        std::cout << "  handing the bake to a worker took " << handOff << " s" << std::endl;
        Check(library.IsBaking() && result.map.generation == 0, "picking a sky hands its bake to a worker and draws without a sky meanwhile");
        // 굽는 도중 파랑으로 바꾼다. 빨강은 멈추고 버려진다.
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        environment.sky = blue;
        library.Resolve(environment, result);
        const double blueBake = settle();
        std::cout << "  the blue bake finished in " << blueBake << " s" << std::endl;
        Check(handOff < blueBake * 0.1, "handing a bake over is a small part of the bake itself");
        Check(false == library.IsBaking() && result.map.generation != 0 && result.map.index == library.GetUploadedMap().index,
            "the finished bake is uploaded by the next resolve");
        Check(library.GetBuildCount() == 1, "the red bake was stopped and dropped - only the blue sky was uploaded");
        Check(result.sh[0][2] > 0.1f && result.sh[0][0] < 0.01f, "the uploaded sky is the blue one");

        // 빨강으로 다시 바꾼다. 굽는 동안 파랑이 그대로 남는다.
        environment.sky = red;
        library.Resolve(environment, result);
        Check(library.IsBaking() && result.map.generation != 0 && result.sh[0][2] > 0.1f && result.sh[0][0] < 0.01f,
            "while the new sky bakes the old one keeps drawing");
        // 굽는 도중 내린다. 다 굽기를 기다리지 않는다.
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        from = Clock::now();
        library.Shutdown();
        const double shutdown = seconds(from);
        std::cout << "  shutting down mid-bake took " << shutdown << " s" << std::endl;
        Check(shutdown < blueBake * 0.5, "shutting down stops the bake instead of waiting it out");
        Check(false == library.IsBaking() && library.GetUploadedMap().generation == 0, "and leaves nothing behind");

        // 태스크 관리자가 없으면 그 자리에서 굽고 올린다(시험·워커 없는 빌드).
        library.Initialize(&stage.renderer, &assets, nullptr);
        environment.sky = red;
        library.Resolve(environment, result);
        Check(false == library.IsBaking() && result.map.generation != 0 && result.sh[0][0] > 0.1f && result.sh[0][2] < 0.01f,
            "without a task manager the sky is baked and uploaded on the spot");
        Check(stage.renderer.GetDevice()->GetValidationErrorCount() == 0, "and the debug layer stayed quiet");
        library.Shutdown();
        tasks.Shutdown();
        assets.Release(red);
        assets.Release(blue);
        assets.Shutdown();
        stage.Close();
        fs::remove_all(root);
    }

    // **계산한 탄젠트로 노멀맵이 선다**(D-444). 시험 사각형 glTF 에는 탄젠트가 없어 임포트가 MikkTSpace 로 잰다. 바닥(법선 +Y)에 누운 사각형을 위에서 보고,
    // 빛은 옆에서만(수평) 온다 - 평평한 면은 빛을 등지지 않지만 받지도 않아 검다. 노멀맵 (1, 0.5, 0.5) 는 법선을 탄젠트(+u = +X)로 눕혀 +X 에서 오는 빛을 받고,
    // (0.5, 1, 0.5) 는 바이탄젠트(그림의 위 = -Z)로 눕혀 -Z 에서 오는 빛을 받는다. 반대쪽 빛에서는 검다 - 바이탄젠트의 부호가 틀리면 둘째가 뒤집힌다.
    template <typename TModule>
    void TestCalculatedTangentsBendTheNormalMap()
    {
        Stage<TModule> stage;
        if (false == stage.Open())
        {
            std::cout << "  [skip] no device for this API; calculated tangents not verified" << std::endl;
            return;
        }
        namespace fs = std::filesystem;
        const fs::path root = fs::temp_directory_path() / L"JBroTangentProbe·탄젠트";
        fs::remove_all(root);
        WriteText(root / "props" / "quad.gltf", TestModels::SelfContainedQuadGltf("Bricks", TestModels::TinyPng()).c_str());
        const auto writeTga = [&](const char* name, unsigned char r, unsigned char g, unsigned char b) {
            const std::string bytes = TestModels::SolidTga(r, g, b, 255);
            std::filesystem::create_directories(root / "art");
            std::ofstream file(root / "art" / name, std::ios::binary);
            file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        };
        writeTga("along_u.tga", 255, 128, 128);
        writeTga("up_picture.tga", 128, 255, 128);
        const std::u8string rootText = root.u8string();
        const JBro::String rootPath(reinterpret_cast<const char*>(rootText.data()), rootText.size());
        JBro::AssetRegistry registry;
        JBro::AssetScanOptions options;
        options.createMissingMeta = true;
        JBro::AssetScanReport report;
        Check(registry.Scan(stage.platform, rootPath.c_str(), options, report), "the probe folder scans");
        const std::string alongU = "Version: 1\nMaterial:\n  shader: Standard\n  normalTextureId: " + IdText(registry.FindByPath("art/along_u.tga")->id) + "\n";
        const std::string upPicture = "Version: 1\nMaterial:\n  shader: Standard\n  normalTextureId: " + IdText(registry.FindByPath("art/up_picture.tga")->id) + "\n";
        WriteText(root / "art" / "along_u.jmat", alongU.c_str());
        WriteText(root / "art" / "up_picture.jmat", upPicture.c_str());
        Check(registry.Scan(stage.platform, rootPath.c_str(), options, report), "the folder scans again with the materials");
        JBro::AssetId meshId;
        JBro::Array<JBro::AssetId> parts;
        registry.CollectOwned(registry.FindByPath("props/quad.gltf")->id, parts);
        for (const JBro::AssetId& id : parts)
        {
            meshId = registry.Find(id)->type == JBro::AssetType::Mesh ? id : meshId;
        }

        JBro::AssetSystem assets;
        Check(assets.Initialize(stage.memory), "the asset system initializes");
        assets.Bind(stage.platform, registry, rootPath.c_str());
        JBro::Framework3D framework;
        JBro::FrameworkContext context;
        JBro::Testing::AttachClock(context);
        context.renderer = &stage.renderer;
        context.assets = &assets;
        Check(framework.Initialize(context), "the 3D framework initializes");
        JBro::Canvas& canvas = *framework.GetCanvas();
        JBro::Object::GameObject* eye = canvas.CreateObject("eye");
        auto* eyeTransform = canvas.AttachComponent<JBro::Component::Transform3D>(eye);
        eyeTransform->position = {0.0f, 3.0f, 0.0f};
        eyeTransform->rotation = JBro::FromAxisAngle({1.0f, 0.0f, 0.0f}, JBro::Radian(-1.5707963f));
        canvas.AttachComponent<JBro::Component::Camera3D>(eye)->clearColor = {0.0f, 0.0f, 0.0f, 1.0f};
        JBro::Object::GameObject* quad = canvas.CreateObject("quad");
        canvas.AttachComponent<JBro::Component::Transform3D>(quad)->position = {-0.5f, 0.0f, -0.5f};
        auto* mesh = canvas.AttachComponent<JBro::Component::MeshRenderer3D>(quad);
        mesh->meshId = meshId;
        JBro::Object::GameObject* lamp = canvas.CreateObject("lamp");
        auto* lampTransform = canvas.AttachComponent<JBro::Component::Transform3D>(lamp);
        auto* light = canvas.AttachComponent<JBro::Component::Light3D>(lamp);
        light->type = JBro::Component::Light3DType::Directional;
        light->intensity = 1.0f;

        JBro::Array<std::byte> image;
        JBro::TextureReadback readback;
        // 빛이 가는 쪽이 앞(-Z)이다. Y 축으로 +90 도면 -X 로 가 +X 에서 오고, -90 도면 -X 에서 온다. 180 도면 +Z 로 가 -Z 에서 온다.
        const auto shade = [&](const char* material, JBro::Float yawRadians) {
            mesh->materialId = registry.FindByPath(material)->id;
            mesh->material = {};
            lampTransform->rotation = JBro::FromAxisAngle({0.0f, 1.0f, 0.0f}, JBro::Radian(yawRadians));
            framework.BindCanvasAssets();
            stage.RenderOnce(framework, image, readback);
            return ReadPixel(image, readback.rowPitch, TargetWidth / 2, TargetHeight / 2).r;
        };
        const JBro::MeshData* loaded = nullptr;
        const JBro::Float fromPlusX = shade("art/along_u.jmat", 1.5707963f);
        loaded = assets.GetMesh(mesh->mesh);
        Check(loaded != nullptr && loaded->hasTangents, "the quad's mesh carries tangents the import calculated");
        const JBro::Float fromMinusX = shade("art/along_u.jmat", -1.5707963f);
        const JBro::Float fromMinusZ = shade("art/up_picture.jmat", 3.1415927f);
        const JBro::Float fromPlusZ = shade("art/up_picture.jmat", 0.0f);
        std::cout << "  normal map along u: +X " << fromPlusX.Get() << ", -X " << fromMinusX.Get() << "; up the picture: -Z " << fromMinusZ.Get()
                  << ", +Z " << fromPlusZ.Get() << std::endl;
        Check(fromPlusX > 0.6f && fromMinusX < 0.1f, "a normal map leaning along +u turns the floor towards a light from +X");
        Check(fromMinusZ > 0.6f && fromPlusZ < 0.1f, "a normal map leaning up the picture turns the floor towards a light from -Z - the bitangent's sign is right");
        Check(stage.renderer.GetDevice()->GetValidationErrorCount() == 0, "and the debug layer stayed quiet");

        framework.Shutdown();
        assets.Shutdown();
        stage.Close();
        fs::remove_all(root);
    }

    // **반투명과 양면**(D-443). 위에서 내려다보는 빌트인 평면 셋 - 맨 아래 붉은 불투명, 그 위 파란 반투명(알파 0.5), 맨 위 초록 반투명(알파 0.5)이다.
    // 일부러 초록·파랑·빨강 차례로 만든다: 제출 차례대로 그리면 불투명이 반투명 뒤에 와서 덮고 반투명끼리도 가까운 것이 먼저다. 렌더러가 불투명을 먼저,
    // 반투명은 먼 것부터 그리면 가운데는 0.5 초록 + 0.25 파랑 + 0.25 빨강이다(`Unlit` 은 sRGB 로 싸서 내고 섞기는 그 값끼리다).
    // 그다음 카메라를 평면 밑으로 옮겨 올려다본다. 한 면 재질은 뒷면이라 버려져 바탕(검정)만 보이고, 양면 재질은 보인다. 위로 비추는 방향광 아래의 양면 `Standard` 는
    // 뒷면에서 법선을 뒤집어 빛을 받는다 - 뒤집지 않으면 빛을 등져 검다.
    template <typename TModule>
    void TestTransparentAndDoubleSidedMaterials()
    {
        Stage<TModule> stage;
        if (false == stage.Open())
        {
            std::cout << "  [skip] no device for this API; transparent and double-sided materials not verified" << std::endl;
            return;
        }
        namespace fs = std::filesystem;
        const fs::path root = fs::temp_directory_path() / L"JBroBlendProbe·반투명";
        fs::remove_all(root);
        const auto unlit = [](const char* alpha, const char* r, const char* g, const char* b, const char* a, const char* sides) {
            return std::string("Version: 1\nMaterial:\n  shader: Unlit\n  alpha: ") + alpha + "\n  doubleSided: " + sides
                + "\n  baseColor:\n    - " + r + "\n    - " + g + "\n    - " + b + "\n    - " + a + "\n";
        };
        WriteText(root / "art" / "red.jmat", unlit("Opaque", "1", "0", "0", "1", "false").c_str());
        WriteText(root / "art" / "blue.jmat", unlit("Blend", "0", "0", "1", "0.5", "false").c_str());
        WriteText(root / "art" / "green.jmat", unlit("Blend", "0", "1", "0", "0.5", "false").c_str());
        WriteText(root / "art" / "both.jmat", unlit("Opaque", "1", "1", "1", "1", "true").c_str());
        WriteText(root / "art" / "lit_both.jmat", "Version: 1\nMaterial:\n  shader: Standard\n  doubleSided: true\n");
        const std::u8string rootText = root.u8string();
        const JBro::String rootPath(reinterpret_cast<const char*>(rootText.data()), rootText.size());
        JBro::AssetRegistry registry;
        JBro::AssetScanOptions options;
        options.createMissingMeta = true;
        JBro::AssetScanReport report;
        Check(registry.Scan(stage.platform, rootPath.c_str(), options, report), "the probe folder scans");
        const auto idOf = [&](const char* path) {
            const JBro::AssetRecord* record = registry.FindByPath(path);
            Check(record != nullptr, "the material file is registered");
            return record != nullptr ? record->id : JBro::AssetId{};
        };

        JBro::AssetSystem assets;
        Check(assets.Initialize(stage.memory), "the asset system initializes");
        assets.Bind(stage.platform, registry, rootPath.c_str());
        JBro::Framework3D framework;
        JBro::FrameworkContext context;
        JBro::Testing::AttachClock(context);
        context.renderer = &stage.renderer;
        context.assets = &assets;
        Check(framework.Initialize(context), "the 3D framework initializes");
        JBro::Canvas& canvas = *framework.GetCanvas();
        JBro::Object::GameObject* eye = canvas.CreateObject("eye");
        auto* eyeTransform = canvas.AttachComponent<JBro::Component::Transform3D>(eye);
        eyeTransform->position = {0.0f, 4.0f, 0.0f};
        eyeTransform->rotation = JBro::FromAxisAngle({1.0f, 0.0f, 0.0f}, JBro::Radian(-1.5707963f));
        canvas.AttachComponent<JBro::Component::Camera3D>(eye)->clearColor = {0.0f, 0.0f, 0.0f, 1.0f};
        JBro::AssetId planeId;
        for (JBro::UInt32 index = 0; index < JBro::MeshLibrary::GetBuiltinCount(); ++index)
        {
            if (std::strcmp(JBro::MeshLibrary::GetBuiltin(index).name, "Plane") == 0)
            {
                planeId = JBro::MeshLibrary::GetBuiltin(index).id;
            }
        }
        const auto place = [&](const char* name, JBro::Float height, const char* material) {
            JBro::Object::GameObject* object = canvas.CreateObject(name);
            canvas.AttachComponent<JBro::Component::Transform3D>(object)->position = {0.0f, height, 0.0f};
            auto* mesh = canvas.AttachComponent<JBro::Component::MeshRenderer3D>(object);
            mesh->meshId = planeId;
            mesh->materialId = idOf(material);
            return mesh;
        };
        auto* green = place("green", 1.5f, "art/green.jmat");
        auto* blue = place("blue", 1.0f, "art/blue.jmat");
        auto* red = place("red", 0.0f, "art/red.jmat");
        framework.BindCanvasAssets();

        JBro::Array<std::byte> image;
        JBro::TextureReadback readback;
        stage.RenderOnce(framework, image, readback);
        const Pixel mixed = ReadPixel(image, readback.rowPitch, TargetWidth / 2, TargetHeight / 2);
        std::cout << "  blended: " << mixed.r.Get() << " " << mixed.g.Get() << " " << mixed.b.Get() << std::endl;
        Check(Near(mixed.r, 0.25f, 0.02f) && Near(mixed.g, 0.5f, 0.02f) && Near(mixed.b, 0.25f, 0.02f),
            "opaque meshes are drawn first and the transparent ones far to near: half green over half blue over red");

        // 밑에서 올려다본다. 셋 다 한 면이라 뒷면만 보여 버려진다.
        eyeTransform->position = {0.0f, -4.0f, 0.0f};
        eyeTransform->rotation = JBro::FromAxisAngle({1.0f, 0.0f, 0.0f}, JBro::Radian(1.5707963f));
        green->visible = false;
        blue->visible = false;
        stage.RenderOnce(framework, image, readback);
        const Pixel culled = ReadPixel(image, readback.rowPitch, TargetWidth / 2, TargetHeight / 2);
        Check(Near(culled.r, 0.0f) && Near(culled.g, 0.0f) && Near(culled.b, 0.0f), "from below a single-sided plane is culled - only the clear colour shows");

        red->materialId = idOf("art/both.jmat");
        red->material = {};
        framework.BindCanvasAssets();
        stage.RenderOnce(framework, image, readback);
        const Pixel both = ReadPixel(image, readback.rowPitch, TargetWidth / 2, TargetHeight / 2);
        Check(Near(both.r, 1.0f) && Near(both.g, 1.0f) && Near(both.b, 1.0f), "a double-sided material shows its back face");

        // 위로 비추는 방향광 하나(앞이 -Z 라 X 축으로 +90 도 돌리면 +Y 를 향한다). 양면 `Standard` 의 뒷면은 법선을 뒤집어 그 빛을 마주한다.
        JBro::Object::GameObject* lamp = canvas.CreateObject("lamp");
        canvas.AttachComponent<JBro::Component::Transform3D>(lamp)->rotation = JBro::FromAxisAngle({1.0f, 0.0f, 0.0f}, JBro::Radian(1.5707963f));
        auto* light = canvas.AttachComponent<JBro::Component::Light3D>(lamp);
        light->type = JBro::Component::Light3DType::Directional;
        light->intensity = 1.0f;
        red->materialId = idOf("art/lit_both.jmat");
        red->material = {};
        framework.BindCanvasAssets();
        stage.RenderOnce(framework, image, readback);
        const Pixel lit = ReadPixel(image, readback.rowPitch, TargetWidth / 2, TargetHeight / 2);
        std::cout << "  double-sided underside lit: " << lit.r.Get() << std::endl;
        Check(lit.r > 0.6f && Near(lit.g, lit.r, 0.01f), "the back face of a double-sided Standard material faces the light from below - its normal is flipped");
        Check(stage.renderer.GetDevice()->GetValidationErrorCount() == 0, "and the debug layer stayed quiet");

        framework.Shutdown();
        assets.Shutdown();
        stage.Close();
        fs::remove_all(root);
    }

    // **3D 렌더 텍스처**(D-439). 뷰포트 `Monitor` 가 탐침 카메라(월드 (100, 0, 3), 파란 바탕)로 붉은 상자를 32 x 32 `.jrt` 에 그리고, 위에서 내려다보는
    // 사각형의 `Unlit` 재질이 그 렌더 텍스처를 기본색 그림으로 읽는다 - 사각형 가운데가 붉고 가장자리가 파랗다. 탐침이 그 사각형을 보게 옮기면 그 렌더 텍스처에 그리는
    // 뷰는 제 대상을 읽는 사각형을 그리지 않는다 - 사각형에는 파란 바탕만 남는다.
    template <typename TModule>
    void TestRenderTexturesShowA3DViewOnAMesh()
    {
        Stage<TModule> stage;
        if (false == stage.Open())
        {
            std::cout << "  [skip] no device for this API; 3D render textures not verified" << std::endl;
            return;
        }
        namespace fs = std::filesystem;
        const fs::path root = fs::temp_directory_path() / L"JBroRenderTexture3DProbe·모니터";
        fs::remove_all(root);
        const std::string quad = TestModels::SelfContainedQuadGltf("Screen", TestModels::TinyPng());
        WriteText(root / "props" / "quad.gltf", quad.c_str());
        {
            JBro::RenderTextureSettings settings;
            settings.width = 32;
            settings.height = 32;
            const JBro::String body = JBro::WriteRenderTexture(settings);
            WriteText(root / "art" / "monitor.jrt", body.c_str());
        }
        const std::u8string rootText = root.u8string();
        const JBro::String rootPath(reinterpret_cast<const char*>(rootText.data()), rootText.size());
        JBro::AssetRegistry registry;
        JBro::AssetScanOptions options;
        options.createMissingMeta = true;
        JBro::AssetScanReport report;
        Check(registry.Scan(stage.platform, rootPath.c_str(), options, report), "the probe folder scans");
        const JBro::AssetRecord* model = registry.FindByPath("props/quad.gltf");
        const JBro::AssetRecord* monitor = registry.FindByPath("art/monitor.jrt");
        Check(model != nullptr && monitor != nullptr && monitor->type == JBro::AssetType::RenderTexture, "the quad and the render texture are registered");
        JBro::AssetId meshId;
        JBro::Array<JBro::AssetId> parts;
        registry.CollectOwned(model->id, parts);
        for (const JBro::AssetId& id : parts)
        {
            meshId = registry.Find(id)->type == JBro::AssetType::Mesh ? id : meshId;
        }
        const JBro::AssetId monitorId = monitor->id;
        const std::string tv = "Version: 1\nMaterial:\n  shader: Unlit\n  baseColorTextureId: " + IdText(monitorId) + "\n";
        WriteText(root / "art" / "tv.jmat", tv.c_str());
        Check(registry.Scan(stage.platform, rootPath.c_str(), options, report), "the folder scans again with the material");
        const JBro::AssetId tvId = registry.FindByPath("art/tv.jmat")->id;

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
        eyeTransform->position = {0.0f, 3.0f, 0.0f};
        eyeTransform->rotation = JBro::FromAxisAngle({1.0f, 0.0f, 0.0f}, JBro::Radian(-1.5707963f));
        canvas.AttachComponent<JBro::Component::Camera3D>(eye)->clearColor = {0.0f, 0.0f, 0.0f, 1.0f};
        JBro::Object::GameObject* probe = canvas.CreateObject("probe");
        auto* probeTransform = canvas.AttachComponent<JBro::Component::Transform3D>(probe);
        probeTransform->position = {100.0f, 0.0f, 3.0f};
        canvas.AttachComponent<JBro::Component::Camera3D>(probe)->clearColor = {0.0f, 0.0f, 1.0f, 1.0f};
        PlaceCube(canvas, "red", {100.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, {1.0f, 0.0f, 0.0f, 1.0f});
        JBro::Object::GameObject* screen = canvas.CreateObject("screen");
        canvas.AttachComponent<JBro::Component::Transform3D>(screen)->position = {-0.5f, 0.0f, -0.5f};
        auto* mesh = canvas.AttachComponent<JBro::Component::MeshRenderer3D>(screen);
        mesh->meshId = meshId;
        mesh->materialId = tvId;
        mesh->tint = {1.0f, 1.0f, 1.0f, 1.0f};
        {
            JBro::Array<JBro::CanvasViewport> viewports;
            JBro::CanvasViewport feed;
            feed.name = "Monitor";
            feed.camera = probe->GetInstanceId();
            feed.renderTextureId = monitorId;
            JBro::CanvasViewport main;
            main.name = "Main";
            main.camera = eye->GetInstanceId();
            viewports.Add(feed);
            viewports.Add(main);
            canvas.SetViewports(viewports);
        }
        framework.BindCanvasAssets();
        Check(canvas.GetViewportAt(0)->renderTexture.generation != 0, "the resolve pass loads the viewport's render texture");

        JBro::Array<std::byte> image;
        JBro::TextureReadback readback;
        stage.RenderOnce(framework, image, readback);
        stage.RenderOnce(framework, image, readback);
        const Pixel middle = ReadPixel(image, readback.rowPitch, TargetWidth / 2, TargetHeight / 2);
        const Pixel edge = ReadPixel(image, readback.rowPitch, TargetWidth / 2 + 7, TargetHeight / 2);
        const Pixel outside = ReadPixel(image, readback.rowPitch, 2, 2);
        std::cout << "  monitor: middle " << middle.r.Get() << "," << middle.g.Get() << "," << middle.b.Get() << " edge " << edge.r.Get() << ","
                  << edge.g.Get() << "," << edge.b.Get() << std::endl;
        Check(middle.r > 0.3f && middle.r > middle.b * 2.0f && middle.r > middle.g * 2.0f, "the quad's middle shows the probe camera's red cube");
        Check(edge.b > 0.5f && edge.r < 0.2f, "towards its edge it shows the probe camera's blue clear");
        Check(Near(outside.r, 0.0f) && Near(outside.b, 0.0f), "around the quad is the main camera's black clear - the render texture view is not on the screen");

        // 탐침이 사각형을 내려다보게 옮긴다. 그 렌더 텍스처에 그리는 뷰는 그것을 읽는 사각형을 그리지 않는다.
        probeTransform->position = {0.0f, 3.0f, 0.0f};
        probeTransform->rotation = eyeTransform->rotation;
        stage.RenderOnce(framework, image, readback);
        stage.RenderOnce(framework, image, readback);
        const Pixel self = ReadPixel(image, readback.rowPitch, TargetWidth / 2, TargetHeight / 2);
        Check(self.b > 0.5f && self.r < 0.2f, "a view never draws the mesh that reads its own render texture - the quad shows only the probe's clear");
        JBro::Bool excluded = false;
        const JBro::RenderWorld3D& world = *framework.GetRenderWorld();
        for (std::size_t index = 0; index < world.GetMeshCount(); ++index)
        {
            excluded = excluded || world.ReadsRenderTarget(world.GetMesh(index).meshMaterial, world.GetCameras()[0].targetSprite);
        }
        Check(excluded, "the render world knows the quad's material reads the monitor's target");
        Check(stage.renderer.GetDevice()->GetValidationErrorCount() == 0, "and the debug layer stayed quiet");

        framework.Shutdown();
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
            Check(framework.GetMeshMaterialLibrary().GetUploadedMaterialCount() == 2 && framework.GetMeshMaterialLibrary().GetUploadedTextureCount() == 1,
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
        Check(framework.GetMeshMaterialLibrary().GetUploadedMaterialCount() == 2 && framework.GetMeshMaterialLibrary().GetUploadedTextureCount() == 1,
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
    // **3D 라이트가 메시를 비춘다**(D-407, lighting3d-plan 1 단계). 위에서 직교로 내려다본 바닥(흰 재질 × 회색 틴트)에 라이트를 내고, 다섯 자리의 픽셀을
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
    // **`Light3D` 컴포넌트가 캔버스를 비춘다**(D-407, lighting3d-plan 2 단계). 위에서 직교로 내려다본 바닥(빌트인 평면)을 프레임워크로 그린다.
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
    JBRO_CASE(TestScreenLayersDrawScreenUiOverThe3DView<JBro::D3D12RHIModule>());
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
    JBRO_CASE(TestShadowsFallBehindMeshes<JBro::D3D12RHIModule>());
    JBRO_CASE(TestShadowsFallBehindMeshes<JBro::D3D11RHIModule>());
    JBRO_CASE(TestShadowsFallBehindMeshes<JBro::VulkanRHIModule>());
    JBRO_CASE(TestEnvironmentLightsMeshes<JBro::D3D12RHIModule>());
    JBRO_CASE(TestEnvironmentLightsMeshes<JBro::D3D11RHIModule>());
    JBRO_CASE(TestEnvironmentLightsMeshes<JBro::VulkanRHIModule>());
    JBRO_CASE(TestSkyRotationTurnsTheSky<JBro::D3D12RHIModule>());
    JBRO_CASE(TestSkyRotationTurnsTheSky<JBro::D3D11RHIModule>());
    JBRO_CASE(TestSkyRotationTurnsTheSky<JBro::VulkanRHIModule>());
    JBRO_CASE(TestSkyBakesOnWorkers<JBro::D3D12RHIModule>());
    JBRO_CASE(TestCalculatedTangentsBendTheNormalMap<JBro::D3D12RHIModule>());
    JBRO_CASE(TestCalculatedTangentsBendTheNormalMap<JBro::D3D11RHIModule>());
    JBRO_CASE(TestCalculatedTangentsBendTheNormalMap<JBro::VulkanRHIModule>());
    JBRO_CASE(TestTransparentAndDoubleSidedMaterials<JBro::D3D12RHIModule>());
    JBRO_CASE(TestTransparentAndDoubleSidedMaterials<JBro::D3D11RHIModule>());
    JBRO_CASE(TestTransparentAndDoubleSidedMaterials<JBro::VulkanRHIModule>());
    JBRO_CASE(TestRenderTexturesShowA3DViewOnAMesh<JBro::D3D12RHIModule>());
    JBRO_CASE(TestRenderTexturesShowA3DViewOnAMesh<JBro::D3D11RHIModule>());
    JBRO_CASE(TestRenderTexturesShowA3DViewOnAMesh<JBro::VulkanRHIModule>());
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
    JBRO_CASE(TestViewportsSplitTheScreenIn3D<JBro::D3D12RHIModule>());
    JBRO_CASE(TestViewportsSplitTheScreenIn3D<JBro::D3D11RHIModule>());
    JBRO_CASE(TestViewportsSplitTheScreenIn3D<JBro::VulkanRHIModule>());
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
