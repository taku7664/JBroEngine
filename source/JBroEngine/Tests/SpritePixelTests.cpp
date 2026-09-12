#include <JBro/D3D12RHI/D3D12RHI.h>
#include <JBro/Graphics/Renderer.h>
#include <JBro/Platform/WindowsPlatform.h>
#include <JBro/Types/Array.h>

#include <cmath>
#include <cstddef>
#include <iostream>
#include <stdexcept>

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

    struct Pixel
    {
        float r = 0.0f;
        float g = 0.0f;
        float b = 0.0f;
        float a = 0.0f;
    };

    // 백버퍼는 BGRA8 이다. 0..255 를 0..1 로 돌려준다.
    Pixel ReadPixel(const JBro::Array<std::byte>& image, std::uint32_t rowPitch,
        std::uint32_t x, std::uint32_t y)
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

    bool Near(float a, float b)
    {
        return std::fabs(a - b) < 0.02f;
    }

    // §12.4 가 열어 둔 구멍을 닫는다. 인스턴스 패킷의 필드를 셰이더가 그 자리에서
    // 읽는지는 픽셀을 되읽지 않고는 증명할 수 없다. 오프셋 하나만 어긋나도
    // D3D12 는 아무 말도 하지 않고 색이 조용히 달라진다.
    void TestSpritePacketReachesTheShaderFields()
    {
        JBro::WindowsPlatform platform;
        JBro::D3D12RHIModule rhi;
        JBro::JMemoryContext memory;
        Check(platform.Initialize(memory), "platform must initialize for the pixel test");
        if (false == rhi.Initialize(memory))
        {
            std::cout << "  [skip] no D3D12 device; sprite pixels not verified" << std::endl;
            platform.Shutdown();
            return;
        }

        JBro::WindowDesc windowDesc;
        constexpr char title[] = "JBro pixel probe";
        windowDesc.title = {title, sizeof(title) - 1};
        windowDesc.width = 64;
        windowDesc.height = 64;
        windowDesc.visible = false;
        const JBro::WindowHandle window = platform.OpenPlatformWindow(windowDesc);
        Check(window.value != 0, "the probe window must open");

        JBro::Renderer renderer;
        JBro::RendererConfig config;
        config.surface = platform.CreateSurface(window);
        config.surfaceExtent = {64, 64};
        config.maxSpriteSubmissions = 4;
        config.presentMode = JBro::PresentMode::Immediate;
        Check(renderer.Initialize(rhi, config), "the pixel renderer must initialize");

        // 화면을 정확히 채우는 직교 카메라. 반높이 1, 종횡비 1 이므로 [-1,1] 이 화면 전체다.
        JBro::CameraParams camera;
        camera.projection = {{1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f}};
        camera.clearColor[0] = 0.0f;
        camera.clearColor[1] = 0.0f;
        camera.clearColor[2] = 0.0f;
        camera.clearColor[3] = 1.0f;
        camera.viewport.width = 64.0f;
        camera.viewport.height = 64.0f;

        // 왼쪽 절반을 덮는 스프라이트. 기본 기하는 중심 단위 사각형([-0.5,0.5])이므로
        // x 를 1 배, y 를 2 배 늘리고 x 로 -0.5 옮기면 왼쪽 절반이 정확히 찬다.
        JBro::SpriteSubmit sprite;
        sprite.world.linear[0] = 1.0f;
        sprite.world.linear[1] = 0.0f;
        sprite.world.linear[2] = 0.0f;
        sprite.world.linear[3] = 2.0f;
        sprite.world.translation[0] = -0.5f;
        sprite.world.translation[1] = 0.0f;
        sprite.world.depth = 0.0f;
        // 채널마다 다른 값이다. 틴트가 통째로 밀리거나 깊이와 겹치면 바로 드러난다.
        sprite.tint[0] = 1.0f;
        sprite.tint[1] = 0.5f;
        sprite.tint[2] = 0.25f;
        sprite.tint[3] = 1.0f;

        Check(renderer.BeginFrame() == JBro::FrameStatus::Ready, "the pixel frame must begin");
        Check(renderer.BeginView(camera), "the pixel view must open");
        Check(renderer.SubmitSprite(sprite), "the probe sprite must submit");
        Check(renderer.EndView(), "the pixel view must close");
        Check(renderer.EndFrame() == JBro::FrameStatus::Ready, "the pixel frame must present");

        JBro::Array<std::byte> image;
        image.Resize(64 * 64 * 4);
        JBro::TextureReadback readback;
        const bool read = renderer.ReadBackBuffer(image.Data(), image.Size(), readback);
        Check(read, "the renderer must read its own back buffer");
        Check(readback.extent.width == 64 && readback.extent.height == 64,
            "the readback must describe the surface it read");
        Check(readback.rowPitch == 64 * 4 && readback.writtenBytes == 64 * 64 * 4,
            "the readback must be packed tightly");

        // 왼쪽 절반은 스프라이트의 틴트, 오른쪽 절반은 지운 색이어야 한다.
        const Pixel left = ReadPixel(image, readback.rowPitch, 16, 32);
        const Pixel right = ReadPixel(image, readback.rowPitch, 48, 32);
        Check(Near(left.r, 1.0f) && Near(left.g, 0.5f) && Near(left.b, 0.25f),
            "the sprite must paint the tint the packet carried, channel for channel");
        Check(Near(right.r, 0.0f) && Near(right.g, 0.0f) && Near(right.b, 0.0f),
            "the half the sprite does not cover must stay the clear colour");

        renderer.Shutdown();
        rhi.Shutdown();
        platform.ClosePlatformWindow(window);
        platform.PumpEvents();
        platform.Shutdown();
    }
}

int RunSpritePixelTests()
{
    TestSpritePacketReachesTheShaderFields();
    std::cout << "Sprite pixel tests passed.\n";
    return 0;
}
