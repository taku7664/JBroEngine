#include <JBro/Asset/AssetRegistry.h>

#include <JBro/Asset/AssetMetaFile.h>
#include <JBro/Asset/AssetTypeRules.h>

#include <filesystem>
#include <string_view>
#include <system_error>

namespace JBro
{
    namespace
    {
        namespace fs = std::filesystem;

        // 경로는 UTF-8 로 오간다. `fs::path(const char*)` 는 Windows 에서 ANSI 로 읽으므로 그 길을 쓰지 않는다.
        fs::path ToPath(std::string_view utf8)
        {
            return fs::path(std::u8string_view(reinterpret_cast<const char8_t*>(utf8.data()), utf8.size()));
        }

        String ToUtf8(const fs::path& path)
        {
            const std::u8string text = path.generic_u8string();
            return String(reinterpret_cast<const char*>(text.data()), text.size());
        }

        char Lower(char c) noexcept
        {
            return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
        }

        // `*` 는 아무 글자 여럿(구분자 포함), `?` 는 한 글자다. 대소문자를 가리지 않는다.
        bool GlobMatches(std::string_view pattern, std::string_view text) noexcept
        {
            std::size_t p = 0;
            std::size_t t = 0;
            std::size_t starPattern = static_cast<std::size_t>(-1);
            std::size_t starText = 0;
            while (t < text.size())
            {
                if (p < pattern.size() && pattern[p] == '*')
                {
                    starPattern = p++;
                    starText = t;
                    continue;
                }
                if (p < pattern.size() && (pattern[p] == '?' || Lower(pattern[p]) == Lower(text[t])))
                {
                    ++p;
                    ++t;
                    continue;
                }
                if (starPattern != static_cast<std::size_t>(-1))
                {
                    p = starPattern + 1;
                    t = ++starText;
                    continue;
                }
                return false;
            }
            while (p < pattern.size() && pattern[p] == '*')
            {
                ++p;
            }
            return p == pattern.size();
        }

        std::string_view FileNameOf(std::string_view relativePath) noexcept
        {
            const std::size_t slash = relativePath.rfind('/');
            return slash == std::string_view::npos ? relativePath : relativePath.substr(slash + 1);
        }

        bool IsHiddenDirectoryName(const fs::path& name)
        {
            const std::u8string text = name.u8string();
            return false == text.empty() && text[0] == u8'.';
        }
    }

    bool AssetRegistry::MatchesIgnorePattern(std::string_view relativePath, JArrayView<String> patterns)
    {
        const std::string_view fileName = FileNameOf(relativePath);
        for (std::uint32_t index = 0; index < patterns.size; ++index)
        {
            const String& pattern = patterns.data[index];
            if (pattern.empty())
            {
                continue;
            }
            if (GlobMatches(pattern, fileName) || GlobMatches(pattern, relativePath))
            {
                return true;
            }
        }
        return false;
    }

    bool AssetRegistry::Scan(const char* assetRoot, const AssetScanOptions& options, AssetScanReport& report)
    {
        Clear();
        report = {};
        if (assetRoot == nullptr)
        {
            return false;
        }
        const fs::path root = ToPath(assetRoot);
        std::error_code errorCode;
        if (false == fs::is_directory(root, errorCode))
        {
            return false;
        }

        // 숨김 폴더는 들어가지 않는다. `recursive_directory_iterator` 는 폴더에 닿았을 때 `disable_recursion_pending`
        // 으로 그 아래를 건너뛸 수 있다.
        fs::recursive_directory_iterator iterator(root, fs::directory_options::skip_permission_denied, errorCode);
        const fs::recursive_directory_iterator end;
        for (; iterator != end; iterator.increment(errorCode))
        {
            if (errorCode)
            {
                break;
            }
            const fs::directory_entry& entry = *iterator;
            if (entry.is_directory(errorCode))
            {
                if (IsHiddenDirectoryName(entry.path().filename()))
                {
                    iterator.disable_recursion_pending();
                }
                continue;
            }
            if (false == entry.is_regular_file(errorCode))
            {
                continue;
            }

            const String relativePath = ToUtf8(fs::relative(entry.path(), root, errorCode));
            if (AssetTypeRules::IsMetaPath(relativePath))
            {
                // 짝 파일이 있는 메타는 그 파일을 만날 때 읽는다. 없으면 고아다.
                const std::size_t cut = relativePath.size() - std::char_traits<char>::length(AssetTypeRules::GetMetaExtension());
                const fs::path partner = root / ToPath(std::string_view(relativePath).substr(0, cut));
                if (false == fs::is_regular_file(partner, errorCode))
                {
                    ++report.orphanMeta;
                }
                continue;
            }
            if (MatchesIgnorePattern(relativePath, options.ignorePatterns))
            {
                ++report.ignored;
                continue;
            }

            const AssetType detected = AssetTypeRules::DetectTypeFromPath(relativePath);
            const fs::path metaPath = root / ToPath(AssetTypeRules::MakeMetaPath(relativePath));
            AssetMetaFile meta;
            if (fs::is_regular_file(metaPath, errorCode))
            {
                AssetMetaError metaError;
                if (false == LoadAssetMetaFile(ToUtf8(metaPath).c_str(), meta, metaError))
                {
                    ++report.invalidMeta;
                    continue;
                }
            }
            else
            {
                if (detected == AssetType::Unknown)
                {
                    ++report.unknownType;
                    continue;
                }
                if (false == options.createMissingMeta)
                {
                    ++report.missingMeta;
                    continue;
                }
                meta.type = detected;
                meta.id = Uuid::Generate();
                if (AssetTypeRules::IsImageType(detected))
                {
                    meta.spriteId = Uuid::Generate();
                }
                if (false == SaveAssetMetaFile(ToUtf8(metaPath).c_str(), meta))
                {
                    ++report.invalidMeta;
                    continue;
                }
                ++report.metaCreated;
            }

            AssetRecord record;
            record.id = meta.id;
            record.type = meta.type;
            record.relativePath = relativePath;
            if (false == Register(record))
            {
                ++report.duplicateId;
                continue;
            }
            ++report.registered;
            if (AssetTypeRules::IsImageType(meta.type))
            {
                AssetRecord sprite;
                sprite.id = meta.spriteId;
                sprite.type = AssetType::Sprite;
                sprite.relativePath = relativePath;
                sprite.owner = meta.id;
                if (false == Register(sprite))
                {
                    // Sprite 아이디가 남의 것이면 Texture 도 물린다 - 반쪽만 선 이미지를 두지 않는다.
                    Unregister(meta.id);
                    --report.registered;
                    ++report.duplicateId;
                    continue;
                }
                ++report.registered;
            }
        }
        return true;
    }

    bool AssetRegistry::Register(const AssetRecord& record)
    {
        if (record.id.IsNull() || record.type == AssetType::Unknown || record.relativePath.empty())
        {
            return false;
        }
        if (m_byId.Contains(record.id))
        {
            return false;
        }
        // 경로 표는 Texture(또는 이미지가 아닌 것)만 가리킨다. Sprite 는 같은 경로의 두 번째 레코드라 아이디로만 찾는다.
        const bool ownsPath = record.owner.IsNull();
        if (ownsPath && m_byPath.Contains(record.relativePath))
        {
            return false;
        }
        const std::uint32_t index = static_cast<std::uint32_t>(m_records.Size());
        m_records.Add(record);
        m_byId.TryAdd(record.id, index);
        if (ownsPath)
        {
            m_byPath.TryAdd(record.relativePath, index);
        }
        return true;
    }

    bool AssetRegistry::Unregister(AssetId id)
    {
        const std::uint32_t* found = m_byId.Find(id);
        if (found == nullptr)
        {
            return false;
        }
        const std::uint32_t index = *found;
        const AssetRecord removed = m_records[index];
        m_byId.Remove(id);
        if (removed.owner.IsNull())
        {
            m_byPath.Remove(removed.relativePath);
        }
        const std::uint32_t last = static_cast<std::uint32_t>(m_records.Size() - 1);
        if (index != last)
        {
            m_records[index] = m_records[last];
            const AssetRecord& moved = m_records[index];
            *m_byId.Find(moved.id) = index;
            if (moved.owner.IsNull())
            {
                *m_byPath.Find(moved.relativePath) = index;
            }
        }
        m_records.Resize(last);
        // 이미지의 Texture 가 빠지면 그 Sprite 도 같이 빠진다.
        if (removed.type == AssetType::Texture)
        {
            for (std::size_t scan = 0; scan < m_records.Size(); ++scan)
            {
                if (m_records[scan].owner == removed.id)
                {
                    Unregister(m_records[scan].id);
                    break;
                }
            }
        }
        return true;
    }

    bool AssetRegistry::Rename(std::string_view oldRelativePath, std::string_view newRelativePath)
    {
        const String oldKey(oldRelativePath);
        const String newKey(newRelativePath);
        if (newKey.empty() || m_byPath.Contains(newKey))
        {
            return false;
        }
        const std::uint32_t* found = m_byPath.Find(oldKey);
        if (found == nullptr)
        {
            return false;
        }
        const std::uint32_t index = *found;
        m_byPath.Remove(oldKey);
        m_records[index].relativePath = newKey;
        m_byPath.TryAdd(newKey, index);
        const AssetId owner = m_records[index].id;
        for (std::size_t scan = 0; scan < m_records.Size(); ++scan)
        {
            if (m_records[scan].owner == owner)
            {
                m_records[scan].relativePath = newKey;
            }
        }
        return true;
    }

    void AssetRegistry::Clear()
    {
        m_records.Clear();
        m_byId.Clear();
        m_byPath.Clear();
    }

    const AssetRecord* AssetRegistry::Find(AssetId id) const
    {
        const std::uint32_t* found = m_byId.Find(id);
        return found == nullptr ? nullptr : &m_records[*found];
    }

    const AssetRecord* AssetRegistry::FindByPath(std::string_view relativePath) const
    {
        const std::uint32_t* found = m_byPath.Find(String(relativePath));
        return found == nullptr ? nullptr : &m_records[*found];
    }

    bool AssetRegistry::GetMetadata(AssetId id, AssetMetadata& metadata) const
    {
        const AssetRecord* record = Find(id);
        if (record == nullptr)
        {
            return false;
        }
        metadata.id = record->id;
        metadata.type = record->type;
        metadata.sourcePath.data = record->relativePath.c_str();
        metadata.sourcePath.size = static_cast<std::uint32_t>(record->relativePath.size());
        return true;
    }

    std::size_t AssetRegistry::GetCount() const
    {
        return m_records.Size();
    }

    const AssetRecord& AssetRegistry::GetRecord(std::size_t index) const
    {
        return m_records[index];
    }
}
