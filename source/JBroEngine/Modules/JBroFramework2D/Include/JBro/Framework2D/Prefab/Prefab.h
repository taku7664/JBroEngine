#pragma once

#include <JBro/AssetTypes/AssetTypes.h>
#include <JBro/Core/Core.h>
#include <JBro/Runtime/GameObjectHandle.h>

namespace JBro
{
    struct PrefabAsset
    {
        AssetId     id;
        JStringView rootObjectName;
    };

    struct PrefabSpawnParams
    {
        // 부모로 붙일 오브젝트. 비어 있으면 최상위로 생성한다.
        GameObjectHandle parent;
        bool             preserveSourceIdentity = false;
    };

    // 스크립트 표면이므로 오브젝트는 핸들로만 주고받는다. 실 객체 접근은 엔진 계층의 몫이다.
    class PrefabSpawner
    {
    public:
        GameObjectHandle Spawn(AssetId prefabAsset, const PrefabSpawnParams& params);
        bool             ApplyOverrides(GameObjectHandle instance, AssetId prefabAsset);
        void             DestroyInstance(GameObjectHandle instance);
    };
}
