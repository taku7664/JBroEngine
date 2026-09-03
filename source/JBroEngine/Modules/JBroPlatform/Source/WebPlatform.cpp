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

    bool WebPlatform::ShouldClose(WindowHandle) const
    {
        return false;
    }

    DynamicLibrary WebPlatform::LoadDynamicLibrary(const char*)
    {
        return {};
    }

    void* WebPlatform::GetSymbol(DynamicLibrary, const char*)
    {
        return nullptr;
    }

    void WebPlatform::UnloadDynamicLibrary(DynamicLibrary)
    {
    }
}
