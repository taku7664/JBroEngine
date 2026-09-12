#include <JBro/D3D12RHI/D3D12RHI.h>
#include <JBro/Graphics/Renderer.h>
#include <JBro/Framework2DSystem/Framework2D.h>
#include <JBro/Platform/WindowsPlatform.h>
#include <JBro/Host/EngineInstance.h>
#include <JBro/Framework2D/ServiceContext.h>
#include <JBro/Runtime/SystemContext.h>

#include <Windows.h>

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

    void TestD3D12EngineHost()
    {
        JBro::JMemoryContext memory;
        JBro::WindowsPlatform platform;
        JBro::D3D12RHIModule rhi;
        Check(platform.Initialize(memory) && rhi.Initialize(memory), "host smoke modules must initialize");
        JBro::Framework2D framework;
        JBro::EngineInstance engine;
        JBro::EngineConfig config;
        constexpr char title[] = "JBro EngineInstance hidden lifecycle test";
        config.window.title = {title, sizeof(title) - 1};
        config.window.width = 96;
        config.window.height = 64;
        config.window.visible = false;
        Check(engine.Initialize(config, platform, rhi), "real host must compose process resources");
        Check(engine.OpenProject(framework), "real host must open its project separately");
        const auto nativeWindow = FindWindowW(L"JBroEngineWindow", L"JBro EngineInstance hidden lifecycle test");
        Check(nativeWindow != nullptr, "real host must own its hidden native window");
        auto* canvas = framework.GetCanvas();
        auto* camera = canvas->CreateObject();
        canvas->AttachComponent<JBro::Component::Transform2D>(camera);
        canvas->AttachComponent<JBro::Component::WorldTransform2D>(camera);
        canvas->AttachComponent<JBro::Component::Camera2D>(camera)->primary = true;
        auto* sprite = canvas->CreateObject();
        canvas->AttachComponent<JBro::Component::Transform2D>(sprite);
        canvas->AttachComponent<JBro::Component::WorldTransform2D>(sprite);
        canvas->AttachComponent<JBro::Component::SpriteRenderer2D>(sprite);
        canvas->AttachComponent<JBro::Component::Collider2D>(sprite);
        const auto& physics = JBro::GetFramework2DServices().Physics2D;
        JBro::Collision2D hit;
        Check(physics.Raycast({-2.0f, 0.0f}, {1.0f, 0.0f}, 4.0f, hit)
            && hit.other.GetInstanceId() == sprite->GetInstanceId(),
            "opening a real project must bind its physics service without manual wiring");
        {
            JBro::Framework2D preview;
            Check(preview.Initialize({}), "an independent preview must initialize");
            Check(physics.Raycast({-2.0f, 0.0f}, {1.0f, 0.0f}, 4.0f, hit)
                && hit.other.GetInstanceId() == sprite->GetInstanceId(),
                "preview initialization must not replace the active project's binding");
        }
        Check(physics.Raycast({-2.0f, 0.0f}, {1.0f, 0.0f}, 4.0f, hit),
            "preview destruction must not unbind the active project");
        for (int frame = 0; frame < 6; ++frame)
        {
            Check(engine.Tick(1.0f / 60.0f), "real host must present across all in-flight slots");
        }
        Check(engine.GetRenderer()->GetLastFrameStats().spriteCount == 1, "real host must submit its framework sprite");
        RECT bounds = {0, 0, 160, 120};
        Check(AdjustWindowRectEx(&bounds, WS_OVERLAPPEDWINDOW, FALSE, 0) != FALSE, "host test must adjust client dimensions");
        Check(SetWindowPos(nativeWindow, nullptr, 0, 0, bounds.right - bounds.left, bounds.bottom - bounds.top,
            SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE) != FALSE, "real host surface must resize");
        Check(engine.Tick(1.0f / 60.0f), "real host must resize before presenting");
        const auto extent = engine.GetRenderer()->GetSurfaceExtent();
        Check(extent.width == 160 && extent.height == 120, "host must forward actual client pixels to D3D12");
        auto* preservedRenderer = engine.GetRenderer();
        const auto oldSprite = sprite->SafeFromThis();
        engine.CloseProject();
        Check(JBro::GetSystemContext().Physics2D == nullptr
            && false == physics.Raycast({-2.0f, 0.0f}, {1.0f, 0.0f}, 4.0f, hit)
            && hit.other.GetInstanceId() == JBro::InvalidInstanceId,
            "project close must disconnect physics before its system is destroyed");
        Check(false == oldSprite.IsValid() && framework.GetCanvas() == nullptr,
            "project close must invalidate its objects and destroy its canvas");
        Check(engine.IsRunning() && engine.GetRenderer() == preservedRenderer && IsWindow(nativeWindow) != FALSE,
            "real project close must preserve renderer and native window");
        Check(engine.Tick(1.0f / 60.0f) && engine.GetLastFrameStatus() == JBro::FrameStatus::Skipped,
            "projectless real host must not submit a backbuffer");
        Check(engine.OpenProject(framework), "real host must reopen a framework without recreating process resources");
        auto* nextCanvas = framework.GetCanvas();
        auto* nextCamera = nextCanvas->CreateObject();
        nextCanvas->AttachComponent<JBro::Component::Transform2D>(nextCamera);
        nextCanvas->AttachComponent<JBro::Component::WorldTransform2D>(nextCamera);
        nextCanvas->AttachComponent<JBro::Component::Camera2D>(nextCamera)->primary = true;
        auto* nextSprite = nextCanvas->CreateObject();
        nextCanvas->AttachComponent<JBro::Component::Transform2D>(nextSprite);
        nextCanvas->AttachComponent<JBro::Component::WorldTransform2D>(nextSprite);
        nextCanvas->AttachComponent<JBro::Component::SpriteRenderer2D>(nextSprite);
        nextCanvas->AttachComponent<JBro::Component::Collider2D>(nextSprite);
        Check(physics.Raycast({-2.0f, 0.0f}, {1.0f, 0.0f}, 4.0f, hit)
            && hit.other.GetInstanceId() == nextSprite->GetInstanceId(),
            "retained service access must follow a reopened project");
        for (int frame = 0; frame < 6; ++frame)
        {
            Check(engine.Tick(1.0f / 60.0f), "reopened real project must present across in-flight slots");
        }
        Check(engine.GetRenderer() == preservedRenderer && engine.GetRenderer()->GetLastFrameStats().spriteCount == 1,
            "reopened project must submit through the existing renderer");
        SendMessageW(nativeWindow, WM_CLOSE, 0, 0);
        Check(IsWindow(nativeWindow) != FALSE, "close must retain the real swapchain surface until host cleanup");
        Check(false == engine.Tick(1.0f / 60.0f), "close must terminate the real host");
        Check(IsWindow(nativeWindow) == FALSE && framework.GetCanvas() == nullptr && engine.GetRenderer() == nullptr,
            "real host must release canvas, renderer and native window");
        Check(JBro::GetSystemContext().Physics2D == nullptr,
            "process exit must leave no dangling physics binding");
        rhi.Shutdown();
        platform.Shutdown();
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
            JBro::SpriteSubmit sprite;
            sprite.tint[0] = 1.0f;
            sprite.tint[1] = 0.5f;
            sprite.tint[2] = 0.25f;
            Check(renderer.SubmitSprite(sprite), "D3D12 renderer must collect one sprite instance");
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
        JBro::SpriteSubmit resizedSprite;
        Check(renderer.SubmitSprite(resizedSprite), "resized D3D12 renderer must collect a sprite");
        Check(renderer.EndView(), "resized D3D12 renderer must end a view");
        Check(renderer.EndFrame() == JBro::FrameStatus::Ready,
            "resized D3D12 renderer must clear and present");
        Check(false == renderer.IsDeviceLost(), "abort and resize recovery must keep the device ready");

        {
            JBro::Framework2D framework;
            JBro::FrameworkContext context;
            context.renderer = &renderer;
            Check(framework.Initialize(context), "D3D12 framework must initialize");
            auto* canvas = framework.GetCanvas();
            auto* cameraObject = canvas->CreateObject("camera");
            canvas->AttachComponent<JBro::Component::Transform2D>(cameraObject);
            canvas->AttachComponent<JBro::Component::WorldTransform2D>(cameraObject);
            auto* camera = canvas->AttachComponent<JBro::Component::Camera2D>(cameraObject);
            camera->primary = true;
            auto* spriteObject = canvas->CreateObject("sprite");
            canvas->AttachComponent<JBro::Component::Transform2D>(spriteObject);
            canvas->AttachComponent<JBro::Component::WorldTransform2D>(spriteObject);
            auto* sprite = canvas->AttachComponent<JBro::Component::SpriteRenderer2D>(spriteObject);
            sprite->size = {5.0f, 5.0f};
            for (int frame = 0; frame < 6; ++frame)
            {
                framework.Update(1.0f / 60.0f);
                Check(renderer.BeginFrame() == JBro::FrameStatus::Ready, "D3D12 framework frame must begin");
                Check(framework.Render(), "D3D12 framework must submit its extracted sprite");
                Check(renderer.EndFrame() == JBro::FrameStatus::Ready, "D3D12 framework frame must present");
                Check(renderer.GetLastFrameStats().spriteCount == 1, "D3D12 frame must contain the extracted sprite");
            }
        }

        renderer.Shutdown();
        rhi.Shutdown();
        platform.ClosePlatformWindow(window);
        platform.PumpEvents();
        platform.Shutdown();
    }

    void TestD3D12ResourceHandleLifecycle()
    {
        JBro::JMemoryContext memory;
        JBro::D3D12RHIModule rhi;
        Check(rhi.Initialize(memory), "D3D12 resource test module must initialize");

        JBro::RHIDeviceCreateInfo createInfo;
        JBro::IRHIDevice* device = rhi.CreateDevice(createInfo);
        Check(device != nullptr, "D3D12 resource test device must initialize");

        JBro::BufferDesc invalidBufferDesc;
        Check(false == device->CreateBuffer(invalidBufferDesc).IsValid(),
            "zero-sized D3D12 buffer must be rejected");

        JBro::BufferDesc bufferDesc;
        bufferDesc.size = 64;
        bufferDesc.usage = JBro::BufferUsage::Vertex | JBro::BufferUsage::CopyDestination;
        const JBro::BufferHandle firstBuffer = device->CreateBuffer(bufferDesc);
        Check(firstBuffer.IsValid(), "D3D12 vertex buffer must be created");
        device->DestroyBuffer(firstBuffer);
        device->DestroyBuffer(firstBuffer);
        const JBro::BufferHandle secondBuffer = device->CreateBuffer(bufferDesc);
        Check(secondBuffer.IsValid(), "destroyed D3D12 buffer slot must be reusable");
        Check(firstBuffer.index == secondBuffer.index,
            "completed D3D12 buffer slot must return to the fixed pool");
        Check(firstBuffer.generation != secondBuffer.generation,
            "reused D3D12 buffer slot must reject stale generations");

        JBro::BufferDesc invalidUploadDesc = bufferDesc;
        invalidUploadDesc.memory = JBro::MemoryType::Upload;
        Check(false == device->CreateBuffer(invalidUploadDesc).IsValid(),
            "upload buffers must not accept copy-destination usage");

        JBro::TextureDesc invalidTextureDesc;
        Check(false == device->CreateTexture(invalidTextureDesc).IsValid(),
            "zero-sized D3D12 texture must be rejected");

        JBro::TextureDesc textureDesc;
        textureDesc.extent = {16, 16};
        textureDesc.format = JBro::TextureFormat::RGBA8Unorm;
        textureDesc.usage = JBro::TextureUsage::Sampled | JBro::TextureUsage::RenderTarget;
        const JBro::TextureHandle firstTexture = device->CreateTexture(textureDesc);
        Check(firstTexture.IsValid(), "D3D12 render-target texture must be created");
        device->DestroyTexture(firstTexture);
        device->DestroyTexture(firstTexture);
        const JBro::TextureHandle secondTexture = device->CreateTexture(textureDesc);
        Check(secondTexture.IsValid(), "destroyed D3D12 texture slot must be reusable");
        Check(firstTexture.index == secondTexture.index,
            "completed D3D12 texture slot must return to the fixed pool");
        Check(firstTexture.generation != secondTexture.generation,
            "reused D3D12 texture slot must reject stale generations");

        JBro::TextureDesc invalidDepthDesc = textureDesc;
        invalidDepthDesc.usage = JBro::TextureUsage::DepthStencil;
        Check(false == device->CreateTexture(invalidDepthDesc).IsValid(),
            "depth-stencil usage must require a depth format");

        device->DestroyTexture(secondTexture);
        device->DestroyBuffer(secondBuffer);
        rhi.DestroyDevice(device);
        rhi.Shutdown();
    }
}

int RunD3D12SmokeTests()
{
    TestD3D12ResourceHandleLifecycle();
    TestD3D12HiddenSurfaceClear();
    TestD3D12EngineHost();
    std::cout << "D3D12 smoke tests passed.\n";
    return 0;
}
