#include <JBro/D3D11RHI/D3D11RHI.h>
#include <JBro/Graphics/Renderer.h>
#include <JBro/Platform/WindowsPlatform.h>
#include <JBro/Types/Array.h>

#include <cmath>
#include <iostream>
#include <stdexcept>

// D3D11 백엔드의 디바이스·스왑체인·프레임·되읽기·크기 바꾸기를 잰다(D-107). 스프라이트와 메시 픽셀
// 테스트가 두 백엔드에서 같이 돌므로 여기는 그쪽이 안 밟는 것만 본다.
namespace
{
    void Check(bool condition, const char* message)
    {
        if (false == condition)
        {
            std::cout << "test failure: " << message << '\n';
            throw std::runtime_error(message);
        }
    }

    unsigned char Channel(const JBro::Array<std::byte>& image, std::uint32_t rowPitch,
        std::uint32_t x, std::uint32_t y, std::uint32_t channel)
    {
        const std::size_t offset = static_cast<std::size_t>(y) * rowPitch + static_cast<std::size_t>(x) * 4;
        return static_cast<unsigned char>(image[offset + channel]);
    }

    void TestD3D11ClearsPresentsResizesAndReadsBack()
    {
        JBro::WindowsPlatform platform;
        JBro::D3D11RHIModule rhi;
        JBro::JMemoryContext memory;
        Check(platform.Initialize(memory), "the platform must initialize");
        if (false == rhi.Initialize(memory))
        {
            std::cout << "  [skip] no D3D11 module" << std::endl;
            platform.Shutdown();
            return;
        }
        Check(rhi.GetApi() == JBro::GraphicsApi::D3D11, "the module must say which API it speaks");
        JBro::WindowDesc windowDesc;
        constexpr char title[] = "JBro D3D11 probe";
        windowDesc.title = {title, sizeof(title) - 1};
        windowDesc.width = 64;
        windowDesc.height = 48;
        windowDesc.visible = false;
        const JBro::WindowHandle window = platform.OpenPlatformWindow(windowDesc);
        Check(window.value != 0, "the probe window must open");

        JBro::Renderer renderer;
        JBro::RendererConfig config;
        config.api = JBro::GraphicsApi::D3D11;
        config.surface = platform.CreateSurface(window);
        config.surfaceExtent = {64, 48};
        config.presentMode = JBro::PresentMode::Immediate;
        config.validation = true;
        if (false == renderer.Initialize(rhi, config))
        {
            std::cout << "  [skip] no D3D11 device here" << std::endl;
            rhi.Shutdown();
            platform.ClosePlatformWindow(window);
            platform.Shutdown();
            return;
        }
        JBro::IRHIDevice* device = renderer.GetDevice();
        Check(device != nullptr && device->GetFramesInFlight() == 1,
            "D3D11 keeps one frame in flight - the immediate context has no slots to rotate");

        // 클리어 색만 있는 뷰. 백버퍼가 그 색으로 읽혀야 한다.
        JBro::CameraParams camera;
        camera.clearColor[0] = 1.0f;
        camera.clearColor[1] = 0.5f;
        camera.clearColor[2] = 0.0f;
        camera.clearColor[3] = 1.0f;
        camera.viewport.width = 64.0f;
        camera.viewport.height = 48.0f;
        Check(renderer.BeginFrame() == JBro::FrameStatus::Ready, "the frame must begin");
        Check(renderer.BeginView(camera) && renderer.EndView(), "an empty view must open and close");
        Check(renderer.EndFrame() == JBro::FrameStatus::Ready, "the frame must present");
        JBro::Array<std::byte> image;
        image.Resize(64 * 48 * 4);
        JBro::TextureReadback readback;
        Check(renderer.ReadBackBuffer(image.Data(), image.Size(), readback), "the back buffer must read back");
        Check(readback.extent.width == 64 && readback.extent.height == 48 && readback.rowPitch == 64 * 4,
            "and describe the surface with a tight row pitch, like D3D12 does");
        // BGRA: 파랑 0, 초록 128 근처, 빨강 255.
        Check(Channel(image, readback.rowPitch, 32, 24, 2) == 255 && Channel(image, readback.rowPitch, 32, 24, 0) == 0
                && std::abs(static_cast<int>(Channel(image, readback.rowPitch, 32, 24, 1)) - 128) <= 1,
            "the back buffer must hold the camera's clear colour");

        // 크기를 바꾼 뒤에도 다음 프레임이 새 크기로 돈다.
        Check(renderer.ResizeSurface({96, 32}), "the surface must resize between frames");
        camera.viewport.width = 96.0f;
        camera.viewport.height = 32.0f;
        camera.clearColor[0] = 0.0f;
        camera.clearColor[2] = 1.0f;
        Check(renderer.BeginFrame() == JBro::FrameStatus::Ready, "the frame after the resize must begin");
        Check(renderer.BeginView(camera) && renderer.EndView(), "its view must open and close");
        Check(renderer.EndFrame() == JBro::FrameStatus::Ready, "and present");
        image.Resize(96 * 32 * 4);
        Check(renderer.ReadBackBuffer(image.Data(), image.Size(), readback), "the resized back buffer must read back");
        Check(readback.extent.width == 96 && readback.extent.height == 32, "at its new size");
        Check(Channel(image, readback.rowPitch, 90, 30, 0) == 255 && Channel(image, readback.rowPitch, 90, 30, 2) == 0,
            "painted with the new clear colour out to the new corner");

        // 프레임 안에서는 자원을 만들지 않고 되읽지도 않는다 - D3D12 와 같은 계약이다.
        Check(renderer.BeginFrame() == JBro::FrameStatus::Ready, "a frame must begin for the in-frame checks");
        JBro::TextureDesc textureDesc;
        textureDesc.extent = {8, 8};
        textureDesc.format = JBro::TextureFormat::BGRA8Unorm;
        textureDesc.usage = JBro::TextureUsage::RenderTarget;
        Check(false == device->CreateTexture(textureDesc).IsValid(), "no texture is created inside a frame");
        Check(false == renderer.ReadBackBuffer(image.Data(), image.Size(), readback), "and nothing reads back inside a frame");
        renderer.AbortFrame();
        const JBro::TextureHandle texture = device->CreateTexture(textureDesc);
        Check(texture.IsValid(), "outside the frame the same texture is created");
        device->DestroyTexture(texture);
        Check(false == device->CreateTexture({}).IsValid(), "an empty description is refused");

        Check(device->GetValidationErrorCount() == 0, "and the D3D11 debug layer must have stayed quiet");
        renderer.Shutdown();
        rhi.Shutdown();
        platform.ClosePlatformWindow(window);
        platform.PumpEvents();
        platform.Shutdown();
    }
}

int RunD3D11SmokeTests()
{
    TestD3D11ClearsPresentsResizesAndReadsBack();
    std::cout << "D3D11 smoke tests passed.\n";
    return 0;
}
