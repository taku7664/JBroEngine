#include <JBro/Asset/AssetRegistry.h>

#include <JBro/Asset/AssetMetaFile.h>
#include <JBro/Asset/AssetTypeRules.h>
#include <JBro/Platform/Platform.h>

#include <string_view>

namespace JBro
{
    namespace
    {
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

        // 열거가 모은 파일 목록이다. 숨김 폴더는 방문자가 내려가지 않는다.
        struct ScanListing
        {
            Array<String> files;
        };

        bool CollectEntry(const char* relativeUtf8Path, bool isDirectory, void* user)
        {
            ScanListing& listing = *static_cast<ScanListing*>(user);
            if (isDirectory)
            {
                const std::string_view name = FileNameOf(relativeUtf8Path);
                return name.empty() || name[0] != '.';
            }
            listing.files.Add(String(relativeUtf8Path));
            return true;
        }

        String JoinPath(const char* root, std::string_view relative)
        {
            String result(root);
            if (false == result.empty() && result.back() != '/' && result.back() != '\\')
            {
                result.push_back('/');
            }
            result.append(relative);
            return result;
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

    bool AssetRegistry::Scan(IPlatform& platform, const char* assetRoot, const AssetScanOptions& options, AssetScanReport& report)
    {
        Clear();
        report = {};
        if (assetRoot == nullptr || false == platform.DirectoryExists(assetRoot))
        {
            return false;
        }

        // 먼저 전부 모은다. 그래야 고아 메타 판정이 파일 시스템을 다시 묻지 않고 목록만 본다.
        ScanListing listing;
        if (false == platform.EnumerateDirectory(assetRoot, &CollectEntry, &listing))
        {
            return false;
        }
        Table<String, bool> present;
        for (std::size_t index = 0; index < listing.files.Size(); ++index)
        {
            present.TryAdd(listing.files[index], true);
        }

        for (std::size_t index = 0; index < listing.files.Size(); ++index)
        {
            const String& relativePath = listing.files[index];
            if (AssetTypeRules::IsMetaPath(relativePath))
            {
                // 짝 파일이 있는 메타는 그 파일을 만날 때 읽는다. 없으면 고아다.
                const std::size_t cut = relativePath.size() - std::char_traits<char>::length(AssetTypeRules::GetMetaExtension());
                if (false == present.Contains(String(std::string_view(relativePath).substr(0, cut))))
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
            const String metaRelative = AssetTypeRules::MakeMetaPath(relativePath);
            const String metaPath = JoinPath(assetRoot, metaRelative);
            AssetMetaFile meta;
            if (present.Contains(metaRelative))
            {
                AssetMetaError metaError;
                if (false == LoadAssetMetaFile(platform, metaPath.c_str(), meta, metaError))
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
                if (false == SaveAssetMetaFile(platform, metaPath.c_str(), meta))
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

    std::uint64_t AssetRegistry::GetRevision() const
    {
        return m_revision;
    }

    void AssetRegistry::Touch()
    {
        // 메인 스레드 전용이다(레지스트리는 편집 시점에만 바뀐다).
        static std::uint64_t s_next = 0;
        m_revision = ++s_next;
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
        else
        {
            if (Array<AssetId>* owned = m_byOwner.Find(record.owner))
            {
                owned->Add(record.id);
            }
            else
            {
                Array<AssetId> first;
                first.Add(record.id);
                m_byOwner.TryAdd(record.owner, std::move(first));
            }
        }
        Touch();
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
        else if (Array<AssetId>* owned = m_byOwner.Find(removed.owner))
        {
            for (std::size_t at = 0; at < owned->Size(); ++at)
            {
                if ((*owned)[at] == id)
                {
                    (*owned)[at] = (*owned)[owned->Size() - 1];
                    owned->Resize(owned->Size() - 1);
                    break;
                }
            }
            if (owned->IsEmpty())
            {
                m_byOwner.Remove(removed.owner);
            }
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
        // 주인이 빠지면 그것을 가리키던 것(이미지의 Sprite)도 같이 빠진다. 색인을 복사해 두고 돈다 - 빼는 동안 색인이 바뀐다.
        Array<AssetId> owned;
        CollectOwned(removed.id, owned);
        for (std::size_t at = 0; at < owned.Size(); ++at)
        {
            Unregister(owned[at]);
        }
        Touch();
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
        Touch();
        return true;
    }

    void AssetRegistry::Clear()
    {
        m_records.Clear();
        m_byId.Clear();
        m_byPath.Clear();
        m_byOwner.Clear();
        Touch();
    }

    void AssetRegistry::CollectOwned(AssetId owner, Array<AssetId>& out) const
    {
        if (const Array<AssetId>* owned = m_byOwner.Find(owner))
        {
            for (std::size_t at = 0; at < owned->Size(); ++at)
            {
                out.Add((*owned)[at]);
            }
        }
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
