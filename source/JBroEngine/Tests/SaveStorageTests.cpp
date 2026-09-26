#include <JBro/Core/Log.h>
#include <JBro/Host/SaveStorage.h>
#include <JBro/Platform/Platform.h>
#include <JBro/Platform/WindowsPlatform.h>
#include <JBro/SaveTypes/Internal/SystemContext.h>
#include <JBro/SaveTypes/Service/SaveService.h>

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <stdexcept>

// 세이브 저장소(D-218). 기존 엔진 `CSaveStorage` 를 옮기며 고친 것(바꿔 넣는 쓰기·예약 이름·DLL 힙)을 잰다.
namespace
{
    namespace fs = std::filesystem;
    using namespace JBro;

    void Check(bool condition, const char* message)
    {
        if (false == condition)
        {
            std::cout << "test failure: " << message << '\n';
            throw std::runtime_error(message);
        }
    }

    String Utf8(const fs::path& path)
    {
        const std::u8string text = path.generic_u8string();
        return String(reinterpret_cast<const char*>(text.data()), text.size());
    }

    fs::path FromUtf8(const String& text)
    {
        return fs::path(std::u8string(reinterpret_cast<const char8_t*>(text.c_str()), text.size()));
    }

    std::size_t CountWarnings(const char* needle)
    {
        std::size_t count = 0;
        for (std::size_t index = 0; index < Log::GetCount(); ++index)
        {
            const LogEntry* entry = Log::GetAt(index);
            if (entry != nullptr && std::strstr(entry->message, needle) != nullptr)
            {
                ++count;
            }
        }
        return count;
    }

    // 파일 일은 실제 플랫폼에 넘기되 쓰기와 바꿔 넣기를 일부러 실패시킨다. 쓰는 중에 꺼진 것과 다른 프로그램이 파일을 줄인 것을 흉내 낸다.
    class FlakyFilePlatform final : public IPlatform
    {
    public:
        bool Initialize(const JMemoryContext&) override
        {
            return true;
        }

        void Shutdown() override
        {
        }

        WindowHandle OpenPlatformWindow(const WindowDesc&) override
        {
            return {};
        }

        void ClosePlatformWindow(WindowHandle) override
        {
        }

        SurfaceHandle CreateSurface(WindowHandle) override
        {
            return {};
        }

        void PumpEvents() override
        {
        }

        JArrayView<InputEvent> GetInputEvents() const override
        {
            return {};
        }

        void WaitForEvents(std::uint32_t) override
        {
        }

        bool ShouldClose(WindowHandle) const override
        {
            return false;
        }

        bool GetWindowState(WindowHandle, WindowState&) const override
        {
            return false;
        }

        DynamicLibrary LoadDynamicLibrary(const char*) override
        {
            return {};
        }

        void* GetSymbol(DynamicLibrary, const char*) override
        {
            return nullptr;
        }

        void UnloadDynamicLibrary(DynamicLibrary) override
        {
        }

        bool WriteWholeFile(const char* path, JArrayView<std::byte> contents) override
        {
            if (failWriteHalfway)
            {
                // 앞 절반만 쓰고 꺼진다.
                inner.WriteWholeFile(path, {contents.data, contents.size / 2});
                return false;
            }
            return inner.WriteWholeFile(path, contents);
        }

        bool MoveFileTo(const char* from, const char* to) override
        {
            return false == failMove && inner.MoveFileTo(from, to);
        }

        bool CreateDirectoryAt(const char* path) override
        {
            return inner.CreateDirectoryAt(path);
        }

        bool DeleteFileAt(const char* path) override
        {
            return inner.DeleteFileAt(path);
        }

        bool FileExists(const char* path) const override
        {
            return inner.FileExists(path);
        }

        OwnerPtr<IFileStream> OpenFileStream(const char* path) override
        {
            ++opens;
            // 크기를 물은 뒤(첫 열기) 다른 프로그램이 파일을 줄였다.
            if (shrinkAfterFirstOpen && opens == 2)
            {
                const char tail[] = "ab";
                inner.WriteWholeFile(path, {reinterpret_cast<const std::byte*>(tail), 2});
            }
            return inner.OpenFileStream(path);
        }

        WindowsPlatform inner;
        bool failWriteHalfway = false;
        bool failMove = false;
        bool shrinkAfterFirstOpen = false;
        std::uint32_t opens = 0;
    };

    void TestAFailedWriteLeavesTheOldSaveAndNoSideFile()
    {
        FlakyFilePlatform platform;
        const fs::path root = fs::temp_directory_path() / u8"JBroSaveFlakyProbe";
        std::error_code error;
        fs::remove_all(root, error);
        SaveStorage storage(platform);
        Check(storage.Open(Utf8(root).c_str()), "the flaky folder opens");
        BindSaveSystemContext({SaveSystemContextAbiVersion, &storage});
        const Service::SaveService save;
        Check(save.WriteText("slot0.yaml", String("level: 3\n")), "the first save goes through");

        platform.failWriteHalfway = true;
        Check(false == save.WriteText("slot0.yaml", String("level: 4 and a much longer line\n")), "a write cut off halfway fails");
        String text;
        Check(save.ReadText("slot0.yaml", text) && text == "level: 3\n", "the old save is whole");
        Check(false == fs::exists(root / u8"slot0.yaml.writing", error), "and the half-written side file is gone");
        platform.failWriteHalfway = false;

        platform.failMove = true;
        Check(false == save.WriteText("slot0.yaml", String("level: 5\n")), "a swap that fails fails the write");
        Check(save.ReadText("slot0.yaml", text) && text == "level: 3\n", "the old save is still whole");
        Check(false == fs::exists(root / u8"slot0.yaml.writing", error), "and the finished side file is gone too");
        platform.failMove = false;

        // 실패한 읽기는 들고 있던 것을 남기지 않는다.
        Array<std::byte> bytes;
        bytes.Resize(5);
        Check(false == save.ReadBytes("missing.bin", bytes) && bytes.IsEmpty(), "a failed read leaves nothing behind");

        // 크기를 물은 뒤 파일이 줄었으면 읽은 만큼만 준다.
        Check(save.WriteText("shrink.txt", String("0123456789")), "a longer file is written");
        platform.opens = 0;
        platform.shrinkAfterFirstOpen = true;
        Check(save.ReadText("shrink.txt", text) && text == "ab", "a file that shrank between the two calls reads as what is there");
        platform.shrinkAfterFirstOpen = false;

        storage.Close();
        BindSaveSystemContext({});
        fs::remove_all(root, error);
    }

    void TestSlotNamesStayInsideTheFolder()
    {
        const char* good[] = {"slot0.yaml", "options", "a b.txt", "CONSOLE.txt", "COM0", "COMA", "nul_", ".hidden",
            "\xEC\x84\xB8\xEC\x9D\xB4\xEB\xB8\x8C 1"};
        for (const char* slot : good)
        {
            Check(SaveStorage::IsValidSlotName(slot), "a plain file name is a slot");
        }
        const char* bad[] = {"", "../escape", "a/b", "a\\b", "C:x", "x..y", "CON", "nul.txt", "Com3", "LPT9.dat", "aux .log",
            "a.", " a", "a ", "slot.writing", "a\tb", "a*b", "a?b", "a|b", "a<b", "a>b", "a\"b"};
        for (const char* slot : bad)
        {
            Check(false == SaveStorage::IsValidSlotName(slot), "a name that leaves the folder or names a device is refused");
        }
        Check(false == SaveStorage::IsValidSlotName(nullptr), "no name is refused");
        const String longest(128, 'a');
        Check(SaveStorage::IsValidSlotName(longest.c_str()), "128 bytes is a slot");
        const String tooLong(129, 'a');
        Check(false == SaveStorage::IsValidSlotName(tooLong.c_str()), "129 is not");
    }

    void TestTheFolderIsNamedAfterTheProduct()
    {
        Check(SaveStorage::MakeFolder("C:/Data", "My Game!", false) == "C:/Data/My Game_/Saves", "symbols become underscores");
        Check(SaveStorage::MakeFolder("C:/Data/", "My Game", true) == "C:/Data/My Game/EditorSaves",
            "the editor's play writes beside the game's saves, not over them");
        Check(SaveStorage::MakeFolder("C:/Data", "", false) == "C:/Data/JBroEngine-Unnamed/Saves", "no product name has a name");
        Check(SaveStorage::MakeFolder("C:/Data", "Game  ", false) == "C:/Data/Game/Saves", "a trailing space is dropped");
        Check(SaveStorage::MakeFolder("C:/Data", "\xEA\xB2\x8C\xEC\x9E\x84", false) == "C:/Data/\xEA\xB2\x8C\xEC\x9E\x84/Saves",
            "a Korean name stays");
        Check(SaveStorage::MakeFolder("", "Game", false).empty(), "no user folder, no save folder");
    }

    void TestThePlatformFindsTheUserFolder()
    {
        WindowsPlatform platform;
        const String folder = platform.GetUserDataFolder();
        Check(false == folder.empty(), "Windows has a local app data folder");
        std::error_code error;
        Check(fs::is_directory(FromUtf8(folder), error), "and it is a folder");
        wchar_t* env = nullptr;
        std::size_t length = 0;
        if (_wdupenv_s(&env, &length, L"LOCALAPPDATA") == 0 && env != nullptr)
        {
            Check(fs::equivalent(fs::path(env), FromUtf8(folder), error), "it is the folder LOCALAPPDATA names");
            std::free(env);
        }
    }

    void TestSavesRoundTripAndReplaceWhole()
    {
        Log::Clear();
        WindowsPlatform platform;
        const fs::path root = fs::temp_directory_path() / u8"JBroSaveProbe" / u8"\uC138\uC774\uBE0C";
        std::error_code error;
        fs::remove_all(root.parent_path(), error);

        SaveStorage storage(platform);
        Check(false == storage.IsReady(), "a storage that is not open is not ready");
        Check(false == storage.Open(""), "an empty folder is refused");
        Check(storage.Open(Utf8(root).c_str()), "a folder opens");
        Log::Clear();
        Check(false == fs::exists(root, error), "opening does not make the folder - a game that never saves leaves nothing");
        BindSaveSystemContext({SaveSystemContextAbiVersion, &storage});
        const Service::SaveService save;
        Check(save.IsReady(), "the service sees the bound storage");

        String text;
        Check(false == save.ReadText("options.yaml", text) && text.empty(), "a slot never written reads as nothing");
        std::size_t size = 7;
        Check(false == storage.GetSize("options.yaml", size) && size == 0, "and has no size");
        Check(CountWarnings("save") == 0, "a missing slot is the first run, not an error");

        Check(save.WriteText("options.yaml", String("volume: 0.8\nlanguage: ko-KR\n")), "a slot writes");
        Check(fs::is_directory(root, error), "the first write makes the folder, under a Korean path");
        Check(save.Exists("options.yaml"), "and the slot is there");
        Check(save.ReadText("options.yaml", text) && text == "volume: 0.8\nlanguage: ko-KR\n", "it reads back whole");

        Check(save.WriteText("options.yaml", String("v: 1\n")), "a shorter write replaces it");
        Check(save.ReadText("options.yaml", text) && text == "v: 1\n", "whole, with nothing of the longer one left");
        Check(false == fs::exists(root / u8"options.yaml.writing", error), "the file written beside it is gone");

        char small[2] = {};
        std::size_t read = 0;
        Check(false == storage.Read("options.yaml", small, sizeof(small), read) && read == 0,
            "a buffer too small is refused, not overrun");

        const unsigned char binary[] = {0, 1, 2, 0xFF, 0};
        Check(save.WriteBytes("state.bin", binary, sizeof(binary)), "bytes write");
        Array<std::byte> bytes;
        Check(save.ReadBytes("state.bin", bytes) && bytes.Size() == sizeof(binary)
                && std::memcmp(bytes.Data(), binary, sizeof(binary)) == 0, "and read back with their zeros");
        Check(save.WriteBytes("empty.bin", nullptr, 0), "an empty slot writes");
        Check(save.ReadBytes("empty.bin", bytes) && bytes.IsEmpty(), "and reads back empty");

        // 옆 파일을 쓸 수 없게 막는다(같은 이름의 폴더). 쓰기가 실패해도 옛 세이브는 그대로다.
        fs::create_directories(root / u8"options.yaml.writing", error);
        Check(false == save.WriteText("options.yaml", String("broken")), "a write that cannot finish fails");
        Check(save.ReadText("options.yaml", text) && text == "v: 1\n", "and the save it would have replaced survives");
        fs::remove_all(root / u8"options.yaml.writing", error);

        Check(false == save.WriteText("../escape.txt", String("x")), "a slot outside the folder is refused");
        Check(false == fs::exists(root.parent_path() / u8"escape.txt", error), "and nothing is written there");
        Check(CountWarnings("is not a save slot name") == 1, "and the script hears why");
        Check(false == save.WriteText("NUL", String("x")), "a device name is refused");

        Check(save.Remove("state.bin") && false == save.Exists("state.bin"), "a slot is removed");
        Check(save.Remove("state.bin"), "removing a slot that is gone is what the caller wanted");
        Check(save.Flush(), "flush on the desktop succeeds");

        storage.Close();
        Check(false == save.IsReady() && false == save.Exists("options.yaml"), "a closed storage answers nothing");
        BindSaveSystemContext({});
        Check(false == save.IsReady() && false == save.WriteText("x", String("y")), "an unbound service fails quietly");
        fs::remove_all(root.parent_path(), error);
    }
}

int RunSaveStorageTests()
{
    const bool echo = Log::GetEchoToConsole();
    Log::SetEchoToConsole(false);
    TestSlotNamesStayInsideTheFolder();
    TestTheFolderIsNamedAfterTheProduct();
    TestThePlatformFindsTheUserFolder();
    TestSavesRoundTripAndReplaceWhole();
    TestAFailedWriteLeavesTheOldSaveAndNoSideFile();
    Log::SetEchoToConsole(echo);
    std::cout << "Save storage tests passed.\n";
    return 0;
}
