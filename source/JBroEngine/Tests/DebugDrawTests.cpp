#include "TestClock.h"

#include <JBro/Canvas/Canvas.h>
#include <JBro/D3D11RHI/D3D11RHI.h>
#include <JBro/D3D12RHI/D3D12RHI.h>
#include <JBro/VulkanRHI/VulkanRHI.h>
#include <JBro/Framework2D/Component/Camera2D.h>
#include <JBro/Framework2D/Component/Transform2D.h>
#include <JBro/Framework2D/Service/DebugDraw2DService.h>
#include <JBro/Framework2DSystem/Framework2D.h>
#include <JBro/Framework3D/Component/Camera3D.h>
#include <JBro/Framework3D/Component/MeshRenderer3D.h>
#include <JBro/Framework3D/Component/Transform3D.h>
#include <JBro/Framework3D/Service/DebugDraw3DService.h>
#include <JBro/Framework3DSystem/Framework3D.h>
#include <JBro/Framework3DSystem/Rendering/MeshLibrary.h>
#include <JBro/Graphics/Renderer.h>
#include <JBro/Host/DebugDrawSystem.h>
#include <JBro/Platform/WindowsPlatform.h>
#include <JBro/Runtime/SystemContext.h>

#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>

#if defined(_MSC_VER) && defined(_DEBUG)
#include <crtdbg.h>
#endif

// 디버그 드로(D-243): 저장소의 수명 셋, 서비스가 도형을 펴는 모양, 두 브리지가 뷰마다 픽셀 두께로 그리는 것을 본다.
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

    bool Near(double left, double right, double tolerance = 1e-4)
    {
        return std::fabs(left - right) <= tolerance;
    }

    // 공통 시스템 컨텍스트에 이 저장소를 건다. 서비스가 이것에 쌓는다.
    class BoundStore
    {
    public:
        explicit BoundStore(JBro::System::DebugDrawSystem& store)
        {
            JBro::SystemContext systems = JBro::GetSystemContext();
            systems.DebugDraw = &store;
            JBro::BindSystemContext(systems);
        }
        ~BoundStore()
        {
            JBro::SystemContext systems = JBro::GetSystemContext();
            systems.DebugDraw = nullptr;
            JBro::BindSystemContext(systems);
        }
        BoundStore(const BoundStore&) = delete;
        BoundStore& operator=(const BoundStore&) = delete;
    };

    JBro::DebugLine MakeLine(float duration = 0.0f)
    {
        JBro::DebugLine line;
        line.to[0] = 1.0f;
        line.duration = duration;
        return line;
    }

    constexpr float Sixtieth = 1.0f / 60.0f;

    // ── 저장소 ───────────────────────────────────────────────────────────

    void TestOneFrameLinesLiveOneFrame()
    {
        JBro::FrameworkContext context;
        JBro::Testing::AttachClock(context);
        JBro::System::TimeSystem& time = JBro::Testing::SharedClock();
        JBro::System::DebugDrawSystem store;
        store.Initialize(16, &time);
        const JBro::DebugLine line = MakeLine();
        time.BeginFrame(Sixtieth);
        store.BeginFrame();
        Check(store.AddLines(&line, 1) == 1 && store.GetLineCount() == 1, "a line drawn this frame is held for this frame");
        time.BeginFrame(Sixtieth);
        store.BeginFrame();
        Check(store.GetLineCount() == 0, "a zero-second line is gone the next frame");
    }

    void TestTimedLinesAgeInGameTime()
    {
        JBro::FrameworkContext context;
        JBro::Testing::AttachClock(context);
        JBro::System::TimeSystem& time = JBro::Testing::SharedClock();
        JBro::System::DebugDrawSystem store;
        store.Initialize(16, &time);
        const JBro::DebugLine line = MakeLine(0.05f);
        time.BeginFrame(Sixtieth);
        store.BeginFrame();
        store.AddLines(&line, 1);
        time.BeginFrame(0.02f);
        store.BeginFrame();
        Check(store.GetLineCount() == 1 && Near(store.GetLine(0).duration, 0.03), "a timed line loses the frame's game time");
        time.SetPaused(true);
        time.BeginFrame(1.0f);
        store.BeginFrame();
        Check(store.GetLineCount() == 1 && Near(store.GetLine(0).duration, 0.03), "while paused it does not age");
        time.SetPaused(false);
        time.BeginFrame(0.02f);
        store.BeginFrame();
        Check(store.GetLineCount() == 1, "0.01 s is still left");
        time.BeginFrame(0.02f);
        store.BeginFrame();
        Check(store.GetLineCount() == 0, "and then it is gone");
    }

    // 고정 스텝에서 그린 0 초짜리 선은 다음 고정 스텝이 돌 때까지 남는다 - 스텝이 없는 프레임에 깜빡이지 않는다(기존 엔진 D4).
    void TestFixedStepLinesLastUntilTheNextStep()
    {
        JBro::FrameworkContext context;
        JBro::Testing::AttachClock(context);
        JBro::System::TimeSystem& time = JBro::Testing::SharedClock();
        JBro::System::DebugDrawSystem store;
        store.Initialize(16, &time);
        const JBro::DebugLine line = MakeLine();
        time.BeginFrame(Sixtieth);
        store.BeginFrame();
        time.BeginFixedStep();
        store.AddLines(&line, 1);
        time.EndFixedSteps();
        // 1/240 초 프레임은 고정 스텝이 없다.
        time.BeginFrame(Sixtieth / 4.0f);
        store.BeginFrame();
        Check(time.GetFrameTime().fixedStepCount == 0 && store.GetLineCount() == 1,
            "a frame without a fixed step keeps what the last step drew");
        time.BeginFrame(Sixtieth);
        store.BeginFrame();
        Check(time.GetFrameTime().fixedStepCount == 1 && store.GetLineCount() == 0,
            "the next frame with a step clears it, because the step draws it again");
    }

    void TestAPausedFrameKeepsTheLastPicture()
    {
        JBro::FrameworkContext context;
        JBro::Testing::AttachClock(context);
        JBro::System::TimeSystem& time = JBro::Testing::SharedClock();
        JBro::System::DebugDrawSystem store;
        store.Initialize(16, &time);
        const JBro::DebugLine line = MakeLine();
        time.BeginFrame(Sixtieth);
        store.BeginFrame();
        store.AddLines(&line, 1);
        time.SetPaused(true);
        for (int frame = 0; frame < 3; ++frame)
        {
            time.BeginFrame(Sixtieth);
            store.BeginFrame();
        }
        Check(store.GetLineCount() == 1, "no script runs while paused, so the last lines must stay on screen");
        time.RequestStep();
        time.BeginFrame(Sixtieth);
        store.BeginFrame();
        Check(store.GetLineCount() == 0, "a single-frame step is a simulated frame and clears them");
        time.SetPaused(false);
        store.AddLines(&line, 1);
        store.Clear();
        Check(store.GetLineCount() == 0, "Clear empties everything");
    }

    void TestCapacityAndBadValues()
    {
        JBro::System::DebugDrawSystem store;
        store.Initialize(4, nullptr);
        JBro::DebugLine lines[6];
        for (JBro::DebugLine& line : lines)
        {
            line = MakeLine();
        }
        Check(store.AddLines(lines, 6) == 4 && store.GetLineCount() == 4, "a full store takes up to its capacity");
        Check(store.GetDroppedCount() == 2, "and counts the rest as dropped");
        store.BeginFrame();
        Check(store.GetDroppedCount() == 0 && store.GetLineCount() == 0, "without a clock every line lives one frame");

        JBro::DebugLine bad = MakeLine();
        bad.to[1] = std::numeric_limits<float>::quiet_NaN();
        JBro::DebugLine thin = MakeLine(-3.0f);
        thin.thickness = 0.0f;
        JBro::DebugLine thick = MakeLine();
        thick.thickness = 1000.0f;
        const JBro::DebugLine mixed[3] = {bad, thin, thick};
        Check(store.AddLines(mixed, 3) == 2 && store.GetRejectedCount() == 1, "a NaN line is refused and counted");
        Check(store.GetLine(0).thickness == 0.25f && store.GetLine(1).thickness == 64.0f, "thickness is clamped to 0.25..64");
        Check(store.GetLine(0).duration == 0.0f, "a negative duration is one frame");
        Check(store.AddLines(nullptr, 3) == 0, "a null list adds nothing");
    }

#if defined(_MSC_VER) && defined(_DEBUG)
    int g_allocations = 0;
    int CountAllocations(int operation, void*, std::size_t, int, long, const unsigned char*, int)
    {
        if (operation == _HOOK_ALLOC || operation == _HOOK_REALLOC)
        {
            ++g_allocations;
        }
        return 1;
    }
#endif

    // 매 프레임 경로(쌓기·거두기·서비스의 도형)는 힙을 건드리지 않는다(§9). 기존 엔진은 선 목록이 자라고 그릴 때마다 GPU 버퍼를 만들었다.
    void TestTheFramePathDoesNotAllocate()
    {
        JBro::FrameworkContext context;
        JBro::Testing::AttachClock(context);
        JBro::System::TimeSystem& time = JBro::Testing::SharedClock();
        JBro::System::DebugDrawSystem store;
        store.Initialize(4096, &time);
        const BoundStore bound(store);
        const JBro::Service::DebugDraw2DService debug2D;
        const JBro::Service::DebugDraw3DService debug3D;
        const auto frame = [&]()
        {
            time.BeginFrame(Sixtieth);
            store.BeginFrame();
            debug2D.Circle({0.0f, 0.0f}, 1.0f);
            debug2D.Rect({1.0f, 1.0f}, {2.0f, 1.0f}, 0.3f, JBro::Color{1.0f, 0.0f, 0.0f, 1.0f}, 0.1f);
            debug3D.Sphere({0.0f, 0.0f, 0.0f}, 1.0f);
            debug3D.Box({0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f});
        };
        for (int i = 0; i < 30; ++i)
        {
            frame();
        }
#if defined(_MSC_VER) && defined(_DEBUG)
        g_allocations = 0;
        const _CRT_ALLOC_HOOK previous = _CrtSetAllocHook(&CountAllocations);
        for (int i = 0; i < 120; ++i)
        {
            frame();
        }
        _CrtSetAllocHook(previous);
        std::cout << "  CRT allocations during 120 debug draw frames: " << g_allocations << std::endl;
        Check(g_allocations == 0, "drawing and ageing debug lines must not allocate");
#endif
        Check(store.GetLineCount() > 0 && store.GetDroppedCount() == 0, "and the lines were stored");
    }

    // ── 서비스 ───────────────────────────────────────────────────────────

    void TestTheTwoDimensionalShapes()
    {
        JBro::System::DebugDrawSystem store;
        store.Initialize(1024, nullptr);
        const BoundStore bound(store);
        const JBro::Service::DebugDraw2DService debug;

        debug.Line({1.0f, 2.0f}, {3.0f, 4.0f}, JBro::Color{1.0f, 0.5f, 0.0f, 1.0f}, 2.0f, 3.0f);
        Check(store.GetLineCount() == 1, "a line is one line");
        const JBro::DebugLine& line = store.GetLine(0);
        Check(line.from[0] == 1.0f && line.from[1] == 2.0f && line.from[2] == 0.0f && line.to[0] == 3.0f && line.to[1] == 4.0f,
            "with its ends on z = 0");
        Check(line.color[0] == 255 && line.color[1] == 128 && line.color[2] == 0 && line.color[3] == 255,
            "its colour in bytes, rounded");
        Check(line.duration == 2.0f && line.thickness == 3.0f, "and its time and thickness");

        store.Clear();
        debug.Ray({1.0f, 1.0f}, {2.0f, 0.0f});
        Check(store.GetLineCount() == 1 && store.GetLine(0).to[0] == 3.0f, "a ray ends at origin + direction");

        store.Clear();
        debug.Circle({0.0f, 0.0f}, 2.0f);
        Check(store.GetLineCount() == 32, "a circle is 32 segments");
        Check(Near(store.GetLine(0).from[0], 2.0) && Near(store.GetLine(31).to[0], 2.0, 1e-4) && Near(store.GetLine(31).to[1], 0.0, 1e-4),
            "starting and ending at (r, 0)");
        for (std::uint32_t index = 0; index < 32; ++index)
        {
            const JBro::DebugLine& segment = store.GetLine(index);
            Check(Near(std::hypot(segment.to[0], segment.to[1]), 2.0, 1e-4), "every point is on the circle");
        }

        store.Clear();
        debug.Rect({0.0f, 0.0f}, {4.0f, 2.0f}, 1.5707963f);
        Check(store.GetLineCount() == 4, "a rectangle is four edges");
        Check(Near(store.GetLine(0).from[0], 1.0) && Near(store.GetLine(0).from[1], -2.0),
            "rotated a quarter turn, the first corner (-2, -1) goes to (1, -2)");

        store.Clear();
        const JBro::Vector2 points[3] = {{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}};
        debug.Polygon(points, 3, false);
        Check(store.GetLineCount() == 2, "an open polyline of three points is two lines");
        debug.Polygon(points, 3, true);
        Check(store.GetLineCount() == 5, "a closed one is three");
        debug.Polygon(points, 1, true);
        Check(store.GetLineCount() == 5, "one point draws nothing");

        store.Clear();
        JBro::Vector2 many[200];
        for (int index = 0; index < 200; ++index)
        {
            many[index] = {static_cast<float>(index), 0.0f};
        }
        debug.Polygon(many, 200, true);
        Check(store.GetLineCount() == 200, "a long polygon crosses the 64-line batch without losing a line");
        Check(store.GetLine(199).from[0] == 199.0f && store.GetLine(199).to[0] == 0.0f, "and closes back to the start");

        store.Clear();
        debug.Arrow({0.0f, 0.0f}, {4.0f, 0.0f});
        Check(store.GetLineCount() == 3, "an arrow is its shaft and two barbs");
        Check(store.GetLine(1).from[0] == 4.0f && store.GetLine(1).to[0] < 4.0f && store.GetLine(2).to[0] < 4.0f
                && store.GetLine(1).to[1] * store.GetLine(2).to[1] < 0.0f,
            "the barbs point back from the tip, one to each side");
        Check(Near(std::hypot(store.GetLine(1).to[0] - 4.0f, store.GetLine(1).to[1]), 1.0, 1e-4), "a quarter of the length long");

        store.Clear();
        debug.Cross({1.0f, 1.0f}, 1.0f);
        Check(store.GetLineCount() == 2, "a cross is two lines");
    }

    void TestTheThreeDimensionalShapes()
    {
        JBro::System::DebugDrawSystem store;
        store.Initialize(1024, nullptr);
        const BoundStore bound(store);
        const JBro::Service::DebugDraw3DService debug;

        debug.Box({0.0f, 0.0f, 0.0f}, {1.0f, 2.0f, 3.0f});
        Check(store.GetLineCount() == 12, "a box is twelve edges");
        for (std::uint32_t index = 0; index < 12; ++index)
        {
            const JBro::DebugLine& edge = store.GetLine(index);
            const float length = std::sqrt((edge.to[0] - edge.from[0]) * (edge.to[0] - edge.from[0])
                + (edge.to[1] - edge.from[1]) * (edge.to[1] - edge.from[1]) + (edge.to[2] - edge.from[2]) * (edge.to[2] - edge.from[2]));
            Check(Near(length, 2.0) || Near(length, 4.0) || Near(length, 6.0), "every edge is along one axis of the box");
        }

        store.Clear();
        debug.Sphere({0.0f, 0.0f, 0.0f}, 1.0f);
        Check(store.GetLineCount() == 96, "a sphere is three great circles");

        store.Clear();
        debug.Circle({0.0f, 0.0f, 5.0f}, {0.0f, 0.0f, 1.0f}, 2.0f);
        Check(store.GetLineCount() == 32, "a circle is 32 segments");
        for (std::uint32_t index = 0; index < 32; ++index)
        {
            Check(Near(store.GetLine(index).to[2], 5.0), "lying in the plane its normal says");
        }

        store.Clear();
        debug.Axes({0.0f, 0.0f, 0.0f}, JBro::Quaternion{}, 2.0f);
        Check(store.GetLineCount() == 3, "axes are three lines");
        Check(store.GetLine(0).to[0] == 2.0f && store.GetLine(0).color[0] == 255, "x is red");
        Check(store.GetLine(1).to[1] == 2.0f && store.GetLine(1).color[1] == 255, "y is green");
        Check(store.GetLine(2).to[2] == 2.0f && store.GetLine(2).color[2] == 255, "z is blue");

        store.Clear();
        debug.Arrow({0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -4.0f});
        Check(store.GetLineCount() == 5, "a 3D arrow is its shaft and four barbs");

        store.Clear();
        debug.Cross({0.0f, 0.0f, 0.0f}, 1.0f);
        Check(store.GetLineCount() == 3, "a 3D cross is three lines");
    }

    void TestUnboundServicesDoNothing()
    {
        JBro::SystemContext systems = JBro::GetSystemContext();
        systems.DebugDraw = nullptr;
        JBro::BindSystemContext(systems);
        const JBro::Service::DebugDraw2DService debug2D;
        const JBro::Service::DebugDraw3DService debug3D;
        debug2D.Circle({0.0f, 0.0f}, 1.0f);
        debug3D.Sphere({0.0f, 0.0f, 0.0f}, 1.0f);
    }

    // ── 픽셀 ─────────────────────────────────────────────────────────────

    constexpr std::uint32_t TargetWidth = 96;
    constexpr std::uint32_t TargetHeight = 64;

    struct Pixel
    {
        float r = 0.0f;
        float g = 0.0f;
        float b = 0.0f;
    };

    Pixel ReadPixel(const JBro::Array<std::byte>& image, std::uint32_t rowPitch, std::uint32_t x, std::uint32_t y)
    {
        const auto* bytes = reinterpret_cast<const unsigned char*>(image.Data() + static_cast<std::size_t>(y) * rowPitch + x * 4);
        return {bytes[2] / 255.0f, bytes[1] / 255.0f, bytes[0] / 255.0f};
    }

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
            constexpr char title[] = "JBro debug draw probe";
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
            config.maxSpriteSubmissions = 256;
            config.maxMeshSubmissions = 8;
            config.maxWorldTextSubmissions = 256;
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

        void Read()
        {
            image.Resize(TargetWidth * TargetHeight * 4);
            Check(renderer.GetDevice()->ReadTexture(target, image.Data(), image.Size(), readback), "the target must read back");
        }

        // 게임 카메라로 타깃에 그린다.
        void RenderGame(JBro::IFramework& framework)
        {
            JBro::FrameTarget frameTarget;
            frameTarget.texture = target;
            frameTarget.extent = {TargetWidth, TargetHeight};
            Check(renderer.BeginFrame(frameTarget) == JBro::FrameStatus::Ready, "the frame must begin");
            Check(framework.Render() == JBro::RenderResult::Submitted, "the game view must submit");
            Check(renderer.EndFrame() == JBro::FrameStatus::Ready, "the frame must finish");
            Read();
        }

        // 편집 카메라로 타깃에 그린다(게임 뷰는 백버퍼로 간다).
        void RenderEditor(JBro::IFramework& framework, const JBro::EditorViewDesc& view)
        {
            Check(renderer.BeginFrame() == JBro::FrameStatus::Ready, "the frame must begin");
            framework.Render();
            Check(framework.RenderEditorView(view) == JBro::RenderResult::Submitted, "the editor view must submit");
            Check(renderer.EndFrame() == JBro::FrameStatus::Ready, "the frame must finish");
            Read();
        }

        // x 열에서 조건에 맞는 행의 수다.
        template <typename TPredicate>
        std::uint32_t CountRows(std::uint32_t x, TPredicate predicate) const
        {
            std::uint32_t rows = 0;
            for (std::uint32_t y = 0; y < TargetHeight; ++y)
            {
                if (predicate(ReadPixel(image, readback.rowPitch, x, y)))
                {
                    ++rows;
                }
            }
            return rows;
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

    bool IsRed(const Pixel& pixel)
    {
        return pixel.r > 0.8f && pixel.g < 0.2f && pixel.b < 0.2f;
    }

    bool IsGreen(const Pixel& pixel)
    {
        return pixel.g > 0.8f && pixel.r < 0.2f && pixel.b < 0.2f;
    }

    // **2D 선은 뷰마다 같은 픽셀 굵기다.** 게임 카메라(세로 절반 4 유닛 = 32 픽셀)와 두 배 당긴 캔버스 뷰(세로 절반 2 유닛) 둘 다에서
    // 3 픽셀 선이 세 행쯤을 칠한다. 월드 두께로 그렸다면 캔버스 뷰에서 두 배가 된다.
    template <typename TModule>
    void TestTwoDimensionalLinesKeepTheirPixelWidth()
    {
        Stage<TModule> stage;
        if (false == stage.Open())
        {
            std::cout << "  [skip] no device for this API; 2D debug lines not verified" << std::endl;
            return;
        }
        JBro::Framework2D framework;
        JBro::FrameworkContext context;
        JBro::Testing::AttachClock(context);
        JBro::System::DebugDrawSystem store;
        store.Initialize(256, &JBro::Testing::SharedClock());
        store.SetGameViewVisible(true);
        const BoundStore bound(store);
        context.renderer = &stage.renderer;
        context.debugDraw = &store;
        Check(framework.Initialize(context), "the 2D framework must initialize with the renderer");
        JBro::Canvas& canvas = *framework.GetCanvas();
        JBro::GameObject* eye = canvas.CreateObject("eye");
        canvas.AttachComponent<JBro::Component::Transform2D>(eye);
        auto* camera = canvas.AttachComponent<JBro::Component::Camera2D>(eye);
        camera->primary = true;
        camera->orthographicSize = 4.0f;
        camera->clearColor = {0.0f, 0.0f, 0.0f, 1.0f};

        const JBro::Service::DebugDraw2DService debug;
        const auto drawFrame = [&]()
        {
            JBro::Testing::SharedClock().BeginFrame(Sixtieth);
            store.BeginFrame();
            // 가로선 y = 0.5 는 화면 가운데 네 픽셀 위다(8 픽셀/유닛). x 는 -3..3 이라 가로 48 픽셀이다.
            debug.Line({-3.0f, 0.5f}, {3.0f, 0.5f}, JBro::Color{1.0f, 0.0f, 0.0f, 1.0f}, 0.0f, 3.0f);
            framework.Update();
        };
        drawFrame();
        stage.RenderGame(framework);
        const auto red = [](const Pixel& pixel) { return IsRed(pixel); };
        const std::uint32_t gameRows = stage.CountRows(TargetWidth / 2, red);
        std::cout << "  a 3 px 2D line covers " << gameRows << " rows in the game view" << std::endl;
        Check(gameRows >= 2 && gameRows <= 4, "a 3 px line must cover about three rows of the game view");
        Check(IsRed(ReadPixel(stage.image, stage.readback.rowPitch, TargetWidth / 2, TargetHeight / 2 - 4)),
            "at y = 0.5, four pixels above the middle");
        Check(false == IsRed(ReadPixel(stage.image, stage.readback.rowPitch, 10, TargetHeight / 2 - 4)),
            "and ending at x = -3, 24 pixels from the middle");

        store.SetGameViewVisible(false);
        drawFrame();
        stage.RenderGame(framework);
        Check(stage.CountRows(TargetWidth / 2, red) == 0, "a hidden game view draws no debug lines");

        JBro::EditorViewDesc view;
        view.target = stage.target;
        view.extent = {TargetWidth, TargetHeight};
        view.orthographicSize = 2.0f;
        view.clearColor[0] = view.clearColor[1] = view.clearColor[2] = 0.0f;
        drawFrame();
        stage.RenderEditor(framework, view);
        const std::uint32_t editorRows = stage.CountRows(TargetWidth / 2, red);
        std::cout << "  the same line covers " << editorRows << " rows in a canvas view zoomed in twice" << std::endl;
        Check(editorRows >= 2 && editorRows <= 4, "zooming the canvas view must not thicken a pixel-width line");
        Check(IsRed(ReadPixel(stage.image, stage.readback.rowPitch, TargetWidth / 2, TargetHeight / 2 - 8)),
            "and it moves to 8 pixels above the middle at 16 pixels per unit");
        view.debugDraw = false;
        drawFrame();
        stage.RenderEditor(framework, view);
        Check(stage.CountRows(TargetWidth / 2, red) == 0, "a canvas view that turns debug lines off draws none");
        Check(stage.renderer.GetDevice()->GetValidationErrorCount() == 0, "and the debug layer stayed quiet");
        framework.Shutdown();
        stage.Close();
    }

    // **3D 선은 메시에 가려지고, 거리에 맞춰 픽셀 굵기를 지킨다.** 카메라(z = 5, 세로 화각 60°) 앞의 가로선 x = -1..1, z = 0 은
    // 11 픽셀/유닛이다. 그 앞(z = 2)의 파란 상자가 가운데를 가리고, 상자 밖(x = 0.8)에서는 4 픽셀 선이 네 행쯤을 칠한다.
    template <typename TModule>
    void TestThreeDimensionalLinesAreHiddenByMeshes()
    {
        Stage<TModule> stage;
        if (false == stage.Open())
        {
            std::cout << "  [skip] no device for this API; 3D debug lines not verified" << std::endl;
            return;
        }
        JBro::Framework3D framework;
        JBro::FrameworkContext context;
        JBro::Testing::AttachClock(context);
        JBro::System::DebugDrawSystem store;
        store.Initialize(256, &JBro::Testing::SharedClock());
        store.SetGameViewVisible(true);
        const BoundStore bound(store);
        context.renderer = &stage.renderer;
        context.debugDraw = &store;
        Check(framework.Initialize(context), "the 3D framework must initialize with the renderer");
        JBro::Canvas& canvas = *framework.GetCanvas();
        JBro::GameObject* eye = canvas.CreateObject("eye");
        canvas.AttachComponent<JBro::Component::Transform3D>(eye)->position = {0.0f, 0.0f, 5.0f};
        auto* camera = canvas.AttachComponent<JBro::Component::Camera3D>(eye);
        camera->primary = true;
        camera->clearColor = {0.0f, 0.0f, 0.0f, 1.0f};
        JBro::GameObject* box = canvas.CreateObject("box");
        auto* boxTransform = canvas.AttachComponent<JBro::Component::Transform3D>(box);
        boxTransform->position = {0.0f, 0.0f, 2.0f};
        boxTransform->scale = {0.6f, 0.6f, 0.6f};
        auto* mesh = canvas.AttachComponent<JBro::Component::MeshRenderer3D>(box);
        mesh->meshId = JBro::MeshLibrary::BuiltinCubeId();
        mesh->tint = {0.0f, 0.0f, 1.0f, 1.0f};

        const JBro::Service::DebugDraw3DService debug;
        JBro::Testing::SharedClock().BeginFrame(Sixtieth);
        store.BeginFrame();
        debug.Line({-1.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, JBro::Color{0.0f, 1.0f, 0.0f, 1.0f}, 0.0f, 4.0f);
        framework.Update();
        stage.RenderGame(framework);
        const Pixel center = ReadPixel(stage.image, stage.readback.rowPitch, TargetWidth / 2, TargetHeight / 2);
        Check(center.b > 0.2f && center.g < 0.2f, "the box in front must hide the line behind it");
        const std::uint32_t besideColumn = TargetWidth / 2 + 9;
        const auto green = [](const Pixel& pixel) { return IsGreen(pixel); };
        const std::uint32_t rows = stage.CountRows(besideColumn, green);
        std::cout << "  a 4 px 3D line covers " << rows << " rows beside the box" << std::endl;
        Check(rows >= 3 && rows <= 5, "beside the box the 4 px line must cover about four rows");
        Check(stage.renderer.GetDevice()->GetValidationErrorCount() == 0, "and the debug layer stayed quiet");
        framework.Shutdown();
        stage.Close();
    }
}

int RunDebugDrawTests()
{
    try
    {
        TestOneFrameLinesLiveOneFrame();
        TestTimedLinesAgeInGameTime();
        TestFixedStepLinesLastUntilTheNextStep();
        TestAPausedFrameKeepsTheLastPicture();
        TestCapacityAndBadValues();
        TestTheFramePathDoesNotAllocate();
        TestTheTwoDimensionalShapes();
        TestTheThreeDimensionalShapes();
        TestUnboundServicesDoNothing();
        TestTwoDimensionalLinesKeepTheirPixelWidth<JBro::D3D12RHIModule>();
        TestTwoDimensionalLinesKeepTheirPixelWidth<JBro::D3D11RHIModule>();
        TestTwoDimensionalLinesKeepTheirPixelWidth<JBro::VulkanRHIModule>();
        TestThreeDimensionalLinesAreHiddenByMeshes<JBro::D3D12RHIModule>();
        TestThreeDimensionalLinesAreHiddenByMeshes<JBro::D3D11RHIModule>();
        TestThreeDimensionalLinesAreHiddenByMeshes<JBro::VulkanRHIModule>();
    }
    catch (const std::exception&)
    {
        return 1;
    }
    std::cout << "Debug draw tests passed." << std::endl;
    return 0;
}
