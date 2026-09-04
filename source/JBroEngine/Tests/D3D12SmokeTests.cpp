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
    std::cout << "D3D12 smoke tests passed.\n";
    return 0;
}
