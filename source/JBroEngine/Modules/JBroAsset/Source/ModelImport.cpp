#include <JBro/Asset/ModelImport.h>

#include <JBro/Asset/ModelDecoder.h>
#include <JBro/Platform/Platform.h>
#include <JBro/Types/Array.h>
#include <JBro/Types/Uuid.h>

#include <string>

namespace JBro
{
    namespace
    {
        String JoinPath(const char* root, std::string_view relative)
        {
            String path(root != nullptr ? root : "");
            if (false == path.empty() && path.back() != '/' && path.back() != '\\')
            {
                path += "/";
            }
            path += String(relative);
            return path;
        }

        // 곁 파일은 모델 파일의 폴더 기준이다.
        struct SideReader
        {
            IPlatform* platform = nullptr;
            String folder;

            static Bool Read(void* user, const char* relativePath, Array<std::byte>& bytes)
            {
                const SideReader& self = *static_cast<const SideReader*>(user);
                return self.platform->ReadWholeFile(JoinPath(self.folder.c_str(), relativePath).c_str(), bytes);
            }
        };

        // 재질 이름은 파일 안에서 겹칠 수 있다(이름 없는 재질이 여럿이면 모두 `Material`). 키가 겹치지 않게 ` (2)` 를 붙인다.
        String UniqueKey(const Array<String>& taken, const String& wanted)
        {
            const auto used = [&](const String& key) {
                for (std::size_t index = 0; index < taken.Size(); ++index)
                {
                    if (taken[index] == key)
                    {
                        return true;
                    }
                }
                return false;
            };
            if (false == used(wanted))
            {
                return wanted;
            }
            for (UInt32 suffix = 2;; ++suffix)
            {
                String candidate = wanted;
                candidate += " (";
                candidate += String(std::to_string(suffix.Get()).c_str());
                candidate += ")";
                if (false == used(candidate))
                {
                    return candidate;
                }
            }
        }
    }

    Bool RefreshModelSubAssets(IPlatform& platform, const char* assetRoot, std::string_view relativePath,
        AssetMetaFile& meta, Bool& changed, String& error)
    {
        changed = false;
        const String path = JoinPath(assetRoot, relativePath);
        Array<std::byte> bytes;
        if (false == platform.ReadWholeFile(path.c_str(), bytes))
        {
            error = "the model file could not be read";
            return false;
        }
        const std::size_t slash = relativePath.find_last_of("/\\");
        SideReader side;
        side.platform = &platform;
        side.folder = JoinPath(assetRoot, slash == std::string_view::npos ? std::string_view() : relativePath.substr(0, slash));
        ModelFileReader reader;
        reader.read = &SideReader::Read;
        reader.user = &side;
        const ModelImportOptions options = meta.hasModelOptions ? meta.modelOptions : ModelImportOptions{};
        ModelData model;
        if (false == DecodeModel({bytes.Data(), static_cast<UInt32>(bytes.Size())}, ModelFormatFromPath(relativePath), options, reader, model, error))
        {
            return false;
        }

        Array<String> keys;
        for (std::size_t index = 0; index < model.meshes.Size(); ++index)
        {
            keys.Add(UniqueKey(keys, String("Mesh/") + model.meshes[index].name));
        }
        // `importMaterials` 가 꺼져 있으면 디코더가 재질을 내지 않는다 - 여기서 다시 거르지 않는다(같은 조건을 두 곳에 두면 한쪽만 고쳐지는 날이 온다).
        for (std::size_t index = 0; index < model.materials.Size(); ++index)
        {
            keys.Add(UniqueKey(keys, String("Material/") + model.materials[index].name));
        }

        Array<AssetSubEntry> merged;
        for (std::size_t index = 0; index < keys.Size(); ++index)
        {
            AssetSubEntry entry;
            entry.key = keys[index];
            for (std::size_t old = 0; old < meta.subAssets.Size(); ++old)
            {
                if (meta.subAssets[old].key == entry.key)
                {
                    entry.id = meta.subAssets[old].id;
                    break;
                }
            }
            if (entry.id.IsNull())
            {
                entry.id = Uuid::Generate();
            }
            merged.Add(std::move(entry));
        }

        Bool same = meta.hasSubAssets && merged.Size() == meta.subAssets.Size();
        for (std::size_t index = 0; same && index < merged.Size(); ++index)
        {
            same = merged[index].key == meta.subAssets[index].key && merged[index].id == meta.subAssets[index].id;
        }
        changed = false == same;
        meta.subAssets = std::move(merged);
        meta.hasSubAssets = true;
        return true;
    }
}
