#pragma once

#include <JBro/Core/Core.h>
#include <JBro/RHI/RHI.h>

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

    class IPlatform : public IModule
    {
    public:
        virtual WindowHandle CreateWindow(const WindowDesc& desc) = 0;
        virtual void DestroyWindow(WindowHandle window) = 0;
        virtual SurfaceHandle CreateSurface(WindowHandle window, GraphicsApi api) = 0;
        virtual void PumpEvents() = 0;
        virtual bool ShouldClose(WindowHandle window) const = 0;
    };
}
