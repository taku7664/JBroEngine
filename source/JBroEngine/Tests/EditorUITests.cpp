#include <JBro/D3D12RHI/D3D12RHI.h>
#include <JBro/Editor/EditorUI.h>
#include <JBro/Platform/WindowsPlatform.h>
#include <JBro/Types/Array.h>

#include <imgui.h>

#include <cstddef>
#include <cstring>
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

    constexpr std::uint32_t SurfaceSize = 512;
    // 창은 화면 거의 전부를 덮는다. 덮는 넓이를 우리가 정해야 "얼마나 칠해져야
    // 맞는가" 를 셀 수 있다.
    constexpr float WindowMargin = 8.0f;
    constexpr float WindowExtent = static_cast<float>(SurfaceSize) - 2.0f * WindowMargin;

    struct Stage
    {
        JBro::WindowsPlatform platform;
        JBro::D3D12RHIModule rhi;
        JBro::IRHIDevice* device = nullptr;
        JBro::WindowHandle window;
        JBro::SwapchainHandle swapchain;
        bool platformOpen = false;
        bool rhiOpen = false;

        bool Open(const char* title);
        void Close();
    };

    bool Stage::Open(const char* title)
    {
        JBro::JMemoryContext memory;
        Check(platform.Initialize(memory), "the platform must initialize");
        platformOpen = true;
        if (false == rhi.Initialize(memory))
        {
            Close();
            return false;
        }
        rhiOpen = true;
        device = rhi.CreateDevice({});
        if (device == nullptr)
        {
            Close();
            return false;
        }

        JBro::WindowDesc windowDesc;
        windowDesc.title = {title, static_cast<std::uint32_t>(std::strlen(title))};
        windowDesc.width = SurfaceSize;
        windowDesc.height = SurfaceSize;
        windowDesc.visible = false;
        window = platform.OpenPlatformWindow(windowDesc);
        Check(window.value != 0, "the probe window must open");

        JBro::SwapchainDesc swapchainDesc;
        swapchainDesc.surface = platform.CreateSurface(window);
        swapchainDesc.extent = {SurfaceSize, SurfaceSize};
        swapchainDesc.presentMode = JBro::PresentMode::Immediate;
        swapchain = device->CreateSwapchain(swapchainDesc);
        Check(swapchain.IsValid(), "the probe swapchain must be created");
        return true;
    }

    void Stage::Close()
    {
        if (device != nullptr)
        {
            device->DestroySwapchain(swapchain);
            rhi.DestroyDevice(device);
            device = nullptr;
        }
        if (window.value != 0)
        {
            platform.ClosePlatformWindow(window);
            window = {};
        }
        if (rhiOpen)
        {
            rhi.Shutdown();
            rhiOpen = false;
        }
        if (platformOpen)
        {
            platform.Shutdown();
            platformOpen = false;
        }
    }

    // UI 가 실제로 픽셀을 남겼는지는 되읽지 않고는 알 수 없다. 드로우가 나갔는데
    // 셰이더나 바인딩이 틀리면 D3D12 는 아무 말도 하지 않고 화면만 검게 남는다.
    void TestTheDemoWindowPaintsSomething()
    {
        Stage stage;
        if (false == stage.Open("JBro editor ui probe"))
        {
            std::cout << "  [skip] no D3D12 device; the editor UI not verified" << std::endl;
            return;
        }

        JBro::EditorUI ui;
        Check(ui.Initialize(*stage.device, JBro::TextureFormat::BGRA8Unorm),
            "the editor UI must initialize");
        Check(ui.IsInitialized(), "and say so");

        // **새 창은 첫 프레임에 그려지지 않는다.** ImGui 가 크기를 재고 자리를 잡는 동안
        // 감춰 두기 때문이다. 실제 에디터는 계속 도니 문제가 없지만, 한 프레임만 돌리는
        // 테스트는 빈 화면을 보게 된다. 몇 프레임 돌린 뒤에 본다.
        //
        // 프레임 순서가 계약이다. 텍스처 요청은 RHI 프레임 **밖에서** 처리되어야 한다 —
        // WriteTexture 가 GPU 를 기다리므로 프레임 안에서는 거절당한다.
        for (int warmUp = 0; warmUp < 3; ++warmUp)
        {
            Check(ui.BeginFrame({SurfaceSize, SurfaceSize}, 1.0f / 60.0f),
                "each UI frame must begin");
            ImGui::SetNextWindowPos(ImVec2(WindowMargin, WindowMargin));
            ImGui::SetNextWindowSize(ImVec2(WindowExtent, WindowExtent));
            ImGui::Begin("Probe", nullptr,
                ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
                    | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings);
            // 글자는 폰트 아틀라스를 지난다. 아틀라스가 안 올라갔으면 창 배경만 칠해지고
            // 밝은 픽셀이 하나도 없다 - 아래에서 그것을 센다.
            ImGui::TextUnformatted("JBro editor UI probe");
            ImGui::TextUnformatted("the font atlas must have reached the GPU");
            ImGui::End();
            Check(ui.EndFrame(), "each UI frame must end and its textures must upload");
        }

        const JBro::BeginFrameResult begun = stage.device->BeginFrame(stage.swapchain);
        Check(begun.status == JBro::FrameStatus::Ready, "the render frame must begin");
        JBro::IRHICommandContext& commands = *begun.frame.commands;

        JBro::ColorAttachmentDesc attachment;
        attachment.texture = begun.frame.backBuffer;
        attachment.loadOperation = JBro::LoadOperation::Clear;
        attachment.clearColor = {0.0f, 0.0f, 0.0f, 1.0f};
        JBro::RenderPassDesc pass;
        pass.colorAttachments = {&attachment, 1};
        Check(commands.BeginRenderPass(pass), "the render pass must begin");

        JBro::Viewport viewport;
        viewport.width = static_cast<float>(SurfaceSize);
        viewport.height = static_cast<float>(SurfaceSize);
        commands.SetViewport(viewport);

        Check(ui.Draw(commands), "the draw lists must go through");
        Check(ui.GetLastDrawCount() > 0, "the demo window must produce draws");

        commands.EndRenderPass();
        Check(stage.device->EndFrame(begun.frame) == JBro::FrameStatus::Ready,
            "the render frame must present");

        JBro::Array<std::byte> image;
        image.Resize(SurfaceSize * SurfaceSize * 4);
        JBro::TextureReadback readback;
        Check(stage.device->ReadTexture(
                begun.frame.backBuffer, image.Data(), image.Size(), readback),
            "the back buffer must read back");

        // 지운 색은 검정이다. 검지 않은 픽셀은 UI 가 칠한 것이다.
        // **밝은 픽셀은 따로 센다.** 창 배경은 어둡고(0.06 x 0.94), 흰 글자만 밝다.
        // 배경만 칠해지고 밝은 픽셀이 없으면 폰트 아틀라스가 GPU 에 안 갔다는 뜻이다.
        std::size_t painted = 0;
        std::size_t bright = 0;
        for (std::uint32_t y = 0; y < SurfaceSize; ++y)
        {
            for (std::uint32_t x = 0; x < SurfaceSize; ++x)
            {
                const std::size_t offset = static_cast<std::size_t>(y) * readback.rowPitch
                    + static_cast<std::size_t>(x) * 4;
                const auto* bytes =
                    reinterpret_cast<const unsigned char*>(image.Data() + offset);
                if (bytes[0] != 0 || bytes[1] != 0 || bytes[2] != 0)
                {
                    ++painted;
                }
                if (bytes[0] > 200 && bytes[1] > 200 && bytes[2] > 200)
                {
                    ++bright;
                }
            }
        }
        const std::size_t windowArea =
            static_cast<std::size_t>(WindowExtent) * static_cast<std::size_t>(WindowExtent);
        std::cout << "  the probe window painted " << painted << " of " << windowArea
            << " pixels (" << bright << " bright) in " << ui.GetLastDrawCount()
            << " draw(s)" << std::endl;
        // 창이 요청한 넓이를 실제로 덮어야 한다. 모서리가 둥글어 몇 픽셀은 빈다.
        Check(painted > windowArea - windowArea / 20,
            "the probe window must cover the area it asked for");
        // 그리고 그 안에 글자가 있어야 한다.
        Check(bright > 100, "the text must reach the screen, so the font atlas must upload");

        ui.Shutdown();
        Check(false == ui.IsInitialized(), "shutting down must release the UI");
        stage.Close();
    }

    void TestTheFrameOrderIsEnforced()
    {
        Stage stage;
        if (false == stage.Open("JBro editor order probe"))
        {
            std::cout << "  [skip] no D3D12 device; the frame order not verified" << std::endl;
            return;
        }

        JBro::EditorUI ui;
        Check(ui.Initialize(*stage.device, JBro::TextureFormat::BGRA8Unorm),
            "the editor UI must initialize");
        Check(false == ui.Initialize(*stage.device, JBro::TextureFormat::BGRA8Unorm),
            "initializing twice must be refused");

        // 프레임을 열지 않고 닫을 수 없다.
        Check(false == ui.EndFrame(), "ending a frame that never began must be refused");

        Check(ui.BeginFrame({SurfaceSize, SurfaceSize}, 1.0f / 60.0f), "the UI frame must begin");
        Check(false == ui.BeginFrame({SurfaceSize, SurfaceSize}, 1.0f / 60.0f),
            "beginning twice must be refused");

        // 아직 열려 있는 동안의 드로우 리스트는 지난 프레임 것이다.
        const JBro::BeginFrameResult begun = stage.device->BeginFrame(stage.swapchain);
        Check(begun.status == JBro::FrameStatus::Ready, "the render frame must begin");
        Check(false == ui.Draw(*begun.frame.commands),
            "drawing before the UI frame is closed must be refused");
        stage.device->AbortFrame(begun.frame);

        Check(ui.EndFrame(), "the UI frame must end");

        // 크기가 0 이거나 시간이 0 이면 ImGui 가 단언에서 멈춘다. 먼저 거절한다.
        Check(false == ui.BeginFrame({0, SurfaceSize}, 1.0f / 60.0f),
            "a frame with no width must be refused");
        Check(false == ui.BeginFrame({SurfaceSize, SurfaceSize}, 0.0f),
            "a frame with no elapsed time must be refused");

        ui.Shutdown();
        stage.Close();
    }
}

int RunEditorUITests()
{
    TestTheDemoWindowPaintsSomething();
    TestTheFrameOrderIsEnforced();
    std::cout << "Editor UI tests passed.\n";
    return 0;
}
