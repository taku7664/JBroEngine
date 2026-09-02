#pragma once

#include <JBro/Platform/Platform.h>

namespace JBro
{
    class AndroidPlatform final : public IPlatform
    {
    public:
        bool Initialize(const JMemoryContext& memory) override;
        void Shutdown() override;
        WindowHandle CreateWindow(const WindowDesc& desc) override;
        void DestroyWindow(WindowHandle window) override;
        SurfaceHandle CreateSurface(WindowHandle window, GraphicsApi api) override;
        void PumpEvents() override;
        bool ShouldClose(WindowHandle window) const override;
    };
}
