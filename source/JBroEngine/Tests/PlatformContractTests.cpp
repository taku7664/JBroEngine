#include <JBro/Platform/WindowsPlatform.h>

#include <Windows.h>
#include <objbase.h>

#include <chrono>
#include <future>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Int.h>

namespace
{
    void Check(JBro::Bool condition, const char* message)
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

        Check(PostMessageW(nativeWindow, WM_APP, 0, 0) != FALSE, "test event must enter the window queue");
        const auto waitStart = std::chrono::steady_clock::now();
        platform.WaitForEvents(1000);
        const auto waitElapsed = std::chrono::steady_clock::now() - waitStart;
        Check(waitElapsed < std::chrono::milliseconds(250),
            "queued window input must wake the host without waiting for the timeout");
        platform.PumpEvents();

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

    // The main thread's COM mode is fixed by whoever initializes it first. miniaudio opens the audio device
    // on the calling thread with COINIT_MULTITHREADED, and the engine opens audio on the main thread - so
    // unless the platform claims STA first, that thread becomes MTA and IFileDialog::Show then hung without
    // ever showing a window (the editor stopped responding on "Open Project", D-256).
    //
    // Each check runs on its own thread so the test runner's own COM state cannot decide the result; a failed
    // check is carried back through the future so it reports its message instead of ending the process.
    // The speakers are not opened: a second CoInitializeEx(MULTITHREADED) is exactly what miniaudio does.
    void TestThePlatformThreadStaysSingleThreadedForDialogs()
    {
        std::packaged_task<void()> claim([] {
            JBro::WindowsPlatform platform;
            JBro::JMemoryContext memory;
            Check(platform.Initialize(memory), "Windows platform must initialize");
            APTTYPE type = APTTYPE_CURRENT;
            APTTYPEQUALIFIER qualifier = APTTYPEQUALIFIER_NONE;
            Check(SUCCEEDED(CoGetApartmentType(&type, &qualifier)), "the platform must have turned COM on for its thread");
            Check(type == APTTYPE_STA || type == APTTYPE_MAINSTA, "and turned it on single-threaded");
            const HRESULT audio = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            Check(audio == RPC_E_CHANGED_MODE, "a later multithreaded request (miniaudio opening a device) must not change it");
            platform.Shutdown();
            Check(CoGetApartmentType(&type, &qualifier) == CO_E_NOTINITIALIZED,
                "shutdown must turn off exactly the COM the platform turned on");
        });
        std::future<void> claimed = claim.get_future();
        std::thread(std::move(claim)).join();
        claimed.get();

        // On a thread that is already multithreaded the dialog must refuse at once rather than hang.
        std::packaged_task<JBro::Bool()> refuse([] {
            CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            JBro::WindowsPlatform platform;
            JBro::JMemoryContext memory;
            const JBro::Bool initialized = platform.Initialize(memory);
            JBro::FileDialogDesc desc;
            desc.title = "JBro test dialog";
            desc.filterName = "JBro project file";
            desc.filterPattern = "*.jproject";
            JBro::String path;
            const JBro::Bool chosen = platform.ShowFileDialog({}, desc, path);
            platform.Shutdown();
            CoUninitialize();
            return initialized && false == chosen && path.empty();
        });
        std::future<JBro::Bool> refused = refuse.get_future();
        std::thread worker(std::move(refuse));
        if (refused.wait_for(std::chrono::seconds(3)) != std::future_status::ready)
        {
            worker.detach();
            Check(false, "a file dialog asked for on a multithreaded COM thread must not block");
        }
        worker.join();
        Check(refused.get(), "it must refuse and choose nothing");
    }
}

JBro::Int32 RunPlatformContractTests()
{
    TestHiddenWindowLifecycle();
    TestThePlatformThreadStaysSingleThreadedForDialogs();
    std::cout << "Platform contract tests passed.\n";
    return 0;
}
