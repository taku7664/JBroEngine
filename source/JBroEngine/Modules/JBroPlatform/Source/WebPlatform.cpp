#include <JBro/Platform/WebPlatform.h>

namespace JBro
{
    bool WebPlatform::Initialize(const JMemoryContext&)
    {
        return true;
    }

    void WebPlatform::Shutdown()
    {
    }

    WindowHandle WebPlatform::OpenPlatformWindow(const WindowDesc&)
    {
        return {};
    }

    void WebPlatform::ClosePlatformWindow(WindowHandle)
    {
    }

    SurfaceHandle WebPlatform::CreateSurface(WindowHandle window)
    {
        return {window.value};
    }

    void WebPlatform::PumpEvents()
    {
    }

    JArrayView<InputEvent> WebPlatform::GetInputEvents() const
    {
        // 이 플랫폼은 아직 입력을 모으지 않는다.
        return {};
    }

    void WebPlatform::WaitForEvents(std::uint32_t)
    {
        // Browser hosts must yield through their external event loop.
    }

    bool WebPlatform::ShouldClose(WindowHandle) const
    {
        return false;
    }

    DynamicLibrary WebPlatform::LoadDynamicLibrary(const char*)
    {
        return {};
    }

    bool WebPlatform::GetWindowState(WindowHandle, WindowState& state) const
    {
        state = {};
        return false;
    }

    void* WebPlatform::GetSymbol(DynamicLibrary, const char*)
    {
        return nullptr;
    }

    void WebPlatform::UnloadDynamicLibrary(DynamicLibrary)
    {
    }
}
