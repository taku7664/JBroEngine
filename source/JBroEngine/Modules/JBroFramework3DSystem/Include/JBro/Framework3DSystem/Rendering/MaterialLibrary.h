#pragma once

#include <JBro/AssetTypes/AssetTypes.h>
#include <JBro/Types/Array.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Int.h>
#include <JBro/Types/UInt.h>

#include <cstddef>

namespace JBro
{
    class AssetSystem;
    class Renderer;

    // **재질을 GPU 에 올린다**(D-346, material-plan 3 단계). `MeshLibrary` 와 같은 자리·같은 모양이다 - 에셋 시스템의 재질(값)을 렌더러의 재질로,
    // 재질이 가리키는 Texture 에셋을 렌더러 텍스처로 올린다. 처음 만날 때·재질의 자료 세대가 오를 때·그림의 픽셀 세대가 오를 때 다시 올린다.
    // 렌더러는 프레임 안의 등록을 거절하므로 Update 에서만 부른다(호스트는 갱신이 먼저, 프레임이 나중이다 - D-342).
    //
    // 그림의 색 공간은 그림 옵션이 정하고, `Auto` 면 칸이 정한다(기본색·발광은 sRGB, 나머지는 선형 - D-339). 밉은 그림 옵션의 `mipmaps` 다.
    // 같은 그림을 같은 색 공간으로 쓰는 재질끼리는 렌더러 텍스처 하나를 나눠 쓴다.
    class MaterialLibrary
    {
    public:
        Bool Initialize(Renderer* renderer, AssetSystem* assets);
        void Shutdown();

        // 에셋 시스템의 재질 핸들을 렌더러 재질로 푼다. 실리지 않았거나 올리지 못했으면 빈 핸들이다(같은 세대로는 다시 시도하지 않는다).
        AssetHandle ResolveAsset(AssetHandle materialAsset);
        // **메시가 파일에서 쓰는 재질**(D-346 (3))을 푼다 - 컴포넌트의 재질 칸이 비었을 때다. 메시가 재질을 모르면 빈 핸들이다.
        // 그 재질은 이 라이브러리가 실어 든다(참조를 하나 잡는다).
        AssetHandle ResolveMeshMaterial(AssetHandle meshAsset);

        // 올린 재질·텍스처 수다(시험용).
        std::size_t GetUploadedMaterialCount() const;
        std::size_t GetUploadedTextureCount() const;

    private:
        static constexpr UInt32 SlotCount = 5;

        // 렌더러 텍스처 하나다. (그림 아이디, sRGB)마다 하나이고 쓰는 재질 수를 센다.
        struct TextureEntry
        {
            AssetId id;
            Bool srgb = false;
            AssetHandle asset;
            AssetHandle rendererTexture;
            UInt32 pixelGeneration = 0;
            UInt32 users = 0;
        };

        // 에셋 풀의 슬롯마다 하나다(`MeshLibrary::AssetEntry` 와 같다).
        struct MaterialEntry
        {
            AssetHandle asset;
            AssetHandle rendererMaterial;
            UInt32 dataGeneration = 0;
            UInt32 failedGeneration = 0;
            // 칸마다 `m_textures` 의 자리다. 없으면 -1.
            Int32 textures[SlotCount] = {-1, -1, -1, -1, -1};
            // 올릴 때 본 칸마다의 픽셀 세대다. 그림이 다시 읽히면 달라진다.
            UInt32 texturePixels[SlotCount] = {0, 0, 0, 0, 0};
        };

        // 메시가 아이디로 가리키는 재질을 실어 든 것이다.
        struct HeldMaterial
        {
            AssetId id;
            AssetHandle asset;
        };

        Int32 AcquireTexture(AssetId id, Bool srgbByDefault);
        void ReleaseTexture(Int32 index);
        void ReleaseEntry(MaterialEntry& entry);
        Bool TexturesChanged(const MaterialEntry& entry) const;

        Array<MaterialEntry> m_materials;
        Array<TextureEntry> m_textures;
        Array<HeldMaterial> m_held;
        Renderer* m_renderer = nullptr;
        AssetSystem* m_assets = nullptr;
    };
}
