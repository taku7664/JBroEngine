#include <JBro/D3D11RHI/D3D11RHI.h>
#include <JBro/D3D12RHI/D3D12RHI.h>
#include <JBro/VulkanRHI/VulkanRHI.h>
#include <JBro/Graphics/Renderer.h>
#include <JBro/Platform/WindowsPlatform.h>
#include <JBro/Types/Array.h>

#include <cmath>
#include <cstddef>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <JBro/Types/Angle.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/Int.h>
#include <JBro/Types/UInt.h>

// **2D 라이팅의 렌더러 쪽**(D-291, tasks/lighting2d-plan.md 1 단계). 64x64 화면에 단위 카메라(월드 = NDC)로 그리고 백버퍼를 되읽는다.
// 기대값은 시험이 따로 세운 식이다(plan §2.2): 빛 = 환경광 + Σ 색 · 반지름 감쇠 · 원뿔 감쇠, 감쇠는 0..1 의 smoothstep.
// 셰이더의 식을 옮겨 적은 것이 아니라 계획서에 적은 뜻을 적은 것이다 - 반지름·각도의 뜻이 바뀌면 여기서 드러난다.

#if defined(_MSC_VER) && defined(_DEBUG)
#include <crtdbg.h>
#endif

namespace
{
#if defined(_MSC_VER) && defined(_DEBUG)
    // 프레임 경로가 CRT 힙을 잡는지 센다(RendererContractTests 와 같은 훅).
    JBro::Int32 lightFrameAllocations = 0;
    int CountLightFrameAllocations(int operation, void*, std::size_t, int, long, const unsigned char*, int)
    {
        if (operation == _HOOK_ALLOC || operation == _HOOK_REALLOC)
        {
            ++lightFrameAllocations;
        }
        return 1;
    }
#endif

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
    };

    constexpr JBro::UInt32 Side = 64;

    // 픽셀 가운데의 월드 자리다. 단위 카메라라 NDC 와 같고, y 는 위가 +다.
    JBro::Float WorldX(JBro::UInt32 x)
    {
        return (static_cast<float>(x.Get()) + 0.5f) / (Side * 0.5f) - 1.0f;
    }

    JBro::Float WorldY(JBro::UInt32 y)
    {
        return 1.0f - (static_cast<float>(y.Get()) + 0.5f) / (Side * 0.5f);
    }

    JBro::Float Smooth(JBro::Float edge0, JBro::Float edge1, JBro::Float value)
    {
        const JBro::Float t = JBro::Float::Clamp((value - edge0) / (edge1 - edge0), 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    }

    // 라이트 하나가 이 자리에 주는 빛의 비율(0..1)이다.
    JBro::Float Reach(const JBro::Light2DSubmit& light, JBro::Float x, JBro::Float y)
    {
        const JBro::Float dx = x - light.position[0];
        const JBro::Float dy = y - light.position[1];
        const JBro::Float distance = std::sqrt(dx * dx + dy * dy);
        // 안쪽 반지름까지 다 닿고 바깥 반지름에서 0 이다.
        JBro::Float reach = 1.0f - Smooth(light.innerRadius, light.outerRadius, distance);
        if (light.kind == JBro::Light2DKind::Spot)
        {
            // 축에서 잰 각이 안쪽 반각 안이면 다 닿고 바깥 반각에서 0 이다.
            const JBro::Float angle = std::acos(JBro::Float::Clamp((dx * light.direction[0] + dy * light.direction[1]) / distance, -1.0f, 1.0f));
            reach = reach * (1.0f - Smooth(light.innerAngle.Get() * 0.5f, light.outerAngle.Get() * 0.5f, angle));
        }
        return reach;
    }

    struct Stage
    {
        JBro::WindowsPlatform platform;
        JBro::JMemoryContext memory;
        JBro::WindowHandle window;
        JBro::Renderer renderer;
        JBro::CameraParams camera;
        JBro::Array<std::byte> image;
        JBro::TextureReadback readback;

        // 장치가 없으면 거짓이다(그 API 는 건너뛴다).
        template <typename TModule>
        JBro::Bool Open(TModule& rhi, const char* what)
        {
            Check(platform.Initialize(memory), "platform must initialize for the light test");
            if (false == rhi.Initialize(memory))
            {
                std::cout << "  [skip] no device for this API; " << what << " not verified" << std::endl;
                platform.Shutdown();
                return false;
            }
            JBro::WindowDesc windowDesc;
            constexpr char title[] = "JBro light probe";
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
            config.maxSpriteSubmissions = 16;
            config.maxLights2D = 8;
            config.presentMode = JBro::PresentMode::Immediate;
            config.validation = true;
            Check(renderer.Initialize(rhi, config), "the light renderer must initialize");
            camera.projection = {{1.0f, 0.0f, 0.0f, 0.0f,
                0.0f, 1.0f, 0.0f, 0.0f,
                0.0f, 0.0f, 1.0f, 0.0f,
                0.0f, 0.0f, 0.0f, 1.0f}};
            camera.clearColor[3] = 1.0f;
            camera.viewport.width = static_cast<float>(Side.Get());
            camera.viewport.height = static_cast<float>(Side.Get());
            image.Resize(Side * Side * 4);
            return true;
        }

        template <typename TModule>
        void Close(TModule& rhi)
        {
            Check(renderer.GetDevice()->GetValidationErrorCount() == 0, "the light frames must not raise validation errors");
            renderer.Shutdown();
            rhi.Shutdown();
            platform.ClosePlatformWindow(window);
            platform.PumpEvents();
            platform.Shutdown();
        }

        // 한 프레임을 그리고 되읽는다.
        void Frame(const std::function<void()>& submit)
        {
            Check(renderer.BeginFrame() == JBro::FrameStatus::Ready, "the light frame must begin");
            Check(renderer.BeginView(camera), "the light view must open");
            submit();
            Check(renderer.EndView(), "the light view must close");
            Check(renderer.EndFrame() == JBro::FrameStatus::Ready, "the light frame must present");
            Check(renderer.ReadBackBuffer(image.Data(), image.Size(), readback), "the renderer must read its own back buffer");
        }

        Pixel Read(JBro::UInt32 x, JBro::UInt32 y) const
        {
            const std::size_t offset = static_cast<std::size_t>(y.Get()) * readback.rowPitch + static_cast<std::size_t>(x.Get()) * 4;
            const auto* bytes = reinterpret_cast<const unsigned char*>(image.Data() + offset);
            Pixel pixel;
            pixel.b = bytes[0] / 255.0f;
            pixel.g = bytes[1] / 255.0f;
            pixel.r = bytes[2] / 255.0f;
            return pixel;
        }

        void Expect(JBro::UInt32 x, JBro::UInt32 y, JBro::Float r, JBro::Float g, JBro::Float b, const char* message) const
        {
            const Pixel pixel = Read(x, y);
            const auto near = [](JBro::Float got, JBro::Float want) {
                return std::fabs(got - JBro::Float::Clamp(want, 0.0f, 1.0f)) < 0.025f;
            };
            const JBro::Bool ok = near(pixel.r, r) && near(pixel.g, g) && near(pixel.b, b);
            if (false == ok)
            {
                std::cout << "  read " << pixel.r << ", " << pixel.g << ", " << pixel.b << " wanted " << r << ", " << g << ", " << b
                          << " at " << x << ", " << y << '\n';
            }
            Check(ok, message);
        }
    };

    JBro::SpriteSubmit Quad(JBro::Float centerX, JBro::Float centerY, JBro::Float width, JBro::Float height, JBro::Float shade)
    {
        JBro::SpriteSubmit sprite;
        sprite.world.linear[0] = width;
        sprite.world.linear[3] = height;
        sprite.world.translation[0] = centerX;
        sprite.world.translation[1] = centerY;
        for (JBro::Int32 channel = 0; channel < 3; ++channel)
        {
            sprite.tint[channel] = shade;
        }
        return sprite;
    }

    // **점 라이트와 환경광이 빛을 받는 스프라이트에 곱해진다.** 화면 전체의 흰 스프라이트는 빛을 받고, 오른쪽 위 칸의 흰 스프라이트는 받지 않는다.
    // 첫 프레임은 라이트맵이 없어 빛 없이 그린다(디바이스는 프레임 안에서 텍스처를 만들지 않는다). 둘째 프레임부터 빛이다.
    template <typename TModule>
    void TestAPointLightAndTheAmbientLightTheLitSprites()
    {
        TModule rhi;
        Stage stage;
        if (false == stage.Open(rhi, "point lights"))
        {
            return;
        }
        JBro::Light2DSubmit ambient;
        ambient.kind = JBro::Light2DKind::Global;
        ambient.color[0] = 0.2f;
        ambient.color[1] = 0.1f;
        ambient.color[2] = 0.05f;
        JBro::Light2DSubmit point;
        point.kind = JBro::Light2DKind::Point;
        point.position[0] = -0.5f;
        point.position[1] = 0.0f;
        point.color[0] = 0.6f;
        point.color[1] = 0.4f;
        point.color[2] = 0.2f;
        point.innerRadius = 0.1f;
        point.outerRadius = 0.5f;
        const auto submit = [&]() {
            Check(stage.renderer.SubmitLight2D(ambient) && stage.renderer.SubmitLight2D(point), "the lights must submit");
            Check(stage.renderer.SetSpriteLighting(true), "lighting must turn on for the next sprites");
            Check(stage.renderer.SubmitSprite(Quad(0.0f, 0.0f, 2.0f, 2.0f, 1.0f)), "the lit sprite must submit");
            Check(stage.renderer.SetSpriteLighting(false), "lighting must turn off again");
            Check(stage.renderer.SubmitSprite(Quad(0.75f, 0.75f, 0.5f, 0.5f, 1.0f)), "the unlit sprite must submit");
        };

        stage.Frame(submit);
        JBro::RendererFrameStats stats = stage.renderer.GetLastFrameStats();
        Check(stats.light2DCount == 2 && stats.litViewCount == 0 && stats.viewsWithoutLightMapCount == 1,
            "the first frame has no light map yet and draws the lit sprites as they are");
        stage.Expect(16, 32, 1.0f, 1.0f, 1.0f, "without a light map a lit sprite is drawn unlit");

        stage.Frame(submit);
        stats = stage.renderer.GetLastFrameStats();
        Check(stats.litViewCount == 1 && stats.viewsWithoutLightMapCount == 0, "from the second frame the view is lit");
        const auto lightAt = [&](JBro::UInt32 x, JBro::UInt32 y, JBro::Int32 channel) {
            return ambient.color[channel] + point.color[channel] * Reach(point, WorldX(x), WorldY(y));
        };
        const auto expectLit = [&](JBro::UInt32 x, JBro::UInt32 y, const char* message) {
            stage.Expect(x, y, lightAt(x, y, 0), lightAt(x, y, 1), lightAt(x, y, 2), message);
        };
        expectLit(16, 32, "inside the inner radius the light reaches in full, on top of the ambient light");
        expectLit(23, 32, "between the radii the light falls off smoothly");
        expectLit(26, 36, "and keeps falling off toward the outer radius");
        stage.Expect(40, 48, 0.2f, 0.1f, 0.05f, "beyond the outer radius only the ambient light is left");
        stage.Expect(56, 8, 1.0f, 1.0f, 1.0f, "a sprite drawn with lighting off keeps its colour");

        // 라이트가 없는 뷰는 빛을 받는 스프라이트도 그대로다 - 라이팅이 꺼진 장면이다.
        stage.Frame([&]() {
            Check(stage.renderer.SetSpriteLighting(true), "lighting must turn on");
            Check(stage.renderer.SubmitSprite(Quad(0.0f, 0.0f, 2.0f, 2.0f, 0.7f)), "the lit sprite must submit");
        });
        stats = stage.renderer.GetLastFrameStats();
        Check(stats.litViewCount == 0 && stats.viewsWithoutLightMapCount == 0, "a view with no lights draws no light map");
        stage.Expect(16, 32, 0.7f, 0.7f, 0.7f, "a view with no lights leaves the lit sprites as they are");
        stage.Close(rhi);
    }

    // **스포트 라이트는 축 둘레의 원뿔만 비춘다.** 화면 가운데에서 오른쪽(+x)을 향하고, 전체 각은 안쪽 40 도·바깥 100 도다.
    // 환경광이 없으면 빛이 닿지 않는 곳은 검다.
    template <typename TModule>
    void TestASpotLightLightsItsCone()
    {
        TModule rhi;
        Stage stage;
        if (false == stage.Open(rhi, "spot lights"))
        {
            return;
        }
        JBro::Light2DSubmit spot;
        spot.kind = JBro::Light2DKind::Spot;
        spot.position[0] = 0.0f;
        spot.position[1] = 0.0f;
        spot.direction[0] = 1.0f;
        spot.direction[1] = 0.0f;
        spot.color[0] = 0.9f;
        spot.color[1] = 0.9f;
        spot.color[2] = 0.9f;
        spot.innerRadius = 0.9f;
        spot.outerRadius = 1.0f;
        spot.innerAngle = JBro::Degree(40.0f);
        spot.outerAngle = JBro::Degree(100.0f);
        const auto submit = [&]() {
            Check(stage.renderer.SubmitLight2D(spot), "the spot must submit");
            Check(stage.renderer.SetSpriteLighting(true), "lighting must turn on");
            Check(stage.renderer.SubmitSprite(Quad(0.0f, 0.0f, 2.0f, 2.0f, 1.0f)), "the lit sprite must submit");
        };
        stage.Frame(submit);
        stage.Frame(submit);
        const auto expectLit = [&](JBro::UInt32 x, JBro::UInt32 y, const char* message) {
            const JBro::Float light = spot.color[0] * Reach(spot, WorldX(x), WorldY(y));
            stage.Expect(x, y, light, light, light, message);
        };
        expectLit(48, 32, "on its axis the spot reaches in full");
        expectLit(48, 26, "inside the inner angle it still reaches in full");
        expectLit(48, 20, "between the angles it falls off");
        stage.Expect(32, 8, 0.0f, 0.0f, 0.0f, "beside the cone it is dark");
        stage.Expect(8, 32, 0.0f, 0.0f, 0.0f, "behind the light it is dark");
        stage.Close(rhi);
    }

    // **라이트맵은 1 을 넘는 빛을 담고, 레이어 묶음 안에서도 같은 자리에서 읽힌다.** 겹친 두 라이트(0.7 씩, 가운데에서 1.4)는 회색(0.5) 스프라이트를
    // 0.7 로 밝힌다 - 8 비트 라이트맵이었으면 1 에서 잘려 0.5 다. 불투명도 50% 레이어 안의 빛을 받는 스프라이트는 빛을 곱한 뒤 아래와 반씩 섞인다.
    // SDF 텍스트도 빛을 받는다 - 텍스처가 없으면 거리값이 1 이라 채우기 색이 그대로 나온다.
    template <typename TModule>
    void TestTheLightMapHoldsBrightLightAndWorksInsideLayers()
    {
        TModule rhi;
        Stage stage;
        if (false == stage.Open(rhi, "bright light"))
        {
            return;
        }
        JBro::Light2DSubmit left;
        left.kind = JBro::Light2DKind::Point;
        left.position[0] = -0.5f;
        left.position[1] = 0.0f;
        left.color[0] = 0.7f;
        left.color[1] = 0.7f;
        left.color[2] = 0.7f;
        left.innerRadius = 0.3f;
        left.outerRadius = 0.4f;
        JBro::Light2DSubmit twin = left;
        JBro::Light2DSubmit right = left;
        right.position[0] = 0.5f;
        right.color[0] = 0.8f;
        right.color[1] = 0.6f;
        right.color[2] = 0.4f;
        JBro::SpriteSubmit text = Quad(0.5f, -0.5f, 0.5f, 0.5f, 1.0f);
        text.shading = JBro::SpriteShading::SdfText;
        text.tint[0] = 0.5f;
        text.tint[1] = 1.0f;
        text.tint[2] = 1.0f;
        const auto submit = [&]() {
            Check(stage.renderer.SubmitLight2D(left) && stage.renderer.SubmitLight2D(twin) && stage.renderer.SubmitLight2D(right),
                "the lights must submit");
            // 아래에 빛을 받지 않는 파랑을 깔고, 왼쪽 위는 빛을 받는 회색, 오른쪽 위는 반투명 레이어 안의 빛을 받는 흰색이다.
            Check(stage.renderer.SubmitSprite(Quad(0.0f, 0.0f, 2.0f, 2.0f, 0.0f)), "the backdrop must submit");
            Check(stage.renderer.SetSpriteLighting(true), "lighting must turn on");
            Check(stage.renderer.SubmitSprite(Quad(-0.5f, 0.0f, 0.4f, 0.4f, 0.5f)), "the grey sprite must submit");
            Check(stage.renderer.BeginLayer(JBro::CompositeBlend::Normal, 0.5f), "a faded layer must open");
            Check(stage.renderer.SubmitSprite(Quad(0.5f, 0.0f, 0.4f, 0.4f, 1.0f)), "the layered sprite must submit");
            Check(stage.renderer.EndLayer(), "the faded layer must close");
            Check(stage.renderer.SubmitSprite(text), "the lit text must submit");
        };
        stage.Frame(submit);
        stage.Frame(submit);
#if defined(_MSC_VER) && defined(_DEBUG)
        // **라이트가 있는 프레임도 힙을 잡지 않는다**(§2 의 프레임 경로 MUST). 라이트맵이 선 뒤의 한 프레임을 제출부터 `EndFrame` 까지 잰다.
        {
            const _CRT_ALLOC_HOOK previous = _CrtSetAllocHook(&CountLightFrameAllocations);
            lightFrameAllocations = 0;
            const JBro::Bool begun = stage.renderer.BeginFrame() == JBro::FrameStatus::Ready && stage.renderer.BeginView(stage.camera);
            if (begun)
            {
                submit();
            }
            const JBro::Bool ended = begun && stage.renderer.EndView() && stage.renderer.EndFrame() == JBro::FrameStatus::Ready;
            _CrtSetAllocHook(previous);
            Check(ended, "the measured light frame must present");
            if (lightFrameAllocations != 0)
            {
                std::cout << "  CRT allocations in a lit frame: " << lightFrameAllocations << '\n';
            }
            Check(lightFrameAllocations == 0, "a frame with lights, lit sprites, a layer and lit text does not allocate on the CRT heap");
            Check(stage.renderer.ReadBackBuffer(stage.image.Data(), stage.image.Size(), stage.readback), "the renderer must read its own back buffer");
        }
#endif
        stage.Expect(16, 32, 0.7f, 0.7f, 0.7f, "two overlapping lights add past one, and the grey sprite shows it");
        stage.Expect(48, 32, 0.4f, 0.3f, 0.2f, "a lit sprite inside a faded layer is lit, then laid on at half opacity");
        stage.Expect(48, 48, 0.0f, 0.0f, 0.0f, "lit text out of the light's reach is dark");
        stage.Close(rhi);
    }
}

namespace
{
    // 가운데가 (x, y) 이고 반 너비가 `half` 인 상자의 네 변을 시계 반대 방향으로 낸다(바깥 법선이 오른쪽).
    void BoxEdges(JBro::Float x, JBro::Float y, JBro::Float half, JBro::Bool selfShadow, JBro::ShadowEdge2D (&edges)[4])
    {
        const JBro::Float corners[4][2] = {{x - half, y - half}, {x + half, y - half}, {x + half, y + half}, {x - half, y + half}};
        for (JBro::Int32 at = 0; at < 4; ++at)
        {
            const JBro::Int32 next = (at + 1) % 4;
            edges[at].from[0] = corners[at][0];
            edges[at].from[1] = corners[at][1];
            edges[at].to[0] = corners[next][0];
            edges[at].to[1] = corners[next][1];
            edges[at].selfShadow = selfShadow;
        }
    }

    // **그림자 변이 라이트를 가린다**(D-291 3 단계). 흰 바닥을 왼쪽의 라이트(0.8, 화면을 다 덮는다)가 비추고 가운데에 작은 상자(반 너비 0.1)가 있다.
    // 상자 뒤는 어둡고, 상자 옆을 지나는 빛과 앞은 밝다. 상자의 안쪽은 밝다(라이트를 향한 변은 밀어내지 않는다) - `selfShadow` 면 어둡다.
    // 그림자를 드리우지 않는 라이트와, 같은 뷰의 그림자 없는 라이트는 마스크를 보지 않는다. 화면 밖의 상자도 화면 안에 그림자를 드리운다(기존 L1).
    // 라이트맵이 첫 프레임에, 그림자 마스크가 둘째 프레임에 생기므로 셋째 프레임부터 그림자다.
    template <typename TModule>
    void TestShadowEdgesBlockTheLight()
    {
        TModule rhi;
        Stage stage;
        if (false == stage.Open(rhi, "shadow edges"))
        {
            return;
        }
        JBro::Light2DSubmit light;
        light.kind = JBro::Light2DKind::Point;
        light.position[0] = -0.6f;
        light.position[1] = 0.0f;
        light.color[0] = 0.8f;
        light.color[1] = 0.8f;
        light.color[2] = 0.8f;
        light.innerRadius = 2.5f;
        light.outerRadius = 3.0f;
        light.castShadows = true;
        JBro::ShadowEdge2D box[4];
        BoxEdges(0.0f, 0.0f, 0.1f, false, box);
        const auto scene = [&](const JBro::Light2DSubmit* extra) {
            return [&, extra]() {
                Check(stage.renderer.SubmitLight2D(light), "the shadowed light must submit");
                if (extra != nullptr)
                {
                    Check(stage.renderer.SubmitLight2D(*extra), "the other light must submit");
                }
                Check(stage.renderer.SubmitShadowEdges2D({box, 4}), "the box edges must submit");
                Check(stage.renderer.SetSpriteLighting(true), "lighting must turn on");
                Check(stage.renderer.SubmitSprite(Quad(0.0f, 0.0f, 2.0f, 2.0f, 1.0f)), "the lit ground must submit");
            };
        };
        stage.Frame(scene(nullptr));
        stage.Frame(scene(nullptr));
        JBro::RendererFrameStats stats = stage.renderer.GetLastFrameStats();
        Check(stats.shadowEdge2DCount == 4 && stats.shadowedLight2DCount == 0 && stats.unshadowedLight2DCount == 1,
            "the frame that meets the mask size first adds the light without its shadow");
        stage.Expect(48, 32, 0.8f, 0.8f, 0.8f, "without a mask yet the light reaches behind the box");
        stage.Frame(scene(nullptr));
        stats = stage.renderer.GetLastFrameStats();
        Check(stats.shadowedLight2DCount == 1 && stats.unshadowedLight2DCount == 0, "from the third frame the light casts its shadow");
        stage.Expect(48, 32, 0.0f, 0.0f, 0.0f, "behind the box the light does not reach");
        stage.Expect(48, 13, 0.8f, 0.8f, 0.8f, "light passing beside the box reaches");
        stage.Expect(22, 32, 0.8f, 0.8f, 0.8f, "in front of the box it reaches");
        stage.Expect(32, 32, 0.8f, 0.8f, 0.8f, "the box's own inside stays lit");

        // 같은 뷰의 그림자 없는 라이트는 마스크를 보지 않는다. 상자 뒤에 작은 라이트를 둔다.
        JBro::Light2DSubmit behind;
        behind.kind = JBro::Light2DKind::Point;
        behind.position[0] = 0.5f;
        behind.position[1] = 0.0f;
        behind.color[0] = 0.4f;
        behind.color[1] = 0.4f;
        behind.color[2] = 0.4f;
        behind.innerRadius = 0.1f;
        behind.outerRadius = 0.2f;
        stage.Frame(scene(&behind));
        stage.Expect(48, 32, 0.4f, 0.4f, 0.4f, "a light that casts no shadow lights behind the box");

        // 가림막도 제 그늘에 든다.
        BoxEdges(0.0f, 0.0f, 0.1f, true, box);
        stage.Frame(scene(nullptr));
        stage.Expect(32, 32, 0.0f, 0.0f, 0.0f, "a self-shadowing box darkens its own inside");
        BoxEdges(0.0f, 0.0f, 0.1f, false, box);

        // 그림자를 드리우지 않는 라이트는 변을 보지 않는다.
        light.castShadows = false;
        stage.Frame(scene(nullptr));
        stage.Expect(48, 32, 0.8f, 0.8f, 0.8f, "a light that casts no shadow ignores the edges");
        light.castShadows = true;

        // 화면 밖의 라이트와 상자도 화면 안에 그림자를 드리운다.
        light.position[0] = 1.6f;
        BoxEdges(1.25f, 0.0f, 0.1f, false, box);
        stage.Frame(scene(nullptr));
        stage.Expect(48, 32, 0.0f, 0.0f, 0.0f, "a box off the screen still shadows the screen");
        stage.Expect(48, 13, 0.8f, 0.8f, 0.8f, "beside its shadow the light reaches");
        stage.Close(rhi);
    }
}

namespace
{
    // **뷰마다 제 그림자 변을 본다**(D-291 3 단계). 한 프레임에 화면의 왼쪽 절반과 오른쪽 절반을 두 뷰로 그린다. 둘 다 왼쪽 끝의 라이트가 그림자를
    // 드리우고, 왼쪽 뷰의 상자는 위쪽에, 오른쪽 뷰의 상자는 아래쪽에 있다. 각 뷰의 그림자는 제 상자 뒤에만 진다 - 앞 뷰의 변을 읽으면 자리가 바뀐다.
    template <typename TModule>
    void TestEachViewShadowsWithItsOwnEdges()
    {
        TModule rhi;
        Stage stage;
        if (false == stage.Open(rhi, "shadows per view"))
        {
            return;
        }
        JBro::Light2DSubmit light;
        light.kind = JBro::Light2DKind::Point;
        light.position[0] = -0.9f;
        light.position[1] = 0.0f;
        light.color[0] = 0.8f;
        light.color[1] = 0.8f;
        light.color[2] = 0.8f;
        light.innerRadius = 2.5f;
        light.outerRadius = 3.0f;
        light.castShadows = true;
        JBro::ShadowEdge2D upper[4];
        JBro::ShadowEdge2D lower[4];
        BoxEdges(-0.5f, 0.2f, 0.1f, false, upper);
        BoxEdges(-0.5f, -0.2f, 0.1f, false, lower);
        const auto frame = [&]() {
            Check(stage.renderer.BeginFrame() == JBro::FrameStatus::Ready, "the frame must begin");
            for (JBro::Int32 half = 0; half < 2; ++half)
            {
                JBro::CameraParams camera = stage.camera;
                camera.viewport.x = half == 0 ? 0.0f : 32.0f;
                camera.viewport.width = 32.0f;
                Check(stage.renderer.BeginView(camera), "the half view must open");
                Check(stage.renderer.SubmitLight2D(light), "the light must submit");
                Check(stage.renderer.SubmitShadowEdges2D({half == 0 ? upper : lower, 4}), "the view's edges must submit");
                Check(stage.renderer.SetSpriteLighting(true), "lighting must turn on");
                Check(stage.renderer.SubmitSprite(Quad(0.0f, 0.0f, 2.0f, 2.0f, 1.0f)), "the lit ground must submit");
                Check(stage.renderer.EndView(), "the half view must close");
            }
            Check(stage.renderer.EndFrame() == JBro::FrameStatus::Ready, "the frame must present");
        };
        for (JBro::Int32 at = 0; at < 3; ++at)
        {
            frame();
        }
        Check(stage.renderer.ReadBackBuffer(stage.image.Data(), stage.image.Size(), stage.readback), "the renderer must read its own back buffer");
        // 반 화면 뷰는 월드 -1..1 이 32 픽셀이다. 상자 뒤(월드 x 0.3)는 뷰 안의 x 21 이다. 라이트에서 상자(y ±0.2)를 지난 빛은 그 자리에서 y ±0.61(픽셀 행 12·51)이다.
        stage.Expect(21, 12, 0.0f, 0.0f, 0.0f, "the left view's box shades the upper side");
        stage.Expect(21, 51, 0.8f, 0.8f, 0.8f, "and not the lower side");
        stage.Expect(32 + 21, 51, 0.0f, 0.0f, 0.0f, "the right view's box shades the lower side");
        stage.Expect(32 + 21, 12, 0.8f, 0.8f, 0.8f, "and not the upper side, which only the left view's edges cover");
        stage.Close(rhi);
    }
}

JBro::Int32 RunLight2DPixelTests()
{
    TestEachViewShadowsWithItsOwnEdges<JBro::D3D12RHIModule>();
    TestEachViewShadowsWithItsOwnEdges<JBro::D3D11RHIModule>();
    TestEachViewShadowsWithItsOwnEdges<JBro::VulkanRHIModule>();
    TestShadowEdgesBlockTheLight<JBro::D3D12RHIModule>();
    TestShadowEdgesBlockTheLight<JBro::D3D11RHIModule>();
    TestShadowEdgesBlockTheLight<JBro::VulkanRHIModule>();
    TestAPointLightAndTheAmbientLightTheLitSprites<JBro::D3D12RHIModule>();
    TestAPointLightAndTheAmbientLightTheLitSprites<JBro::D3D11RHIModule>();
    TestAPointLightAndTheAmbientLightTheLitSprites<JBro::VulkanRHIModule>();
    TestASpotLightLightsItsCone<JBro::D3D12RHIModule>();
    TestASpotLightLightsItsCone<JBro::D3D11RHIModule>();
    TestASpotLightLightsItsCone<JBro::VulkanRHIModule>();
    TestTheLightMapHoldsBrightLightAndWorksInsideLayers<JBro::D3D12RHIModule>();
    TestTheLightMapHoldsBrightLightAndWorksInsideLayers<JBro::D3D11RHIModule>();
    TestTheLightMapHoldsBrightLightAndWorksInsideLayers<JBro::VulkanRHIModule>();
    std::cout << "Light 2D pixel tests passed.\n";
    return 0;
}
