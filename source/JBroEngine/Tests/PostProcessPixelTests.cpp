#include <JBro/D3D11RHI/D3D11RHI.h>
#include <JBro/D3D12RHI/D3D12RHI.h>
#include <JBro/VulkanRHI/VulkanRHI.h>
#include <JBro/Graphics/PostProcess.h>
#include <JBro/Graphics/Renderer.h>
#include <JBro/Platform/WindowsPlatform.h>
#include <JBro/Types/Array.h>

#include <cmath>
#include <cstddef>
#include <cstring>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/Int.h>
#include <JBro/Types/UInt.h>

// **후처리 6 단계**(D-342, postprocess-plan §3.9f). 효과 종류 표·주입 지점·HDR 장면·노출·톤맵을 64x64 화면에 단위 카메라(월드 = NDC)로 그려 되읽는다.
// 기대값은 시험이 따로 세운 식이다: 장면은 화면 값이고, 노출은 빛을 2^EV 배 하며, 톤 곡선은 화면 값을 2.2f 로 풀어 곡선에 넣고 다시 싼다.
// 곡선의 식은 그 곡선의 정의(Reinhard, Narkowicz 의 ACES 근사, Wrensch 의 최소 AgX)를 적은 것이다.

#if defined(_MSC_VER) && defined(_DEBUG)
#include <crtdbg.h>
#endif

namespace
{
#if defined(_MSC_VER) && defined(_DEBUG)
    JBro::Int32 postFrameAllocations = 0;
    int CountPostFrameAllocations(int operation, void*, std::size_t, int, long, const unsigned char*, int)
    {
        if (operation == _HOOK_ALLOC || operation == _HOOK_REALLOC)
        {
            ++postFrameAllocations;
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

    // ── 기대값의 식 ──
    JBro::Float Decode(JBro::Float display)
    {
        return std::pow(JBro::Float::Clamp(display, 0.0f, 1.0e6f), 2.2f);
    }

    JBro::Float Clamp01(JBro::Float value)
    {
        return JBro::Float::Clamp(value, 0.0f, 1.0f);
    }

    JBro::Float Encode(JBro::Float linearLight)
    {
        return std::pow(Clamp01(linearLight), 1.0f / 2.2f);
    }

    JBro::Float ExposureScale(JBro::Float ev)
    {
        return std::exp2(ev / 2.2f);
    }

    // 회색 하나(세 채널이 같다)를 곡선에 넣은 화면 값이다.
    JBro::Float Curve(JBro::Tonemapper curve, JBro::Float display)
    {
        const JBro::Float light = Decode(display);
        switch (curve)
        {
        case JBro::Tonemapper::Reinhard:
            return Encode(light / (1.0f + light));
        case JBro::Tonemapper::Aces:
            return Encode(Clamp01((light * (2.51f * light + 0.03f)) / (light * (2.43f * light + 0.59f) + 0.14f)));
        case JBro::Tonemapper::AgX:
        {
            // Troy Sobotka 의 AgX 기본 대비를 Benjamin Wrensch 가 6 차식으로 맞춘 것. 행렬은 GLSL 원본의 열 차례다.
            const JBro::Float inset[3][3] = {{0.842479062253094f, 0.0423282422610123f, 0.0423756549057051f},
                {0.0784335999999992f, 0.878468636469772f, 0.0784336f}, {0.0792237451477643f, 0.0791661274605434f, 0.879142973793104f}};
            const JBro::Float outset[3][3] = {{1.19687900512017f, -0.0528968517574562f, -0.0529716355144438f},
                {-0.0980208811401368f, 1.15190312990417f, -0.0980434501171241f}, {-0.0990297440797205f, -0.0989611768448433f, 1.15107367264116f}};
            const JBro::Float minEv = -12.47393f;
            const JBro::Float maxEv = 4.026069f;
            JBro::Float v[3];
            for (JBro::Int32 column = 0; column < 3; ++column)
            {
                JBro::Float sum = 0.0f;
                for (JBro::Int32 row = 0; row < 3; ++row)
                {
                    sum += JBro::Float::Clamp(light, 1e-10f, 1.0e6f) * inset[row.Get()][column.Get()];
                }
                JBro::Float encoded = std::log2(sum);
                encoded = JBro::Float::Clamp(encoded, minEv, maxEv);
                const JBro::Float x = (encoded - minEv) / (maxEv - minEv);
                const JBro::Float x2 = x * x;
                const JBro::Float x4 = x2 * x2;
                v[column.Get()] = 15.5f * x4 * x2 - 40.14f * x4 * x + 31.96f * x4 - 6.868f * x2 * x + 0.4298f * x2 + 0.1191f * x - 0.00232f;
            }
            JBro::Float out = 0.0f;
            for (JBro::Int32 row = 0; row < 3; ++row)
            {
                out += v[row.Get()] * outset[row.Get()][0];
            }
            return Clamp01(out);
        }
        default:
            return Clamp01(display);
        }
    }

    // 뷰에 낼 효과 목록이다.
    struct Effects
    {
        JBro::PostProcessEntry entries[8];
        JBro::UInt32 count = 0;
        alignas(16) std::byte blocks[512] = {};
        JBro::UInt32 used = 0;

        template <typename T>
        void Add(JBro::UInt32 type, const T& block, JBro::Bool enabled = true)
        {
            std::memcpy(blocks + used.Get(), &block, sizeof(T));
            entries[count.Get()] = {type, used, enabled};
            used = static_cast<JBro::UInt32>((static_cast<std::size_t>(used.Get()) + sizeof(T) + 15u) & ~static_cast<std::size_t>(15u));
            ++count;
        }

        void AddExposure(JBro::Float ev)
        {
            JBro::ExposureEffect effect;
            effect.exposure = ev;
            Add(JBro::PostProcessEffects::Exposure, effect);
        }

        void AddTonemap(JBro::Tonemapper curve)
        {
            JBro::TonemapEffect effect;
            effect.curve = curve;
            Add(JBro::PostProcessEffects::Tonemap, effect);
        }

        JBro::PostProcessStack Stack() const
        {
            return {{entries, count}, {blocks, used}};
        }
    };

    struct Stage
    {
        JBro::WindowsPlatform platform;
        JBro::JMemoryContext memory;
        JBro::WindowHandle window;
        JBro::Renderer renderer;
        JBro::CameraParams camera;
        JBro::Array<std::byte> image;
        JBro::TextureReadback readback;

        template <typename TModule>
        JBro::Bool Open(TModule& rhi, const char* what)
        {
            Check(platform.Initialize(memory), "platform must initialize for the post-process test");
            if (false == rhi.Initialize(memory))
            {
                std::cout << "  [skip] no device for this API; " << what << " not verified" << std::endl;
                platform.Shutdown();
                return false;
            }
            JBro::WindowDesc windowDesc;
            constexpr char title[] = "JBro post-process probe";
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
            config.maxMeshSubmissions = 4;
            config.maxLights2D = 4;
            config.presentMode = JBro::PresentMode::Immediate;
            config.validation = true;
            Check(renderer.Initialize(rhi, config), "the post-process renderer must initialize");
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
            Check(renderer.GetDevice()->GetValidationErrorCount() == 0, "the post-process frames must not raise validation errors");
            renderer.Shutdown();
            rhi.Shutdown();
            platform.ClosePlatformWindow(window);
            platform.PumpEvents();
            platform.Shutdown();
        }

        // 뷰 하나로 한 프레임을 그리고 되읽는다.
        void Frame(const Effects& effects, const std::function<void()>& submit)
        {
            Check(renderer.BeginFrame() == JBro::FrameStatus::Ready, "the post-process frame must begin");
            JBro::CameraParams view = camera;
            view.postProcess = effects.Stack();
            Check(renderer.BeginView(view), "the post-process view must open");
            submit();
            Check(renderer.EndView(), "the post-process view must close");
            Check(renderer.EndFrame() == JBro::FrameStatus::Ready, "the post-process frame must present");
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
                return std::fabs(got - Clamp01(want)) < 0.02f;
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

    // 8 비트 틴트로 실린 회색이다. 기대값도 그 값으로 센다.
    JBro::Float Quantized(JBro::Float shade)
    {
        return std::floor(shade * 255.0f + 0.5f) / 255.0f;
    }

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

    JBro::Light2DSubmit GlobalLight(JBro::Float level)
    {
        JBro::Light2DSubmit light;
        light.kind = JBro::Light2DKind::Global;
        light.color[0] = level;
        light.color[1] = level;
        light.color[2] = level;
        return light;
    }

    // **HDR 장면은 1 을 넘는 값을 들고 있다.** 빛 1.6f 을 받은 0.8f 회색은 장면에서 1.28f 이고, 노출 -2.2f EV(화면 값 0.5f 배)를 지나 0.64f 가 된다 -
    // 백버퍼 포맷으로 그렸다면 1 에서 잘려 0.5f 였다. 빛을 받지 않은 0.5f 회색은 0.25f 다. 톤맵은 `None` 이다.
    // 첫 프레임은 파이프라인과 장면 텍스처가 없어 후처리 없이 그리고(기다림으로 센다), 둘째 프레임부터 후처리다.
    template <typename TModule>
    void TestAnHdrSceneKeepsLightAboveOne()
    {
        TModule rhi;
        Stage stage;
        if (false == stage.Open(rhi, "an HDR scene"))
        {
            return;
        }
        Effects effects;
        effects.AddExposure(-2.2f);
        effects.AddTonemap(JBro::Tonemapper::None);
        const auto submit = [&]() {
            Check(stage.renderer.SubmitLight2D(GlobalLight(1.6f)), "the ambient light must submit");
            Check(stage.renderer.SubmitSprite(Quad(0.5f, 0.0f, 1.0f, 2.0f, 0.5f)), "the unlit grey must submit");
            Check(stage.renderer.SetSpriteLighting(true), "lighting must turn on");
            Check(stage.renderer.SubmitSprite(Quad(-0.5f, 0.0f, 1.0f, 2.0f, 0.8f)), "the lit grey must submit");
        };
        stage.Frame(effects, submit);
        Check(stage.renderer.GetLastFrameStats().postProcessWaitingViewCount == 1, "the first frame waits for the post-process resources");
        Check(stage.renderer.GetLastFrameStats().postProcessedViewCount == 0, "and draws the view as it is");
        stage.Frame(effects, submit);
        Check(stage.renderer.IsPostProcessReady(), "the post-process pipelines are made before the second frame");
        Check(stage.renderer.GetLastFrameStats().postProcessedViewCount == 1, "the second frame post-processes the view");
        const JBro::Float lit = Quantized(0.8f) * 1.6f * ExposureScale(-2.2f);
        stage.Expect(16, 32, lit, lit, lit, "light above one survives the HDR scene and the exposure brings it down");
        const JBro::Float unlit = Quantized(0.5f) * ExposureScale(-2.2f);
        stage.Expect(48, 32, unlit, unlit, unlit, "an unlit grey is only exposed");
        stage.Close(rhi);
    }

    // **네 곡선이 정의대로 내린다.** 회색 넷(0.1f·0.3f·0.6f·1.0f)을 세로 띠로 깔고 노출 +4 EV 로 1 을 넘긴 뒤 곡선마다 한 프레임씩 그린다.
    // `None` 은 1 위를 자르고, 나머지는 곡선의 식이다.
    template <typename TModule>
    void TestTheToneCurvesFollowTheirDefinitions()
    {
        TModule rhi;
        Stage stage;
        if (false == stage.Open(rhi, "tone curves"))
        {
            return;
        }
        const JBro::Float greys[4] = {0.1f, 0.3f, 0.6f, 1.0f};
        const auto submit = [&]() {
            for (JBro::Int32 at = 0; at < 4; ++at)
            {
                Check(stage.renderer.SubmitSprite(Quad(-0.75f + 0.5f * static_cast<float>(at.Get()), 0.0f, 0.5f, 2.0f, greys[at.Get()])),
                    "the grey stripe must submit");
            }
        };
        const JBro::Tonemapper curves[4] = {
            JBro::Tonemapper::None, JBro::Tonemapper::Reinhard, JBro::Tonemapper::Aces, JBro::Tonemapper::AgX};
        for (JBro::Tonemapper curve : curves)
        {
            Effects effects;
            effects.AddExposure(4.0f);
            effects.AddTonemap(curve);
            stage.Frame(effects, submit);
            stage.Frame(effects, submit);
            JBro::Float previous = -1.0f;
            for (JBro::Int32 at = 0; at < 4; ++at)
            {
                const JBro::Float scene = Quantized(greys[at.Get()]) * ExposureScale(4.0f);
                const JBro::Float want = Curve(curve, scene);
                stage.Expect(static_cast<JBro::UInt32>(8 + 16 * at.Get()), 32, want, want, want, "the tone curve maps the stripe as defined");
                Check(want >= previous, "the curve rises with the scene");
                previous = want;
            }
        }
        stage.Close(rhi);
    }

    // **효과는 지점 차례로 돈다.** 톤맵을 노출보다 먼저 내도 노출이 먼저 걸린다. 톤맵이 둘이면 앞의 것만 쓰고 뒤의 것은 버리고 센다.
    template <typename TModule>
    void TestEffectsRunInStageOrderWithOneTonemap()
    {
        TModule rhi;
        Stage stage;
        if (false == stage.Open(rhi, "stage order"))
        {
            return;
        }
        Effects effects;
        effects.AddTonemap(JBro::Tonemapper::Reinhard);
        effects.AddExposure(4.0f);
        effects.AddTonemap(JBro::Tonemapper::Aces);
        const auto submit = [&]() {
            Check(stage.renderer.SubmitSprite(Quad(0.0f, 0.0f, 2.0f, 2.0f, 0.6f)), "the grey must submit");
        };
        stage.Frame(effects, submit);
        stage.Frame(effects, submit);
        Check(stage.renderer.GetLastFrameStats().droppedPostProcessCount == 1, "the second tonemap is dropped and counted");
        const JBro::Float want = Curve(JBro::Tonemapper::Reinhard, Quantized(0.6f) * ExposureScale(4.0f));
        stage.Expect(32, 32, want, want, want, "the exposure runs before the first tonemap whatever order they came in");
        stage.Close(rhi);
    }

    // **뷰포트 밖은 그대로다.** 처음 그리는 타깃에서 뷰포트가 왼쪽 반이면 오른쪽 반은 지우는 색이고, 앞 뷰가 그린 타깃이면 앞 뷰의 그림이다.
    // 앞 뷰가 그린 타깃 위의 후처리 뷰는 그 그림을 장면에 옮겨 이어 그린다 - 왼쪽 반의 빨강도 노출을 받는다.
    template <typename TModule>
    void TestTheViewportBoundsThePostProcess()
    {
        TModule rhi;
        Stage stage;
        if (false == stage.Open(rhi, "post-process viewports"))
        {
            return;
        }
        Effects effects;
        effects.AddExposure(-2.2f);
        JBro::CameraParams left = stage.camera;
        left.viewport.width = static_cast<float>(Side.Get() / 2);
        left.clearColor[0] = 0.0f;
        left.clearColor[1] = 1.0f;
        left.clearColor[2] = 0.0f;
        left.postProcess = effects.Stack();
        const auto alone = [&]() {
            Check(stage.renderer.BeginFrame() == JBro::FrameStatus::Ready, "the viewport frame must begin");
            Check(stage.renderer.BeginView(left), "the left view must open");
            Check(stage.renderer.EndView(), "the left view must close");
            Check(stage.renderer.EndFrame() == JBro::FrameStatus::Ready, "the viewport frame must present");
            Check(stage.renderer.ReadBackBuffer(stage.image.Data(), stage.image.Size(), stage.readback), "the back buffer must read");
        };
        alone();
        alone();
        Check(stage.renderer.GetLastFrameStats().postProcessedViewCount == 1, "the left view is post-processed");
        const JBro::Float half = ExposureScale(-2.2f);
        stage.Expect(16, 32, 0.0f, half, 0.0f, "inside the viewport the cleared scene is exposed");
        stage.Expect(48, 32, 0.0f, 1.0f, 0.0f, "outside the viewport the first view's clear colour stays");

        JBro::CameraParams below = stage.camera;
        below.clearColor[0] = 1.0f;
        const auto stacked = [&]() {
            Check(stage.renderer.BeginFrame() == JBro::FrameStatus::Ready, "the stacked frame must begin");
            Check(stage.renderer.BeginView(below), "the view below must open");
            Check(stage.renderer.EndView(), "the view below must close");
            Check(stage.renderer.BeginView(left), "the post-processed view must open over it");
            Check(stage.renderer.SubmitSprite(Quad(-0.5f, 0.5f, 1.0f, 1.0f, 1.0f)), "the white square must submit");
            Check(stage.renderer.EndView(), "the post-processed view must close");
            Check(stage.renderer.EndFrame() == JBro::FrameStatus::Ready, "the stacked frame must present");
            Check(stage.renderer.ReadBackBuffer(stage.image.Data(), stage.image.Size(), stage.readback), "the back buffer must read");
        };
        stacked();
        stacked();
        stacked();
        Check(stage.renderer.GetLastFrameStats().postProcessedViewCount == 1, "the stacked view is post-processed");
        // 그 뷰의 NDC 는 뷰포트(왼쪽 반)다 - 흰 사각형은 왼쪽 위 16x32 픽셀이다.
        stage.Expect(8, 16, half, half, half, "what the post-processed view drew is exposed");
        stage.Expect(16, 48, half, 0.0f, 0.0f, "the red below, inside its viewport, is carried into its scene and exposed");
        stage.Expect(48, 32, 1.0f, 0.0f, 0.0f, "the red outside its viewport stays as the view below left it");
        stage.Close(rhi);
    }

    // **레이어 묶음도 HDR 장면에서 얹힌다.** 반투명 레이어(표준)와 아래 그림을 읽는 레이어(오버레이)를 후처리 뷰에 둔다. 레이어 텍스처와
    // 아래 그림 사본은 장면과 같은 포맷이라 HDR 파이프라인으로 얹는다 - 검증 레이어가 포맷이 다른 첨부를 잡는다.
    template <typename TModule>
    void TestLayersCompositeInsideAnHdrScene()
    {
        TModule rhi;
        Stage stage;
        if (false == stage.Open(rhi, "layers in an HDR scene"))
        {
            return;
        }
        Effects effects;
        effects.AddExposure(0.0f);
        const auto submit = [&]() {
            Check(stage.renderer.SubmitSprite(Quad(0.0f, 0.0f, 2.0f, 2.0f, 0.5f)), "the grey backdrop must submit");
            Check(stage.renderer.BeginLayer(JBro::CompositeBlend::Normal, 0.5f), "the faded layer must open");
            Check(stage.renderer.SubmitSprite(Quad(-0.5f, 0.0f, 1.0f, 2.0f, 1.0f)), "the white must submit into it");
            Check(stage.renderer.EndLayer(), "the faded layer must close");
            Check(stage.renderer.BeginLayer(JBro::CompositeBlend::Overlay, 1.0f), "the overlay layer must open");
            Check(stage.renderer.SubmitSprite(Quad(0.5f, 0.0f, 1.0f, 2.0f, 1.0f)), "the white must submit into it");
            Check(stage.renderer.EndLayer(), "the overlay layer must close");
        };
        for (JBro::Int32 frame = 0; frame < 4; ++frame)
        {
            stage.Frame(effects, submit);
        }
        Check(stage.renderer.GetLastFrameStats().postProcessedViewCount == 1, "the layered view is post-processed");
        Check(stage.renderer.GetLastFrameStats().compositedLayerCount == 2 && stage.renderer.GetLastFrameStats().uncompositedLayerCount == 0,
            "both layers are laid on through their textures");
        const JBro::Float grey = Quantized(0.5f);
        const JBro::Float faded = grey * 0.5f + 0.5f;
        stage.Expect(16, 32, faded, faded, faded, "white at half opacity over grey");
        // 오버레이: 아래가 0.5f 이하면 2·아래·위 = 2 · 0.5f · 1.
        const JBro::Float overlay = 2.0f * grey * 1.0f;
        stage.Expect(48, 32, overlay, overlay, overlay, "white overlaid on grey");
        stage.Close(rhi);
    }

    // **메시가 있는 3D 뷰도 HDR 이다.** 깊이가 달린 패스의 메시 파이프라인이 HDR 묶음에 있다. 흰 판의 셰이딩 값(고정 방향광)에
    // 노출 +2.2f EV(화면 값 2 배)를 걸면 1 을 넘어 `None` 이 자르고, 0.4f 판은 두 배가 된다.
    template <typename TModule>
    void TestAMeshViewIsHdrToo()
    {
        TModule rhi;
        Stage stage;
        if (false == stage.Open(rhi, "an HDR mesh view"))
        {
            return;
        }
        // 화면 가운데 띠를 덮는 판 둘(앞뒤 감김). 법선은 +z 다.
        const JBro::MeshVertex vertices[] = {
            {{-1.0f, -0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}}, {{1.0f, -0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}},
            {{1.0f, 0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}}, {{-1.0f, 0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}}};
        const JBro::UInt32 indices[] = {0, 1, 2, 0, 2, 3, 0, 2, 1, 0, 3, 2};
        const JBro::AssetHandle mesh = stage.renderer.RegisterMesh({vertices, 4}, {indices, 12});
        Check(mesh.generation != 0, "the plate must register");
        Effects effects;
        effects.AddExposure(2.2f);
        JBro::MeshSubmit plate;
        plate.mesh = mesh;
        plate.world = {{1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f}};
        const auto submit = [&]() {
            plate.tint[0] = 0.4f;
            plate.tint[1] = 0.4f;
            plate.tint[2] = 0.4f;
            Check(stage.renderer.SubmitMesh(plate), "the grey plate must submit");
        };
        for (JBro::Int32 frame = 0; frame < 3; ++frame)
        {
            stage.Frame(effects, submit);
        }
        Check(stage.renderer.GetLastFrameStats().postProcessedViewCount == 1 && stage.renderer.GetLastFrameStats().viewsWithoutDepthCount == 0,
            "the mesh view is post-processed with its depth");
        // 셰이더의 고정 빛: 0.25f + 0.75f · dot(+z, normalize(0.4f, 0.8f, 0.45f)).
        const JBro::Float lambert = 0.45f / std::sqrt(0.4f * 0.4f + 0.8f * 0.8f + 0.45f * 0.45f);
        const JBro::Float shaded = 0.4f * (0.25f + 0.75f * lambert) * ExposureScale(2.2f);
        stage.Expect(32, 32, shaded, shaded, shaded, "the plate is shaded and exposed");
        stage.Expect(32, 4, 0.0f, 0.0f, 0.0f, "the clear colour outside the plate stays black");
        stage.Close(rhi);
    }

    // ── 사용자 효과 ──
    struct HalveEffect
    {
        JBRO_REFLECT_BODY(HalveEffect)

        JBRO_FIELD(JBro::Float, factor) = 0.5f;
    };
    const HalveEffect DefaultHalve = {};

    JBro::Bool AddHalvePasses(JBro::PostProcessBuilder& builder, const void* parameters)
    {
        const HalveEffect& effect = *static_cast<const HalveEffect*>(parameters);
        const JBro::Float constants[4] = {effect.factor, 0.0f, 0.0f, 0.0f};
        return builder.AddFullscreenPass("Halve", JBro::PostProcessProgram::Exposure,
            {reinterpret_cast<const std::byte*>(constants), static_cast<JBro::UInt32>(sizeof(constants))});
    }

    JBro::Bool AddNothing(JBro::PostProcessBuilder&, const void*)
    {
        return true;
    }

    JBro::UInt32 HalveType()
    {
        JBro::UInt32 id = JBro::PostProcessEffectRegistry::Find("JBroTestHalve");
        if (id == JBro::PostProcessEffectRegistry::NotFound)
        {
            JBro::PostProcessEffectType type;
            type.name = "JBroTestHalve";
            type.stage = JBro::PostProcessStage::AfterTonemap;
            type.parameters = &JBro::GetPropertyTable<HalveEffect>();
            type.blockSize = static_cast<JBro::UInt32>(sizeof(HalveEffect));
            type.defaults = &DefaultHalve;
            type.addPasses = &AddHalvePasses;
            id = JBro::PostProcessEffectRegistry::Register(type);
        }
        return id;
    }

    // **효과 종류 표**. 빌트인이 정해진 번호에 있고, 이름이 겹치거나 패스 함수가 없거나 블록과 기본값이 맞지 않는 종류는 받지 않는다.
    void TestTheEffectRegistry()
    {
        Check(JBro::PostProcessEffectRegistry::Find("Exposure") == JBro::PostProcessEffects::Exposure, "exposure is the first built-in");
        Check(JBro::PostProcessEffectRegistry::Find("Tonemap") == JBro::PostProcessEffects::Tonemap, "tonemap is the second built-in");
        const JBro::PostProcessEffectType* tonemap = JBro::PostProcessEffectRegistry::Get(JBro::PostProcessEffects::Tonemap);
        Check(tonemap != nullptr && tonemap->stage == JBro::PostProcessStage::Tonemap && JBro::HasNeed(tonemap->needs, JBro::PostProcessNeeds::Hdr),
            "the tonemap sits at its point and needs an HDR scene");
        Check(tonemap->parameters != nullptr && tonemap->parameters->count == 1 && tonemap->blockSize == sizeof(JBro::TonemapEffect),
            "its parameter block is reflected");
        Check(JBro::PostProcessEffectRegistry::Find("NoSuchEffect") == JBro::PostProcessEffectRegistry::NotFound, "an unknown name is not found");
        Check(JBro::PostProcessEffectRegistry::Get(JBro::PostProcessEffectRegistry::Capacity) == nullptr, "an id past the table is empty");

        const JBro::UInt32 halve = HalveType();
        Check(halve != JBro::PostProcessEffectRegistry::NotFound && halve >= 2, "a user effect takes the next id");
        JBro::PostProcessEffectType copy = *JBro::PostProcessEffectRegistry::Get(halve);
        Check(JBro::PostProcessEffectRegistry::Register(copy) == JBro::PostProcessEffectRegistry::NotFound, "a name is registered once");
        copy.name = "JBroTestNoPasses";
        copy.addPasses = nullptr;
        Check(JBro::PostProcessEffectRegistry::Register(copy) == JBro::PostProcessEffectRegistry::NotFound, "an effect needs its pass function");
        copy.addPasses = &AddHalvePasses;
        copy.defaults = nullptr;
        Check(JBro::PostProcessEffectRegistry::Register(copy) == JBro::PostProcessEffectRegistry::NotFound, "a block needs its defaults");
    }

    // **사용자 효과도 같은 길이다.** 톤맵 뒤의 효과(`AfterTonemap`)를 표에 들이면 HDR 뷰에서 톤맵 뒤에 돈다 - 톤맵을 내지 않았으므로 `None` 이
    // 그 앞에 끼어 1 위를 자른 뒤 반으로 줄인다(반으로 먼저 줄였다면 0.64f). HDR 효과가 없는 뷰는 장면을 백버퍼 포맷으로 그린다.
    template <typename TModule>
    void TestAUserEffectRunsAfterTheTonemap()
    {
        TModule rhi;
        Stage stage;
        if (false == stage.Open(rhi, "a user effect"))
        {
            return;
        }
        const JBro::UInt32 halve = HalveType();
        Effects hdr;
        hdr.Add(halve, DefaultHalve);
        hdr.AddExposure(0.0f);
        const auto submit = [&]() {
            Check(stage.renderer.SubmitLight2D(GlobalLight(1.6f)), "the ambient light must submit");
            Check(stage.renderer.SetSpriteLighting(true), "lighting must turn on");
            Check(stage.renderer.SubmitSprite(Quad(0.0f, 0.0f, 2.0f, 2.0f, 0.8f)), "the lit grey must submit");
        };
        stage.Frame(hdr, submit);
        stage.Frame(hdr, submit);
        Check(stage.renderer.GetLastFrameStats().postProcessedViewCount == 1, "the view with the user effect is post-processed");
        stage.Expect(32, 32, 0.5f, 0.5f, 0.5f, "the implied tonemap clips before the user effect halves");

        Effects ldr;
        ldr.Add(halve, DefaultHalve);
        const auto white = [&]() {
            Check(stage.renderer.SubmitSprite(Quad(0.0f, 0.0f, 2.0f, 2.0f, 1.0f)), "the white must submit");
        };
        stage.Frame(ldr, white);
        stage.Frame(ldr, white);
        Check(stage.renderer.GetLastFrameStats().postProcessedViewCount == 1, "an LDR view is post-processed");
        stage.Expect(32, 32, 0.5f, 0.5f, 0.5f, "the user effect halves the white");
        stage.Close(rhi);
    }

    // **걸 수 없는 효과는 버리고 센다 - 뷰는 그린다.** 모르는 종류, 배열 밖의 블록, 받을 자리가 아직 없는 지점(`AfterLayer`)이다. 꺼진 효과는
    // 버린 것이 아니다. 통째로 얹는 뷰는 후처리를 받지 않는다(9 단계).
    template <typename TModule>
    void TestEntriesThatCannotRunAreCounted()
    {
        TModule rhi;
        Stage stage;
        if (false == stage.Open(rhi, "entries that cannot run"))
        {
            return;
        }
        JBro::UInt32 layerType = JBro::PostProcessEffectRegistry::Find("JBroTestPerLayer");
        if (layerType == JBro::PostProcessEffectRegistry::NotFound)
        {
            JBro::PostProcessEffectType type;
            type.name = "JBroTestPerLayer";
            type.stage = JBro::PostProcessStage::AfterLayer;
            type.addPasses = &AddNothing;
            layerType = JBro::PostProcessEffectRegistry::Register(type);
        }
        Effects effects;
        JBro::ExposureEffect darker;
        darker.exposure = -2.2f;
        effects.Add(JBro::PostProcessEffects::Exposure, darker, false);
        effects.Add(999, darker);
        effects.Add(layerType, darker);
        effects.entries[effects.count.Get()] = {JBro::PostProcessEffects::Exposure, 4096, true};
        ++effects.count;
        const auto submit = [&]() {
            Check(stage.renderer.SubmitSprite(Quad(0.0f, 0.0f, 2.0f, 2.0f, 1.0f)), "the white must submit");
        };
        stage.Frame(effects, submit);
        stage.Frame(effects, submit);
        Check(stage.renderer.GetLastFrameStats().droppedPostProcessCount == 3, "the unknown type, the stray block and the layer effect are dropped");
        Check(stage.renderer.GetLastFrameStats().postProcessedViewCount == 0 && stage.renderer.GetLastFrameStats().postProcessWaitingViewCount == 0,
            "with nothing left to run the view is drawn as it is");
        stage.Expect(32, 32, 1.0f, 1.0f, 1.0f, "and the disabled exposure does not darken it");

        Effects exposure;
        exposure.AddExposure(-2.2f);
        Check(stage.renderer.BeginFrame() == JBro::FrameStatus::Ready, "the composite frame must begin");
        JBro::CameraParams faded = stage.camera;
        faded.composite = JBro::CompositeBlend::Normal;
        faded.compositeOpacity = 0.5f;
        faded.postProcess = exposure.Stack();
        Check(stage.renderer.BeginView(faded), "the faded view must open");
        Check(stage.renderer.EndView(), "the faded view must close");
        Check(stage.renderer.EndFrame() == JBro::FrameStatus::Ready, "the composite frame must present");
        Check(stage.renderer.GetLastFrameStats().unpostprocessedCompositeViewCount == 1, "a view laid on as a whole does not take a post-process yet");
        stage.Close(rhi);
    }

    // **후처리 프레임도 힙을 잡지 않는다**(프레임 경로 MUST). 자원이 선 뒤의 한 프레임을 제출부터 `EndFrame` 까지 잰다.
    template <typename TModule>
    void TestAPostProcessFrameDoesNotAllocate()
    {
#if defined(_MSC_VER) && defined(_DEBUG)
        TModule rhi;
        Stage stage;
        if (false == stage.Open(rhi, "post-process allocations"))
        {
            return;
        }
        Effects effects;
        effects.AddExposure(1.0f);
        effects.AddTonemap(JBro::Tonemapper::AgX);
        effects.Add(HalveType(), DefaultHalve);
        const auto submit = [&]() {
            Check(stage.renderer.SubmitLight2D(GlobalLight(1.2f)), "the ambient light must submit");
            Check(stage.renderer.SetSpriteLighting(true), "lighting must turn on");
            Check(stage.renderer.SubmitSprite(Quad(0.0f, 0.0f, 2.0f, 2.0f, 0.8f)), "the lit grey must submit");
        };
        stage.Frame(effects, submit);
        stage.Frame(effects, submit);
        const JBro::PostProcessStack stack = effects.Stack();
        const _CRT_ALLOC_HOOK previous = _CrtSetAllocHook(&CountPostFrameAllocations);
        postFrameAllocations = 0;
        JBro::CameraParams view = stage.camera;
        view.postProcess = stack;
        const JBro::Bool begun = stage.renderer.BeginFrame() == JBro::FrameStatus::Ready && stage.renderer.BeginView(view);
        if (begun)
        {
            submit();
        }
        const JBro::Bool ended = begun && stage.renderer.EndView() && stage.renderer.EndFrame() == JBro::FrameStatus::Ready;
        _CrtSetAllocHook(previous);
        Check(ended, "the measured post-process frame must present");
        if (postFrameAllocations != 0)
        {
            std::cout << "  CRT allocations in a post-processed frame: " << postFrameAllocations << '\n';
        }
        Check(postFrameAllocations == 0, "a post-processed frame does not allocate on the CRT heap");
        Check(stage.renderer.GetLastFrameStats().postProcessedViewCount == 1, "and it was post-processed");
        stage.Close(rhi);
#endif
    }

    template <typename TModule>
    void RunOn()
    {
        TestAnHdrSceneKeepsLightAboveOne<TModule>();
        TestTheToneCurvesFollowTheirDefinitions<TModule>();
        TestEffectsRunInStageOrderWithOneTonemap<TModule>();
        TestTheViewportBoundsThePostProcess<TModule>();
        TestLayersCompositeInsideAnHdrScene<TModule>();
        TestAMeshViewIsHdrToo<TModule>();
        TestAUserEffectRunsAfterTheTonemap<TModule>();
        TestEntriesThatCannotRunAreCounted<TModule>();
        TestAPostProcessFrameDoesNotAllocate<TModule>();
    }
}

JBro::Int32 RunPostProcessPixelTests()
{
    TestTheEffectRegistry();
    RunOn<JBro::D3D12RHIModule>();
    RunOn<JBro::D3D11RHIModule>();
    RunOn<JBro::VulkanRHIModule>();
    std::cout << "Post-process pixel tests passed.\n";
    return 0;
}
