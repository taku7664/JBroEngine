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

    bool AssetManager::Initialize(const JMemoryContext&)
    {
        return true;
    }

    void AssetManager::Shutdown()
    {
    }

    AssetHandle AssetManager::Load(AssetId)
    {
        return {};
    }

    AssetHandle AssetManager::LoadTexture(AssetId id)
    {
        return Load(id);
    }

    AssetHandle AssetManager::LoadSprite(AssetId id)
    {
        return Load(id);
    }

    AssetHandle AssetManager::LoadMesh(AssetId id)
    {
        return Load(id);
    }

    AssetHandle AssetManager::LoadMaterial(AssetId id)
    {
        return Load(id);
    }

    AssetHandle AssetManager::LoadShader(AssetId id)
    {
        return Load(id);
    }

    void AssetManager::Unload(AssetHandle)
    {
    }

    bool AssetManager::IsLoaded(AssetHandle) const
    {
        return false;
    }
}
