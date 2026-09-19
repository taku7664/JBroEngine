#pragma once

#include <JBro/AssetTypes/AssetTypes.h>
#include <JBro/Types/Array.h>

#include <cstdint>

namespace JBro
{
    class AssetSystem;
    class Renderer;

    // 스프라이트 에셋과 GPU 텍스처를 잇는다(asset-plan §2.5, D-113). `MeshLibrary` 와 같은 자리다 - 에셋은 CPU 자료만
    // 들고(P5), 렌더러에 올린 텍스처 핸들은 여기가 든다.
    //
    // **프레임 경로에 조회가 없다.** 표는 에셋 핸들의 슬롯 번호로 곧장 찍는 배열이고 세대로 검증한다. 텍스처는 처음
    // 만날 때 한 번 올리고, 에셋이 in-place 재로드되면(`pixelGeneration`) 같은 핸들에 다시 올린다.
    // **렌더러 프레임 밖에서만 부른다** - `RegisterTexture` 가 프레임 안에서는 거절하므로 Update 단계(렌더 추출)가 그 자리다.
    // 풀린 칸의 크기(유닛)와 피벗이다. 크기 = 칸 픽셀 / 에셋 PPU 다(D-117).
    struct SpriteFrameView
    {
        float widthUnits = 0.0f;
        float heightUnits = 0.0f;
        float pivotX = 0.5f;
        float pivotY = 0.5f;
        // 텍스처의 유효 샘플러다(프로젝트 기본이 이미 적용된 값, D-117). `Default` 는 오지 않는다.
        TextureFilter filter = TextureFilter::Nearest;
    };

    // 0 이하로 적힌 PPU 는 이 값으로 본다.
    inline constexpr float DefaultPixelsPerUnit = 100.0f;

    class SpriteLibrary final
    {
    public:
        void Initialize(AssetSystem* assets, Renderer* renderer);
        void Shutdown();

        // 스프라이트 에셋의 `frameIndex` 번째 칸을 렌더러 텍스처와 UV 사각형으로 푼다. 칸 번호가 넘치면 마지막 칸이다.
        // 스프라이트가 로드돼 있지 않거나 텍스처를 올리지 못하면 거짓이고 출력은 손대지 않는다.
        // `frameView` 를 주면 그 칸의 유닛 크기와 피벗도 준다(D-117).
        bool Resolve(AssetHandle spriteAsset, std::uint32_t frameIndex, AssetHandle& rendererTexture, float uvRect[4],
            SpriteFrameView* frameView = nullptr);

        std::uint32_t GetUploadedTextureCount() const;

    private:
        struct TextureEntry
        {
            // 이 자리를 차지한 텍스처 에셋 핸들이다. 세대가 다르면 다른 에셋이 그 슬롯을 다시 쓴 것이다.
            AssetHandle asset;
            AssetHandle rendererTexture;
            std::uint32_t pixelGeneration = 0;
        };

        bool EnsureTexture(AssetHandle textureAsset, AssetHandle& rendererTexture);

        AssetSystem* m_assets = nullptr;
        Renderer* m_renderer = nullptr;
        // 텍스처 에셋의 슬롯 번호로 찍는다.
        Array<TextureEntry> m_textures;
    };
}
