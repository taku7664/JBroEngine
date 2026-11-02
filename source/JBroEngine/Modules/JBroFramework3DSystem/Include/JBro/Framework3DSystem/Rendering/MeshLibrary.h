#pragma once

#include <JBro/AssetTypes/AssetTypes.h>
#include <JBro/Types/Array.h>
#include <JBro/Types/Bool.h>

namespace JBro
{
    class AssetSystem;
    class Renderer;

    // `AssetId → AssetHandle` 표다. 렌더러에 올린 빌트인 메시를 아이디로 판다(framework3d-plan §2.3).
    // 메시 에셋(glTF)은 에셋 시스템이 싣고, 여기는 그 자료를 렌더러에 올려 핸들을 잇는다(D-366).
    class MeshLibrary
    {
    public:
        // 빌트인 정육면체의 아이디. `MeshRenderer3D::meshId` 에 넣으면 그 도형이 그려진다.
        static AssetId BuiltinCubeId();

        // 빌트인 도형을 렌더러에 올린다. 렌더러가 없으면(테스트) 표만 비어 있고 참이다. `assets` 가 있으면 메시 에셋(glTF, D-366)도 푼다.
        Bool Initialize(Renderer* renderer, AssetSystem* assets = nullptr);
        void Shutdown();

        // 아이디로 핸들을 찾는다. 모르면 빈 핸들이다.
        AssetHandle Resolve(AssetId id) const;
        // **컴포넌트의 `mesh` 핸들을 렌더러 메시로 푼다**(D-366). 메시 에셋이면 처음 볼 때 올리고 그 렌더러 핸들을 준다. 에셋이 아니면(빌트인이 쓴 렌더러
        // 핸들) 그대로 준다. 본문이 다시 읽히면(`dataGeneration`) 다음 부름에서 다시 올린다.
        AssetHandle ResolveHandle(AssetHandle handle);
        // 표에 더한다. 같은 아이디가 있으면 덮는다.
        void Register(AssetId id, AssetHandle handle);
        std::size_t GetCount() const;

    private:
        struct Entry
        {
            AssetId id;
            AssetHandle handle;
        };
        struct AssetEntry
        {
            AssetHandle asset;
            UInt32 dataGeneration = 0;
            AssetHandle renderer;
        };
        Array<Entry> m_entries;
        Array<AssetEntry> m_assetEntries;
        Renderer* m_renderer = nullptr;
        AssetSystem* m_assets = nullptr;
    };
}
