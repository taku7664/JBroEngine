#pragma once

#include <JBro/AssetTypes/AssetTypes.h>
#include <JBro/Core/Core.h>

namespace JBro
{
    // 에셋 메타데이터 보관소. 로드·캐시 소유는 AssetSystem 이 따로 가진다(D-50).
    class AssetRegistry
    {
    public:
        void Register(const AssetMetadata& metadata);
        void Unregister(AssetId id);
        const AssetMetadata* Find(AssetId id) const;
    };

    // 프로젝트 수명 동안 에셋 로드와 캐시를 소유한다. 사용자 호출 표면은 값형 Service::AssetService 다.
    class AssetSystem final : public IModule
    {
    public:
        bool Initialize(const JMemoryContext& memory) override;
        void Shutdown() override;

        AssetHandle Load(AssetId id);
        AssetHandle LoadTexture(AssetId id);
        AssetHandle LoadSprite(AssetId id);
        AssetHandle LoadMesh(AssetId id);
        AssetHandle LoadMaterial(AssetId id);
        AssetHandle LoadShader(AssetId id);
        void Unload(AssetHandle handle);
        bool IsLoaded(AssetHandle handle) const;
    };
}
