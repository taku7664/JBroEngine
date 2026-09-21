#pragma once

#include <JBro/Platform/Platform.h>
#include <JBro/Types/Array.h>
#include <JBro/Types/SafePtr.h>

namespace JBro
{
    struct FileWatcher;

    class WindowsPlatform final : public IPlatform
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
        bool ShowFileDialog(WindowHandle owner, const FileDialogDesc& desc, String& outPath) override;
        bool ReadWholeFile(const char* utf8Path, Array<std::byte>& contents) override;
        bool WriteWholeFile(const char* utf8Path, JArrayView<std::byte> contents) override;
        bool MoveFileTo(const char* fromUtf8Path, const char* toUtf8Path) override;
        bool CreateDirectoryAt(const char* utf8Path) override;
        bool DeleteFileAt(const char* utf8Path) override;
        bool DeleteDirectoryAt(const char* utf8Path) override;
        bool RevealInFileBrowser(const char* utf8Path) override;
        bool FileExists(const char* utf8Path) const override;
        bool DirectoryExists(const char* utf8Path) const override;
        bool EnumerateDirectory(const char* utf8Root, DirectoryVisitor visitor, void* user) override;
        bool WatchDirectory(const char* utf8Root) override;
        void StopWatching() override;
        bool IsWatching() const override;
        std::uint32_t TakeFileEvents(FileEvent* events, std::uint32_t capacity) override;
        // `WindowsSockets.cpp` 의 것이다.
        OwnerPtr<Network::ISocketProvider> CreateSocketProvider() override;

        // WndProc 이 부른다. 공개 API 가 아니다.
        void RecordInputEvent(const InputEvent& event);
        // UTF-16 서러게이트 쌍의 앞쪽을 들고 있는 자리다.
        std::uint16_t TakePendingHighSurrogate();
        void SetPendingHighSurrogate(std::uint16_t unit);

    private:
        // 한 프레임에 받아 둘 입력의 상한이다.
        static constexpr std::uint32_t MaxInputEventsPerFrame = 4096;

        Array<InputEvent> m_inputEvents;
        // `WindowsFileWatcher.cpp` 의 것이다. 감시하지 않으면 비어 있다.
        OwnerPtr<FileWatcher> m_fileWatcher;
        std::uint16_t m_pendingHighSurrogate = 0;
        void* m_instance = nullptr;
        std::uint16_t m_windowClassAtom = 0;
        bool m_ownsWindowClass = false;
        bool m_quitRequested = false;
    };
}
