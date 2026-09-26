#include <JBro/Package/PackageCollect.h>

#include <JBro/Asset/AssetSource.h>
#include <JBro/Types/Table.h>

namespace JBro::Package
{
    namespace
    {
        bool IsHex(char value)
        {
            return (value >= '0' && value <= '9') || (value >= 'a' && value <= 'f') || (value >= 'A' && value <= 'F');
        }

        void Warn(CollectReport& report, const char* where, const AssetId& id)
        {
            char text[Uuid::TextCapacity] = {};
            id.ToText(text, sizeof(text));
            String line(where);
            line.append(" refers to ");
            line.append(text);
            line.append(", which is not in the project");
            report.warnings.Add(std::move(line));
        }
    }

    void FindIdsInText(const char* text, std::size_t length, Array<AssetId>& out)
    {
        std::size_t at = 0;
        while (at < length)
        {
            if (false == IsHex(text[at]))
            {
                ++at;
                continue;
            }
            std::size_t end = at;
            while (end < length && IsHex(text[end]))
            {
                ++end;
            }
            AssetId id;
            if (end - at == Uuid::TextLength && Uuid::Parse(text + at, Uuid::TextLength, id) && false == id.IsNull())
            {
                out.Add(id);
            }
            at = end;
        }
    }

    void CollectAssets(IPlatform& platform, const AssetRegistry& registry, const char* assetRoot, ArrayView<const AssetId> seeds,
        Array<AssetId>& out, CollectReport& report)
    {
        out.Clear();
        LooseAssetSource loose;
        loose.Bind(&platform, assetRoot);
        Table<AssetId, bool> seen;
        const auto visit = [&](const AssetId& id) {
            if (seen.Contains(id) || nullptr == registry.Find(id))
            {
                return;
            }
            seen.TryAdd(id, true);
            out.Add(id);
        };
        for (const AssetId& seed : seeds)
        {
            if (seed.IsNull())
            {
                continue;
            }
            if (nullptr == registry.Find(seed))
            {
                Warn(report, "the build settings", seed);
                continue;
            }
            visit(seed);
        }
        // 너비 우선이다. `out` 이 자라는 동안 앞에서부터 본다.
        Array<AssetId> found;
        Array<AssetId> owned;
        Array<std::byte> bytes;
        for (std::size_t cursor = 0; cursor < out.Size(); ++cursor)
        {
            const AssetRecord record = *registry.Find(out[cursor]);
            if (false == record.owner.IsNull())
            {
                visit(record.owner);
            }
            owned.Clear();
            registry.CollectOwned(record.id, owned);
            for (const AssetId& child : owned)
            {
                visit(child);
            }
            found.Clear();
            // Sprite 의 메타는 주인의 것이라 주인에서 한 번 읽는다.
            if (record.owner.IsNull() && loose.Read(record, AssetBlob::Meta, bytes))
            {
                FindIdsInText(reinterpret_cast<const char*>(bytes.Data()), bytes.Size(), found);
            }
            if (record.type == AssetType::Canvas && loose.Read(record, AssetBlob::Source, bytes))
            {
                FindIdsInText(reinterpret_cast<const char*>(bytes.Data()), bytes.Size(), found);
            }
            for (const AssetId& id : found)
            {
                if (nullptr == registry.Find(id))
                {
                    // 이름에서 만든 아이디(버전 8)는 빌트인 에셋이다(내장 큐브 따위). 엔진이 들고 있으므로 싸 가지 않고 경고도 하지 않는다.
                    if (id.GetVersion() != 8)
                    {
                        Warn(report, record.relativePath.c_str(), id);
                    }
                    continue;
                }
                visit(id);
            }
        }
    }
}
