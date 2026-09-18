#include <JBro/Platform/WindowsPlatform.h>

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
