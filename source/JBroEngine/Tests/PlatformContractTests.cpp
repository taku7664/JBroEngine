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

    void TestHiddenWindowLifecycle()
    {
        JBro::WindowsPlatform platform;
        JBro::JMemoryContext memory;
        Check(platform.Initialize(memory), "Windows platform must initialize");

        constexpr char title[] = "JBro hidden test window";
        JBro::WindowDesc desc;
        desc.title = {title, sizeof(title) - 1};
        desc.width = 320;
        desc.height = 180;
        desc.visible = false;

        const JBro::WindowHandle window = platform.OpenPlatformWindow(desc);
        Check(window.value != 0, "Windows platform must create a hidden window");
        Check(false == platform.ShouldClose(window), "new window must remain open");

        const JBro::SurfaceHandle surface = platform.CreateSurface(window);
        Check(surface.value == window.value, "Windows surface must borrow the native window handle");

        platform.PumpEvents();
        platform.ClosePlatformWindow(window);
        platform.PumpEvents();
        Check(platform.ShouldClose(window), "closed window must report completion");

        platform.ClosePlatformWindow(window);
        platform.Shutdown();
    }
}

int RunPlatformContractTests()
{
    TestHiddenWindowLifecycle();
    std::cout << "Platform contract tests passed.\n";
    return 0;
}
