#pragma once

#include <JBro/Platform/Platform.h>

namespace JBro
{
    class AndroidPlatform final : public IPlatform
    {
    public:
        bool Initialize(const JMemoryContext& memory) override;
        void Shutdown() override;
        WindowHandle OpenPlatformWindow(const WindowDesc& desc) override;
        void ClosePlatformWindow(WindowHandle window) override;
        SurfaceHandle CreateSurface(WindowHandle window) override;
        void PumpEvents() override;
        JArrayView<InputEvent> GetInputEvents() const override;
        void WaitForEvents(std::uint32_t timeoutMilliseconds) override;
        bool ShouldClose(WindowHandle window) const override;
        bool GetWindowState(WindowHandle window, WindowState& state) const override;
        DynamicLibrary LoadDynamicLibrary(const char* utf8Path) override;
        void* GetSymbol(DynamicLibrary library, const char* name) override;
        void UnloadDynamicLibrary(DynamicLibrary library) override;
    };
}
