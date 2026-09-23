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
        void ClearInputEvents() override;
        void WaitForEvents(std::uint32_t timeoutMilliseconds) override;
        bool ShouldClose(WindowHandle window) const override;
        bool GetWindowState(WindowHandle window, WindowState& state) const override;
        DynamicLibrary LoadDynamicLibrary(const char* utf8Path) override;
        void* GetSymbol(DynamicLibrary library, const char* name) override;
        void UnloadDynamicLibrary(DynamicLibrary library) override;
        // 파일 시스템은 아직 없다(D-112). Web 은 가상 파일 시스템, Android 는 APK 에셋으로 붙일 자리다.
        bool ReadWholeFile(const char* utf8Path, Array<std::byte>& contents) override;
        bool WriteWholeFile(const char* utf8Path, JArrayView<std::byte> contents) override;
        bool FileExists(const char* utf8Path) const override;
        bool DirectoryExists(const char* utf8Path) const override;
        bool EnumerateDirectory(const char* utf8Root, DirectoryVisitor visitor, void* user) override;
    };
}
