#include <JBro/Editor/Localization.h>

#include <JBro/Core/Yaml.h>
#include <JBro/Platform/Platform.h>

#include <cstdio>
#include <string>

namespace JBro
{
    LocalizationTable& LocalizationTable::Get()
    {
        static LocalizationTable table;
        return table;
    }

    bool LocalizationTable::LoadFile(IPlatform& platform, const char* directory, const char* locale,
        Table<String, String>& out) const
    {
        if (directory == nullptr || locale == nullptr)
        {
            return false;
        }
        String path(directory);
        if (false == path.empty() && path.back() != '/' && path.back() != '\\')
        {
            path.push_back('/');
        }
        path.append(locale);
        path.append(".yaml");
        Array<std::byte> bytes;
        if (false == platform.ReadWholeFile(path.c_str(), bytes))
        {
            return false;
        }
        const std::string text(reinterpret_cast<const char*>(bytes.Data()), bytes.Size());

        YamlDocument document;
        YamlError error;
        if (false == document.Parse(text.c_str(), text.size(), error))
        {
            return false;
        }
        const std::uint32_t entries = document.Find(document.GetRoot(), "Entries");
        if (entries == 0 || document.GetKind(entries) != YamlKind::Map)
        {
            return false;
        }
        const std::size_t count = document.GetCount(entries);
        for (std::size_t index = 0; index < count; ++index)
        {
            const char* key = document.GetKey(entries, index);
            const std::uint32_t value = document.GetValue(entries, index);
            if (key == nullptr || value == 0)
            {
                continue;
            }
            const char* text_ = document.GetText(value);
            out.TryAdd(String(key), String(text_ != nullptr ? text_ : ""));
        }
        return true;
    }

    bool LocalizationTable::Load(IPlatform& platform, const char* directory, const char* locale,
        const char* fallback)
    {
        Table<String, String> entries;
        Table<String, String> fallbackEntries;
        const bool loaded = LoadFile(platform, directory, locale, entries);
        const bool needsFallback = fallback != nullptr && locale != nullptr
            && std::string(fallback) != std::string(locale);
        // **폴백이 없을 때 `true` 로 두면 안 된다.** 기존 엔진이 그렇게 되어 있는데,
        // 로케일과 폴백이 같고 그 파일이 없으면 "폴백은 필요 없었으니 성공" 이 되어
        // **빈 표를 성공이라며 깔아 버린다.** 그러면 이미 그려지던 화면이 통째로
        // 키로 바뀌고, 부르는 쪽은 참을 받았으니 아무 말도 하지 않는다.
        const bool loadedFallback = needsFallback
            ? LoadFile(platform, directory, fallback, fallbackEntries)
            : false;
        if (false == loaded && false == loadedFallback)
        {
            // 하나도 못 읽었다. **있던 표를 지우지 않는다** - 파일 하나 잘못
            // 건드린 대가로 화면의 모든 글자를 잃을 이유가 없다.
            return false;
        }
        m_entries = std::move(entries);
        m_fallbackEntries = needsFallback ? std::move(fallbackEntries) : m_entries;
        m_locale = locale != nullptr ? locale : "";
        m_fallbackLocale = fallback != nullptr ? fallback : "";
        ++m_revision;
        return true;
    }

    void LocalizationTable::Clear()
    {
        m_entries.Clear();
        m_fallbackEntries.Clear();
        m_locale = "";
        m_fallbackLocale = "";
        ++m_revision;
    }

    const char* LocalizationTable::Find(const char* key) const
    {
        if (key == nullptr || *key == '\0')
        {
            return nullptr;
        }
        if (const String* found = m_entries.Find(String(key)))
        {
            return found->c_str();
        }
        if (const String* found = m_fallbackEntries.Find(String(key)))
        {
            return found->c_str();
        }
        return nullptr;
    }

    const String& LocalizationTable::GetLocale() const
    {
        return m_locale;
    }

    const String& LocalizationTable::GetFallbackLocale() const
    {
        return m_fallbackLocale;
    }

    std::size_t LocalizationTable::GetCount() const
    {
        return m_entries.Size();
    }

    std::uint64_t LocalizationTable::GetRevision() const
    {
        return m_revision;
    }

    namespace Loc
    {
        const char* Text(const char* key)
        {
            const char* found = LocalizationTable::Get().Find(key);
            return found != nullptr ? found : (key != nullptr ? key : "");
        }

        const char* TextOr(const char* key, const char* fallback)
        {
            const char* found = LocalizationTable::Get().Find(key);
            if (found != nullptr)
            {
                return found;
            }
            return fallback != nullptr ? fallback : (key != nullptr ? key : "");
        }
    }
}
