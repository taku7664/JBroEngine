#pragma once

#include <JBro/Core/Core.h>
#include <JBro/Platform/Input.h>

namespace JBro
{
    struct WindowDesc
    {
        JStringView title;
        std::uint32_t width = 1280;
        std::uint32_t height = 720;
        bool visible = true;
    };

    struct WindowHandle
    {
        std::uintptr_t value = 0;
    };

    struct SurfaceHandle
    {
        // The platform window owns the native surface lifetime.
        std::uintptr_t value = 0;
    };

    struct DynamicLibrary
    {
        void* opaque = nullptr;
    };

    struct WindowState
    {
        // Client area in surface pixels, not the outer window rectangle.
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        bool minimized = false;
    };

    class IPlatform : public IModule
    {
    public:
        virtual WindowHandle OpenPlatformWindow(const WindowDesc& desc) = 0;
        virtual void ClosePlatformWindow(WindowHandle window) = 0;
        virtual SurfaceHandle CreateSurface(WindowHandle window) = 0;
        virtual void PumpEvents() = 0;
        // Input gathered by the last PumpEvents. The next PumpEvents clears it,
        // so the view is only valid until then. Main-thread only.
        virtual JArrayView<InputEvent> GetInputEvents() const = 0;
        // Main-thread only. Waits up to the timeout; externally paced platforms may return early.
        virtual void WaitForEvents(std::uint32_t timeoutMilliseconds) = 0;
        // A close request does not destroy the surface. The host drains GPU work first.
        virtual bool ShouldClose(WindowHandle window) const = 0;
        // Main-thread only. False means unavailable/invalid; zero extent cannot render.
        virtual bool GetWindowState(WindowHandle window, WindowState& state) const = 0;
        // Windows loads a disposable sibling copy so the source path remains replaceable.
        virtual DynamicLibrary LoadDynamicLibrary(const char* utf8Path) = 0;
        virtual void* GetSymbol(DynamicLibrary library, const char* name) = 0;
        virtual void UnloadDynamicLibrary(DynamicLibrary library) = 0;
    };
}
