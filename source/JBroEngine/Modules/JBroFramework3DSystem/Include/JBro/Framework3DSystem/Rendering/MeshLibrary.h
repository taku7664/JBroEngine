#pragma once

#include <JBro/AssetTypes/AssetTypes.h>
#include <JBro/Types/Array.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/UInt.h>

namespace JBro
{
    class AssetSystem;
    class Renderer;
    struct MeshVertex;

    // **GPU 메시를 든다**(framework3d-plan §2.3, asset-plan §2.5). 두 갈래다.
    // - 빌트인 도형: 레지스트리에 없어 `AssetId → 렌더러 핸들` 표로 판다(`Resolve(AssetId)`).
    // - 모델의 메시(D-391): 컴포넌트의 `mesh` 칸에는 해석 패스가 채운 **에셋 시스템의 핸들**이 있다. `ResolveAsset` 이 그 CPU 자료를
    //   처음 만날 때 렌더러에 올리고, 자료 세대가 오르면(in-place 재로드) 다시 올린다. 2D 의 `SpriteLibrary` 와 같은 자리·같은 모양이다 -
    //   전에는 이 라이브러리가 렌더러 핸들을 컴포넌트에 되써서 같은 칸에 두 종류의 핸들이 섞였다.
    class MeshLibrary
    {
    public:
        // 빌트인 정육면체의 아이디. `MeshRenderer3D::meshId` 에 넣으면 그 도형이 그려진다.
        static AssetId BuiltinCubeId();

        // **빌트인 도형 표**(D-388 우선순위 2). 정육면체·구·캡슐·원기둥·평면이고, 아이디는 `Uuid::FromName("builtin/<이름>")` 이다.
        // 레지스트리에 없는 아이디라 에디터의 메시 칸·만들기 메뉴가 이 표에서 이름을 얻는다. 이름은 번역하지 않는다.
        struct BuiltinMesh
        {
            const char* name = nullptr;
            AssetId id;
        };
        static UInt32 GetBuiltinCount();
        // 범위 밖이면 이름이 널이다.
        static BuiltinMesh GetBuiltin(UInt32 index);
        // 빌트인 도형 하나의 정점과 인덱스를 짓는다(올리지 않는다). UV 는 정육면체·평면이 면마다 0..1, 구·캡슐·원기둥 옆면이 원통 감기(u 가 둘레, v 가 위에서 아래),
        // 원기둥 뚜껑이 위에서 내려다본 평면 투영이다. 탄젠트는 삼각형의 UV 에서 계산한다(+u 방향, w 는 +v 쪽 바이탄젠트의 부호). 범위 밖이면 false.
        static Bool BuildBuiltin(UInt32 index, Array<MeshVertex>& vertices, Array<UInt32>& indices);

        // 빌트인 도형을 렌더러에 올린다. 렌더러가 없으면(테스트) 표만 비어 있고 참이다. 에셋 시스템이 없으면 모델의 메시는 풀리지 않는다.
        Bool Initialize(Renderer* renderer, AssetSystem* assets = nullptr);
        void Shutdown();

        // 빌트인 아이디로 렌더러 핸들을 찾는다. 모르면 빈 핸들이다.
        AssetHandle Resolve(AssetId id) const;
        // 에셋 시스템의 메시 핸들을 렌더러 핸들로 푼다. 처음이거나 자료 세대가 바뀌었으면 올린다 - 렌더러의 등록은 프레임 밖이라
        // Update 에서만 부른다. 실리지 않았거나 올리지 못했으면 빈 핸들이다(같은 세대로는 다시 시도하지 않는다).
        AssetHandle ResolveAsset(AssetHandle meshAsset);
        // 표에 더한다. 같은 아이디가 있으면 덮는다.
        void Register(AssetId id, AssetHandle handle);
        std::size_t GetCount() const;
        // 모델에서 올린 메시 수다(시험용).
        std::size_t GetUploadedAssetCount() const;

    private:
        struct Entry
        {
            AssetId id;
            AssetHandle handle;
        };
        // 에셋 풀의 슬롯마다 하나다. 핸들의 세대로 그 슬롯을 다시 쓴 다른 메시와 가른다.
        struct AssetEntry
        {
            AssetHandle asset;
            AssetHandle rendererMesh;
            UInt32 dataGeneration = 0;
            UInt32 failedGeneration = 0;
        };
        Array<Entry> m_entries;
        Array<AssetEntry> m_assetEntries;
        Renderer* m_renderer = nullptr;
        AssetSystem* m_assets = nullptr;
    };
}
