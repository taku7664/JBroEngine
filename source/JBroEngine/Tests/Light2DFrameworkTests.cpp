#include "TestClock.h"

#include <JBro/Canvas/Canvas.h>
#include <JBro/Canvas/Internal/CanvasAccess.h>
#include <JBro/D3D11RHI/D3D11RHI.h>
#include <JBro/D3D12RHI/D3D12RHI.h>
#include <JBro/VulkanRHI/VulkanRHI.h>
#include <JBro/Framework2D/Component/Camera2D.h>
#include <JBro/Framework2D/Component/Light2D.h>
#include <JBro/Framework2D/Component/Physics2D.h>
#include <JBro/Framework2D/Component/ShadowCaster2D.h>
#include <JBro/Framework2D/Component/SpriteRenderer2D.h>
#include <JBro/Framework2D/Component/Transform2D.h>
#include <JBro/Framework2DSystem/Framework2D.h>
#include <JBro/Graphics/Renderer.h>
#include <JBro/Platform/WindowsPlatform.h>

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/Int.h>
#include <JBro/Types/UInt.h>

// **2D 라이팅의 프레임워크 쪽**(D-291, tasks/lighting2d-plan.md 2 단계). 컴포넌트 `Light2D` 와 레이어의 `lit` 가 게임 화면과 캔버스 뷰에 닿는지
// 프레임워크를 통째로 돌려 픽셀로 본다. 카메라는 세로 절반 4 유닛이라 64 픽셀 화면에서 8 픽셀이 1 유닛이다.

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

    constexpr JBro::UInt32 Side = 64;
    constexpr JBro::Float PixelsPerUnit = 8.0f;
    constexpr JBro::Float Sixtieth = 1.0f / 60.0f;

    struct Pixel
    {
        JBro::Float r = 0.0f;
        JBro::Float g = 0.0f;
        JBro::Float b = 0.0f;
    };

    template <typename TModule>
    struct Stage
    {
        JBro::WindowsPlatform platform;
        TModule rhi;
        JBro::JMemoryContext memory;
        JBro::WindowHandle window;
        JBro::Renderer renderer;
        JBro::TextureHandle target;
        JBro::Array<std::byte> image;
        JBro::TextureReadback readback;
        JBro::Bool ready = false;

        JBro::Bool Open()
        {
            Check(platform.Initialize(memory), "the platform must initialize");
            if (false == rhi.Initialize(memory))
            {
                platform.Shutdown();
                return false;
            }
            JBro::WindowDesc windowDesc;
            constexpr char title[] = "JBro light framework probe";
            windowDesc.title = {title, static_cast<JBro::UInt32>(sizeof(title) - 1)};
            windowDesc.width = Side;
            windowDesc.height = Side;
            windowDesc.visible = false;
            window = platform.OpenPlatformWindow(windowDesc);
            Check(window.value != 0, "the probe window must open");
            JBro::RendererConfig config;
            config.api = rhi.GetApi();
            config.surface = platform.CreateSurface(window);
            config.surfaceExtent = {Side, Side};
            config.maxSpriteSubmissions = 64;
            config.maxMeshSubmissions = 8;
            config.maxWorldTextSubmissions = 8;
            config.maxLights2D = 8;
            config.presentMode = JBro::PresentMode::Immediate;
            config.validation = true;
            Check(renderer.Initialize(rhi, config), "the renderer must initialize");
            JBro::TextureDesc targetDesc;
            targetDesc.extent = {Side, Side};
            targetDesc.format = JBro::TextureFormat::BGRA8Unorm;
            targetDesc.usage = JBro::TextureUsage::RenderTarget | JBro::TextureUsage::Sampled;
            target = renderer.GetDevice()->CreateTexture(targetDesc);
            Check(target.IsValid(), "the target texture must be created");
            image.Resize(Side * Side * 4);
            ready = true;
            return true;
        }

        void Read()
        {
            Check(renderer.GetDevice()->ReadTexture(target, image.Data(), image.Size(), readback), "the target must read back");
        }

        // 게임 카메라로 타깃에 세 번 그린다 - 첫 프레임은 라이트맵이, 둘째는 그림자 마스크가 아직 없다.
        void RenderGame(JBro::IFramework& framework)
        {
            for (JBro::Int32 frame = 0; frame < 3; ++frame)
            {
                JBro::FrameTarget frameTarget;
                frameTarget.texture = target;
                frameTarget.extent = {Side, Side};
                Check(renderer.BeginFrame(frameTarget) == JBro::FrameStatus::Ready, "the frame must begin");
                Check(framework.Render() == JBro::RenderResult::Submitted, "the game view must submit");
                Check(renderer.EndFrame() == JBro::FrameStatus::Ready, "the frame must finish");
            }
            Read();
        }

        void RenderEditor(JBro::IFramework& framework, const JBro::EditorViewDesc& view)
        {
            for (JBro::Int32 frame = 0; frame < 2; ++frame)
            {
                Check(renderer.BeginFrame() == JBro::FrameStatus::Ready, "the frame must begin");
                framework.Render();
                Check(framework.RenderEditorView(view) == JBro::RenderResult::Submitted, "the editor view must submit");
                Check(renderer.EndFrame() == JBro::FrameStatus::Ready, "the frame must finish");
            }
            Read();
        }

        // 월드 자리(카메라 중심에서 잰)의 픽셀이다.
        Pixel At(JBro::Float x, JBro::Float y) const
        {
            const JBro::UInt32 column = static_cast<JBro::UInt32>(std::floor(x * PixelsPerUnit + Side * 0.5f));
            const JBro::UInt32 row = static_cast<JBro::UInt32>(std::floor(Side * 0.5f - y * PixelsPerUnit));
            const auto* bytes = reinterpret_cast<const unsigned char*>(
                image.Data() + static_cast<std::size_t>(row.Get()) * readback.rowPitch + static_cast<std::size_t>(column.Get()) * 4);
            return {bytes[2] / 255.0f, bytes[1] / 255.0f, bytes[0] / 255.0f};
        }

        void Expect(JBro::Float x, JBro::Float y, JBro::Float r, JBro::Float g, JBro::Float b, const char* message) const
        {
            const Pixel pixel = At(x, y);
            const auto near = [](JBro::Float got, JBro::Float want) { return std::fabs(got - want) < 0.03f; };
            const JBro::Bool ok = near(pixel.r, r) && near(pixel.g, g) && near(pixel.b, b);
            if (false == ok)
            {
                std::cout << "  read " << pixel.r << ", " << pixel.g << ", " << pixel.b << " wanted " << r << ", " << g << ", " << b
                          << " at " << x << ", " << y << '\n';
            }
            Check(ok, message);
        }

        void Close()
        {
            if (false == ready)
            {
                return;
            }
            Check(renderer.GetDevice()->GetValidationErrorCount() == 0, "the debug layer stayed quiet");
            renderer.GetDevice()->DestroyTexture(target);
            renderer.Shutdown();
            rhi.Shutdown();
            platform.ClosePlatformWindow(window);
            platform.PumpEvents();
            platform.Shutdown();
            ready = false;
        }
    };

    JBro::GameObject* MakeSprite(JBro::Canvas& canvas, const char* name, JBro::Float x, JBro::Float y, JBro::Float width, JBro::Float height)
    {
        JBro::GameObject* object = canvas.CreateObject(name);
        canvas.AttachComponent<JBro::Component::Transform2D>(object)->position = {x, y};
        auto* sprite = canvas.AttachComponent<JBro::Component::SpriteRenderer2D>(object);
        sprite->sizeMode = JBro::Component::SpriteSizeMode::Custom;
        sprite->size = {width, height};
        return object;
    }

    JBro::Component::Light2D* MakeLight(JBro::Canvas& canvas, const char* name, JBro::Float x, JBro::Float y)
    {
        JBro::GameObject* object = canvas.CreateObject(name);
        canvas.AttachComponent<JBro::Component::Transform2D>(object)->position = {x, y};
        return canvas.AttachComponent<JBro::Component::Light2D>(object);
    }

    // **라이트 컴포넌트가 빛을 받는 레이어를 비춘다.** 화면 전체의 흰 바닥(빛을 받는 기본 레이어) 위에 환경광 0.1, 왼쪽의 주황 점 라이트(세기 0.8),
    // 위를 향하게 돌린 흰 스포트(세기 0.5)가 있다. 오른쪽 위의 흰 칸은 빛을 받지 않는 레이어라 그대로 희다. 감춘 레이어의 라이트는 꺼진다.
    // 캔버스 뷰도 같은 빛이다. 패럴랙스 레이어의 라이트는 그 레이어의 스프라이트와 함께 옮겨진다.
    template <typename TModule>
    void TestLightComponentsLightTheLitLayers()
    {
        Stage<TModule> stage;
        if (false == stage.Open())
        {
            std::cout << "  [skip] no device for this API; light components not verified" << std::endl;
            return;
        }
        JBro::Framework2D framework;
        JBro::FrameworkContext context;
        JBro::Testing::AttachClock(context);
        context.renderer = &stage.renderer;
        Check(framework.Initialize(context), "the 2D framework must initialize with the renderer");
        JBro::Canvas& canvas = *framework.GetCanvas();

        JBro::GameObject* eye = canvas.CreateObject("eye");
        auto* eyeTransform = canvas.AttachComponent<JBro::Component::Transform2D>(eye);
        auto* camera = canvas.AttachComponent<JBro::Component::Camera2D>(eye);
        camera->primary = true;
        camera->orthographicSize = 4.0f;
        camera->clearColor = {0.0f, 0.0f, 0.0f, 1.0f};

        MakeSprite(canvas, "ground", 0.0f, 0.0f, 20.0f, 8.0f);
        JBro::Layer& unlit = canvas.CreateLayer("Unlit");
        unlit.SetLit(false);
        canvas.SetObjectLayer(MakeSprite(canvas, "sign", 3.0f, 3.0f, 1.0f, 1.0f), unlit.GetId());

        auto* ambient = MakeLight(canvas, "ambient", 0.0f, 0.0f);
        ambient->type = JBro::Component::Light2DType::Global;
        ambient->color = {0.1f, 0.1f, 0.1f, 1.0f};
        auto* torch = MakeLight(canvas, "torch", -2.0f, 0.0f);
        torch->color = {1.0f, 0.5f, 0.25f, 1.0f};
        torch->intensity = 0.8f;
        torch->innerRadius = 0.5f;
        torch->outerRadius = 0.8f;
        auto* lamp = MakeLight(canvas, "lamp", 1.5f, -2.5f);
        lamp->type = JBro::Component::Light2DType::Spot;
        lamp->intensity = 0.5f;
        lamp->innerRadius = 1.0f;
        lamp->outerRadius = 1.2f;
        lamp->innerAngle = 40.0f;
        lamp->outerAngle = 60.0f;
        canvas.FindComponentRaw<JBro::Component::Transform2D>(JBro::Internal::CanvasAccess::GetOwner(*lamp))->SetRotation(90.0f);
        JBro::Layer& hidden = canvas.CreateLayer("Hidden");
        hidden.SetVisible(false);
        JBro::Component::Light2D* ghost = MakeLight(canvas, "ghost", 0.0f, 2.5f);
        canvas.SetObjectLayer(JBro::Internal::CanvasAccess::GetOwner(*ghost), hidden.GetId());

        const auto update = [&]() {
            JBro::Testing::SharedClock().BeginFrame(Sixtieth);
            framework.Update();
        };
        update();
        stage.RenderGame(framework);
        stage.Expect(-2.0f, 0.0f, 0.1f + 0.8f, 0.1f + 0.4f, 0.1f + 0.2f, "the point light adds its colour times its intensity to the ambient light");
        stage.Expect(0.0f, -3.5f, 0.1f, 0.1f, 0.1f, "beyond the lights only the ambient light is left");
        stage.Expect(3.0f, 3.0f, 1.0f, 1.0f, 1.0f, "a sprite on a layer that is not lit keeps its colour");
        stage.Expect(1.5f, -1.8f, 0.6f, 0.6f, 0.6f, "a spot turned up by its object lights above it");
        stage.Expect(2.2f, -2.5f, 0.1f, 0.1f, 0.1f, "and not to the side its object faces when unturned");
        stage.Expect(0.0f, 2.5f, 0.1f, 0.1f, 0.1f, "a light on a hidden layer is off");
        const JBro::RendererFrameStats stats = stage.renderer.GetLastFrameStats();
        Check(stats.light2DCount == 3 && stats.litViewCount == 1, "the game view takes the three lights that shine");

        // 캔버스 뷰도 같은 빛이다. 패럴랙스는 걸지 않는다.
        JBro::EditorViewDesc view;
        view.target = stage.target;
        view.extent = {Side, Side};
        view.orthographicSize = 4.0f;
        stage.RenderEditor(framework, view);
        stage.Expect(-2.0f, 0.0f, 0.9f, 0.5f, 0.3f, "the canvas view is lit by the same light");
        stage.Expect(3.0f, 3.0f, 1.0f, 1.0f, 1.0f, "and leaves the unlit layer alone");

        // 패럴랙스 0.5 인 레이어의 라이트는 카메라가 2 만큼 가면 1 만큼 따라간다 - 그 레이어의 스프라이트와 같은 자리다.
        JBro::Layer& far = canvas.CreateLayer("Far");
        far.SetParallax(0.5f);
        canvas.SetObjectLayer(JBro::Internal::CanvasAccess::GetOwner(*torch), far.GetId());
        eyeTransform->position = {2.0f, 0.0f};
        update();
        stage.RenderGame(framework);
        // 카메라 중심에서 잰다. 라이트는 월드 -1 이라 화면에서 -3 이다.
        stage.Expect(-3.0f, 0.0f, 0.9f, 0.5f, 0.3f, "a light on a parallax layer moves with that layer");
        stage.Expect(-4.0f + 0.1f, 0.0f, 0.1f, 0.1f, 0.1f, "and is no longer where the camera-fixed world puts it");

        framework.Shutdown();
        stage.Close();
    }
}

namespace
{
    // **`ShadowCaster2D` 가 그림자를 드리우는 라이트를 가린다**(D-291 3 단계). 가운데의 라이트(0.8, 화면을 다 덮는다)를 네 가림막이 둘러싼다:
    // 오른쪽 상자 콜라이더, 왼쪽 원 콜라이더, 위쪽 스프라이트 사각형, 아래쪽 x 를 뒤집은(거울) 폴리곤 콜라이더. 가림막 뒤는 어둡고 그 옆은 밝다.
    // 폴리곤은 시계 반대 방향으로 적었지만 거울로 뒤집혀 월드에서는 시계 방향이다 - 감긴 방향을 넓이로 바로잡지 않으면 라이트를 향한 변이 밀려
    // 안쪽이 어두워진다(처음에는 시계 방향으로 적어 거울과 상쇄되는 바람에 이 검사가 비어 있었다). 감춘 레이어의 가림막은 그림자가 없다.
    // 패럴랙스 레이어의 가림막은 라이트처럼 그 레이어와 함께 옮겨진다. 라이트가 그림자를 드리우지 않으면 뒤도 밝다.
    template <typename TModule>
    void TestShadowCastersShadeTheLight()
    {
        Stage<TModule> stage;
        if (false == stage.Open())
        {
            std::cout << "  [skip] no device for this API; shadow casters not verified" << std::endl;
            return;
        }
        JBro::Framework2D framework;
        JBro::FrameworkContext context;
        JBro::Testing::AttachClock(context);
        context.renderer = &stage.renderer;
        Check(framework.Initialize(context), "the 2D framework must initialize with the renderer");
        JBro::Canvas& canvas = *framework.GetCanvas();
        JBro::GameObject* eye = canvas.CreateObject("eye");
        canvas.AttachComponent<JBro::Component::Transform2D>(eye);
        auto* camera = canvas.AttachComponent<JBro::Component::Camera2D>(eye);
        camera->primary = true;
        camera->orthographicSize = 4.0f;
        camera->clearColor = {0.0f, 0.0f, 0.0f, 1.0f};
        MakeSprite(canvas, "ground", 0.0f, 0.0f, 20.0f, 8.0f);

        auto* light = MakeLight(canvas, "light", 0.0f, 0.0f);
        light->color = {0.8f, 0.8f, 0.8f, 1.0f};
        light->innerRadius = 6.0f;
        light->outerRadius = 7.0f;
        light->castShadows = true;

        const auto caster = [&](const char* name, JBro::Float x, JBro::Float y) {
            JBro::GameObject* object = canvas.CreateObject(name);
            canvas.AttachComponent<JBro::Component::Transform2D>(object)->position = {x, y};
            canvas.AttachComponent<JBro::Component::ShadowCaster2D>(object);
            return object;
        };
        auto* box = canvas.AttachComponent<JBro::Component::Collider2D>(caster("box", 1.5f, 0.0f));
        box->shape = JBro::Component::ColliderShape2D::Box;
        box->size = {0.6f, 0.6f};
        auto* circle = canvas.AttachComponent<JBro::Component::Collider2D>(caster("circle", -1.5f, 0.0f));
        circle->shape = JBro::Component::ColliderShape2D::Circle;
        circle->radius = 0.3f;
        JBro::GameObject* sign = caster("sign", 0.0f, 1.5f);
        canvas.FindComponentRaw<JBro::Component::ShadowCaster2D>(sign)->shape = JBro::Component::ShadowShape2D::Sprite;
        auto* signSprite = canvas.AttachComponent<JBro::Component::SpriteRenderer2D>(sign);
        signSprite->sizeMode = JBro::Component::SpriteSizeMode::Custom;
        signSprite->size = {0.6f, 0.6f};
        signSprite->tint = {0.0f, 0.0f, 1.0f, 1.0f};
        JBro::GameObject* rock = caster("rock", 0.0f, -1.5f);
        canvas.FindComponentRaw<JBro::Component::Transform2D>(rock)->scale = {-1.0f, 1.0f};
        auto* polygon = canvas.AttachComponent<JBro::Component::Collider2D>(rock);
        polygon->shape = JBro::Component::ColliderShape2D::Polygon;
        // 로컬에서는 시계 반대 방향이다. x 를 뒤집어 월드에서는 시계 방향이 된다.
        polygon->points.Add({-0.3f, -0.3f});
        polygon->points.Add({0.3f, -0.3f});
        polygon->points.Add({0.3f, 0.3f});
        polygon->points.Add({-0.3f, 0.3f});
        // 감춘 레이어의 상자는 오른쪽 아래 대각선을 가렸을 것이다.
        JBro::Layer& hidden = canvas.CreateLayer("Hidden");
        hidden.SetVisible(false);
        JBro::GameObject* ghost = caster("ghost", 1.5f, -1.5f);
        canvas.SetObjectLayer(ghost, hidden.GetId());
        auto* ghostBox = canvas.AttachComponent<JBro::Component::Collider2D>(ghost);
        ghostBox->size = {0.6f, 0.6f};

        const auto update = [&]() {
            JBro::Testing::SharedClock().BeginFrame(Sixtieth);
            framework.Update();
        };
        update();
        stage.RenderGame(framework);
        const JBro::RendererFrameStats stats = stage.renderer.GetLastFrameStats();
        Check(stats.shadowedLight2DCount == 1, "the light casts its shadow");
        stage.Expect(3.0f, 0.0f, 0.0f, 0.0f, 0.0f, "a box collider shades what is behind it");
        stage.Expect(3.0f, 1.0f, 0.8f, 0.8f, 0.8f, "and not what is beside it");
        stage.Expect(-3.0f, 0.0f, 0.0f, 0.0f, 0.0f, "a circle collider shades what is behind it");
        stage.Expect(-3.0f, 1.0f, 0.8f, 0.8f, 0.8f, "and not what is beside it");
        stage.Expect(0.0f, 3.0f, 0.0f, 0.0f, 0.0f, "a sprite's rectangle shades what is behind it");
        stage.Expect(0.0f, 1.5f, 0.0f, 0.0f, 0.8f, "and the sprite itself stays lit");
        stage.Expect(0.0f, -3.0f, 0.0f, 0.0f, 0.0f, "a mirrored polygon written clockwise shades what is behind it");
        stage.Expect(0.0f, -1.5f, 0.8f, 0.8f, 0.8f, "and keeps its own inside lit");
        stage.Expect(3.0f, -3.0f, 0.8f, 0.8f, 0.8f, "a caster on a hidden layer casts no shadow");

        // 패럴랙스 0.5 레이어의 상자는 카메라가 2 가면 1 따라간다. 라이트(기본 레이어)는 그대로다. 상자는 월드 x 2.5 라 화면에서 0.5 이고,
        // 그 그림자는 화면 x 1.5 쪽으로 뻗는다(카메라 중심에서 잰다).
        JBro::Layer& far = canvas.CreateLayer("Far");
        far.SetParallax(0.5f);
        canvas.SetObjectLayer(JBro::Internal::CanvasAccess::GetOwner(*box), far.GetId());
        canvas.FindComponentRaw<JBro::Component::Transform2D>(eye)->position = {2.0f, 0.0f};
        update();
        stage.RenderGame(framework);
        stage.Expect(1.5f, 0.0f, 0.0f, 0.0f, 0.0f, "a caster on a parallax layer shades where its layer is drawn");
        stage.Expect(0.0f, 0.0f, 0.8f, 0.8f, 0.8f, "and not where it would be without the parallax, between the light and the box");
        canvas.FindComponentRaw<JBro::Component::Transform2D>(eye)->position = {0.0f, 0.0f};

        // `shadowSoftness` 는 빛을 그 반지름의 원판으로 본다(4 단계). 상자의 왼쪽 위 모서리를 지난 그림자 가장자리 바로 안쪽은 단단한 그림자에서는 어둡고,
        // 번지면 빛이 일부 닿는다. 가운데는 그대로 어둡다.
        update();
        stage.RenderGame(framework);
        stage.Expect(3.0f, 0.6f, 0.0f, 0.0f, 0.0f, "just inside a hard shadow's edge it is dark");
        light->shadowSoftness = 0.4f;
        update();
        stage.RenderGame(framework);
        const JBro::Float blurred = stage.At(3.0f, 0.6f).r;
        if (false == (blurred > 0.1f && blurred < 0.7f))
        {
            std::cout << "  read " << blurred << " at the soft shadow's edge\n";
        }
        Check(blurred > 0.1f && blurred < 0.7f, "a soft light's shadow edge is partly lit");
        stage.Expect(3.0f, 0.0f, 0.0f, 0.0f, 0.0f, "and the soft shadow's middle stays dark");
        light->shadowSoftness = 0.0f;

        light->castShadows = false;
        update();
        stage.RenderGame(framework);
        stage.Expect(3.0f, 0.0f, 0.8f, 0.8f, 0.8f, "a light that casts no shadow reaches behind the casters");
        framework.Shutdown();
        stage.Close();
    }
}

JBro::Int32 RunLight2DFrameworkTests()
{
    TestShadowCastersShadeTheLight<JBro::D3D12RHIModule>();
    TestShadowCastersShadeTheLight<JBro::D3D11RHIModule>();
    TestShadowCastersShadeTheLight<JBro::VulkanRHIModule>();
    TestLightComponentsLightTheLitLayers<JBro::D3D12RHIModule>();
    TestLightComponentsLightTheLitLayers<JBro::D3D11RHIModule>();
    TestLightComponentsLightTheLitLayers<JBro::VulkanRHIModule>();
    std::cout << "Light 2D framework tests passed.\n";
    return 0;
}
