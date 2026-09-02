#pragma once

#include <JBro/Asset/Asset.h>
#include <JBro/Runtime/GameObject.h>

namespace JBro
{
    class Canvas;

    struct PrefabAsset
    {
        AssetId     id;
        JStringView rootObjectName;
    };

    struct PrefabSpawnParams
    {
        GameObject* parent                 = nullptr;
        bool        preserveSourceIdentity = false;
    };

    class PrefabSpawner
    {
    public:
        GameObject* Spawn(Canvas& canvas, AssetId prefabAsset, const PrefabSpawnParams& params);
        bool        ApplyOverrides(GameObject* instance, AssetId prefabAsset);
        void        DestroyInstance(GameObject* instance);
    };
}
