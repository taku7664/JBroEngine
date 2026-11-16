#include <JBro/Platform/WindowsPlatform.h>

#include <windows.h>
// `SHChangeNotify` 가 여기 있다. 탐색기에게 연결이 바뀌었다고 알리는 데만 쓴다.
#include <shlobj.h>

#pragma comment(lib, "Advapi32.lib")
#pragma comment(lib, "Shell32.lib")

#include <cstring>
#include <string>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Int.h>

// 확장자 연결이다(D-336). `.jproject` 를 두 번 누르면 에디터가 그 프로젝트를 연다.
// **현재 사용자의 자리(`HKEY_CURRENT_USER\Software\Classes`)에만 쓴다** - 관리자 권한을 묻지 않고, 다른 사용자의 연결을 바꾸지 않는다.
// 쓰는 것은 셋이다: `<클래스>\<ProgID>`(설명·아이콘·`shell\open\command`), `<클래스>\<확장자>` 의 기본 값, `<클래스>\<확장자>\OpenWithProgids`.
namespace JBro
{
    namespace
    {
        std::wstring WidenText(const char* utf8)
        {
            if (utf8 == nullptr || utf8[0] == '\0')
            {
                return std::wstring();
            }
            const Int32 length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8, -1, nullptr, 0);
            if (length <= 1)
            {
                return std::wstring();
            }
            std::wstring wide(static_cast<std::size_t>(length - 1), L'\0');
            MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8, -1, wide.data(), length);
            return wide;
        }

        String NarrowText(const std::wstring& wide)
        {
            if (wide.empty())
            {
                return String();
            }
            const Int32 bytes = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<Int32>(wide.size()), nullptr, 0, nullptr, nullptr);
            if (bytes <= 0)
            {
                return String();
            }
            String utf8(static_cast<std::size_t>(bytes), '\0');
            WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<Int32>(wide.size()), utf8.data(), bytes, nullptr, nullptr);
            return utf8;
        }

        // 키를 만들거나 열어 기본 값(`name` 이 널) 또는 이름 붙은 값을 글자로 쓴다.
        Bool WriteText(const std::wstring& key, const wchar_t* name, const std::wstring& value)
        {
            HKEY handle = nullptr;
            if (RegCreateKeyExW(HKEY_CURRENT_USER, key.c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, nullptr, &handle, nullptr)
                != ERROR_SUCCESS)
            {
                return false;
            }
            const DWORD bytes = static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t));
            const LSTATUS status = RegSetValueExW(handle, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()), bytes);
            RegCloseKey(handle);
            return status == ERROR_SUCCESS;
        }

        // 없으면 빈 글자다.
        std::wstring ReadText(const std::wstring& key, const wchar_t* name)
        {
            DWORD bytes = 0;
            if (RegGetValueW(HKEY_CURRENT_USER, key.c_str(), name, RRF_RT_REG_SZ, nullptr, nullptr, &bytes) != ERROR_SUCCESS || bytes == 0)
            {
                return std::wstring();
            }
            std::wstring value(bytes / sizeof(wchar_t), L'\0');
            if (RegGetValueW(HKEY_CURRENT_USER, key.c_str(), name, RRF_RT_REG_SZ, nullptr, value.data(), &bytes) != ERROR_SUCCESS)
            {
                return std::wstring();
            }
            value.resize(wcsnlen(value.c_str(), value.size()));
            return value;
        }

        // 값도 하위 키도 없는 키만 지운다.
        void DeleteKeyIfEmpty(const std::wstring& key)
        {
            HKEY handle = nullptr;
            if (RegOpenKeyExW(HKEY_CURRENT_USER, key.c_str(), 0, KEY_QUERY_VALUE, &handle) != ERROR_SUCCESS)
            {
                return;
            }
            DWORD subKeys = 0;
            DWORD values = 0;
            const LSTATUS status = RegQueryInfoKeyW(handle, nullptr, nullptr, nullptr, &subKeys, nullptr, nullptr, &values, nullptr, nullptr, nullptr, nullptr);
            RegCloseKey(handle);
            if (status == ERROR_SUCCESS && subKeys == 0 && values == 0)
            {
                RegDeleteKeyW(HKEY_CURRENT_USER, key.c_str());
            }
        }

        Bool IsUserClasses(const String& classesKey)
        {
            return _stricmp(classesKey.c_str(), "Software\\Classes") == 0;
        }

        // 확장자는 점으로 시작하고 ProgID 는 비지 않으며, 둘 다 키 이름을 넘어가지 않는다(`\` 가 없다).
        Bool IsWellFormed(const FileTypeAssociation& desc)
        {
            return desc.extension.size() > 1 && desc.extension[0] == '.' && false == desc.progId.empty()
                && false == desc.classesKey.empty() && desc.extension.find('\\') == String::npos
                && desc.progId.find('\\') == String::npos;
        }
    }

    Bool WindowsPlatform::RegisterFileType(const FileTypeAssociation& desc)
    {
        if (false == IsWellFormed(desc) || desc.command.empty())
        {
            return false;
        }
        const std::wstring classes = WidenText(desc.classesKey.c_str());
        const std::wstring progId = WidenText(desc.progId.c_str());
        const std::wstring progIdKey = classes + L"\\" + progId;
        const std::wstring extensionKey = classes + L"\\" + WidenText(desc.extension.c_str());
        // **명령을 먼저, 확장자를 나중에 쓴다.** 중간에 실패하면 확장자가 아직 옛 연결을 가리켜, 두 번 눌러 아무것도 안 뜨는 일이 없다.
        if (false == WriteText(progIdKey, nullptr, WidenText(desc.description.c_str())))
        {
            return false;
        }
        if (false == desc.icon.empty() && false == WriteText(progIdKey + L"\\DefaultIcon", nullptr, WidenText(desc.icon.c_str())))
        {
            return false;
        }
        if (false == WriteText(progIdKey + L"\\shell\\open\\command", nullptr, WidenText(desc.command.c_str())))
        {
            return false;
        }
        if (false == WriteText(extensionKey + L"\\OpenWithProgids", progId.c_str(), std::wstring()))
        {
            return false;
        }
        if (false == WriteText(extensionKey, nullptr, progId))
        {
            return false;
        }
        if (IsUserClasses(desc.classesKey))
        {
            // 탐색기가 아이콘과 연결을 다시 읽게 한다. 알리지 않으면 로그오프 전까지 옛 아이콘이 보인다.
            SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
        }
        return true;
    }

    Bool WindowsPlatform::UnregisterFileType(const FileTypeAssociation& desc)
    {
        if (false == IsWellFormed(desc))
        {
            return false;
        }
        const std::wstring classes = WidenText(desc.classesKey.c_str());
        const std::wstring progId = WidenText(desc.progId.c_str());
        const std::wstring extensionKey = classes + L"\\" + WidenText(desc.extension.c_str());
        // 확장자의 기본 값은 이 연결을 가리킬 때만 지운다 - 사람이 다른 프로그램으로 바꿔 두었으면 그것을 지키는 것이 맞다.
        if (ReadText(extensionKey, nullptr) == progId)
        {
            HKEY handle = nullptr;
            if (RegOpenKeyExW(HKEY_CURRENT_USER, extensionKey.c_str(), 0, KEY_SET_VALUE, &handle) == ERROR_SUCCESS)
            {
                RegDeleteValueW(handle, nullptr);
                RegCloseKey(handle);
            }
        }
        HKEY openWith = nullptr;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, (extensionKey + L"\\OpenWithProgids").c_str(), 0, KEY_SET_VALUE, &openWith) == ERROR_SUCCESS)
        {
            RegDeleteValueW(openWith, progId.c_str());
            RegCloseKey(openWith);
        }
        // 비어 버린 확장자 키는 남기지 않는다. **`RegDeleteKeyW` 는 값이 남은 키도 지우므로**(하위 키만 막는다) 값도 하위 키도 없을 때만 지운다 -
        // 그러지 않으면 다른 프로그램이 걸어 둔 기본 값까지 지워졌다(시험이 잡았다).
        DeleteKeyIfEmpty(extensionKey + L"\\OpenWithProgids");
        DeleteKeyIfEmpty(extensionKey);
        const LSTATUS status = RegDeleteTreeW(HKEY_CURRENT_USER, (classes + L"\\" + progId).c_str());
        if (IsUserClasses(desc.classesKey))
        {
            SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
        }
        return status == ERROR_SUCCESS || status == ERROR_FILE_NOT_FOUND;
    }

    String WindowsPlatform::ReadFileTypeCommand(const char* classesKey, const char* extension) const
    {
        if (classesKey == nullptr || extension == nullptr || extension[0] != '.')
        {
            return String();
        }
        const std::wstring classes = WidenText(classesKey);
        const std::wstring progId = ReadText(classes + L"\\" + WidenText(extension), nullptr);
        if (progId.empty())
        {
            return String();
        }
        return NarrowText(ReadText(classes + L"\\" + progId + L"\\shell\\open\\command", nullptr));
    }

    String WindowsPlatform::GetExecutablePath() const
    {
        // `GetExecutableFolder` 와 같이 경로 길이를 정해 두지 않는다.
        std::wstring buffer(512, L'\0');
        for (Int32 attempt = 0; attempt < 5; ++attempt)
        {
            const DWORD written = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
            if (written == 0)
            {
                return String();
            }
            if (written < buffer.size())
            {
                buffer.resize(written);
                return NarrowText(buffer);
            }
            buffer.resize(buffer.size() * 2);
        }
        return String();
    }
}
