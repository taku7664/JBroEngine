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

    JArrayView<InputEvent> AndroidPlatform::GetInputEvents() const
    {
        // 이 플랫폼은 아직 입력을 모으지 않는다.
        return {};
    }

    void AndroidPlatform::ClearInputEvents()
    {
        // 모으지 않으니 비울 것도 없다.
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

    bool AndroidPlatform::ReadWholeFile(const char*, Array<std::byte>&)
    {
        return false;
    }

    bool AndroidPlatform::WriteWholeFile(const char*, JArrayView<std::byte>)
    {
        return false;
    }

    bool AndroidPlatform::FileExists(const char*) const
    {
        return false;
    }

    bool AndroidPlatform::DirectoryExists(const char*) const
    {
        return false;
    }

    bool AndroidPlatform::EnumerateDirectory(const char*, DirectoryVisitor, void*)
    {
        return false;
    }
}
