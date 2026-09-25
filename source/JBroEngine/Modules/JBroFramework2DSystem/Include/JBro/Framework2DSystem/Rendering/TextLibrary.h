#pragma once

#include <JBro/AssetTypes/AssetTypes.h>
#include <JBro/Text/FontFace.h>
#include <JBro/Text/GlyphAtlas.h>
#include <JBro/Types/Array.h>
#include <JBro/Types/SafePtr.h>

#include <cstdint>

namespace JBro
{
    class AssetSystem;
    class Renderer;

    // 폰트 에셋과 글리프 아틀라스, 그리고 아틀라스 페이지의 GPU 텍스처를 잇는다(D-200, text-plan §4.4). `SpriteLibrary` 와 같은 자리다.
    //
    // 폰트 에셋 하나에 face 하나(에셋 바이트의 **자기 사본**, `FontFace::Load`)와 아틀라스 하나다. 표는 에셋 핸들의 슬롯 번호로
    // 곧장 찍고 세대로 검증한다. 에셋이 in-place 재로드되면(`dataGeneration`) face 를 다시 열고 아틀라스와 페이지 텍스처를 버린다 -
    // 옛 글리프는 옛 바이트의 것이다.
    //
    // **렌더러 프레임 밖에서만 부른다.** 텍스트 시스템의 Update 가 그 자리다(`RegisterTexture`·`UpdateTexture` 가 프레임 안에서는 거절한다).
    struct FontView
    {
        const Text::FontFace* face = nullptr;
        Text::GlyphAtlas*     atlas = nullptr;
        float                 pixelsPerUnit = DefaultPixelsPerUnit;
        TextureFilter         filter = TextureFilter::Nearest; // 이미 정해진 값이다. `Default` 는 오지 않는다
        std::uint32_t         dataGeneration = 0;
    };

    class TextLibrary final
    {
    public:
        void Initialize(AssetSystem* assets, Renderer* renderer);
        void Shutdown();

        // 폰트 에셋을 연다. 처음이거나 재로드됐으면 face 를 새로 열고 아틀라스를 비운다. 로드돼 있지 않거나 바이트가 폰트가
        // 아니면 거짓이다(같은 데이터 세대에는 다시 열어 보지 않는다). 받은 포인터는 이 라이브러리가 살아 있는 동안, 그리고
        // 그 폰트가 다시 열리기 전까지 유효하다.
        bool Acquire(AssetHandle font, FontView& view);

        // 더러운 아틀라스 페이지를 올린다. 새 페이지는 등록하고 있던 페이지는 같은 핸들에 다시 쓴다. 올린 페이지 수다.
        std::uint32_t UploadDirtyPages();

        // 폰트의 page 번째 페이지 텍스처다. 아직 올리지 않았으면 빈 핸들이다.
        AssetHandle GetPageTexture(AssetHandle font, std::uint32_t page) const;

        // 지금까지 올린 페이지 수(등록과 다시 쓰기를 모두 센다)와 살아 있는 페이지 텍스처 수다. 테스트가 "새 글자가 없는 프레임에는
        // 올리지 않는다" 를 이것으로 잰다 - 렌더러에는 올린 횟수를 세는 자리가 없다.
        std::uint64_t GetUploadCount() const;
        std::uint32_t GetPageTextureCount() const;

    private:
        struct FontEntry
        {
            AssetHandle           asset;
            std::uint32_t         dataGeneration = 0;
            std::uint32_t         failedGeneration = 0;
            Text::FontFace        face;
            Text::GlyphAtlas      atlas;
            Array<AssetHandle>    pageTextures;
            float                 pixelsPerUnit = DefaultPixelsPerUnit;
            TextureFilter         filter = TextureFilter::Nearest;
        };

        void ReleasePages(FontEntry& entry);

        AssetSystem* m_assets = nullptr;
        Renderer*    m_renderer = nullptr;
        // 폰트 에셋의 슬롯 번호로 찍는다. 원소가 옮겨 다니지 않게 따로 잡는다(FontView 가 face·atlas 를 가리킨다).
        Array<OwnerPtr<FontEntry>> m_fonts;
        std::uint64_t m_uploadCount = 0;
    };
}
