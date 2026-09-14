#include <JBro/D3D12RHI/D3D12RHI.h>
#include <JBro/Editor/EditorUI.h>
#include <JBro/Platform/WindowsPlatform.h>
#include <JBro/Types/Array.h>

#include <imgui.h>

#include <cstddef>
#include <filesystem>
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
    // **창을 일부러 비대칭으로 놓는다.** 화면 가운데에 두면 y 를 뒤집거나 translate 가
    // 틀려도 같은 자리에 떨어져서, 넓이만 세는 검사는 통과해 버린다.
    // 오른쪽 여백 104, 아래쪽 여백 184 - 어느 축으로 뒤집어도 자리가 달라진다.
    constexpr std::uint32_t WindowLeft = 8;
    constexpr std::uint32_t WindowTop = 8;
    constexpr std::uint32_t WindowWidth = 400;
    constexpr std::uint32_t WindowHeight = 320;
    // 창 아래쪽 빈 자리 - 세 구역이 겹치지 않게 나눠 쓴다.
    constexpr std::uint32_t WindowBandBottom = 340;
    // **잘라내기 사각형을 화면 왼쪽 밖까지 민다.** D3D12 는 음수 시저를 받지 않으므로
    // 0 으로 붙여야 하고, 그 붙이는 코드가 실제로 필요한지는 이런 도형이 있어야 드러난다.
    constexpr std::uint32_t ClipTop = 350;
    constexpr std::uint32_t ClipBottom = 390;
    constexpr std::uint32_t ClipRight = 40;
    // **맨 마지막 드로우 리스트에 놓는 사각형이다.** 리스트마다 더해 주는 정점·인덱스
    // 오프셋은 앞 리스트에서 전부 0 이라, 마지막 리스트의 도형만이 그 덧셈을 붙잡는다.
    // 전경 드로우 리스트는 창들 뒤에 제출되므로 여기가 그 자리다.
    constexpr std::uint32_t ForeLeft = 200;
    constexpr std::uint32_t ForeTop = 400;
    constexpr std::uint32_t ForeSize = 40;
    constexpr std::uint32_t ForeRounding = 12;
    // 첫 드로우 리스트(배경)의 사각형이다.
    constexpr std::uint32_t CornerLeft = 440;
    constexpr std::uint32_t CornerTop = 450;
    constexpr std::uint32_t CornerSize = 50;

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

        // 지난 실행이 남긴 것이 있으면 치우고 시작한다. 아래에서 "다시 생겼는가" 를 묻는데,
        // 남의 찌꺼기에 걸리면 그 질문이 아니라 다른 질문에 답하는 셈이 된다.
        std::filesystem::remove("imgui.ini");

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
            ImGui::SetNextWindowPos(
                ImVec2(static_cast<float>(WindowLeft), static_cast<float>(WindowTop)));
            ImGui::SetNextWindowSize(
                ImVec2(static_cast<float>(WindowWidth), static_cast<float>(WindowHeight)));
            ImGui::Begin("Probe", nullptr,
                ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
                    | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings);
            // 글자는 폰트 아틀라스를 지난다. 아틀라스가 안 올라갔으면 창 배경만 칠해지고
            // 밝은 픽셀이 하나도 없다 - 아래에서 그것을 센다.
            ImGui::TextUnformatted("JBro editor UI probe");
            ImGui::TextUnformatted("the font atlas must have reached the GPU");
            ImGui::End();

            // **드로우 리스트를 둘로 만든다.** 하나뿐이면 리스트마다 더해 주는 정점·인덱스
            // 오프셋이 전부 0 이라, 그 덧셈을 빼먹어도 화면이 똑같이 나온다.
            // 배경 드로우 리스트는 창들과 별도 리스트로 제출된다.
            ImDrawList* background = ImGui::GetBackgroundDrawList();
            background->AddRectFilled(
                ImVec2(static_cast<float>(CornerLeft), static_cast<float>(CornerTop)),
                ImVec2(static_cast<float>(CornerLeft + CornerSize),
                    static_cast<float>(CornerTop + CornerSize)),
                IM_COL32(255, 255, 255, 255));
            background->PushClipRect(
                ImVec2(-40.0f, static_cast<float>(ClipTop)),
                ImVec2(static_cast<float>(ClipRight), static_cast<float>(ClipBottom)),
                false);
            background->AddRectFilled(
                ImVec2(-40.0f, static_cast<float>(ClipTop)),
                ImVec2(static_cast<float>(ClipRight), static_cast<float>(ClipBottom)),
                IM_COL32(255, 255, 255, 255));
            background->PopClipRect();

            // 전경 리스트는 창 다음에 나온다. 이 도형이 제자리에 있으려면 앞의 두
            // 리스트가 쓴 정점·인덱스만큼 밀어서 읽어야 한다.
            //
            // **모서리를 둥글게 하는 것이 핵심이다.** 반듯한 사각형은 인덱스가
            // (0,1,2, 0,2,3) 인데 그것은 앞 리스트의 사각형과 글자 그대로 같은 값이라,
            // 인덱스를 엉뚱한 데서 읽어와도 결과가 똑같이 나온다. 둥근 모서리는
            // 삼각형 부채꼴이 되어 그 패턴이 달라진다.
            ImGui::GetForegroundDrawList()->AddRectFilled(
                ImVec2(static_cast<float>(ForeLeft), static_cast<float>(ForeTop)),
                ImVec2(static_cast<float>(ForeLeft + ForeSize),
                    static_cast<float>(ForeTop + ForeSize)),
                IM_COL32(255, 255, 255, 255),
                static_cast<float>(ForeRounding));
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
        // 세 구역을 따로 센다. 창(위쪽 띠), 왼쪽으로 잘린 사각형, 오른쪽 아래 구석이다.
        const auto Pixel = [&](std::uint32_t x, std::uint32_t y) {
            const std::size_t offset = static_cast<std::size_t>(y) * readback.rowPitch
                + static_cast<std::size_t>(x) * 4;
            return reinterpret_cast<const unsigned char*>(image.Data() + offset);
        };
        const auto Painted = [&](std::uint32_t x, std::uint32_t y) {
            const unsigned char* p = Pixel(x, y);
            return p[0] != 0 || p[1] != 0 || p[2] != 0;
        };

        // 창 - 넓이와 자리, 그리고 글자.
        // **밝은 픽셀은 창 띠 안에서만 센다.** 아래 흰 사각형들까지 세면 폰트 아틀라스가
        // 안 올라가도 숫자가 채워진다.
        std::size_t painted = 0;
        std::size_t bright = 0;
        std::uint32_t minX = SurfaceSize;
        std::uint32_t maxX = 0;
        std::uint32_t minY = SurfaceSize;
        std::uint32_t maxY = 0;
        for (std::uint32_t y = 0; y < WindowBandBottom; ++y)
        {
            for (std::uint32_t x = 0; x < SurfaceSize; ++x)
            {
                if (false == Painted(x, y))
                {
                    continue;
                }
                ++painted;
                minX = x < minX ? x : minX;
                maxX = x > maxX ? x : maxX;
                minY = y < minY ? y : minY;
                maxY = y > maxY ? y : maxY;
                const unsigned char* p = Pixel(x, y);
                if (p[0] > 200 && p[1] > 200 && p[2] > 200)
                {
                    ++bright;
                }
            }
        }

        // 왼쪽으로 잘린 사각형 - x 는 0 에서 시작해 ClipRight 에서 끝난다.
        std::size_t clipPainted = 0;
        std::uint32_t clipMaxX = 0;
        for (std::uint32_t y = ClipTop; y < ClipBottom; ++y)
        {
            for (std::uint32_t x = 0; x < SurfaceSize; ++x)
            {
                if (Painted(x, y))
                {
                    ++clipPainted;
                    clipMaxX = x > clipMaxX ? x : clipMaxX;
                }
            }
        }

        // 전경 사각형 - 마지막 드로우 리스트가 제자리에 갔는지. 자리까지 본다.
        std::size_t forePainted = 0;
        std::uint32_t foreMinX = SurfaceSize;
        std::uint32_t foreMaxX = 0;
        for (std::uint32_t y = ForeTop; y < ForeTop + ForeSize; ++y)
        {
            for (std::uint32_t x = 0; x < SurfaceSize; ++x)
            {
                if (Painted(x, y))
                {
                    ++forePainted;
                    foreMinX = x < foreMinX ? x : foreMinX;
                    foreMaxX = x > foreMaxX ? x : foreMaxX;
                }
            }
        }

        // 구석 사각형 - 첫 드로우 리스트가 제자리에 갔는지.
        std::size_t cornerPainted = 0;
        for (std::uint32_t y = CornerTop; y < CornerTop + CornerSize; ++y)
        {
            for (std::uint32_t x = CornerLeft; x < CornerLeft + CornerSize; ++x)
            {
                if (Painted(x, y))
                {
                    ++cornerPainted;
                }
            }
        }

        const std::size_t windowArea =
            static_cast<std::size_t>(WindowWidth) * static_cast<std::size_t>(WindowHeight);
        std::cout << "  the probe window painted " << painted << " of " << windowArea
            << " pixels (" << bright << " bright) at x[" << minX << ".." << maxX
            << "] y[" << minY << ".." << maxY << "]; clipped " << clipPainted
            << ", foreground " << forePainted << " at x[" << foreMinX << ".."
            << foreMaxX << "], corner " << cornerPainted << "; "
            << ui.GetLastDrawCount() << " draw(s)" << std::endl;

        // **칠해진 자리가 우리가 지정한 자리여야 한다.** 넓이만 세면 창이 엉뚱한 곳에
        // 통째로 옮겨가도 통과한다 - 투영 상수나 뷰포트가 틀리면 정확히 그렇게 된다.
        // 모서리가 둥글어 가장자리 한두 픽셀은 흐리므로 2픽셀까지 봐준다.
        const auto Near = [](std::uint32_t got, std::uint32_t want) {
            const std::uint32_t gap = got > want ? got - want : want - got;
            return gap <= 2;
        };
        Check(Near(minX, WindowLeft) && Near(maxX, WindowLeft + WindowWidth - 1),
            "the window must be painted at the x it was placed at");
        Check(Near(minY, WindowTop) && Near(maxY, WindowTop + WindowHeight - 1),
            "the window must be painted at the y it was placed at");
        // 그 자리를 실제로 채워야 한다. 테두리만 나오면 여기서 걸린다.
        Check(painted > windowArea - windowArea / 20,
            "the probe window must cover the area it asked for");
        // 그리고 그 안에 글자가 있어야 한다.
        Check(bright > 100, "the text must reach the screen, so the font atlas must upload");

        // 화면 밖까지 민 사각형은 보이는 부분만 남는다.
        const std::size_t clipArea = static_cast<std::size_t>(ClipRight)
            * static_cast<std::size_t>(ClipBottom - ClipTop);
        Check(clipPainted > clipArea - clipArea / 20,
            "the part of the clipped rectangle that is on screen must be painted");
        Check(clipMaxX < ClipRight + 2, "and nothing past its clip rectangle may be");

        // **마지막 드로우 리스트.** 정점·인덱스 오프셋을 리스트마다 밀어 주지 않으면
        // 이 사각형이 앞 리스트의 정점을 읽어서 딴 데 그려지거나 찌그러진다.
        // 둥근 사각형이라 네 귀퉁이가 비어 있다. 그 비어 있음이 모양의 증거다 -
        // 인덱스를 잘못 읽으면 모서리가 반듯해지거나 도형이 흩어진다.
        const std::size_t foreArea = static_cast<std::size_t>(ForeSize) * ForeSize;
        Check(forePainted > foreArea - foreArea / 5 && forePainted < foreArea,
            "the last draw list must fill the rounded rectangle it asked for");
        Check(foreMinX == ForeLeft && foreMaxX == ForeLeft + ForeSize - 1,
            "and it must be at the x it asked for");
        Check(false == Painted(ForeLeft, ForeTop),
            "and its corner must be round, so the indices must come from its own list");
        // 첫 드로우 리스트도 마찬가지로 제자리여야 한다.
        Check(cornerPainted == static_cast<std::size_t>(CornerSize) * CornerSize,
            "the first draw list must be painted where it asked to be");

        ui.Shutdown();
        Check(false == ui.IsInitialized(), "shutting down must release the UI");
        // ImGui 는 컨텍스트를 지울 때 ini 를 쓴다. 한 번 돌리고 나면 파일이 남고,
        // 다음 실행은 그 파일에서 창 자리를 읽는다 - 위에서 지정한 자리가 아니라.
        // 위에서 지우고 시작했으므로, 있다면 이번 실행이 만든 것이다.
        Check(false == std::filesystem::exists("imgui.ini"),
            "running the UI must not leave a settings file in the working directory");
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
