#pragma once

#include <JBro/Core/Core.h>
#include <JBro/Platform/Input.h>
#include <JBro/Types/String.h>

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

    // 파일 대화상자 하나. 글자는 전부 UTF-8 이고 널이면 비운 것으로 본다.
    struct FileDialogDesc
    {
        const char* title = nullptr;
        // 한 종류만 받는다 - "JBro 캔버스" + "*.jcanvas". 필터 이름이 널이면 종류를 걸지 않는다.
        const char* filterName = nullptr;
        const char* filterPattern = nullptr;
        const char* defaultFileName = nullptr;
        const char* initialDirectory = nullptr;
        // 참이면 저장 대화상자(덮어쓰기 확인), 거짓이면 열기 대화상자(있는 파일만).
        bool save = false;
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
        // **막힌다.** 사용자가 고르거나 취소할 때까지 돌아오지 않는다. 프레임 밖에서 부른다.
        // 고르면 참이고 `outPath` 에 UTF-8 경로가 온다. 취소하거나 이 플랫폼에 대화상자가
        // 없으면 거짓이다 - 기본은 없다. 에디터 저장 메뉴가 부른다.
        virtual bool ShowFileDialog(WindowHandle owner, const FileDialogDesc& desc, String& outPath)
        {
            (void)owner;
            (void)desc;
            (void)outPath;
            return false;
        }
    };
}
