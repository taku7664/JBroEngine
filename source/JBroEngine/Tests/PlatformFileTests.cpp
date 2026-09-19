#include <JBro/Platform/WindowsPlatform.h>

#include <windows.h>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace
{
    namespace fs = std::filesystem;

    void Check(bool condition, const char* message)
    {
        if (false == condition)
        {
            std::cout << "test failure: " << message << '\n';
            throw std::runtime_error(message);
        }
    }

    JBro::String Utf8(const fs::path& path)
    {
        const std::u8string text = path.generic_u8string();
        return JBro::String(reinterpret_cast<const char*>(text.data()), text.size());
    }

    struct Seen
    {
        JBro::Array<JBro::String> files;
        JBro::Array<JBro::String> directories;
    };

    bool Visit(const char* relative, bool isDirectory, void* user)
    {
        Seen& seen = *static_cast<Seen*>(user);
        if (isDirectory)
        {
            seen.directories.Add(JBro::String(relative));
            // `skip` 폴더 아래로는 내려가지 않는다.
            return std::strcmp(relative, "skip") != 0;
        }
        seen.files.Add(JBro::String(relative));
        return true;
    }

    bool Has(const JBro::Array<JBro::String>& list, const char* value)
    {
        for (std::size_t index = 0; index < list.Size(); ++index)
        {
            if (list[index] == value)
            {
                return true;
            }
        }
        return false;
    }

    // 꺼낸 감시 이벤트를 전부 모아 둔다. 한 묶음에 여럿이 오므로(옮기기 = 삭제 + 생성) 찾는 것 뒤의 것을 버리면 안 된다.
    struct EventLog
    {
        JBro::Array<JBro::FileEvent> events;

        bool Has(JBro::FileEventKind kind, const char* path, const char* oldPath) const
        {
            for (std::size_t at = 0; at < events.Size(); ++at)
            {
                if (events[at].kind == kind && std::strcmp(events[at].path, path) == 0
                    && (oldPath == nullptr || std::strcmp(events[at].oldPath, oldPath) == 0))
                {
                    return true;
                }
            }
            return false;
        }
    };

    // 지정한 종류와 경로가 올 때까지 꺼내 모은다. OS 알림은 비동기라 잠깐 기다린다.
    bool WaitForEvent(JBro::WindowsPlatform& platform, EventLog& log, JBro::FileEventKind kind, const char* path,
        const char* oldPath = nullptr)
    {
        JBro::FileEvent events[32];
        for (int attempt = 0; attempt < 300; ++attempt)
        {
            const std::uint32_t taken = platform.TakeFileEvents(events, 32);
            for (std::uint32_t at = 0; at < taken; ++at)
            {
                log.events.Add(events[at]);
            }
            if (log.Has(kind, path, oldPath))
            {
                return true;
            }
            Sleep(10);
        }
        return false;
    }

    // **폴더 감시는 플랫폼이 든다(D-117·D-121).** 만들고 고치고 이름 바꾸고 지운 것이 상대경로(`/`)로 오는지, 감시를
    // 멈추면 아무것도 오지 않는지, 없는 폴더는 거짓인지 본다.
    void TestTheWindowsFileWatcherReportsChanges()
    {
        const fs::path root = fs::temp_directory_path() / L"JBroPlatformWatchProbe\uac10\uc2dc";
        fs::remove_all(root);
        fs::create_directories(root / "sub");

        JBro::WindowsPlatform platform;
        JBro::JMemoryContext memory;
        Check(platform.Initialize(memory), "the platform must initialize");
        JBro::FileEvent none[4];
        Check(platform.TakeFileEvents(none, 4) == 0, "nothing comes before watching");
        Check(false == platform.WatchDirectory(Utf8(root / "nowhere").c_str()), "a missing folder cannot be watched");
        Check(platform.WatchDirectory(Utf8(root).c_str()), "the folder is watched");
        EventLog log;

        {
            std::ofstream(root / "a.txt", std::ios::binary) << "a";
        }
        Check(WaitForEvent(platform, log, JBro::FileEventKind::Created, "a.txt"), "creating a file is reported");
        {
            std::ofstream(root / "a.txt", std::ios::binary) << "aa";
        }
        Check(WaitForEvent(platform, log, JBro::FileEventKind::Modified, "a.txt"), "writing it is reported");
        {
            std::ofstream(root / "sub" / L"\uba54\ud0dc.png", std::ios::binary) << "p";
        }
        Check(WaitForEvent(platform, log, JBro::FileEventKind::Created,
                JBro::String(Utf8(fs::path("sub") / L"\uba54\ud0dc.png")).c_str()),
            "a file in a subfolder comes with a forward slash and UTF-8");
        // 같은 폴더 안의 이름 바꾸기는 Renamed 하나다. 폴더를 건너 옮기면 Windows 는 삭제와 생성으로 알린다 -
        // 받는 쪽(엔진)이 그 둘을 레코드 제거와 다시 스캔으로 처리한다.
        std::error_code errorCode;
        fs::rename(root / "a.txt", root / "c.txt", errorCode);
        Check(false == static_cast<bool>(errorCode), "the test must be able to rename");
        Check(WaitForEvent(platform, log, JBro::FileEventKind::Renamed, "c.txt", "a.txt"),
            "a rename reports the old and the new path");
        fs::rename(root / "c.txt", root / "sub" / "c.txt", errorCode);
        Check(false == static_cast<bool>(errorCode), "the test must be able to move");
        Check(WaitForEvent(platform, log, JBro::FileEventKind::Removed, "c.txt")
                && WaitForEvent(platform, log, JBro::FileEventKind::Created, "sub/c.txt"),
            "a move across folders is a removal and a creation");
        fs::remove(root / "sub" / "c.txt", errorCode);
        Check(WaitForEvent(platform, log, JBro::FileEventKind::Removed, "sub/c.txt"), "deleting is reported");

        platform.StopWatching();
        {
            std::ofstream(root / "after.txt", std::ios::binary) << "x";
        }
        Sleep(50);
        Check(platform.TakeFileEvents(none, 4) == 0, "nothing comes after stopping");
        platform.StopWatching();
        platform.Shutdown();
        fs::remove_all(root);
    }

    // **파일 시스템은 플랫폼이 든다(D-112).** 한글 경로에서 읽기·쓰기·존재 확인·열거가 되는지, 없는 것이 거짓인지 본다.
    void TestTheWindowsFileSystemRoundTrips()
    {
        const fs::path root = fs::temp_directory_path() / L"JBroPlatformFileProbe\u00b7\ud30c\uc77c";
        fs::remove_all(root);
        fs::create_directories(root / "sub" / "deeper");
        fs::create_directories(root / "skip" / "inside");
        {
            std::ofstream(root / "skip" / "inside" / "hidden.txt", std::ios::binary) << "x";
            std::ofstream(root / "sub" / "deeper" / "c.txt", std::ios::binary) << "c";
        }

        JBro::WindowsPlatform platform;
        JBro::JMemoryContext memory;
        Check(platform.Initialize(memory), "the platform must initialize");

        const char payload[] = "\xef\xbb\xbfVersion: 1\n\x00" "binary too";
        JBro::JArrayView<std::byte> view;
        view.data = reinterpret_cast<const std::byte*>(payload);
        view.size = sizeof(payload) - 1;
        const JBro::String filePath = Utf8(root / "sub" / L"\uba54\ud0dc.jmeta");
        Check(platform.WriteWholeFile(filePath.c_str(), view), "a file writes under a Korean path");
        Check(platform.FileExists(filePath.c_str()), "and exists afterwards");
        Check(false == platform.DirectoryExists(filePath.c_str()), "a file is not a directory");
        Check(platform.DirectoryExists(Utf8(root / "sub").c_str()), "a directory is");
        Check(false == platform.FileExists(Utf8(root / "sub").c_str()), "and is not a file");

        JBro::Array<std::byte> read;
        Check(platform.ReadWholeFile(filePath.c_str(), read), "the file reads back");
        Check(read.Size() == view.size && std::memcmp(read.Data(), payload, view.size) == 0,
            "byte for byte, including the BOM and the embedded NUL");

        JBro::Array<std::byte> untouched;
        untouched.Resize(3);
        Check(false == platform.ReadWholeFile(Utf8(root / "missing.bin").c_str(), untouched), "a missing file is false");
        Check(untouched.Size() == 3, "and leaves the buffer alone");
        Check(false == platform.WriteWholeFile(Utf8(root / "nowhere" / "x.bin").c_str(), view),
            "writing into a folder that does not exist is false, not a folder creation");
        Check(false == platform.FileExists(nullptr) && false == platform.DirectoryExists(nullptr), "null is false");

        JBro::JArrayView<std::byte> empty;
        const JBro::String emptyPath = Utf8(root / "empty.bin");
        Check(platform.WriteWholeFile(emptyPath.c_str(), empty), "an empty file writes");
        Check(platform.ReadWholeFile(emptyPath.c_str(), read) && read.IsEmpty(), "and reads back empty");

        Seen seen;
        Check(platform.EnumerateDirectory(Utf8(root).c_str(), &Visit, &seen), "the folder enumerates");
        Check(Has(seen.directories, "sub") && Has(seen.directories, "sub/deeper") && Has(seen.directories, "skip"),
            "directories are reported with forward slashes relative to the root");
        Check(Has(seen.files, "sub/deeper/c.txt") && Has(seen.files, "empty.bin"), "files too");
        Check(Has(seen.files, JBro::String(Utf8(fs::path("sub") / L"\uba54\ud0dc.jmeta")).c_str()), "Korean names come back as UTF-8");
        Check(false == Has(seen.files, "skip/inside/hidden.txt") && false == Has(seen.directories, "skip/inside"),
            "a directory the visitor declined is not entered");
        Check(false == platform.EnumerateDirectory(Utf8(root / "nowhere").c_str(), &Visit, &seen), "a missing folder is false");
        Check(false == platform.EnumerateDirectory(Utf8(root).c_str(), nullptr, &seen), "no visitor is false");

        platform.Shutdown();
        fs::remove_all(root);
    }
}

int RunPlatformFileTests()
{
    TestTheWindowsFileSystemRoundTrips();
    TestTheWindowsFileWatcherReportsChanges();
    std::cout << "Platform file tests passed.\n";
    return 0;
}
