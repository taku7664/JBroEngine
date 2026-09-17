#include <JBro/D3D11RHI/D3D11RHI.h>
#include <JBro/D3D12RHI/D3D12RHI.h>
#include <JBro/Platform/WindowsPlatform.h>
#include <JBro/Host/EngineInstance.h>

#if defined(JBRO_GAME_DIMENSION_2D)
#include <JBro/Framework2DSystem/Framework2D.h>
#elif defined(JBRO_GAME_DIMENSION_3D)
#include <JBro/Framework3DSystem/Framework3D.h>
#else
#error A game dimension must be selected.
#endif

#include <chrono>

namespace
{
    constexpr std::uint32_t SkippedFrameWaitMilliseconds = 16;

    template <typename TFramework>
    int RunGameHost()
    {
        JBro::EngineConfig config;
        constexpr char title[] = "JBro Engine";
        config.window.title = {title, sizeof(title) - 1};

        JBro::WindowsPlatform platform;
        if (false == platform.Initialize(config.memory))
        {
            return 1;
        }

        // 백엔드는 설정이 고른다(D-107). 기본은 D3D12 다.
        JBro::D3D12RHIModule d3d12;
        JBro::D3D11RHIModule d3d11;
        JBro::IRHIModule& rhi = config.graphicsApi == JBro::GraphicsApi::D3D11
            ? static_cast<JBro::IRHIModule&>(d3d11)
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
            if (false == engine.Initialize(config, platform, rhi)
                || false == engine.OpenProject(framework))
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

int main()
{
#if defined(JBRO_GAME_DIMENSION_2D)
    return RunGameHost<JBro::Framework2D>();
#else
    return RunGameHost<JBro::Framework3D>();
#endif
}
