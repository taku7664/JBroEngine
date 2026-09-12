#include <JBro/Asset/Asset.h>

namespace JBro
{
    void AssetRegistry::Register(const AssetMetadata&)
    {
    }

    void AssetRegistry::Unregister(AssetId)
    {
    }

    const AssetMetadata* AssetRegistry::Find(AssetId) const
    {
        return nullptr;
    }

    bool AssetSystem::Initialize(const JMemoryContext&)
    {
        return true;
    }

    void AssetSystem::Shutdown()
    {
    }

    AssetHandle AssetSystem::Load(AssetId)
    {
        return {};
    }

    AssetHandle AssetSystem::LoadTexture(AssetId id)
    {
        return Load(id);
    }

    AssetHandle AssetSystem::LoadSprite(AssetId id)
    {
        return Load(id);
    }

    AssetHandle AssetSystem::LoadMesh(AssetId id)
    {
        return Load(id);
    }

    AssetHandle AssetSystem::LoadMaterial(AssetId id)
    {
        return Load(id);
    }

    AssetHandle AssetSystem::LoadShader(AssetId id)
    {
        return Load(id);
    }

    void AssetSystem::Unload(AssetHandle)
    {
    }

    bool AssetSystem::IsLoaded(AssetHandle) const
    {
        return false;
    }
}
