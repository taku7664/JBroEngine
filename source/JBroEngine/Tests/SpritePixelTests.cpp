#include <JBro/D3D11RHI/D3D11RHI.h>
#include <JBro/D3D12RHI/D3D12RHI.h>
#include <JBro/VulkanRHI/VulkanRHI.h>
#include <JBro/Graphics/Renderer.h>
#include <JBro/Platform/WindowsPlatform.h>
#include <JBro/Types/Array.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <stdexcept>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/Int.h>
#include <JBro/Types/UInt.h>
#include <JBro/Types/ValueMath.h>

namespace
{
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
        JBro::Float a = 0.0f;
    };

    // 백버퍼는 BGRA8 이다. 0..255 를 0..1 로 돌려준다.
    Pixel ReadPixel(const JBro::Array<std::byte>& image, JBro::UInt32 rowPitch,
        JBro::UInt32 x, JBro::UInt32 y)
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

    JBro::Bool Near(JBro::Float a, JBro::Float b)
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

        // 오른쪽 아래 사분면을 덮는 반투명 흰 스프라이트. 알파 블렌드가 켜져 있으면 검은 바탕과
        // 반씩 섞여 회색이고, 꺼져 있으면 흰색이다 - 뮤테이션이 이 차이를 잡지 못해 더했다(D-108).
        JBro::SpriteSubmit translucent;
        translucent.world.linear[0] = 1.0f;
        translucent.world.linear[1] = 0.0f;
        translucent.world.linear[2] = 0.0f;
        translucent.world.linear[3] = 1.0f;
        translucent.world.translation[0] = 0.5f;
        translucent.world.translation[1] = -0.5f;
        translucent.world.depth = 0.0f;
        translucent.tint[0] = 1.0f;
        translucent.tint[1] = 1.0f;
        translucent.tint[2] = 1.0f;
        translucent.tint[3] = 0.5f;

        Check(renderer.BeginFrame() == JBro::FrameStatus::Ready, "the pixel frame must begin");
        Check(renderer.BeginView(camera), "the pixel view must open");
        Check(renderer.SubmitSprite(sprite), "the probe sprite must submit");
        Check(renderer.SubmitSprite(translucent), "the translucent sprite must submit");
        Check(renderer.EndView(), "the pixel view must close");
        Check(renderer.EndFrame() == JBro::FrameStatus::Ready, "the pixel frame must present");

        JBro::Array<std::byte> image;
        image.Resize(64 * 64 * 4);
        JBro::TextureReadback readback;
        const JBro::Bool read = renderer.ReadBackBuffer(image.Data(), image.Size(), readback);
        Check(read, "the renderer must read its own back buffer");
        Check(readback.extent.width == 64 && readback.extent.height == 64,
            "the readback must describe the surface it read");
        Check(readback.rowPitch == 64 * 4 && readback.writtenBytes == 64 * 64 * 4,
            "the readback must be packed tightly");

        // 왼쪽 절반은 스프라이트의 틴트, 오른쪽 위는 지운 색, 오른쪽 아래는 반투명이 섞인 회색이어야 한다.
        const Pixel left = ReadPixel(image, readback.rowPitch, 16, 32);
        const Pixel right = ReadPixel(image, readback.rowPitch, 48, 16);
        const Pixel blended = ReadPixel(image, readback.rowPitch, 48, 48);
        Check(Near(left.r, 1.0f) && Near(left.g, 0.5f) && Near(left.b, 0.25f),
            "the sprite must paint the tint the packet carried, channel for channel");
        Check(Near(right.r, 0.0f) && Near(right.g, 0.0f) && Near(right.b, 0.0f),
            "the quarter no sprite covers must stay the clear colour");
        Check(std::abs(blended.r - 0.5f) <= 0.02f && std::abs(blended.g - 0.5f) <= 0.02f && std::abs(blended.b - 0.5f) <= 0.02f,
            "a half-transparent white sprite over black must come out mid grey - alpha blending is on");

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
        constexpr JBro::UInt32 TargetWidth = 96;
        constexpr JBro::UInt32 TargetHeight = 48;
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
        JBro::Int32 calls = 0;
        JBro::UInt32 lastSlot = 0xFFFFFFFFu;
        JBro::UInt32 firstSlot = 0xFFFFFFFFu;
        JBro::Bool succeed = true;
        JBro::Float mark[4] = {0.25f, 0.75f, 0.5f, 1.0f};
    };

    JBro::Bool DrawOverlayProbe(
        JBro::IRHICommandContext& commands,
        JBro::TextureHandle backBuffer,
        JBro::UInt32 frameSlot,
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

namespace
{
    // **텍스처가 화면에 나온다(D-113).** 2x2 텍스처의 네 텍셀이 스프라이트의 네 사분면에 앉는지, UV 사각형이 시트의 한 칸만
    // 고르는지, 같은 뷰 안에서 텍스처 있는 스프라이트와 없는 스프라이트가 묶음을 갈라 제 색으로 나오는지, 죽은 핸들이 흰색으로
    // 그려지며 세어지는지를 세 백엔드에서 픽셀로 본다.
    template <typename TModule>
    void TestATexturedSpriteShowsItsTexelsAndCells()
    {
        JBro::WindowsPlatform platform;
        TModule rhi;
        JBro::JMemoryContext memory;
        Check(platform.Initialize(memory), "platform must initialize for the texture pixel test");
        if (false == rhi.Initialize(memory))
        {
            std::cout << "  [skip] no device for this API; textured sprites not verified" << std::endl;
            platform.Shutdown();
            return;
        }
        JBro::WindowDesc windowDesc;
        constexpr char title[] = "JBro texture probe";
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
        config.maxSpriteSubmissions = 8;
        config.presentMode = JBro::PresentMode::Immediate;
        config.validation = true;
        Check(renderer.Initialize(rhi, config), "the texture renderer must initialize");

        // 왼쪽 위 빨강, 오른쪽 위 초록, 왼쪽 아래 파랑, 오른쪽 아래 흰색(불투명).
        const std::byte texels[16] = {
            std::byte{255}, std::byte{0}, std::byte{0}, std::byte{255},
            std::byte{0}, std::byte{255}, std::byte{0}, std::byte{255},
            std::byte{0}, std::byte{0}, std::byte{255}, std::byte{255},
            std::byte{255}, std::byte{255}, std::byte{255}, std::byte{255}};
        const JBro::AssetHandle texture = renderer.RegisterTexture({2, 2}, {texels, 16});
        Check(texture.generation != 0, "a 2x2 texture registers");
        Check(renderer.RegisterTexture({2, 2}, {texels, 15}).generation == 0, "the wrong byte count is refused");
        Check(renderer.RegisterTexture({0, 2}, {texels, 0}).generation == 0, "an empty extent is refused");
        Check(renderer.GetTextureCount() == 1, "one texture lives");

        JBro::CameraParams camera;
        camera.projection = {{1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f}};
        camera.clearColor[0] = 0.0f;
        camera.clearColor[1] = 0.0f;
        camera.clearColor[2] = 0.0f;
        camera.clearColor[3] = 1.0f;
        camera.viewport.width = 64.0f;
        camera.viewport.height = 64.0f;

        // A: 왼쪽 절반, 텍스처 전체. 네 텍셀이 왼쪽 절반의 네 사분면이 된다.
        JBro::SpriteSubmit textured;
        textured.world.linear[0] = 1.0f;
        textured.world.linear[3] = 2.0f;
        textured.world.translation[0] = -0.5f;
        textured.texture = texture;
        // B: 오른쪽 절반, 초록 칸만(uMin 0.5, vMin 0, 절반씩).
        JBro::SpriteSubmit cell = textured;
        cell.world.translation[0] = 0.5f;
        cell.uvRect[0] = 0.5f;
        cell.uvRect[1] = 0.0f;
        cell.uvRect[2] = 0.5f;
        cell.uvRect[3] = 0.5f;
        // C: 오른쪽 아래 사분면, 텍스처 없음(흰색 x 파란 틴트). B 와 다른 텍스처라 묶음이 갈린다.
        JBro::SpriteSubmit plain;
        plain.world.linear[0] = 1.0f;
        plain.world.linear[3] = 1.0f;
        plain.world.translation[0] = 0.5f;
        plain.world.translation[1] = -0.5f;
        // 틴트는 0..1 로 잘린다(D-114). 1.5 는 1 로, -1 은 0 으로 - 자르지 않으면 바이트로 접힐 때 1.5 가 0.498 이 된다.
        plain.tint[0] = 1.5f;
        plain.tint[1] = -1.0f;
        plain.tint[2] = 1.0f;

        Check(renderer.BeginFrame() == JBro::FrameStatus::Ready, "the frame must begin");
        Check(renderer.BeginView(camera), "the view must open");
        Check(renderer.SubmitSprite(textured) && renderer.SubmitSprite(cell) && renderer.SubmitSprite(plain),
            "three sprites submit");
        Check(renderer.EndView() && renderer.EndFrame() == JBro::FrameStatus::Ready, "the frame must present");
        Check(renderer.GetLastFrameStats().staleTextureSpriteCount == 0, "every handle was live");

        JBro::Array<std::byte> image;
        image.Resize(64 * 64 * 4);
        JBro::TextureReadback readback;
        Check(renderer.ReadBackBuffer(image.Data(), image.Size(), readback), "the back buffer reads back");
        const Pixel topLeft = ReadPixel(image, readback.rowPitch, 8, 16);
        const Pixel topRight = ReadPixel(image, readback.rowPitch, 24, 16);
        const Pixel bottomLeft = ReadPixel(image, readback.rowPitch, 8, 48);
        const Pixel bottomRight = ReadPixel(image, readback.rowPitch, 24, 48);
        Check(Near(topLeft.r, 1.0f) && Near(topLeft.g, 0.0f) && Near(topLeft.b, 0.0f), "texel (0,0) sits top-left");
        Check(Near(topRight.r, 0.0f) && Near(topRight.g, 1.0f) && Near(topRight.b, 0.0f), "texel (1,0) sits top-right");
        Check(Near(bottomLeft.r, 0.0f) && Near(bottomLeft.g, 0.0f) && Near(bottomLeft.b, 1.0f), "texel (0,1) sits bottom-left");
        Check(Near(bottomRight.r, 1.0f) && Near(bottomRight.g, 1.0f) && Near(bottomRight.b, 1.0f), "texel (1,1) sits bottom-right");
        const Pixel cellTop = ReadPixel(image, readback.rowPitch, 40, 8);
        const Pixel cellMid = ReadPixel(image, readback.rowPitch, 56, 24);
        Check(Near(cellTop.g, 1.0f) && Near(cellTop.r, 0.0f) && Near(cellMid.g, 1.0f) && Near(cellMid.b, 0.0f),
            "the uv rectangle shows only the green cell across the whole sprite");
        const Pixel plainPixel = ReadPixel(image, readback.rowPitch, 48, 48);
        Check(Near(plainPixel.b, 1.0f) && Near(plainPixel.r, 1.0f) && Near(plainPixel.g, 0.0f),
            "an untextured sprite after a textured one paints its tint, clamped to 0..1 - the runs switched back to white");

        // 두 번째 프레임: 핸들을 내린 뒤 그 핸들로 그린다. 흰색 x 틴트로 나오고 센다.
        renderer.UnregisterTexture(texture);
        Check(renderer.GetTextureCount() == 0, "no texture remains");
        JBro::SpriteSubmit stale;
        stale.world.linear[0] = 2.0f;
        stale.world.linear[3] = 2.0f;
        stale.texture = texture;
        stale.tint[0] = 1.0f;
        stale.tint[1] = 0.0f;
        stale.tint[2] = 1.0f;
        Check(renderer.BeginFrame() == JBro::FrameStatus::Ready, "the second frame must begin");
        Check(renderer.BeginView(camera) && renderer.SubmitSprite(stale) && renderer.EndView(), "the stale sprite submits");
        Check(renderer.EndFrame() == JBro::FrameStatus::Ready, "the second frame must present");
        Check(renderer.GetLastFrameStats().staleTextureSpriteCount == 1, "the stale handle is counted");
        Check(renderer.ReadBackBuffer(image.Data(), image.Size(), readback), "the second frame reads back");
        const Pixel magenta = ReadPixel(image, readback.rowPitch, 32, 32);
        Check(Near(magenta.r, 1.0f) && Near(magenta.g, 0.0f) && Near(magenta.b, 1.0f), "and drawn as white times the tint");

        // 세 번째 프레임: 같은 텍스처를 Linear 로. 텍셀 경계(x = 16)에서 빨강과 초록이 섞인다 - Nearest 는 순색이었다.
        const JBro::AssetHandle again = renderer.RegisterTexture({2, 2}, {texels, 16});
        Check(again.generation != 0, "the texture registers again");
        JBro::SpriteSubmit smooth = textured;
        smooth.texture = again;
        smooth.filter = JBro::SpriteFilter::Linear;
        Check(renderer.BeginFrame() == JBro::FrameStatus::Ready, "the third frame must begin");
        Check(renderer.BeginView(camera) && renderer.SubmitSprite(smooth) && renderer.EndView(), "the smooth sprite submits");
        Check(renderer.EndFrame() == JBro::FrameStatus::Ready, "the third frame must present");
        Check(renderer.ReadBackBuffer(image.Data(), image.Size(), readback), "the third frame reads back");
        const Pixel seam = ReadPixel(image, readback.rowPitch, 16, 16);
        Check(seam.r > 0.3f && seam.r < 0.7f && seam.g > 0.3f && seam.g < 0.7f && seam.b < 0.1f,
            "Linear blends red and green at the texel seam");
        const Pixel inside = ReadPixel(image, readback.rowPitch, 8, 16);
        Check(inside.r > 0.9f && inside.g < 0.1f, "and stays red away from it");
        Check(renderer.GetDevice()->GetValidationErrorCount() == 0, "the debug layer accepted every frame");

        renderer.Shutdown();
        rhi.Shutdown();
        platform.ClosePlatformWindow(window);
        platform.PumpEvents();
        platform.Shutdown();
    }
}

namespace
{
    // **SDF 텍스트 셰이더**(text-plan §5 의 4 단계). 알파가 가로로 255 → 0 으로 떨어지는 16 x 1 거리장을 화면 전체에 펴고,
    // 채우기 빨강·외곽선 파랑으로 그린다. 거리값 0.5 까지가 채우기, 외곽선 문턱까지가 외곽선, 그 밖은 비어 있다. 같은 뷰에서
    // 스프라이트 → SDF → 스프라이트로 파이프라인을 두 번 바꿔도 셋이 제 자리에 나오는지, 문턱을 0 으로 줘도 칸 전체가 칠해지지
    // 않는지, 반투명 채우기에 외곽선이 겹쳐 진해지지 않는지를 세 백엔드에서 본다.
    template <typename TModule>
    void TestSdfTextDrawsFillAndOutlineInOnePass()
    {
        JBro::WindowsPlatform platform;
        TModule rhi;
        JBro::JMemoryContext memory;
        Check(platform.Initialize(memory), "platform must initialize for the sdf pixel test");
        if (false == rhi.Initialize(memory))
        {
            std::cout << "  [skip] no device for this API; sdf text not verified" << std::endl;
            platform.Shutdown();
            return;
        }
        JBro::WindowDesc windowDesc;
        constexpr char title[] = "JBro sdf probe";
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
        config.maxSpriteSubmissions = 8;
        config.presentMode = JBro::PresentMode::Immediate;
        config.validation = true;
        Check(renderer.Initialize(rhi, config), "the sdf renderer must initialize");

        // 거리값이 x 를 따라 1 → 0 으로 떨어진다. 0.5 는 x ≈ 7.5 텍셀, 0.25 는 x ≈ 11.25 텍셀이다.
        std::byte texels[16 * 4] = {};
        for (JBro::Int32 x = 0; x < 16; ++x)
        {
            texels[x * 4 + 0] = std::byte{255};
            texels[x * 4 + 1] = std::byte{255};
            texels[x * 4 + 2] = std::byte{255};
            texels[x * 4 + 3] = static_cast<std::byte>(static_cast<int>(std::lround(255.0 * (15 - x) / 15.0)));
        }
        const JBro::AssetHandle field = renderer.RegisterTexture({16, 1}, {texels, sizeof(texels)});
        Check(field.generation != 0, "the distance field registers");

        JBro::CameraParams camera;
        camera.projection = {{1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f}};
        camera.clearColor[3] = 1.0f;
        camera.viewport.width = 64.0f;
        camera.viewport.height = 64.0f;

        JBro::SpriteSubmit text;
        text.world.linear[0] = 2.0f;
        text.world.linear[3] = 2.0f;
        text.texture = field;
        text.filter = JBro::SpriteFilter::Linear;
        text.shading = JBro::SpriteShading::SdfText;
        text.tint[0] = 1.0f;
        text.tint[1] = 0.0f;
        text.tint[2] = 0.0f;
        text.outlineColor[0] = 0;
        text.outlineColor[1] = 0;
        text.outlineColor[2] = 255;
        text.outlineColor[3] = 255;
        text.outlineEdge = 16384;
        // 앞 스프라이트는 오른쪽 아래(거리장이 비어 있는 자리), 뒤 스프라이트는 왼쪽 위(채우기 위)다.
        JBro::SpriteSubmit before;
        before.world.linear[0] = 0.25f;
        before.world.linear[3] = 0.25f;
        before.world.translation[0] = 0.875f;
        before.world.translation[1] = -0.875f;
        JBro::SpriteSubmit after = before;
        after.world.translation[0] = -0.875f;
        after.world.translation[1] = 0.875f;
        after.tint[0] = 0.0f;
        after.tint[2] = 0.0f;

        JBro::Array<std::byte> image;
        image.Resize(64 * 64 * 4);
        JBro::TextureReadback readback;
        const auto paint = [&](const JBro::SpriteSubmit& sdf, JBro::Bool withSprites) {
            Check(renderer.BeginFrame() == JBro::FrameStatus::Ready, "the frame must begin");
            Check(renderer.BeginView(camera), "the view must open");
            if (withSprites)
            {
                Check(renderer.SubmitSprite(before), "the sprite before submits");
            }
            Check(renderer.SubmitSprite(sdf), "the sdf text submits");
            if (withSprites)
            {
                Check(renderer.SubmitSprite(after), "the sprite after submits");
            }
            Check(renderer.EndView() && renderer.EndFrame() == JBro::FrameStatus::Ready, "the frame must present");
            Check(renderer.ReadBackBuffer(image.Data(), image.Size(), readback), "the back buffer reads back");
        };

        paint(text, true);
        const Pixel fill = ReadPixel(image, readback.rowPitch, 12, 32);
        const Pixel outline = ReadPixel(image, readback.rowPitch, 38, 32);
        const Pixel outside = ReadPixel(image, readback.rowPitch, 54, 32);
        const Pixel spriteBefore = ReadPixel(image, readback.rowPitch, 60, 60);
        const Pixel spriteAfter = ReadPixel(image, readback.rowPitch, 3, 3);
        Check(Near(fill.r, 1.0f) && Near(fill.g, 0.0f) && Near(fill.b, 0.0f), "inside the edge is the fill colour");
        Check(Near(outline.r, 0.0f) && Near(outline.b, 1.0f), "between the edge and the outline's edge is the outline colour");
        Check(Near(outside.r, 0.0f) && Near(outside.g, 0.0f) && Near(outside.b, 0.0f), "past the outline the quad is empty");
        Check(Near(spriteBefore.r, 1.0f) && Near(spriteBefore.g, 1.0f) && Near(spriteBefore.b, 1.0f),
            "a sprite before the text still draws - the pipeline switched to the text and not the other way round");
        Check(Near(spriteAfter.g, 1.0f) && Near(spriteAfter.r, 0.0f), "a sprite after the text draws with the sprite pipeline again");

        // 문턱을 0 으로 줘도 거리값 0 인 자리(칸의 모서리)는 칠하지 않는다. 기존 엔진은 여기서 칸 전체가 외곽선 색이 됐다.
        JBro::SpriteSubmit widest = text;
        widest.outlineEdge = 0;
        paint(widest, false);
        const Pixel corner = ReadPixel(image, readback.rowPitch, 63, 32);
        Check(Near(corner.b, 0.0f), "an outline edge of zero still leaves the zero-distance edge of the quad empty");

        // 반투명 채우기와 외곽선을 한 번에 합성한다. 채우기 자리는 빨강 절반이지 외곽선 위에 한 번 더 얹힌 색이 아니다.
        JBro::SpriteSubmit ghost = text;
        ghost.tint[3] = 0.5f;
        ghost.outlineColor[3] = 128;
        paint(ghost, false);
        const Pixel ghostFill = ReadPixel(image, readback.rowPitch, 12, 32);
        const Pixel ghostOutline = ReadPixel(image, readback.rowPitch, 38, 32);
        Check(Near(ghostFill.r, 0.5f) && Near(ghostFill.b, 0.0f),
            "a half-transparent fill is half red over black, with no outline under it");
        Check(Near(ghostOutline.b, 0.5f) && Near(ghostOutline.r, 0.0f), "and its outline is half blue");
        Check(renderer.GetDevice()->GetValidationErrorCount() == 0, "the debug layer accepted every frame");

        renderer.UnregisterTexture(field);
        renderer.Shutdown();
        rhi.Shutdown();
        platform.ClosePlatformWindow(window);
        platform.PumpEvents();
        platform.Shutdown();
    }
}

namespace
{
    // **텍스처의 사각형 하나만 올린다**(text-plan §3.6, 글리프 아틀라스의 새 칸). 4 x 4 검은 텍스처의 오른쪽 위 2 x 2 에, 8 텍셀 폭
    // 버퍼(행 간격 32 바이트)의 한 조각을 올린다. 그 사분면만 빨갛고 나머지는 검은 채다. 텍스처 밖으로 나가는 사각형은 거절한다.
    template <typename TModule>
    void TestATextureRegionUpdatesOnlyItsRectangle()
    {
        JBro::WindowsPlatform platform;
        TModule rhi;
        JBro::JMemoryContext memory;
        Check(platform.Initialize(memory), "platform must initialize for the region test");
        if (false == rhi.Initialize(memory))
        {
            std::cout << "  [skip] no device for this API; texture regions not verified" << std::endl;
            platform.Shutdown();
            return;
        }
        JBro::WindowDesc windowDesc;
        constexpr char title[] = "JBro region probe";
        windowDesc.title = {title, sizeof(title) - 1};
        windowDesc.width = 64;
        windowDesc.height = 64;
        windowDesc.visible = false;
        const JBro::WindowHandle window = platform.OpenPlatformWindow(windowDesc);
        JBro::Renderer renderer;
        JBro::RendererConfig config;
        config.api = rhi.GetApi();
        config.surface = platform.CreateSurface(window);
        config.surfaceExtent = {64, 64};
        config.maxSpriteSubmissions = 8;
        config.presentMode = JBro::PresentMode::Immediate;
        config.validation = true;
        Check(renderer.Initialize(rhi, config), "the region renderer must initialize");

        std::byte black[4 * 4 * 4] = {};
        for (JBro::Int32 texel = 0; texel < 16; ++texel)
        {
            black[texel * 4 + 3] = std::byte{255};
        }
        const JBro::AssetHandle texture = renderer.RegisterTexture({4, 4}, {black, sizeof(black)});
        Check(texture.generation != 0, "a 4x4 texture registers");
        // 8 x 2 텍셀 버퍼의 앞 두 칸이 빨강이다. 행 간격은 8 텍셀(32 바이트)이다.
        std::byte strip[8 * 2 * 4] = {};
        for (JBro::Int32 row = 0; row < 2; ++row)
        {
            for (JBro::Int32 column = 0; column < 2; ++column)
            {
                std::byte* texel = strip + (row * 8 + column) * 4;
                texel[0] = std::byte{255};
                texel[3] = std::byte{255};
            }
        }
        Check(renderer.UpdateTextureRegion(texture, 2, 0, 2, 2, {strip, sizeof(strip)}, 32), "a 2x2 region goes up");
        Check(false == renderer.UpdateTextureRegion(texture, 3, 0, 2, 2, {strip, sizeof(strip)}, 32),
            "a region past the texture is refused");
        Check(false == renderer.UpdateTextureRegion(texture, 0, 0, 2, 2, {strip, sizeof(strip)}, 4),
            "a row pitch shorter than a row is refused");

        JBro::CameraParams camera;
        camera.projection = {{1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f}};
        camera.clearColor[3] = 1.0f;
        camera.viewport.width = 64.0f;
        camera.viewport.height = 64.0f;
        JBro::SpriteSubmit sprite;
        sprite.world.linear[0] = 2.0f;
        sprite.world.linear[3] = 2.0f;
        sprite.texture = texture;
        Check(renderer.BeginFrame() == JBro::FrameStatus::Ready && renderer.BeginView(camera) && renderer.SubmitSprite(sprite)
                && renderer.EndView() && renderer.EndFrame() == JBro::FrameStatus::Ready,
            "the textured frame presents");
        JBro::Array<std::byte> image;
        image.Resize(64 * 64 * 4);
        JBro::TextureReadback readback;
        Check(renderer.ReadBackBuffer(image.Data(), image.Size(), readback), "the back buffer reads back");
        const Pixel topRight = ReadPixel(image, readback.rowPitch, 48, 16);
        const Pixel topLeft = ReadPixel(image, readback.rowPitch, 16, 16);
        const Pixel bottomRight = ReadPixel(image, readback.rowPitch, 48, 48);
        Check(Near(topRight.r, 1.0f) && Near(topRight.g, 0.0f), "the uploaded quadrant is red");
        Check(Near(topLeft.r, 0.0f) && Near(bottomRight.r, 0.0f), "and the rest of the texture kept its black");
        Check(renderer.GetDevice()->GetValidationErrorCount() == 0, "the debug layer accepted the region upload");

        renderer.UnregisterTexture(texture);
        renderer.Shutdown();
        rhi.Shutdown();
        platform.ClosePlatformWindow(window);
        platform.PumpEvents();
        platform.Shutdown();
    }
}

namespace
{
    // 화면을 네 칸(가로 16 픽셀씩)으로 나눈 한 칸을 덮는 스프라이트다. `half` 가 음수면 그 칸의 위 절반, 양수면 아래 절반이다.
    JBro::SpriteSubmit ColumnSprite(JBro::Int32 column, JBro::Int32 half, JBro::Float r, JBro::Float g, JBro::Float b, JBro::Float a)
    {
        JBro::SpriteSubmit sprite;
        sprite.world.linear[0] = 0.5f;
        sprite.world.linear[3] = half == 0 ? 2.0f : 1.0f;
        sprite.world.translation[0] = -0.75f + 0.5f * static_cast<float>(column);
        sprite.world.translation[1] = half == 0 ? 0.0f : (half < 0 ? 0.5f : -0.5f);
        sprite.tint[0] = r;
        sprite.tint[1] = g;
        sprite.tint[2] = b;
        sprite.tint[3] = a;
        return sprite;
    }

    // **레이어 블렌드는 레이어를 제 텍스처에 그렸다 한 장으로 얹는다**(D-279, 기존 `CompositeLayer`). 회색(0.5) 바탕 위에
    // 칸마다 한 레이어다. 기대값은 미리 곱한 색의 `Layer*` 계수에서 나온다:
    //   0 Normal 50% - 겹친 빨강 둘: 0.5·s + 0.5·d. 스프라이트마다 알파를 곱했다면 겹친 곳이 0.875 로 짙어진다.
    //   1 Additive    - 위: 반투명 회색(0.4, 알파 0.5)은 d + 0.2 = 0.7. 합성에서 알파를 또 곱하면 0.6 이다. 아래: 빨강은 s + d.
    //   2 Multiply    - s·d.
    //   3 Screen      - s·(1 - d) + d.
    // **첫 프레임은 얹을 텍스처가 없어 그대로 그린다**(디바이스는 프레임 안에서 텍스처를 만들지 않는다). 둘째 프레임부터 얹는다.
    template <typename TModule>
    void TestLayerBlendsCompositeTheWholeLayer()
    {
        JBro::WindowsPlatform platform;
        TModule rhi;
        JBro::JMemoryContext memory;
        Check(platform.Initialize(memory), "platform must initialize for the layer blend test");
        if (false == rhi.Initialize(memory))
        {
            std::cout << "  [skip] no device for this API; layer blends not verified" << std::endl;
            platform.Shutdown();
            return;
        }
        JBro::WindowDesc windowDesc;
        constexpr char title[] = "JBro layer blend probe";
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
        config.maxSpriteSubmissions = 16;
        config.presentMode = JBro::PresentMode::Immediate;
        Check(renderer.Initialize(rhi, config), "the layer blend renderer must initialize");

        JBro::CameraParams camera;
        camera.projection = {{1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f}};
        camera.clearColor[3] = 1.0f;
        camera.viewport.width = 64.0f;
        camera.viewport.height = 64.0f;

        JBro::SpriteSubmit background;
        background.world.linear[0] = 2.0f;
        background.world.linear[3] = 2.0f;
        for (JBro::Int32 channel = 0; channel < 3; ++channel)
        {
            background.tint[channel] = 0.5f;
        }

        const auto frame = [&]() {
            Check(renderer.BeginFrame() == JBro::FrameStatus::Ready, "the layer blend frame must begin");
            Check(renderer.BeginView(camera), "the layer blend view must open");
            Check(renderer.SubmitSprite(background), "the background must submit");
            Check(renderer.BeginLayer(JBro::CompositeBlend::Normal, 0.5f), "a faded layer must open");
            Check(false == renderer.BeginLayer(JBro::CompositeBlend::Normal, 0.5f), "layers do not nest");
            Check(renderer.SubmitSprite(ColumnSprite(0, 0, 1.0f, 0.2f, 0.2f, 1.0f))
                    && renderer.SubmitSprite(ColumnSprite(0, 0, 1.0f, 0.2f, 0.2f, 1.0f)),
                "two overlapping sprites go into the faded layer");
            Check(renderer.EndLayer(), "the faded layer must close");
            Check(renderer.BeginLayer(JBro::CompositeBlend::Additive, 1.0f), "an additive layer must open");
            Check(renderer.SubmitSprite(ColumnSprite(1, -1, 0.4f, 0.4f, 0.4f, 0.5f))
                    && renderer.SubmitSprite(ColumnSprite(1, 1, 1.0f, 0.2f, 0.2f, 1.0f)),
                "the additive layer takes a soft and a solid sprite");
            Check(renderer.EndLayer(), "the additive layer must close");
            Check(renderer.BeginLayer(JBro::CompositeBlend::Multiply, 1.0f), "a multiply layer must open");
            Check(renderer.SubmitSprite(ColumnSprite(2, 0, 1.0f, 0.2f, 0.2f, 1.0f)), "the multiply sprite must submit");
            Check(renderer.EndLayer(), "the multiply layer must close");
            // 닫지 않은 묶음은 `EndView` 가 닫는다.
            Check(renderer.BeginLayer(JBro::CompositeBlend::Screen, 1.0f), "a screen layer must open");
            Check(renderer.SubmitSprite(ColumnSprite(3, 0, 1.0f, 0.2f, 0.2f, 1.0f)), "the screen sprite must submit");
            Check(renderer.EndView(), "the layer blend view must close");
            Check(renderer.EndFrame() == JBro::FrameStatus::Ready, "the layer blend frame must present");
        };

        frame();
        const JBro::RendererFrameStats first = renderer.GetLastFrameStats();
        Check(first.compositedLayerCount == 0 && first.uncompositedLayerCount == 4,
            "the first frame has no layer texture yet and draws the layers as they are");
        frame();
        const JBro::RendererFrameStats second = renderer.GetLastFrameStats();
        Check(second.compositedLayerCount == 4 && second.uncompositedLayerCount == 0,
            "from the second frame every layer is composited");

        JBro::Array<std::byte> image;
        image.Resize(64 * 64 * 4);
        JBro::TextureReadback readback;
        Check(renderer.ReadBackBuffer(image.Data(), image.Size(), readback), "the renderer must read its own back buffer");
        const auto expect = [&](JBro::UInt32 x, JBro::UInt32 y, JBro::Float r, JBro::Float g, JBro::Float b, const char* message) {
            const Pixel pixel = ReadPixel(image, readback.rowPitch, x, y);
            if (false == (Near(pixel.r, r) && Near(pixel.g, g) && Near(pixel.b, b)))
            {
                std::cout << "  read " << pixel.r << ", " << pixel.g << ", " << pixel.b << " at " << x << ", " << y << '\n';
            }
            Check(Near(pixel.r, r) && Near(pixel.g, g) && Near(pixel.b, b), message);
        };
        expect(8, 32, 0.75f, 0.35f, 0.35f,
            "a layer at half opacity fades as one image - its two overlapping sprites do not show through each other");
        expect(24, 16, 0.7f, 0.7f, 0.7f,
            "an additive layer adds its premultiplied colour - a half-transparent sprite is not darkened twice");
        expect(24, 48, 1.0f, 0.7f, 0.7f, "an additive layer adds its colour to what is below");
        expect(40, 32, 0.5f, 0.1f, 0.1f, "a multiply layer multiplies what is below");
        expect(56, 32, 1.0f, 0.6f, 0.6f, "a screen layer screens what is below");

        renderer.Shutdown();
        rhi.Shutdown();
        platform.ClosePlatformWindow(window);
        platform.PumpEvents();
        platform.Shutdown();
    }
}

namespace
{
    // 시험이 따로 세운 기대값의 식이다(W3C 합성 명세의 분리형 블렌드, 포토샵과 같다). 셰이더의 식을 옮겨 적지 않는다 - 같은 실수가 두 번 맞는다.
    JBro::Float ReferenceChannel(JBro::CompositeBlend mode, JBro::Float b, JBro::Float s)
    {
        const auto screen = [](JBro::Float x, JBro::Float y) { return x + y - x * y; };
        const auto hardLight = [&](JBro::Float back, JBro::Float source) {
            return source <= 0.5f ? back * 2.0f * source : screen(back, 2.0f * source - 1.0f);
        };
        switch (mode)
        {
        case JBro::CompositeBlend::Subtract:
            return JBro::Max(b - s, 0.0f);
        case JBro::CompositeBlend::Lighten:
            return std::max(b, s);
        case JBro::CompositeBlend::Darken:
            return std::min(b, s);
        case JBro::CompositeBlend::Overlay:
            return hardLight(s, b);
        case JBro::CompositeBlend::SoftLight:
        {
            if (s <= 0.5f)
            {
                return b - (1.0f - 2.0f * s) * b * (1.0f - b);
            }
            const JBro::Float d = b <= 0.25f ? ((16.0f * b - 12.0f) * b + 4.0f) * b : JBro::Float(std::sqrt(b));
            return b + (2.0f * s - 1.0f) * (d - b);
        }
        case JBro::CompositeBlend::HardLight:
            return hardLight(b, s);
        case JBro::CompositeBlend::ColorDodge:
            return b <= 0.0f ? JBro::Float(0.0f) : (s >= 1.0f ? JBro::Float(1.0f) : JBro::Min(1.0f, b / (1.0f - s)));
        case JBro::CompositeBlend::ColorBurn:
            return b >= 1.0f ? JBro::Float(1.0f) : (s <= 0.0f ? JBro::Float(0.0f) : 1.0f - JBro::Min(1.0f, (1.0f - b) / s));
        case JBro::CompositeBlend::Difference:
        default:
            return std::fabs(b - s);
        }
    }

    // **아래 그림을 읽는 블렌드 아홉**(D-283). 바탕(0.6, 0.3, 0.8) 위에, 왼쪽 절반만 덮는 반투명(0.75) 스프라이트 한 장의 레이어를 불투명도 0.8 로
    // 얹는다. 덮인 곳은 (1 - a)·아래 + a·B(아래, 위) 이고(a = 0.75 x 0.8), 레이어가 빈 오른쪽은 아래 그림 그대로다 - 셰이더가 그 자리도 덮어쓰므로
    // 사본이 틀리면 거기서 드러난다. 채널마다 식의 다른 갈래를 타도록 위 색의 채널을 0.5 의 양쪽에 둔다. 첫 프레임은 사본을 둘 자리가 없어 표준으로
    // 얹고, 둘째부터 그 식이다.
    template <typename TModule>
    void TestBackdropBlendsFollowTheirFormulas()
    {
        JBro::WindowsPlatform platform;
        TModule rhi;
        JBro::JMemoryContext memory;
        Check(platform.Initialize(memory), "platform must initialize for the backdrop blend test");
        if (false == rhi.Initialize(memory))
        {
            std::cout << "  [skip] no device for this API; backdrop blends not verified" << std::endl;
            platform.Shutdown();
            return;
        }
        JBro::WindowDesc windowDesc;
        constexpr char title[] = "JBro backdrop blend probe";
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
        config.maxSpriteSubmissions = 8;
        config.presentMode = JBro::PresentMode::Immediate;
        Check(renderer.Initialize(rhi, config), "the backdrop blend renderer must initialize");

        JBro::CameraParams camera;
        camera.projection = {{1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f}};
        camera.clearColor[3] = 1.0f;
        camera.viewport.width = 64.0f;
        camera.viewport.height = 64.0f;
        const JBro::Float back[3] = {0.6f, 0.3f, 0.8f};
        const JBro::Float top[3] = {0.2f, 0.7f, 0.5f};
        constexpr JBro::Float topAlpha = 0.75f;
        constexpr JBro::Float opacity = 0.8f;
        JBro::SpriteSubmit background;
        background.world.linear[0] = 2.0f;
        background.world.linear[3] = 2.0f;
        JBro::SpriteSubmit layerSprite;
        layerSprite.world.linear[0] = 1.0f;
        layerSprite.world.linear[3] = 2.0f;
        layerSprite.world.translation[0] = -0.5f;
        for (JBro::Int32 channel = 0; channel < 3; ++channel)
        {
            background.tint[channel] = back[channel];
            layerSprite.tint[channel] = top[channel];
        }
        layerSprite.tint[3] = topAlpha;

        JBro::Array<std::byte> image;
        image.Resize(64 * 64 * 4);
        JBro::TextureReadback readback;
        for (JBro::UInt32 mode = JBro::FirstBackdropBlend; mode < JBro::CompositeBlendCount; ++mode)
        {
            const JBro::CompositeBlend blend = static_cast<JBro::CompositeBlend>(mode.Get());
            for (JBro::Int32 frame = 0; frame < 2; ++frame)
            {
                Check(renderer.BeginFrame() == JBro::FrameStatus::Ready, "the backdrop frame must begin");
                Check(renderer.BeginView(camera), "the backdrop view must open");
                Check(renderer.SubmitSprite(background), "the background must submit");
                Check(renderer.BeginLayer(blend, opacity), "the blended layer must open");
                Check(renderer.SubmitSprite(layerSprite), "the layer sprite must submit");
                Check(renderer.EndLayer() && renderer.EndView(), "the layer and view must close");
                Check(renderer.EndFrame() == JBro::FrameStatus::Ready, "the backdrop frame must present");
            }
            const JBro::RendererFrameStats stats = renderer.GetLastFrameStats();
            Check(stats.compositedLayerCount == 1 && stats.uncompositedLayerCount == 0,
                "from the second frame the layer is laid on with its own formula");
            Check(renderer.ReadBackBuffer(image.Data(), image.Size(), readback), "the renderer must read its own back buffer");
            const Pixel covered = ReadPixel(image, readback.rowPitch, 16, 32);
            const Pixel empty = ReadPixel(image, readback.rowPitch, 48, 32);
            const JBro::Float a = topAlpha * opacity;
            const JBro::Float got[3] = {covered.r, covered.g, covered.b};
            const JBro::Float left[3] = {empty.r, empty.g, empty.b};
            for (JBro::Int32 channel = 0; channel < 3; ++channel)
            {
                const JBro::Float expected = (1.0f - a) * back[channel] + a * ReferenceChannel(blend, back[channel], top[channel]);
                if (false == Near(got[channel], expected) || false == Near(left[channel], back[channel]))
                {
                    std::cout << "  mode " << mode << " channel " << channel << ": read " << got[channel] << " wanted " << expected
                              << ", empty half " << left[channel] << '\n';
                }
                Check(Near(got[channel], expected), "a backdrop blend mixes the layer and what is below with its formula");
                Check(Near(left[channel], back[channel]), "and leaves what is below untouched where the layer is empty");
            }
        }

        renderer.Shutdown();
        rhi.Shutdown();
        platform.ClosePlatformWindow(window);
        platform.PumpEvents();
        platform.Shutdown();
    }
}

JBro::Int32 RunSpritePixelTests()
{
    TestBackdropBlendsFollowTheirFormulas<JBro::D3D12RHIModule>();
    TestBackdropBlendsFollowTheirFormulas<JBro::D3D11RHIModule>();
    TestBackdropBlendsFollowTheirFormulas<JBro::VulkanRHIModule>();
    TestLayerBlendsCompositeTheWholeLayer<JBro::D3D12RHIModule>();
    TestLayerBlendsCompositeTheWholeLayer<JBro::D3D11RHIModule>();
    TestLayerBlendsCompositeTheWholeLayer<JBro::VulkanRHIModule>();
    TestATexturedSpriteShowsItsTexelsAndCells<JBro::D3D12RHIModule>();
    TestATexturedSpriteShowsItsTexelsAndCells<JBro::D3D11RHIModule>();
    TestATexturedSpriteShowsItsTexelsAndCells<JBro::VulkanRHIModule>();
    TestSpritePacketReachesTheShaderFields<JBro::D3D12RHIModule>();
    TestSpritePacketReachesTheShaderFields<JBro::D3D11RHIModule>();
    TestSpritePacketReachesTheShaderFields<JBro::VulkanRHIModule>();
    TestTheSameSpriteGoesToATextureInstead<JBro::D3D12RHIModule>();
    TestTheSameSpriteGoesToATextureInstead<JBro::D3D11RHIModule>();
    TestTheSameSpriteGoesToATextureInstead<JBro::VulkanRHIModule>();
    TestTheOverlayGetsTheFrameAfterTheGame<JBro::D3D12RHIModule>();
    TestTheOverlayGetsTheFrameAfterTheGame<JBro::D3D11RHIModule>();
    TestTheOverlayGetsTheFrameAfterTheGame<JBro::VulkanRHIModule>();
    TestSdfTextDrawsFillAndOutlineInOnePass<JBro::D3D12RHIModule>();
    TestSdfTextDrawsFillAndOutlineInOnePass<JBro::D3D11RHIModule>();
    TestSdfTextDrawsFillAndOutlineInOnePass<JBro::VulkanRHIModule>();
    TestATextureRegionUpdatesOnlyItsRectangle<JBro::D3D12RHIModule>();
    TestATextureRegionUpdatesOnlyItsRectangle<JBro::D3D11RHIModule>();
    TestATextureRegionUpdatesOnlyItsRectangle<JBro::VulkanRHIModule>();
    std::cout << "Sprite pixel tests passed.\n";
    return 0;
}
