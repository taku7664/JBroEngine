#include <JBro/Platform/WindowsPlatform.h>

#include <Windows.h>

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
        const auto nativeWindow = reinterpret_cast<HWND>(window.value);
        JBro::WindowState state;
        Check(platform.GetWindowState(window, state), "live window must expose its surface state");
        Check(state.width == 320 && state.height == 180 && false == state.minimized,
            "surface dimensions must exclude borders and title bar");
        RECT resized = {0, 0, 480, 240};
        Check(AdjustWindowRectEx(&resized, WS_OVERLAPPEDWINDOW, FALSE, 0) != FALSE, "test resize must adjust borders");
        Check(SetWindowPos(nativeWindow, nullptr, 0, 0, resized.right - resized.left, resized.bottom - resized.top,
            SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE) != FALSE, "test window must resize without showing");
        platform.PumpEvents();
        Check(platform.GetWindowState(window, state) && state.width == 480 && state.height == 240,
            "surface state must reflect the latest client area");
        SendMessageW(nativeWindow, WM_CLOSE, 0, 0);
        Check(platform.ShouldClose(window), "close message must request host shutdown");
        Check(IsWindow(nativeWindow) != FALSE, "close request must preserve the GPU surface until explicit teardown");
        Check(platform.CreateSurface(window).value == surface.value, "surface must survive the close request");
        platform.ClosePlatformWindow(window);
        platform.PumpEvents();
        Check(platform.ShouldClose(window), "closed window must report completion");
        Check(false == platform.GetWindowState(window, state) && state.width == 0 && state.height == 0,
            "destroyed window must not return stale dimensions");

        const auto secondWindow = platform.OpenPlatformWindow(desc);
        Check(false == platform.ShouldClose(secondWindow), "close requests must not leak to a new window");
        PostQuitMessage(0);
        platform.PumpEvents();
        Check(platform.ShouldClose(secondWindow), "thread quit must request shutdown without destroying the window");
        Check(platform.CreateSurface(secondWindow).value != 0, "quit must preserve the surface until host cleanup");
        platform.ClosePlatformWindow(secondWindow);

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
