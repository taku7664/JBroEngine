#include <JBro/Core/Log.h>
#include <JBro/D3D11RHI/D3D11RHI.h>
#include <JBro/D3D12RHI/D3D12RHI.h>
#include <JBro/VulkanRHI/VulkanRHI.h>
#include <JBro/Platform/WindowsPlatform.h>
#include <JBro/Host/EngineInstance.h>
#include <JBro/Host/GameHostArguments.h>
#include <JBro/Canvas/Canvas.h>
#include <JBro/Canvas/CanvasFile.h>

#if defined(JBRO_GAME_DIMENSION_2D)
#include <JBro/Framework2DSystem/Framework2D.h>
#elif defined(JBRO_GAME_DIMENSION_3D)
#include <JBro/Framework3DSystem/Framework3D.h>
#else
#error A game dimension must be selected.
#endif

#include <Windows.h>

#include <chrono>
#include <cstdio>

namespace
{
    constexpr std::uint32_t SkippedFrameWaitMilliseconds = 16;

    // 실행 인자는 와이드로 받아 UTF-8 로 바꾼다. 좁은 `main` 의 인자는 ANSI 라 한글 경로가 깨진다(D-112).
    JBro::String ToUtf8(const wchar_t* wide)
    {
        JBro::String result;
        if (wide == nullptr || wide[0] == L'\0')
        {
            return result;
        }
        const int length = WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
        if (length <= 1)
        {
            return result;
        }
        result.resize(static_cast<std::size_t>(length - 1));
        WideCharToMultiByte(CP_UTF8, 0, wide, -1, result.data(), length, nullptr, nullptr);
        return result;
    }

    // 캔버스 파일을 읽어 프레임워크의 캔버스에 넣고 에셋을 푼다(D-115). 실패는 알리되 게임은 뜬다 - 빈 화면이 아무것도
    // 안 뜨는 것보다 낫고, 원인은 표준 출력에 있다.
    template <typename TFramework>
    void LoadStartupCanvas(TFramework& framework, JBro::IPlatform& platform, const JBro::String& path)
    {
        if (path.empty())
        {
            return;
        }
        JBro::Array<std::byte> text;
        if (false == platform.ReadWholeFile(path.c_str(), text))
        {
            JBro::Log::Write(JBro::LogLevel::Info, "canvas",
                "the startup canvas could not be read: %s", path.c_str());
            return;
        }
        JBro::Canvas* canvas = framework.GetCanvas();
        JBro::CanvasFileError error;
        if (canvas == nullptr
            || false == JBro::ReadCanvasText(*canvas, reinterpret_cast<const char*>(text.Data()), text.Size(), error))
        {
            JBro::Log::Write(JBro::LogLevel::Info, "canvas",
                "the startup canvas could not be loaded: %s (%s)",
                path.c_str(), error.message.c_str());
            return;
        }
        framework.BindCanvasAssets();
    }

    template <typename TFramework>
    int RunGameHost(const JBro::GameHostArguments& arguments)
    {
        JBro::EngineConfig config;
        constexpr char title[] = "JBro Engine";
        config.window.title = {title, sizeof(title) - 1};

        JBro::WindowsPlatform platform;
        if (false == platform.Initialize(config.memory))
        {
            return 1;
        }

        // 백엔드는 설정이 고른다(D-107·D-108). 기본은 D3D12 다.
        JBro::D3D12RHIModule d3d12;
        JBro::D3D11RHIModule d3d11;
        JBro::VulkanRHIModule vulkan;
        JBro::IRHIModule& rhi = config.graphicsApi == JBro::GraphicsApi::D3D11 ? static_cast<JBro::IRHIModule&>(d3d11)
            : config.graphicsApi == JBro::GraphicsApi::Vulkan                  ? static_cast<JBro::IRHIModule&>(vulkan)
                                                                                : static_cast<JBro::IRHIModule&>(d3d12);
        if (false == rhi.Initialize(config.memory))
        {
            platform.Shutdown();
            return 2;
        }

        int result = 0;
        try
        {
            TFramework framework;
            JBro::EngineInstance engine;
            bool opened = engine.Initialize(config, platform, rhi);
            if (opened)
            {
                if (arguments.projectFile.empty())
                {
                    // 프로젝트 없이도 뜬다 - 지금까지의 동작이고, 테스트와 스모크가 이 길을 쓴다.
                    opened = engine.OpenProject(framework);
                }
                else
                {
                    JBro::ProjectFileError error;
                    opened = engine.OpenProjectFile(framework, arguments.projectFile.c_str(), error);
                    if (false == opened)
                    {
                        std::printf("error: the project could not be opened: %s (line %u: %s)\n",
                            arguments.projectFile.c_str(), error.line, error.message.c_str());
                    }
                    else
                    {
                        LoadStartupCanvas(framework, platform,
                            JBro::ResolveStartupCanvasPath(arguments, engine.GetProjectFile(), arguments.projectFile.c_str()));
                    }
                }
            }
            if (false == opened)
            {
                result = 3;
            }
            else
            {
                auto previousTime = std::chrono::steady_clock::now();
                while (engine.IsRunning())
                {
                    const auto currentTime = std::chrono::steady_clock::now();
                    const std::chrono::duration<float> elapsed = currentTime - previousTime;
                    previousTime = currentTime;

                    if (false == engine.Tick(elapsed.count()))
                    {
                        break;
                    }
                    if (engine.GetLastFrameStatus() == JBro::FrameStatus::Skipped)
                    {
                        platform.WaitForEvents(SkippedFrameWaitMilliseconds);
                    }
                }
                const JBro::FrameStatus lastStatus = engine.GetLastFrameStatus();
                if (lastStatus != JBro::FrameStatus::Ready
                    && lastStatus != JBro::FrameStatus::Skipped)
                {
                    result = 4;
                }
            }
            engine.Shutdown();
        }
        catch (...)
        {
            result = 4;
        }

        rhi.Shutdown();
        platform.Shutdown();
        return result;
    }
}

int wmain(int argc, wchar_t** argv)
{
    JBro::Array<JBro::String> utf8;
    JBro::Array<const char*> pointers;
    for (int index = 0; index < argc; ++index)
    {
        utf8.Add(ToUtf8(argv[index]));
    }
    for (std::size_t index = 0; index < utf8.Size(); ++index)
    {
        pointers.Add(utf8[index].c_str());
    }
    const JBro::GameHostArguments arguments = JBro::ParseGameHostArguments(argc, pointers.Data());
    if (false == arguments.IsValid())
    {
        std::printf("error: %s\nusage: JBroGameHost [--project <path>] [--canvas <path>]\n", arguments.error.c_str());
        return 5;
    }
#if defined(JBRO_GAME_DIMENSION_2D)
    return RunGameHost<JBro::Framework2D>(arguments);
#else
    return RunGameHost<JBro::Framework3D>(arguments);
#endif
}
