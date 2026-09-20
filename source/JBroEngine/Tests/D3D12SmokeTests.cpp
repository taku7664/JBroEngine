#include <JBro/D3D12RHI/D3D12RHI.h>
#include <JBro/Graphics/Renderer.h>
#include <JBro/Framework2DSystem/Framework2D.h>
#include <JBro/Platform/WindowsPlatform.h>
#include <JBro/Host/EngineInstance.h>
#include <JBro/Framework2D/ServiceContext.h>
#include <JBro/Framework2D/Internal/SystemContext.h>

#include <Windows.h>

#include <filesystem>
#include <fstream>

#include <JBro/Asset/Asset.h>
#include <JBro/Asset/AssetRegistry.h>

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

    // 2x2 RGBA PNG. AssetSystemTests 와 같은 바이트다.
    constexpr unsigned char TinyPng[] =
    {
        0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52,
        0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x02, 0x08, 0x06, 0x00, 0x00, 0x00, 0x72, 0xb6, 0x0d,
        0x24, 0x00, 0x00, 0x00, 0x13, 0x49, 0x44, 0x41, 0x54, 0x78, 0xda, 0x63, 0xf8, 0xcf, 0xc0, 0xf0,
        0x1f, 0x0c, 0x81, 0x34, 0x08, 0x34, 0x00, 0x00, 0x49, 0x49, 0x09, 0x78, 0x9c, 0x51, 0x17, 0x92,
        0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82
    };

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
        // 에디터가 켜는 둘이다. 메타 없는 그림이 등록되고, 폴더 변경이 감시로 온다.
        config.createMissingAssetMeta = true;
        config.watchAssetDirectory = true;
        Check(engine.Initialize(config, platform, rhi), "real host must compose process resources");
        Check(engine.OpenProject(framework), "real host must open its project separately");
        const auto nativeWindow = FindWindowW(L"JBroEngineWindow", L"JBro EngineInstance hidden lifecycle test");
        Check(nativeWindow != nullptr, "real host must own its hidden native window");
        auto* canvas = framework.GetCanvas();
        auto* camera = canvas->CreateObject();
        canvas->AttachComponent<JBro::Component::Transform2D>(camera);        canvas->AttachComponent<JBro::Component::Camera2D>(camera)->primary = true;
        auto* sprite = canvas->CreateObject();
        canvas->AttachComponent<JBro::Component::Transform2D>(sprite);        canvas->AttachComponent<JBro::Component::SpriteRenderer2D>(sprite);
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
        Check(JBro::GetFramework2DSystems().Physics2D == nullptr
            && false == physics.Raycast({-2.0f, 0.0f}, {1.0f, 0.0f}, 4.0f, hit)
            && hit.other.GetInstanceId() == JBro::InvalidInstanceId,
            "project close must disconnect physics before its system is destroyed");
        Check(false == oldSprite.IsValid() && framework.GetCanvas() == nullptr,
            "project close must invalidate its objects and destroy its canvas");
        Check(engine.IsRunning() && engine.GetRenderer() == preservedRenderer && IsWindow(nativeWindow) != FALSE,
            "real project close must preserve renderer and native window");
        Check(engine.Tick(1.0f / 60.0f) && engine.GetLastFrameStatus() == JBro::FrameStatus::Skipped,
            "projectless real host must not submit a backbuffer");
        // 프로젝트 파일의 `TextureFilter` 는 에셋 시스템의 기본 샘플러가 된다(D-117). 에셋 폴더의 변경은 감시가 보고
        // `PollAssetChanges` 가 적용한다(D-121): 원본을 고치면 로드된 것이 in-place 재로드되고, 새 파일은 다시 스캔되며,
        // 지우면 레코드가 빠지되 로드된 자료는 남는다.
        {
            namespace fs = std::filesystem;
            const fs::path root = fs::temp_directory_path() / L"JBroSmokeAssets";
            std::error_code ignored;
            fs::remove_all(root, ignored);
            fs::create_directories(root / "Assets", ignored);
            const auto writePng = [&](const char* name) {
                std::ofstream png(root / "Assets" / name, std::ios::binary);
                png.write(reinterpret_cast<const char*>(TinyPng), sizeof(TinyPng));
            };
            writePng("hero.png");
            const fs::path projectPath = root / "Smoke.jproject";
            {
                std::ofstream file(projectPath, std::ios::binary);
                file << "Version: 1\nEngineVersion: 0.1.0\nFramework: 2D\nRootPath: .\nTextureFilter: Linear\n"
                        "AssetDirectory: Assets\nScriptOutputLibraryPath: \"\"\nBuild:\n  ProductName: Smoke\n";
            }
            JBro::ProjectFileError projectError;
            Check(engine.OpenProjectFile(framework, projectPath.string().c_str(), projectError),
                "the host must open a project file that names a texture filter");
            JBro::AssetSystem* assets = engine.GetAssetSystem();
            Check(assets != nullptr && assets->GetDefaultTextureFilter() == JBro::TextureFilter::Linear,
                "the project's texture filter must reach the asset system before anything loads");

            const JBro::AssetRecord* hero = engine.GetAssetRegistry().FindByPath("hero.png");
            Check(hero != nullptr, "the scan registered hero.png");
            const JBro::AssetId heroId = hero->id;
            const JBro::AssetHandle texture = assets->Load(heroId);
            Check(texture.generation != 0 && assets->GetTexture(texture)->pixelGeneration == 1, "the texture loads");

            // 원본을 다시 쓰면 재로드된다. OS 알림은 비동기라 잠깐 기다린다.
            const auto pollUntil = [&](auto&& condition) {
                for (int attempt = 0; attempt < 300; ++attempt)
                {
                    engine.PollAssetChanges();
                    if (condition())
                    {
                        return true;
                    }
                    Sleep(10);
                }
                return false;
            };
            writePng("hero.png");
            Check(pollUntil([&]() { return assets->GetTexture(texture)->pixelGeneration >= 2; }),
                "rewriting the source reloads the loaded texture in place");
            Check(assets->Find(heroId).generation == texture.generation, "under the same handle");

            writePng("extra.png");
            Check(pollUntil([&]() { return engine.GetAssetRegistry().FindByPath("extra.png") != nullptr; }),
                "a new file is registered by a rescan");
            Check(engine.GetAssetRegistry().Find(heroId) != nullptr, "and the rescan keeps hero's id");
            Check(engine.IsWatchingAssets(), "the watcher is alive");

            // 이름을 바꾸면 메타가 따라가 아이디가 산다(D-121). 옛 메타는 고아로 남되 등록되지 않는다.
            const JBro::AssetId extraId = engine.GetAssetRegistry().FindByPath("extra.png")->id;
            fs::rename(root / "Assets" / "extra.png", root / "Assets" / "renamed.png", ignored);
            Check(pollUntil([&]() {
                    const JBro::AssetRecord* renamed = engine.GetAssetRegistry().FindByPath("renamed.png");
                    return renamed != nullptr && renamed->id == extraId;
                }), "a renamed file keeps its id");
            Check(fs::exists(root / "Assets" / "renamed.png.jmeta", ignored), "and its meta moved with it");
            Check(engine.GetAssetRegistry().FindByPath("extra.png") == nullptr, "and the old path is gone");

            fs::remove(root / "Assets" / "hero.png", ignored);
            Check(pollUntil([&]() { return engine.GetAssetRegistry().FindByPath("hero.png") == nullptr; }),
                "deleting the source drops its records");
            Check(assets->GetTexture(texture) != nullptr, "but the loaded data stays until nobody holds it");
            assets->Release(texture);

            engine.CloseProject();
            fs::remove_all(root, ignored);
        }
        Check(engine.OpenProject(framework), "real host must reopen a framework without recreating process resources");
        auto* nextCanvas = framework.GetCanvas();
        auto* nextCamera = nextCanvas->CreateObject();
        nextCanvas->AttachComponent<JBro::Component::Transform2D>(nextCamera);        nextCanvas->AttachComponent<JBro::Component::Camera2D>(nextCamera)->primary = true;
        auto* nextSprite = nextCanvas->CreateObject();
        nextCanvas->AttachComponent<JBro::Component::Transform2D>(nextSprite);        nextCanvas->AttachComponent<JBro::Component::SpriteRenderer2D>(nextSprite);
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
        Check(JBro::GetFramework2DSystems().Physics2D == nullptr,
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
            canvas->AttachComponent<JBro::Component::Transform2D>(cameraObject);            auto* camera = canvas->AttachComponent<JBro::Component::Camera2D>(cameraObject);
            camera->primary = true;
            auto* spriteObject = canvas->CreateObject("sprite");
            canvas->AttachComponent<JBro::Component::Transform2D>(spriteObject);            auto* sprite = canvas->AttachComponent<JBro::Component::SpriteRenderer2D>(spriteObject);
            sprite->size = {5.0f, 5.0f};
            for (int frame = 0; frame < 6; ++frame)
            {
                framework.Update(1.0f / 60.0f);
                Check(renderer.BeginFrame() == JBro::FrameStatus::Ready, "D3D12 framework frame must begin");
                Check(framework.Render() == JBro::RenderResult::Submitted, "D3D12 framework must submit its extracted sprite");
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

namespace
{
    // **버린 프레임은 상태를 앞질러 두지 않는다.** 프레임 안에서 텍스처를 렌더 타깃으로 전이하는 배리어를 기록만
    // 하고 버리면 GPU 의 실제 상태는 그대로다. 추적 상태가 함께 되돌아가지 않으면 다음 프레임이 배리어를 건너뛰고,
    // 디버그 레이어가 실행 시점에 상태 불일치를 잡는다.
    void TestD3D12AnAbortedFrameRollsTrackedStatesBack()
    {
        JBro::WindowsPlatform platform;
        JBro::D3D12RHIModule rhi;
        JBro::JMemoryContext memory;
        Check(platform.Initialize(memory), "the platform must initialize");
        Check(rhi.Initialize(memory), "the D3D12 module must initialize");
        JBro::RHIDeviceCreateInfo createInfo;
        createInfo.enableValidation = true;
        JBro::IRHIDevice* device = rhi.CreateDevice(createInfo);
        if (device == nullptr)
        {
            std::cout << "  [skip] no D3D12 device; abort rollback not verified" << std::endl;
            rhi.Shutdown();
            platform.Shutdown();
            return;
        }
        JBro::WindowDesc windowDesc;
        constexpr char title[] = "JBro abort probe";
        windowDesc.title = {title, sizeof(title) - 1};
        windowDesc.width = 64;
        windowDesc.height = 64;
        windowDesc.visible = false;
        const JBro::WindowHandle window = platform.OpenPlatformWindow(windowDesc);
        JBro::SwapchainDesc swapchainDesc;
        swapchainDesc.surface = platform.CreateSurface(window);
        swapchainDesc.extent = {64, 64};
        swapchainDesc.presentMode = JBro::PresentMode::Immediate;
        const JBro::SwapchainHandle swapchain = device->CreateSwapchain(swapchainDesc);
        Check(swapchain.IsValid(), "the probe swapchain must be created");
        JBro::TextureDesc textureDesc;
        textureDesc.extent = {32, 32};
        textureDesc.format = JBro::TextureFormat::BGRA8Unorm;
        textureDesc.usage = JBro::TextureUsage::RenderTarget | JBro::TextureUsage::Sampled;
        const JBro::TextureHandle texture = device->CreateTexture(textureDesc);
        Check(texture.IsValid(), "the probe texture must be created");

        for (int round = 0; round < 2; ++round)
        {
            const JBro::BeginFrameResult begun = device->BeginFrame(swapchain);
            Check(begun.status == JBro::FrameStatus::Ready, "each frame must begin");
            JBro::ColorAttachmentDesc attachment;
            attachment.texture = texture;
            attachment.loadOperation = JBro::LoadOperation::Clear;
            JBro::RenderPassDesc pass;
            pass.colorAttachments = {&attachment, 1};
            Check(begun.frame.commands->BeginRenderPass(pass), "the pass on the texture must begin");
            begun.frame.commands->EndRenderPass();
            if (round == 0)
            {
                // 첫 프레임은 버린다. 기록된 전이(COMMON -> RENDER_TARGET -> 읽기)는 실행되지 않는다.
                device->AbortFrame(begun.frame);
            }
            else
            {
                Check(device->EndFrame(begun.frame) == JBro::FrameStatus::Ready, "the second frame must present");
            }
        }
        device->WaitIdle();
        Check(device->GetValidationErrorCount() == 0,
            "the frame after an abort must transition from the state the GPU really has");

        device->DestroyTexture(texture);
        device->DestroySwapchain(swapchain);
        rhi.DestroyDevice(device);
        rhi.Shutdown();
        platform.ClosePlatformWindow(window);
        platform.PumpEvents();
        platform.Shutdown();
    }
}

int RunD3D12SmokeTests()
{
    TestD3D12AnAbortedFrameRollsTrackedStatesBack();
    TestD3D12ResourceHandleLifecycle();
    TestD3D12HiddenSurfaceClear();
    TestD3D12EngineHost();
    std::cout << "D3D12 smoke tests passed.\n";
    return 0;
}
