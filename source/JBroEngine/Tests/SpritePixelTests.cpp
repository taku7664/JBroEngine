#include <JBro/D3D11RHI/D3D11RHI.h>
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
    template <typename TModule>
    void TestSpritePacketReachesTheShaderFields()
    {
        JBro::WindowsPlatform platform;
        TModule rhi;
        JBro::JMemoryContext memory;
        Check(platform.Initialize(memory), "platform must initialize for the pixel test");
        if (false == rhi.Initialize(memory))
        {
            std::cout << "  [skip] no device for this API; sprite pixels not verified" << std::endl;
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
        config.api = rhi.GetApi();
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
    // **같은 렌더러가 같은 그림을 텍스처로도 내놓아야 한다.** 에디터의 게임 뷰는
    // 그 차이 하나로 성립한다 - 렌더 경로가 갈리면 에디터에서 보는 것과 실행했을 때
    // 보는 것이 달라지고, 그 어긋남은 한참 뒤에야 드러난다(D-63).
    template <typename TModule>
    void TestTheSameSpriteGoesToATextureInstead()
    {
        JBro::WindowsPlatform platform;
        TModule rhi;
        JBro::JMemoryContext memory;
        Check(platform.Initialize(memory), "platform must initialize for the target test");
        if (false == rhi.Initialize(memory))
        {
            std::cout << "  [skip] no device for this API; the frame target not verified" << std::endl;
            platform.Shutdown();
            return;
        }

        JBro::WindowDesc windowDesc;
        constexpr char title[] = "JBro target probe";
        windowDesc.title = {title, sizeof(title) - 1};
        windowDesc.width = 64;
        windowDesc.height = 64;
        windowDesc.visible = false;
        const JBro::WindowHandle window = platform.OpenPlatformWindow(windowDesc);
        Check(window.value != 0, "the probe window must open");

        JBro::Renderer renderer;
        JBro::RendererConfig config;
        config.api = rhi.GetApi();
        config.surface = platform.CreateSurface(window);
        config.surfaceExtent = {64, 64};
        config.maxSpriteSubmissions = 4;
        config.presentMode = JBro::PresentMode::Immediate;
        Check(renderer.Initialize(rhi, config), "the target renderer must initialize");

        JBro::IRHIDevice* device = renderer.GetDevice();
        Check(device != nullptr, "the renderer must hand out the device it made");

        // **게임 뷰는 창보다 크다.** 게임 해상도가 에디터 창보다 큰 것이 보통이고,
        // 여기서 일부러 그렇게 잡는다 - 뷰포트를 재는 기준이 창에 묶여 있으면
        // 타깃 안에 멀쩡히 들어가는 뷰포트가 "화면 밖" 으로 거절당한다.
        constexpr std::uint32_t TargetWidth = 96;
        constexpr std::uint32_t TargetHeight = 48;
        static_assert(TargetWidth > 64, "the target must be wider than the window");
        JBro::TextureDesc targetDesc;
        targetDesc.extent = {TargetWidth, TargetHeight};
        targetDesc.format = JBro::TextureFormat::BGRA8Unorm;
        targetDesc.usage = JBro::TextureUsage::RenderTarget | JBro::TextureUsage::Sampled;
        const JBro::TextureHandle gameView = device->CreateTexture(targetDesc);
        Check(gameView.IsValid(), "the game view texture must be created");

        JBro::CameraParams camera;
        camera.projection = {{1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f}};
        camera.clearColor[0] = 0.0f;
        camera.clearColor[1] = 0.0f;
        camera.clearColor[2] = 0.0f;
        camera.clearColor[3] = 1.0f;
        camera.viewport.width = static_cast<float>(TargetWidth);
        camera.viewport.height = static_cast<float>(TargetHeight);

        // 위의 테스트와 같은 스프라이트다. 왼쪽 절반을 덮는다.
        JBro::SpriteSubmit sprite;
        sprite.world.linear[0] = 1.0f;
        sprite.world.linear[3] = 2.0f;
        sprite.world.translation[0] = -0.5f;
        sprite.tint[0] = 1.0f;
        sprite.tint[1] = 0.5f;
        sprite.tint[2] = 0.25f;
        sprite.tint[3] = 1.0f;

        // 크기 없이 텍스처만 주는 것은 거절한다.
        JBro::FrameTarget sizeless;
        sizeless.texture = gameView;
        Check(renderer.BeginFrame(sizeless) == JBro::FrameStatus::InvalidState,
            "a target without a size must be refused");

        JBro::FrameTarget target;
        target.texture = gameView;
        target.extent = {TargetWidth, TargetHeight};
        Check(renderer.BeginFrame(target) == JBro::FrameStatus::Ready,
            "the frame aimed at a texture must begin");
        Check(renderer.BeginView(camera), "the view must open");
        Check(renderer.SubmitSprite(sprite), "the probe sprite must submit");
        Check(renderer.EndView(), "the view must close");
        Check(renderer.EndFrame() == JBro::FrameStatus::Ready, "the frame must finish");

        JBro::Array<std::byte> image;
        image.Resize(TargetWidth * TargetHeight * 4);
        JBro::TextureReadback readback;
        Check(device->ReadTexture(gameView, image.Data(), image.Size(), readback),
            "the game view texture must read back");
        Check(readback.extent.width == TargetWidth && readback.extent.height == TargetHeight,
            "and describe the texture, not the window");

        // 백버퍼에 그렸을 때와 같은 그림이어야 한다.
        const Pixel left = ReadPixel(image, readback.rowPitch, TargetWidth / 4, TargetHeight / 2);
        const Pixel right =
            ReadPixel(image, readback.rowPitch, TargetWidth * 3 / 4, TargetHeight / 2);
        Check(Near(left.r, 1.0f) && Near(left.g, 0.5f) && Near(left.b, 0.25f),
            "the sprite must paint the same tint it paints on the back buffer");
        Check(Near(right.r, 0.0f) && Near(right.g, 0.0f) && Near(right.b, 0.0f),
            "and leave the rest of the texture at the clear colour");

        // **같은 렌더러로 다음 프레임은 백버퍼에 그린다.** 타깃이 프레임에 매인 것이지
        // 렌더러에 매인 것이 아님을 그것이 보인다 - 한 번 텍스처로 보냈다고 그 뒤로
        // 계속 텍스처로 가면, 게임을 실행했을 때 화면이 검게 남는다.
        JBro::CameraParams windowCamera = camera;
        windowCamera.viewport.width = 64.0f;
        windowCamera.viewport.height = 64.0f;
        Check(renderer.BeginFrame() == JBro::FrameStatus::Ready,
            "the next frame must begin with no target");
        Check(renderer.BeginView(windowCamera), "the window view must open");
        Check(renderer.SubmitSprite(sprite), "the probe sprite must submit again");
        Check(renderer.EndView(), "the window view must close");
        Check(renderer.EndFrame() == JBro::FrameStatus::Ready, "the window frame must present");

        JBro::Array<std::byte> windowImage;
        windowImage.Resize(64 * 64 * 4);
        JBro::TextureReadback windowReadback;
        Check(renderer.ReadBackBuffer(
                windowImage.Data(), windowImage.Size(), windowReadback),
            "the back buffer must read back");
        const Pixel windowLeft = ReadPixel(windowImage, windowReadback.rowPitch, 16, 32);
        Check(Near(windowLeft.r, 1.0f) && Near(windowLeft.g, 0.5f) && Near(windowLeft.b, 0.25f),
            "and the sprite must be on it, not still going to the texture");

        device->DestroyTexture(gameView);
        renderer.Shutdown();
        rhi.Shutdown();
        platform.ClosePlatformWindow(window);
        platform.PumpEvents();
        platform.Shutdown();
    }
    // 오버레이가 백버퍼에 남긴 표시다. 콜백이 실제로 불렸는지, 그때 프레임이 아직
    // 열려 있었는지는 픽셀로만 확인할 수 있다.
    struct OverlayProbe
    {
        int calls = 0;
        std::uint32_t lastSlot = 0xFFFFFFFFu;
        std::uint32_t firstSlot = 0xFFFFFFFFu;
        bool succeed = true;
        float mark[4] = {0.25f, 0.75f, 0.5f, 1.0f};
    };

    bool DrawOverlayProbe(
        JBro::IRHICommandContext& commands,
        JBro::TextureHandle backBuffer,
        std::uint32_t frameSlot,
        void* user)
    {
        auto* probe = static_cast<OverlayProbe*>(user);
        ++probe->calls;
        if (probe->firstSlot == 0xFFFFFFFFu)
        {
            probe->firstSlot = frameSlot;
        }
        probe->lastSlot = frameSlot;
        if (false == probe->succeed)
        {
            return false;
        }
        JBro::ColorAttachmentDesc attachment;
        attachment.texture = backBuffer;
        attachment.loadOperation = JBro::LoadOperation::Clear;
        attachment.clearColor = {probe->mark[0], probe->mark[1], probe->mark[2], probe->mark[3]};
        JBro::RenderPassDesc pass;
        pass.colorAttachments = {&attachment, 1};
        if (false == commands.BeginRenderPass(pass))
        {
            return false;
        }
        commands.EndRenderPass();
        return true;
    }

    // **에디터 프레임의 모양이다.** 게임은 텍스처로 가고 백버퍼에는 낼 것이 없다.
    // 그 프레임을 "그릴 게 없다" 고 버리면 에디터 UI 까지 같이 사라진다 - 화면이
    // 통째로 멈춘 것처럼 보이고, 원인은 게임 쪽이 아니라 여기다(D-63).
    template <typename TModule>
    void TestTheOverlayGetsTheFrameAfterTheGame()
    {
        JBro::WindowsPlatform platform;
        TModule rhi;
        JBro::JMemoryContext memory;
        Check(platform.Initialize(memory), "platform must initialize for the overlay test");
        if (false == rhi.Initialize(memory))
        {
            std::cout << "  [skip] no device for this API; the frame overlay not verified" << std::endl;
            platform.Shutdown();
            return;
        }

        JBro::WindowDesc windowDesc;
        constexpr char title[] = "JBro overlay probe";
        windowDesc.title = {title, sizeof(title) - 1};
        windowDesc.width = 64;
        windowDesc.height = 64;
        windowDesc.visible = false;
        const JBro::WindowHandle window = platform.OpenPlatformWindow(windowDesc);
        Check(window.value != 0, "the probe window must open");

        JBro::Renderer renderer;
        JBro::RendererConfig config;
        config.api = rhi.GetApi();
        config.surface = platform.CreateSurface(window);
        config.surfaceExtent = {64, 64};
        config.maxSpriteSubmissions = 4;
        config.presentMode = JBro::PresentMode::Immediate;
        Check(renderer.Initialize(rhi, config), "the overlay renderer must initialize");
        JBro::IRHIDevice* device = renderer.GetDevice();
        Check(device != nullptr, "the device must be reachable");

        JBro::TextureDesc targetDesc;
        targetDesc.extent = {32, 32};
        targetDesc.format = JBro::TextureFormat::BGRA8Unorm;
        targetDesc.usage = JBro::TextureUsage::RenderTarget | JBro::TextureUsage::Sampled;
        const JBro::TextureHandle gameView = device->CreateTexture(targetDesc);
        Check(gameView.IsValid(), "the game view texture must be created");

        OverlayProbe probe;
        Check(renderer.HasFrameOverlay() == false, "there is no overlay to begin with");
        Check(renderer.SetFrameOverlay(&DrawOverlayProbe, &probe), "the overlay must attach");
        Check(renderer.HasFrameOverlay(), "and say so");

        JBro::FrameTarget target;
        target.texture = gameView;
        target.extent = {32, 32};
        Check(renderer.BeginFrame(target) == JBro::FrameStatus::Ready, "the frame must begin");
        // 프레임이 열린 동안에는 오버레이를 바꿀 수 없다.
        Check(false == renderer.SetFrameOverlay(nullptr, nullptr),
            "changing the overlay mid frame must be refused");
        // 게임은 아무것도 제출하지 않는다. 그래도 오버레이는 불려야 한다.
        Check(renderer.EndFrame() == JBro::FrameStatus::Ready, "the frame must present");
        Check(probe.calls == 1, "the overlay must run once");
        // 슬롯을 알려 줘야 매 프레임 덮어쓰는 자원을 갈라 쓸 수 있다.
        Check(probe.lastSlot != 0xFFFFFFFFu, "and be told which slot the frame uses");

        // **한 번 더 돌려 슬롯이 바뀌는지 본다.** 늘 같은 값을 넘기면 받는 쪽은
        // 갈라 쓸 수가 없고, 한 프레임만 보아서는 그것이 드러나지 않는다.
        Check(renderer.BeginFrame(target) == JBro::FrameStatus::Ready,
            "a second frame must begin");
        Check(renderer.EndFrame() == JBro::FrameStatus::Ready, "and present");
        Check(probe.calls == 2, "the overlay must run again");
        Check(device->GetFramesInFlight() == 1 || probe.lastSlot != probe.firstSlot,
            "and be told a different slot, or nobody can split anything by it");

        JBro::Array<std::byte> image;
        image.Resize(64 * 64 * 4);
        JBro::TextureReadback readback;
        Check(renderer.ReadBackBuffer(image.Data(), image.Size(), readback),
            "the back buffer must read back");
        const Pixel painted = ReadPixel(image, readback.rowPitch, 32, 32);
        Check(Near(painted.r, probe.mark[0])
                && Near(painted.g, probe.mark[1])
                && Near(painted.b, probe.mark[2]),
            "the overlay must have reached the back buffer of the frame the game skipped");

        // 오버레이가 실패하면 프레임을 버린다. 반쯤 그려진 UI 를 내보내지 않는다.
        probe.succeed = false;
        Check(renderer.BeginFrame(target) == JBro::FrameStatus::Ready,
            "the next frame must begin");
        Check(renderer.EndFrame() == JBro::FrameStatus::InvalidState,
            "a failing overlay must throw the frame away");
        Check(probe.calls == 3, "and it must have been the overlay that was asked");

        Check(renderer.SetFrameOverlay(nullptr, nullptr), "the overlay must detach");
        Check(renderer.HasFrameOverlay() == false, "and say so");

        device->DestroyTexture(gameView);
        renderer.Shutdown();
        rhi.Shutdown();
        platform.ClosePlatformWindow(window);
        platform.PumpEvents();
        platform.Shutdown();
    }
}

int RunSpritePixelTests()
{
    TestSpritePacketReachesTheShaderFields<JBro::D3D12RHIModule>();
    TestSpritePacketReachesTheShaderFields<JBro::D3D11RHIModule>();
    TestTheSameSpriteGoesToATextureInstead<JBro::D3D12RHIModule>();
    TestTheSameSpriteGoesToATextureInstead<JBro::D3D11RHIModule>();
    TestTheOverlayGetsTheFrameAfterTheGame<JBro::D3D12RHIModule>();
    TestTheOverlayGetsTheFrameAfterTheGame<JBro::D3D11RHIModule>();
    std::cout << "Sprite pixel tests passed.\n";
    return 0;
}
