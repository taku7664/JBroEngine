#pragma once

#include <JBro/Platform/Platform.h>
#include <JBro/Types/Array.h>
#include <JBro/Types/SafePtr.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/Int.h>
#include <JBro/Types/UInt.h>

namespace JBro
{
    struct FileWatcher;
    class AudioDeviceWatch;

    class WindowsPlatform final : public IPlatform
    {
    public:
        Bool Initialize(const JMemoryContext& memory) override;
        void Shutdown() override;
        WindowHandle OpenPlatformWindow(const WindowDesc& desc) override;
        void ClosePlatformWindow(WindowHandle window) override;
        SurfaceHandle CreateSurface(WindowHandle window) override;
        void PumpEvents() override;
        JArrayView<InputEvent> GetInputEvents() const override;
        void ClearInputEvents() override;
        void WaitForEvents(UInt32 timeoutMilliseconds) override;
        Bool ShouldClose(WindowHandle window) const override;
        Bool GetWindowState(WindowHandle window, WindowState& state) const override;
        DynamicLibrary LoadDynamicLibrary(const char* utf8Path) override;
        void* GetSymbol(DynamicLibrary library, const char* name) override;
        void UnloadDynamicLibrary(DynamicLibrary library) override;
        Bool ShowFileDialog(WindowHandle owner, const FileDialogDesc& desc, String& outPath) override;
        Bool ReadWholeFile(const char* utf8Path, Array<std::byte>& contents) override;
        OwnerPtr<IFileStream> OpenFileStream(const char* utf8Path) override;
        Bool WriteWholeFile(const char* utf8Path, JArrayView<std::byte> contents) override;
        Bool MoveFileTo(const char* fromUtf8Path, const char* toUtf8Path) override;
        Bool CreateDirectoryAt(const char* utf8Path) override;
        Bool DeleteFileAt(const char* utf8Path) override;
        Bool DeleteDirectoryAt(const char* utf8Path) override;
        String GetExecutableFolder() const override;
        String GetUserDataFolder() const override;
        Bool GetFileWriteTime(const char* utf8Path, Int64& outUnixSeconds) const override;
        Bool OpenPathWithShell(const char* utf8Path) override;
        Bool RevealInFileBrowser(const char* utf8Path) override;
        Bool FileExists(const char* utf8Path) const override;
        Bool DirectoryExists(const char* utf8Path) const override;
        Bool EnumerateDirectory(const char* utf8Root, DirectoryVisitor visitor, void* user) override;
        Bool WatchDirectory(const char* utf8Root) override;
        void StopWatching() override;
        Bool IsWatching() const override;
        UInt32 TakeFileEvents(FileEvent* events, UInt32 capacity) override;
        // `WindowsSockets.cpp` 의 것이다.
        OwnerPtr<Network::ISocketProvider> CreateSocketProvider() override;
        // `WindowsAudio.cpp` 의 것이다. miniaudio 의 WASAPI 장치다(D-197).
        OwnerPtr<IAudioOutput> CreateAudioOutput(const AudioOutputDesc& desc) override;
        UInt32 EnumerateAudioOutputs(AudioDeviceInfo* devices, UInt32 capacity) override;
        Bool PollGamepad(UInt32 slot, GamepadRawState& state) override;
        void SetGamepadVibration(UInt32 slot, Float low, Float high) override;
        Bool TakeAudioDevicesChanged() override;

        // WndProc 이 부른다. 공개 API 가 아니다.
        void RecordInputEvent(const InputEvent& event);
        // UTF-16 서러게이트 쌍의 앞쪽을 들고 있는 자리다.
        std::uint16_t TakePendingHighSurrogate();
        void SetPendingHighSurrogate(std::uint16_t unit);
        // 지금 눌려 있는 마우스 버튼의 수다(D-158). 첫 버튼에 마우스를 붙잡고 마지막 버튼에 놓는다.
        UInt32& HeldMouseButtons() { return m_heldButtons; }

    private:
        // 한 프레임에 받아 둘 입력의 상한이다.
        static constexpr UInt32 MaxInputEventsPerFrame = 4096;

        Array<InputEvent> m_inputEvents;
        // `WindowsFileWatcher.cpp` 의 것이다. 감시하지 않으면 비어 있다.
        OwnerPtr<FileWatcher> m_fileWatcher;
        // `WindowsAudio.cpp` 의 것이다. 처음 물을 때 켠다.
        OwnerPtr<AudioDeviceWatch> m_audioWatch;
        std::uint16_t m_pendingHighSurrogate = 0;
        void* m_instance = nullptr;
        std::uint16_t m_windowClassAtom = 0;
        Bool m_ownsWindowClass = false;
        // `Initialize` 가 이 스레드의 COM 을 STA 로 켰으면 참이다. 그때만 `Shutdown` 이 끈다(D-256).
        Bool m_comInitialized = false;
        Bool m_quitRequested = false;
        UInt32 m_heldButtons = 0;
    };
}
