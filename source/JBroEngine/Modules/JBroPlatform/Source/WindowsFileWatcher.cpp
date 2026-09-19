#include <JBro/Platform/WindowsPlatform.h>

#include <windows.h>

#include <cstring>
#include <mutex>
#include <thread>

// Windows 의 폴더 감시다(D-117·D-121). `ReadDirectoryChangesW` 를 워커 스레드가 돌리고, 이벤트는 경로 글자만 든
// POD(`FileEvent`)로 고정 크기 고리 버퍼에 쌓는다. 워커는 할당도 `SafePtr` 도 만지지 않는다 - 메인 스레드가
// `TakeFileEvents` 로 프레임 밖에서 꺼낸다. 기존 엔진의 mtime 폴링(`WindowsFileWatcher.cpp`)은 에셋 수에 비례해
// 매 주기 폴더를 걸었고, 이것은 변경 수에 비례한다.
namespace JBro
{
    struct FileWatcher
    {
        static constexpr std::uint32_t Capacity = 256;
        static constexpr DWORD BufferBytes = 64 * 1024;

        HANDLE directory = INVALID_HANDLE_VALUE;
        HANDLE stopEvent = nullptr;
        HANDLE readyEvent = nullptr;
        std::thread thread;

        std::mutex mutex;
        FileEvent ring[Capacity] = {};
        std::uint32_t head = 0;
        std::uint32_t count = 0;
        bool overflowed = false;
        // RENAMED_OLD_NAME 은 NEW_NAME 과 짝이다. 앞것을 들고 있다가 뒷것이 오면 하나로 낸다.
        char pendingOldName[FileEvent::MaxPathBytes] = {};
        bool hasPendingOldName = false;

        alignas(DWORD) unsigned char buffer[BufferBytes] = {};
        OVERLAPPED overlapped = {};

        // 다음 알림을 건다. **첫 요청은 `WatchDirectory` 가 워커를 띄우기 전에 건다** - 워커가 늦게 걸면 그 사이의
        // 변경을 놓친다(처음에는 감시 직후 만든 파일이 오지 않았다).
        bool Issue()
        {
            overlapped = {};
            overlapped.hEvent = readyEvent;
            DWORD unused = 0;
            return ReadDirectoryChangesW(directory, buffer, BufferBytes, TRUE,
                FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_SIZE
                    | FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_CREATION,
                &unused, &overlapped, nullptr) != FALSE;
        }

        void Push(const FileEvent& event)
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (count == Capacity)
            {
                overflowed = true;
                return;
            }
            ring[(head + count) % Capacity] = event;
            ++count;
        }

        // 와이드 이름을 UTF-8 로, 구분자를 `/` 로. 버퍼에 안 들어가면 거짓이다 - 그 이벤트는 넘침으로 센다.
        static bool ToUtf8(const wchar_t* name, std::size_t length, char* out, std::size_t capacity)
        {
            if (length == 0)
            {
                out[0] = '\0';
                return true;
            }
            const int written = WideCharToMultiByte(CP_UTF8, 0, name, static_cast<int>(length),
                out, static_cast<int>(capacity - 1), nullptr, nullptr);
            if (written <= 0)
            {
                return false;
            }
            out[written] = '\0';
            for (int at = 0; at < written; ++at)
            {
                if (out[at] == '\\')
                {
                    out[at] = '/';
                }
            }
            return true;
        }

        void Handle(const FILE_NOTIFY_INFORMATION& info)
        {
            FileEvent event;
            const std::size_t length = info.FileNameLength / sizeof(wchar_t);
            if (false == ToUtf8(info.FileName, length, event.path, sizeof(event.path)))
            {
                std::lock_guard<std::mutex> lock(mutex);
                overflowed = true;
                return;
            }
            switch (info.Action)
            {
            case FILE_ACTION_ADDED:
                event.kind = FileEventKind::Created;
                break;
            case FILE_ACTION_REMOVED:
                event.kind = FileEventKind::Removed;
                break;
            case FILE_ACTION_MODIFIED:
                event.kind = FileEventKind::Modified;
                break;
            case FILE_ACTION_RENAMED_OLD_NAME:
                std::memcpy(pendingOldName, event.path, sizeof(pendingOldName));
                hasPendingOldName = true;
                return;
            case FILE_ACTION_RENAMED_NEW_NAME:
                event.kind = FileEventKind::Renamed;
                if (hasPendingOldName)
                {
                    std::memcpy(event.oldPath, pendingOldName, sizeof(event.oldPath));
                    hasPendingOldName = false;
                }
                break;
            default:
                return;
            }
            Push(event);
        }

        void Run()
        {
            const HANDLE waits[2] = { stopEvent, readyEvent };
            for (;;)
            {
                const DWORD woken = WaitForMultipleObjects(2, waits, FALSE, INFINITE);
                if (woken != WAIT_OBJECT_0 + 1)
                {
                    CancelIoEx(directory, &overlapped);
                    DWORD ignored = 0;
                    GetOverlappedResult(directory, &overlapped, &ignored, TRUE);
                    return;
                }
                DWORD bytes = 0;
                if (false == GetOverlappedResult(directory, &overlapped, &bytes, FALSE))
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    overflowed = true;
                    return;
                }
                if (bytes == 0)
                {
                    // 버퍼가 넘쳐 OS 가 알림을 버렸다. 전부 다시 볼 수밖에 없다.
                    std::lock_guard<std::mutex> lock(mutex);
                    overflowed = true;
                }
                else
                {
                    DWORD offset = 0;
                    for (;;)
                    {
                        const auto& info = *reinterpret_cast<const FILE_NOTIFY_INFORMATION*>(buffer + offset);
                        Handle(info);
                        if (info.NextEntryOffset == 0)
                        {
                            break;
                        }
                        offset += info.NextEntryOffset;
                    }
                }
                if (false == Issue())
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    overflowed = true;
                    return;
                }
            }
        }
    };

    namespace
    {
        // `WindowsFileSystem.cpp` 의 것과 같은 규칙이다: UTF-8 이 아니면 빈 경로다.
        std::wstring ToWide(const char* utf8)
        {
            if (utf8 == nullptr || utf8[0] == '\0')
            {
                return {};
            }
            const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8, -1, nullptr, 0);
            if (length <= 0)
            {
                return {};
            }
            std::wstring wide(static_cast<std::size_t>(length), L'\0');
            MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8, -1, wide.data(), length);
            wide.resize(static_cast<std::size_t>(length) - 1);
            return wide;
        }
    }

    bool WindowsPlatform::WatchDirectory(const char* utf8Root)
    {
        StopWatching();
        const std::wstring wide = ToWide(utf8Root);
        if (wide.empty())
        {
            return false;
        }
        const HANDLE directory = CreateFileW(wide.c_str(), FILE_LIST_DIRECTORY,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr);
        if (directory == INVALID_HANDLE_VALUE)
        {
            return false;
        }
        OwnerPtr<FileWatcher> watcher = MakeOwnerPtr<FileWatcher>();
        watcher->directory = directory;
        watcher->stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        watcher->readyEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (watcher->stopEvent == nullptr || watcher->readyEvent == nullptr)
        {
            if (watcher->stopEvent != nullptr)
            {
                CloseHandle(watcher->stopEvent);
            }
            if (watcher->readyEvent != nullptr)
            {
                CloseHandle(watcher->readyEvent);
            }
            CloseHandle(directory);
            return false;
        }
        if (false == watcher->Issue())
        {
            CloseHandle(watcher->stopEvent);
            CloseHandle(watcher->readyEvent);
            CloseHandle(directory);
            return false;
        }
        FileWatcher* raw = watcher.Get();
        watcher->thread = std::thread([raw]() { raw->Run(); });
        m_fileWatcher = std::move(watcher);
        return true;
    }

    void WindowsPlatform::StopWatching()
    {
        FileWatcher* watcher = m_fileWatcher.Get();
        if (watcher == nullptr)
        {
            return;
        }
        SetEvent(watcher->stopEvent);
        if (watcher->thread.joinable())
        {
            watcher->thread.join();
        }
        CloseHandle(watcher->directory);
        CloseHandle(watcher->stopEvent);
        CloseHandle(watcher->readyEvent);
        m_fileWatcher = {};
    }

    std::uint32_t WindowsPlatform::TakeFileEvents(FileEvent* events, std::uint32_t capacity)
    {
        FileWatcher* watcher = m_fileWatcher.Get();
        if (watcher == nullptr || events == nullptr || capacity == 0)
        {
            return 0;
        }
        std::lock_guard<std::mutex> lock(watcher->mutex);
        std::uint32_t taken = 0;
        while (taken < capacity && watcher->count > 0)
        {
            events[taken++] = watcher->ring[watcher->head];
            watcher->head = (watcher->head + 1) % FileWatcher::Capacity;
            --watcher->count;
        }
        if (watcher->count == 0 && watcher->overflowed && taken < capacity)
        {
            // 넘침은 쌓인 것을 다 꺼낸 뒤 마지막에 알린다 - 받는 쪽이 전부 다시 보게.
            FileEvent overflow;
            overflow.kind = FileEventKind::Overflow;
            events[taken++] = overflow;
            watcher->overflowed = false;
        }
        return taken;
    }
}
