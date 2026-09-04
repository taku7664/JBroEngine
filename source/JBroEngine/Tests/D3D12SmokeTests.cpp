#include <JBro/D3D12RHI/D3D12RHI.h>
#include <JBro/Graphics/Renderer.h>
#include <JBro/Platform/WindowsPlatform.h>

#include <iostream>
#include <stdexcept>

namespace
{
    void Check(bool condition, const char* message)
    {
        if (false == condition)
        {
            throw std::runtime_error(message);
        }
    }

    void TestD3D12HiddenSurfaceClear()
    {
        JBro::JMemoryContext memory;
        JBro::WindowsPlatform platform;
        Check(platform.Initialize(memory), "D3D12 smoke test platform must initialize");

        constexpr char title[] = "JBro D3D12 smoke test";
        JBro::WindowDesc windowDesc;
        windowDesc.title = {title, sizeof(title) - 1};
        windowDesc.width = 64;
        windowDesc.height = 64;
        windowDesc.visible = false;

        const JBro::WindowHandle window = platform.OpenPlatformWindow(windowDesc);
        Check(window.value != 0, "D3D12 smoke test must create a hidden window");

        JBro::D3D12RHIModule rhi;
        Check(rhi.Initialize(memory), "D3D12 RHI module must initialize");

        JBro::Renderer renderer;
        JBro::RendererConfig rendererConfig;
        rendererConfig.surface = platform.CreateSurface(window);
        rendererConfig.surfaceExtent = {64, 64};
        rendererConfig.maxViews = 2;
        rendererConfig.maxSpriteSubmissions = 1;
        rendererConfig.maxMeshSubmissions = 1;
        rendererConfig.validation = false;
        Check(renderer.Initialize(rhi, rendererConfig), "D3D12 renderer must initialize");

        for (std::uint32_t frameIndex = 0; frameIndex < 6; ++frameIndex)
        {
            Check(renderer.BeginFrame() == JBro::FrameStatus::Ready,
                "D3D12 renderer must begin every frame slot cycle");

            JBro::CameraParams camera;
            camera.clearColor[0] = 0.125f;
            camera.clearColor[1] = 0.25f;
            camera.clearColor[2] = 0.5f;
            Check(renderer.BeginView(camera), "D3D12 renderer must begin a view");
            Check(renderer.EndView(), "D3D12 renderer must end a view");
            Check(renderer.EndFrame() == JBro::FrameStatus::Ready,
                "D3D12 renderer must clear and present every frame slot cycle");
        }
        Check(false == renderer.IsDeviceLost(), "D3D12 clear path must keep the device ready");

        Check(renderer.BeginFrame() == JBro::FrameStatus::Ready,
            "D3D12 renderer must begin an abort recovery frame");
        JBro::CameraParams validCamera;
        Check(renderer.BeginView(validCamera), "abort recovery frame must record its first view");
        Check(renderer.EndView(), "abort recovery frame must close its first view");
        JBro::CameraParams invalidCamera;
        invalidCamera.viewport.x = -1.0f;
        invalidCamera.viewport.width = 32.0f;
        invalidCamera.viewport.height = 32.0f;
        Check(renderer.BeginView(invalidCamera), "invalid viewport must remain a record-time validation case");
        Check(renderer.EndView(), "invalid viewport view must close its collection scope");
        Check(renderer.EndFrame() == JBro::FrameStatus::InvalidState,
            "invalid viewport must abort the recorded D3D12 frame");

        Check(renderer.ResizeSurface({96, 80}), "D3D12 swapchain must resize after an aborted frame");
        Check(renderer.BeginFrame() == JBro::FrameStatus::Ready,
            "D3D12 renderer must begin after swapchain resize");
        JBro::CameraParams resizedCamera;
        Check(renderer.BeginView(resizedCamera), "resized D3D12 renderer must begin a view");
        Check(renderer.EndView(), "resized D3D12 renderer must end a view");
        Check(renderer.EndFrame() == JBro::FrameStatus::Ready,
            "resized D3D12 renderer must clear and present");
        Check(false == renderer.IsDeviceLost(), "abort and resize recovery must keep the device ready");

        renderer.Shutdown();
        rhi.Shutdown();
        platform.ClosePlatformWindow(window);
        platform.PumpEvents();
        platform.Shutdown();
    }
}

int RunD3D12SmokeTests()
{
    TestD3D12HiddenSurfaceClear();
    std::cout << "D3D12 smoke tests passed.\n";
    return 0;
}
