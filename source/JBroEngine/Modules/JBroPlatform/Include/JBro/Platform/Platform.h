#pragma once

#include <JBro/Core/Core.h>
#include <JBro/Platform/Input.h>
#include <JBro/Types/Array.h>
#include <JBro/Types/String.h>

#include <cstddef>

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

    // 폴더 열거의 방문자다. `relativeUtf8Path` 는 열거를 시작한 폴더 기준 상대경로이고 구분자는 `/` 다.
    // 폴더에 대해 거짓을 돌려주면 그 아래로 내려가지 않는다. 파일에 대한 반환값은 뜻이 없다.
    using DirectoryVisitor = bool (*)(const char* relativeUtf8Path, bool isDirectory, void* user);

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
        // ── 파일 시스템 (D-112) ──────────────────────────────────────────────────────────────
        // **엔진 모듈은 파일을 직접 열지 않고 이것을 거친다.** 플랫폼마다 읽는 길이 다르다 - Windows 는 파일이고,
        // Android 는 APK 안의 에셋이며, Web 은 가상 파일 시스템이다. 경로는 전부 UTF-8 이다. 전부 프레임 밖의 일이다.
        // 없는 파일·열 수 없는 파일은 거짓이고 `contents` 는 손대지 않는다. **기본은 "파일 시스템이 없다"** - 테스트의
        // 가짜 플랫폼과 아직 붙이지 않은 플랫폼이 그것이다. 파일을 읽는 플랫폼은 다섯을 함께 덮어쓴다.
        virtual bool ReadWholeFile(const char* utf8Path, Array<std::byte>& contents)
        {
            (void)utf8Path;
            (void)contents;
            return false;
        }
        // 덮어쓴다. 부모 폴더는 만들지 않는다. 쓰기가 없는 플랫폼(패키지 안의 에셋)은 거짓이다.
        virtual bool WriteWholeFile(const char* utf8Path, JArrayView<std::byte> contents)
        {
            (void)utf8Path;
            (void)contents;
            return false;
        }
        virtual bool FileExists(const char* utf8Path) const
        {
            (void)utf8Path;
            return false;
        }
        virtual bool DirectoryExists(const char* utf8Path) const
        {
            (void)utf8Path;
            return false;
        }
        // `utf8Root` 아래를 깊이 우선으로 돈다. 폴더를 먼저 알리고, 방문자가 참을 돌려주면 그 아래로 내려간다.
        // 폴더가 없거나 열거가 없는 플랫폼이면 거짓이다.
        virtual bool EnumerateDirectory(const char* utf8Root, DirectoryVisitor visitor, void* user)
        {
            (void)utf8Root;
            (void)visitor;
            (void)user;
            return false;
        }

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
