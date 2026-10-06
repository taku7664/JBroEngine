#include <JBro/Platform/AndroidPlatform.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/UInt.h>

namespace JBro
{
    Bool AndroidPlatform::Initialize(const JMemoryContext&)
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

    void AndroidPlatform::WaitForEvents(UInt32)
    {
        // The Android backend is still a declared extension point.
    }

    Bool AndroidPlatform::ShouldClose(WindowHandle) const
    {
        return false;
    }

    DynamicLibrary AndroidPlatform::LoadDynamicLibrary(const char*)
    {
        return {};
    }

    Bool AndroidPlatform::GetWindowState(WindowHandle, WindowState& state) const
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

    Bool AndroidPlatform::ReadWholeFile(const char*, Array<std::byte>&)
    {
        return false;
    }

    Bool AndroidPlatform::WriteWholeFile(const char*, JArrayView<std::byte>)
    {
        return false;
    }

    Bool AndroidPlatform::FileExists(const char*) const
    {
        return false;
    }

    Bool AndroidPlatform::DirectoryExists(const char*) const
    {
        return false;
    }

    Bool AndroidPlatform::EnumerateDirectory(const char*, DirectoryVisitor, void*)
    {
        return false;
    }
}
