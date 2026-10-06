#include <JBro/Host/SaveStorage.h>

#include <JBro/Core/Log.h>
#include <JBro/Platform/Platform.h>
#include <JBro/Types/Array.h>

#include <cstring>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Int.h>
#include <JBro/Types/UInt.h>

namespace JBro
{
    namespace
    {
        constexpr const char* UnnamedProduct = "JBroEngine-Unnamed";
        // 쓰는 중인 옆 파일의 꼬리다. 슬롯 이름이 이것으로 끝나면 거절한다 - 남의 쓰기와 겹친다.
        constexpr const char* WritingSuffix = ".writing";
        // 슬롯 이름의 바이트 상한이다. 경로 전체가 Windows 의 짧은 경로 한도에 닿지 않게 한다.
        constexpr std::size_t MaxSlotBytes = 128;

        Bool EndsWith(const char* text, std::size_t length, const char* suffix)
        {
            const std::size_t suffixLength = std::strlen(suffix);
            return length >= suffixLength && std::memcmp(text + length - suffixLength, suffix, suffixLength) == 0;
        }

        char Upper(char c)
        {
            return (c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : c;
        }

        // `CON`·`NUL`·`COM1` 따위는 확장자를 붙여도(`nul.txt`) 장치다. 쓰면 오류 없이 사라진다.
        Bool IsReservedDeviceName(const char* slot, std::size_t length)
        {
            std::size_t stem = 0;
            while (stem < length && slot[stem] != '.')
            {
                ++stem;
            }
            // 장치 이름 뒤의 공백도 같은 이름으로 본다(`NUL .txt`).
            while (stem > 0 && slot[stem - 1] == ' ')
            {
                --stem;
            }
            const char* const plain[] = {"CON", "PRN", "AUX", "NUL"};
            for (const char* name : plain)
            {
                if (stem == 3 && Upper(slot[0]) == name[0] && Upper(slot[1]) == name[1] && Upper(slot[2]) == name[2])
                {
                    return true;
                }
            }
            if (stem == 4 && slot[3] >= '1' && slot[3] <= '9')
            {
                const char a = Upper(slot[0]);
                const char b = Upper(slot[1]);
                const char c = Upper(slot[2]);
                if ((a == 'C' && b == 'O' && c == 'M') || (a == 'L' && b == 'P' && c == 'T'))
                {
                    return true;
                }
            }
            return false;
        }
    }

    SaveStorage::SaveStorage(IPlatform& platform)
        : m_platform(platform)
    {
    }

    Bool SaveStorage::Open(const char* folder)
    {
        Close();
        if (folder == nullptr || folder[0] == '\0')
        {
            Log::Write(LogLevel::Error, "save", "the save storage has no folder; saves are off");
            return false;
        }
        // 폴더는 처음 쓸 때 만든다. 세이브를 쓰지 않는 게임(과 프로젝트를 여는 시험)이 사용자 폴더에 빈 폴더를 남기지 않는다.
        m_folder = folder;
        m_ready = true;
        return true;
    }

    void SaveStorage::Close()
    {
        if (m_ready)
        {
            Flush();
        }
        m_ready = false;
        m_folder.clear();
    }

    const String& SaveStorage::GetFolder() const
    {
        return m_folder;
    }

    String SaveStorage::MakeFolder(const char* userDataFolder, const char* productName, Bool editor)
    {
        if (userDataFolder == nullptr || userDataFolder[0] == '\0')
        {
            return String();
        }
        // 사람이 설정 창에 적는 값이라 공백·기호가 섞인다. 경로에 그대로 붙이면 플랫폼마다 다르게 깨진다(기존 `SanitizeProductName`).
        // 한글 같은 UTF-8 바이트는 둔다 - 파일 이름에 쓸 수 있다.
        // 빈 이름은 다듬은 뒤(끝 공백만인 이름도 비게 된다) 한 번에 기본 이름으로 바꾼다.
        String product = productName != nullptr ? String(productName) : String();
        for (char& c : product)
        {
            const unsigned char byte = static_cast<unsigned char>(c);
            const Bool allowed = byte >= 0x80 || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
                || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == ' ';
            if (false == allowed)
            {
                c = '_';
            }
        }
        // 끝의 공백은 Windows 가 지워 다른 폴더가 된다.
        while (false == product.empty() && product.back() == ' ')
        {
            product.pop_back();
        }
        if (product.empty())
        {
            product = UnnamedProduct;
        }
        String folder(userDataFolder);
        if (folder.back() != '/' && folder.back() != '\\')
        {
            folder += '/';
        }
        folder += product;
        folder += editor ? "/EditorSaves" : "/Saves";
        return folder;
    }

    Bool SaveStorage::IsValidSlotName(const char* slot)
    {
        if (slot == nullptr || slot[0] == '\0')
        {
            return false;
        }
        const std::size_t length = std::strlen(slot);
        if (length > MaxSlotBytes)
        {
            return false;
        }
        for (std::size_t index = 0; index < length; ++index)
        {
            const unsigned char byte = static_cast<unsigned char>(slot[index]);
            if (byte < 0x20 || byte == 0x7F)
            {
                return false;
            }
            if (std::strchr("<>:\"/\\|?*", static_cast<char>(byte)) != nullptr)
            {
                return false;
            }
        }
        if (std::strstr(slot, "..") != nullptr)
        {
            return false;
        }
        // 앞뒤의 공백과 끝의 점은 Windows 가 지워 다른 파일이 된다(`a.` 와 `a` 가 같다).
        if (slot[0] == ' ' || slot[length - 1] == ' ' || slot[length - 1] == '.')
        {
            return false;
        }
        if (EndsWith(slot, length, WritingSuffix))
        {
            return false;
        }
        return false == IsReservedDeviceName(slot, length);
    }

    String SaveStorage::ResolveSlot(const char* slot) const
    {
        if (false == m_ready)
        {
            return String();
        }
        if (false == IsValidSlotName(slot))
        {
            // 스크립트가 넘긴 값이라 믿지 않는다. 잘못 적은 이름은 매번 말한다 - 저장이 조용히 사라지면 안 된다(콜드 경로).
            Log::Write(LogLevel::Warning, "save", "\"%s\" is not a save slot name; use a plain file name like slot0.yaml",
                slot != nullptr ? slot : "");
            return String();
        }
        String path = m_folder;
        path += '/';
        path += slot;
        return path;
    }

    Bool SaveStorage::IsReady() const noexcept
    {
        return m_ready;
    }

    Bool SaveStorage::Write(const char* slot, const void* data, std::size_t size) noexcept
    {
        const String path = ResolveSlot(slot);
        // 플랫폼의 쓰기는 32 비트 길이를 받는다. 4 GiB 를 넘는 세이브는 쓰지 않는다.
        if (path.empty() || (data == nullptr && size > 0) || size > 0xFFFFFFFFu)
        {
            return false;
        }
        if (false == m_platform.CreateDirectoryAt(m_folder.c_str()))
        {
            Log::Write(LogLevel::Error, "save", "the save folder \"%s\" could not be made", m_folder.c_str());
            return false;
        }
        String writing = path;
        writing += WritingSuffix;
        const JArrayView<std::byte> bytes(static_cast<const std::byte*>(data), static_cast<JBro::UInt32>(size));
        if (false == m_platform.WriteWholeFile(writing.c_str(), bytes))
        {
            // 디스크가 찼거나 권한이 없다. 반쯤 쓴 옆 파일은 지우고, 원래 세이브는 그대로다.
            m_platform.DeleteFileAt(writing.c_str());
            Log::Write(LogLevel::Error, "save", "the save slot \"%s\" could not be written", slot);
            return false;
        }
        if (false == m_platform.MoveFileTo(writing.c_str(), path.c_str()))
        {
            m_platform.DeleteFileAt(writing.c_str());
            Log::Write(LogLevel::Error, "save", "the save slot \"%s\" could not be replaced", slot);
            return false;
        }
        return true;
    }

    Bool SaveStorage::GetSize(const char* slot, std::size_t& outSize) const noexcept
    {
        outSize = 0;
        const String path = ResolveSlot(slot);
        if (path.empty() || false == m_platform.FileExists(path.c_str()))
        {
            return false;
        }
        const OwnerPtr<IFileStream> stream = m_platform.OpenFileStream(path.c_str());
        if (stream.Get() == nullptr)
        {
            return false;
        }
        const Int64 size = stream->GetSize();
        if (size < 0)
        {
            return false;
        }
        outSize = static_cast<std::size_t>(size);
        return true;
    }

    Bool SaveStorage::Read(const char* slot, void* buffer, std::size_t capacity, std::size_t& outSize) const noexcept
    {
        outSize = 0;
        const String path = ResolveSlot(slot);
        if (path.empty() || false == m_platform.FileExists(path.c_str()))
        {
            return false;
        }
        const OwnerPtr<IFileStream> stream = m_platform.OpenFileStream(path.c_str());
        if (stream.Get() == nullptr)
        {
            return false;
        }
        const Int64 size = stream->GetSize();
        if (size < 0 || static_cast<JBro::UInt64>(size) > capacity || (buffer == nullptr && size > 0))
        {
            return false;
        }
        std::size_t read = 0;
        while (read < static_cast<std::size_t>(size))
        {
            const std::size_t got = stream->Read(static_cast<std::byte*>(buffer) + read, static_cast<std::size_t>(size) - read);
            if (got == 0)
            {
                return false;
            }
            read += got;
        }
        outSize = read;
        return true;
    }

    Bool SaveStorage::Exists(const char* slot) const noexcept
    {
        const String path = ResolveSlot(slot);
        return false == path.empty() && m_platform.FileExists(path.c_str());
    }

    Bool SaveStorage::Remove(const char* slot) noexcept
    {
        const String path = ResolveSlot(slot);
        if (path.empty())
        {
            return false;
        }
        if (false == m_platform.FileExists(path.c_str()))
        {
            return true;
        }
        return m_platform.DeleteFileAt(path.c_str());
    }

    Bool SaveStorage::Flush() noexcept
    {
        // 데스크톱은 `MoveFileTo` 가 쓰기를 디스크까지 밀었다(`MOVEFILE_WRITE_THROUGH`). 웹이 서면 여기서 IndexedDB 로 넘긴다.
        return m_ready;
    }
}
