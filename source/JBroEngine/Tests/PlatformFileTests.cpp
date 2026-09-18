#include <JBro/Platform/WindowsPlatform.h>

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
    std::cout << "Platform file tests passed.\n";
    return 0;
}
