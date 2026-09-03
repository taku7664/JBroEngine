#include <JBro/Platform/WindowsPlatform.h>

#include <Windows.h>

namespace JBro
{
    bool WindowsPlatform::Initialize(const JMemoryContext&)
    {
        mInstance = GetModuleHandleW(nullptr);
        return mInstance != nullptr;
    }

    void WindowsPlatform::Shutdown()
    {
        mInstance = nullptr;
    }

    WindowHandle WindowsPlatform::OpenPlatformWindow(const WindowDesc&)
    {
        return {};
    }

    void WindowsPlatform::ClosePlatformWindow(WindowHandle)
    {
    }

    SurfaceHandle WindowsPlatform::CreateSurface(WindowHandle window)
    {
        return {window.value};
    }

    void WindowsPlatform::PumpEvents()
    {
    }

    bool WindowsPlatform::ShouldClose(WindowHandle) const
    {
        return false;
    }

    DynamicLibrary WindowsPlatform::LoadDynamicLibrary(const char* utf8Path)
    {
        if (utf8Path == nullptr || utf8Path[0] == '\0')
        {
            return {};
        }

        const int wideLength = MultiByteToWideChar(
            CP_UTF8,
            MB_ERR_INVALID_CHARS,
            utf8Path,
            -1,
            nullptr,
            0);
        if (wideLength == 0)
        {
            return {};
        }

        HANDLE processHeap = GetProcessHeap();
        auto* widePath = static_cast<wchar_t*>(HeapAlloc(
            processHeap,
            0,
            static_cast<SIZE_T>(wideLength) * sizeof(wchar_t)));
        if (widePath == nullptr)
        {
            return {};
        }

        const int convertedLength = MultiByteToWideChar(
            CP_UTF8,
            MB_ERR_INVALID_CHARS,
            utf8Path,
            -1,
            widePath,
            wideLength);
        HMODULE module = nullptr;
        if (convertedLength != 0)
        {
            module = LoadLibraryW(widePath);
        }

        HeapFree(processHeap, 0, widePath);
        return {module};
    }

    void* WindowsPlatform::GetSymbol(DynamicLibrary library, const char* name)
    {
        if (library.opaque == nullptr || name == nullptr || name[0] == '\0')
        {
            return nullptr;
        }

        const auto module = static_cast<HMODULE>(library.opaque);
        return reinterpret_cast<void*>(GetProcAddress(module, name));
    }

    void WindowsPlatform::UnloadDynamicLibrary(DynamicLibrary library)
    {
        if (library.opaque == nullptr)
        {
            return;
        }

        const auto module = static_cast<HMODULE>(library.opaque);
        FreeLibrary(module);
    }
}
