#pragma once

#include <JBro/Asset/Asset.h>
#include <JBro/Runtime/GameObject.h>

namespace JBro::Engine
{
    class CWorld;

    struct PrefabAsset
    {
        AssetId id;
        JStringView rootObjectName;
    };

    struct PrefabSpawnParams
    {
        GameObject parent;
        bool preserveSourceIdentity = false;
    };

    class PrefabSpawner
    {
    public:
        GameObject Spawn(CWorld& world, AssetId prefabAsset, const PrefabSpawnParams& params);
        bool ApplyOverrides(GameObject instance, AssetId prefabAsset);
        void DestroyInstance(GameObject instance);

    private:
        GameObject CreateObjectHierarchy(CWorld& world, const PrefabAsset& prefab, GameObject parent);
        void CreateComponents(CWorld& world, GameObject object, const PrefabAsset& prefab);
    };
}
