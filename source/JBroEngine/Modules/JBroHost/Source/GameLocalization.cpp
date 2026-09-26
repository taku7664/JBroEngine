#include <JBro/Host/GameLocalization.h>

#include <algorithm>
#include <cstring>
#include <string_view>

namespace JBro
{
    GameLocalization::GameLocalization()
    {
        m_systemContext.Localization = this;
    }

    GameLocalization::~GameLocalization()
    {
        ReleaseTables();
    }

    void GameLocalization::Attach(AssetSystem* assets, const AssetRegistry* registry)
    {
        ReleaseTables();
        m_assets = assets;
        m_registry = registry;
        m_gathered = false;
        ++m_revision;
    }

    void GameLocalization::SetFallbackLocale(const char* locale)
    {
        const String next = locale != nullptr ? String(locale) : String();
        if (next == m_fallback)
        {
            return;
        }
        m_fallback = next;
        ++m_revision;
    }

    const String& GameLocalization::GetFallbackLocale() const
    {
        return m_fallback;
    }

    void GameLocalization::ReleaseTables()
    {
        if (m_assets != nullptr)
        {
            for (const HeldTable& table : m_tables)
            {
                m_assets->Release(table.handle);
            }
        }
        m_tables.Clear();
    }

    void GameLocalization::Gather()
    {
        ReleaseTables();
        m_gathered = true;
        if (m_assets == nullptr || m_registry == nullptr)
        {
            return;
        }
        m_registryRevision = m_registry->GetRevision();
        for (std::size_t index = 0; index < m_registry->GetCount(); ++index)
        {
            const AssetRecord& record = m_registry->GetRecord(index);
            if (record.type != AssetType::StringTable)
            {
                continue;
            }
            const AssetHandle handle = m_assets->Load(record.id);
            const StringTableData* data = m_assets->GetStringTable(handle);
            if (data == nullptr)
            {
                // 읽지 못한 표다(YAML 오류). 핸들이 있으면 놓는다 - 고쳐 저장하면 레지스트리가 바뀌지 않아도 재로드가 다시 싣는다.
                if (handle.generation != 0)
                {
                    m_assets->Release(handle);
                }
                continue;
            }
            m_tables.Add({ handle, data->dataGeneration });
        }
    }

    void GameLocalization::Refresh()
    {
        if (m_assets == nullptr || m_registry == nullptr)
        {
            return;
        }
        if (false == m_gathered || m_registryRevision != m_registry->GetRevision())
        {
            Gather();
            ++m_revision;
            return;
        }
        bool changed = false;
        for (HeldTable& table : m_tables)
        {
            const StringTableData* data = m_assets->GetStringTable(table.handle);
            const std::uint32_t generation = data != nullptr ? data->dataGeneration : 0;
            if (generation != table.dataGeneration)
            {
                table.dataGeneration = generation;
                changed = true;
            }
        }
        if (changed)
        {
            ++m_revision;
        }
    }

    std::uint32_t GameLocalization::GetTableCount() const
    {
        return static_cast<std::uint32_t>(m_tables.Size());
    }

    const LocalizationSystemContext& GameLocalization::GetSystemContext() const
    {
        return m_systemContext;
    }

    const LocalizationServiceContext& GameLocalization::GetServiceContext() const
    {
        return m_serviceContext;
    }

    std::size_t GameLocalization::GetLocale(char* buffer, std::size_t capacity) const noexcept
    {
        if (buffer != nullptr && capacity > 0)
        {
            const std::size_t copied = m_locale.size() < capacity ? m_locale.size() : capacity - 1;
            std::memcpy(buffer, m_locale.data(), copied);
            buffer[copied] = 0;
        }
        return m_locale.size();
    }

    bool GameLocalization::SetLocale(const char* locale) noexcept
    {
        if (locale == nullptr || locale[0] == 0)
        {
            return false;
        }
        if (m_locale != locale)
        {
            m_locale = locale;
            ++m_revision;
        }
        return true;
    }

    const String& GameLocalization::GetLocaleName() const
    {
        return m_locale;
    }

    void GameLocalization::CollectKeys(Array<String>& out) const
    {
        out.Clear();
        if (m_assets == nullptr)
        {
            return;
        }
        for (const HeldTable& table : m_tables)
        {
            const StringTableData* data = m_assets->GetStringTable(table.handle);
            if (data == nullptr)
            {
                continue;
            }
            for (const auto& entry : data->entries)
            {
                out.Add(entry.KeyValue);
            }
        }
        std::sort(out.begin(), out.end());
        const auto last = std::unique(out.begin(), out.end());
        out.Resize(static_cast<std::size_t>(last - out.begin()));
    }

    bool GameLocalization::FindIn(const String& locale, std::string_view key, const char*& text, std::size_t& textLength) const
    {
        if (locale.empty() || m_assets == nullptr)
        {
            return false;
        }
        for (const HeldTable& table : m_tables)
        {
            const StringTableData* data = m_assets->GetStringTable(table.handle);
            if (data == nullptr || data->options.locale != locale)
            {
                continue;
            }
            if (const String* found = data->entries.Find(key))
            {
                text = found->data();
                textLength = found->size();
                return true;
            }
        }
        return false;
    }

    bool GameLocalization::Find(const char* key, std::size_t keyLength, const char*& text, std::size_t& textLength) const noexcept
    {
        text = nullptr;
        textLength = 0;
        if (key == nullptr || keyLength == 0)
        {
            return false;
        }
        const std::string_view name(key, keyLength);
        if (FindIn(m_locale, name, text, textLength))
        {
            return true;
        }
        return m_fallback != m_locale && FindIn(m_fallback, name, text, textLength);
    }

    std::uint32_t GameLocalization::GetRevision() const noexcept
    {
        return m_revision;
    }
}
