#pragma once

#include <JBro/Core/Core.h>

namespace JBro
{
    struct WindowDesc
    {
        JStringView title;
        std::uint32_t width = 1280;
        std::uint32_t height = 720;
    };

    struct WindowHandle
    {
        std::uintptr_t value = 0;
    };

    struct SurfaceHandle
    {
        std::uintptr_t value = 0;
    };

    struct DynamicLibrary
    {
        void* opaque = nullptr;
    };

    class IPlatform : public IModule
    {
    public:
        virtual WindowHandle OpenPlatformWindow(const WindowDesc& desc) = 0;
        virtual void ClosePlatformWindow(WindowHandle window) = 0;
        virtual SurfaceHandle CreateSurface(WindowHandle window) = 0;
        virtual void PumpEvents() = 0;
        virtual bool ShouldClose(WindowHandle window) const = 0;
        virtual DynamicLibrary LoadDynamicLibrary(const char* utf8Path) = 0;
        virtual void* GetSymbol(DynamicLibrary library, const char* name) = 0;
        virtual void UnloadDynamicLibrary(DynamicLibrary library) = 0;
    };
}
