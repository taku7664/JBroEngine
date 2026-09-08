#include <JBro/Platform/AndroidPlatform.h>

namespace JBro
{
    bool AndroidPlatform::Initialize(const JMemoryContext&)
    {
        return true;
    }

    void AndroidPlatform::Shutdown()
    {
    }

    WindowHandle AndroidPlatform::OpenPlatformWindow(const WindowDesc&)
    {
        return {};
    }

    void AndroidPlatform::ClosePlatformWindow(WindowHandle)
    {
    }

    SurfaceHandle AndroidPlatform::CreateSurface(WindowHandle window)
    {
        return {window.value};
    }

    void AndroidPlatform::PumpEvents()
    {
    }

    void AndroidPlatform::WaitForEvents(std::uint32_t)
    {
        // The Android backend is still a declared extension point.
    }

    bool AndroidPlatform::ShouldClose(WindowHandle) const
    {
        return false;
    }

    DynamicLibrary AndroidPlatform::LoadDynamicLibrary(const char*)
    {
        return {};
    }

    bool AndroidPlatform::GetWindowState(WindowHandle, WindowState& state) const
    {
        state = {};
        return false;
    }

    void* AndroidPlatform::GetSymbol(DynamicLibrary, const char*)
    {
        return nullptr;
    }

    void AndroidPlatform::UnloadDynamicLibrary(DynamicLibrary)
    {
    }
}
