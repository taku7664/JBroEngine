#include <JBro/Platform/WindowsPlatform.h>

#include <windows.h>
// `ShellExecuteW` 가 여기 있다. 탐색기에서 보여 주는 데만 쓴다.
#include <shellapi.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string_view>
#include <system_error>

// Windows 의 파일 시스템이다(D-112). `fopen` 류는 UTF-8 경로를 ANSI 로 읽어 한글 폴더에서 조용히 실패하므로
// 경로는 `filesystem::path` 의 UTF-8 생성자를 거쳐 와이드로 넘긴다.
namespace JBro
{
    namespace
    {
        namespace fs = std::filesystem;

        // UTF-8 이 아닌 바이트(ANSI 로 온 경로)는 변환이 던진다. 그것은 "없는 파일" 이지 예외가 아니다 - 빈 경로로
        // 돌려 모든 함수가 거짓을 돌려주게 한다.
        fs::path ToPath(const char* utf8)
        {
            if (utf8 == nullptr)
            {
                return {};
            }
            try
            {
                const std::string_view view(utf8);
                return fs::path(std::u8string_view(reinterpret_cast<const char8_t*>(view.data()), view.size()));
            }
            catch (...)
            {
                return {};
            }
        }

        String ToUtf8(const fs::path& path)
        {
            const std::u8string text = path.generic_u8string();
            return String(reinterpret_cast<const char*>(text.data()), text.size());
        }

        bool Walk(const fs::path& root, const fs::path& directory, DirectoryVisitor visitor, void* user)
        {
            std::error_code errorCode;
            fs::directory_iterator iterator(directory, fs::directory_options::skip_permission_denied, errorCode);
            if (errorCode)
            {
                return false;
            }
            const fs::directory_iterator end;
            for (; iterator != end; iterator.increment(errorCode))
            {
                if (errorCode)
                {
                    return false;
                }
                const fs::directory_entry& entry = *iterator;
                const String relative = ToUtf8(fs::relative(entry.path(), root, errorCode));
                if (entry.is_directory(errorCode))
                {
                    if (visitor(relative.c_str(), true, user))
                    {
                        if (false == Walk(root, entry.path(), visitor, user))
                        {
                            return false;
                        }
                    }
                }
                else if (entry.is_regular_file(errorCode))
                {
                    visitor(relative.c_str(), false, user);
                }
            }
            return true;
        }
    }

    bool WindowsPlatform::ReadWholeFile(const char* utf8Path, Array<std::byte>& contents)
    {
        std::ifstream file(ToPath(utf8Path), std::ios::binary | std::ios::ate);
        if (false == file.is_open())
        {
            return false;
        }
        const std::streamoff size = file.tellg();
        if (size < 0)
        {
            return false;
        }
        file.seekg(0, std::ios::beg);
        Array<std::byte> read;
        read.Resize(static_cast<std::size_t>(size));
        if (size != 0 && false == static_cast<bool>(file.read(reinterpret_cast<char*>(read.Data()), size)))
        {
            return false;
        }
        contents = std::move(read);
        return true;
    }

    bool WindowsPlatform::WriteWholeFile(const char* utf8Path, JArrayView<std::byte> contents)
    {
        std::ofstream file(ToPath(utf8Path), std::ios::binary | std::ios::trunc);
        if (false == file.is_open())
        {
            return false;
        }
        if (contents.size != 0)
        {
            file.write(reinterpret_cast<const char*>(contents.data), static_cast<std::streamsize>(contents.size));
        }
        return file.good();
    }

    bool WindowsPlatform::MoveFileTo(const char* fromUtf8Path, const char* toUtf8Path)
    {
        const fs::path from = ToPath(fromUtf8Path);
        const fs::path to = ToPath(toUtf8Path);
        if (from.empty() || to.empty())
        {
            return false;
        }
        return MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
    }

    bool WindowsPlatform::CreateDirectoryAt(const char* utf8Path)
    {
        const fs::path path = ToPath(utf8Path);
        if (path.empty())
        {
            return false;
        }
        std::error_code errorCode;
        // 이미 있으면 `create_directories` 는 거짓을 돌려주되 오류는 아니다.
        // 있는 것도 만든 것과 같게 본다 - 부르는 쪽이 원하는 것은 "그 폴더가 있는 상태" 다.
        fs::create_directories(path, errorCode);
        return (false == static_cast<bool>(errorCode)) && fs::is_directory(path, errorCode);
    }

    bool WindowsPlatform::DeleteFileAt(const char* utf8Path)
    {
        const fs::path path = ToPath(utf8Path);
        std::error_code errorCode;
        if (path.empty() || false == fs::is_regular_file(path, errorCode))
        {
            return false;
        }
        return fs::remove(path, errorCode) && (false == static_cast<bool>(errorCode));
    }

    bool WindowsPlatform::DeleteDirectoryAt(const char* utf8Path)
    {
        const fs::path path = ToPath(utf8Path);
        std::error_code errorCode;
        if (path.empty() || false == fs::is_directory(path, errorCode))
        {
            return false;
        }
        fs::remove_all(path, errorCode);
        return false == static_cast<bool>(errorCode);
    }

    String WindowsPlatform::GetExecutableFolder() const
    {
        // **경로 길이를 정해 두지 않는다.** `MAX_PATH` 로 잘라 두면 깊은 폴더에 설치한 사람의
        // 에디터가 글자 표를 못 찾는다 - 모자라면 버퍼를 늘려 다시 묻는다.
        std::wstring buffer(512, L'\0');
        for (int attempt = 0; attempt < 5; ++attempt)
        {
            const DWORD written = GetModuleFileNameW(
                nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
            if (written == 0)
            {
                return String();
            }
            if (written < buffer.size())
            {
                buffer.resize(written);
                break;
            }
            buffer.resize(buffer.size() * 2);
        }
        std::error_code errorCode;
        const fs::path folder = fs::path(buffer).parent_path();
        if (folder.empty())
        {
            return String();
        }
        const std::string utf8 = folder.generic_u8string().empty()
            ? std::string()
            : std::string(reinterpret_cast<const char*>(folder.generic_u8string().c_str()));
        (void)errorCode;
        return String(utf8.c_str());
    }

    bool WindowsPlatform::GetFileWriteTime(const char* utf8Path, std::int64_t& outUnixSeconds) const
    {
        outUnixSeconds = 0;
        const fs::path path = ToPath(utf8Path);
        if (path.empty())
        {
            return false;
        }
        std::error_code errorCode;
        const fs::file_time_type written = fs::last_write_time(path, errorCode);
        if (errorCode)
        {
            return false;
        }
        // 파일 시계를 벽시계로 옮겨 유닉스 초로 센다. 표시와 비교만 하므로 초면 넉넉하다.
        const auto wall = std::chrono::clock_cast<std::chrono::system_clock>(written);
        outUnixSeconds = std::chrono::duration_cast<std::chrono::seconds>(
            wall.time_since_epoch()).count();
        return true;
    }

    bool WindowsPlatform::OpenPathWithShell(const char* utf8Path)
    {
        const fs::path path = ToPath(utf8Path);
        if (path.empty())
        {
            return false;
        }
        // 확장자에 맞는 프로그램이 없으면 실패한다. 그때는 부르는 쪽이 탐색기로 보여 준다.
        const HINSTANCE result = ShellExecuteW(
            nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        return reinterpret_cast<INT_PTR>(result) > 32;
    }

    bool WindowsPlatform::RevealInFileBrowser(const char* utf8Path)
    {
        const fs::path path = ToPath(utf8Path);
        std::error_code errorCode;
        if (path.empty())
        {
            return false;
        }
        // 파일이면 그 파일을 고른 채로, 폴더면 그 폴더를 연다.
        const bool isFile = fs::is_regular_file(path, errorCode);
        std::wstring parameters;
        if (isFile)
        {
            parameters = L"/select,\"";
            parameters += path.wstring();
            parameters += L"\"";
        }
        else
        {
            parameters = L"\"";
            parameters += path.wstring();
            parameters += L"\"";
        }
        // `ShellExecuteW` 의 성공은 32 보다 큰 값이다(옛 API 의 규약이다).
        const HINSTANCE result = ShellExecuteW(
            nullptr, L"open", L"explorer.exe", parameters.c_str(), nullptr, SW_SHOWNORMAL);
        return reinterpret_cast<INT_PTR>(result) > 32;
    }

    bool WindowsPlatform::FileExists(const char* utf8Path) const
    {
        std::error_code errorCode;
        return fs::is_regular_file(ToPath(utf8Path), errorCode);
    }

    bool WindowsPlatform::DirectoryExists(const char* utf8Path) const
    {
        std::error_code errorCode;
        return fs::is_directory(ToPath(utf8Path), errorCode);
    }

    bool WindowsPlatform::EnumerateDirectory(const char* utf8Root, DirectoryVisitor visitor, void* user)
    {
        if (visitor == nullptr)
        {
            return false;
        }
        const fs::path root = ToPath(utf8Root);
        std::error_code errorCode;
        if (false == fs::is_directory(root, errorCode))
        {
            return false;
        }
        return Walk(root, root, visitor, user);
    }
}
