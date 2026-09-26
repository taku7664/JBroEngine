#pragma once

#include <JBro/AssetTypes/AssetTypes.h>
#include <JBro/Runtime/TextStore.h>
#include <JBro/Text/TextLayout.h>
#include <JBro/TextRendering/GlyphMesh.h>
#include <JBro/TextRendering/TextLibrary.h>
#include <JBro/Types/Array.h>
#include <JBro/Types/TextOptions.h>

#include <cstdint>

// 텍스트 한 덩어리의 레이아웃 캐시다(D-222). 2D `Text2D` 와 3D `Text3D` 가 같이 쓴다 - 컴포넌트는 제 필드로 `TextBlockSettings` 를
// 채우고, 시스템은 매 프레임 `Update` 를 부른다. 글자·폰트·옵션·아틀라스가 바뀌었을 때만 다시 레이아웃하고, 결과는 글자 픽셀의 쿼드다.
// 월드로 옮기는 것(PPU·변환·빌보드)은 각 시스템의 일이다.
namespace JBro
{
    // 컴포넌트의 배치 값(Tier S, JBroCore)과 커널의 값(Tier E)은 따로 있고 정수로 옮긴다. 순서가 어긋나면 여기서 멈춘다.
    static_assert(static_cast<int>(Component::TextOverflow::Clip) == static_cast<int>(Text::Overflow::Clip));
    static_assert(static_cast<int>(Component::TextOverflow::Wrap) == static_cast<int>(Text::Overflow::Wrap));
    static_assert(static_cast<int>(Component::TextWrapMode::Character) == static_cast<int>(Text::WrapMode::Character));
    static_assert(static_cast<int>(Component::TextAlignX::Right) == static_cast<int>(Text::AlignX::Right));
    static_assert(static_cast<int>(Component::TextAlignX::Center) == static_cast<int>(Text::AlignX::Center));
    static_assert(static_cast<int>(Component::TextAlignY::Middle) == static_cast<int>(Text::AlignY::Middle));
    static_assert(static_cast<int>(Component::TextAlignY::Baseline) == static_cast<int>(Text::AlignY::Baseline));
    static_assert(static_cast<int>(Component::TextAlignY::Bottom) == static_cast<int>(Text::AlignY::Bottom));

    inline Text::Overflow ToLayout(Component::TextOverflow value)
    {
        return static_cast<Text::Overflow>(value);
    }

    inline Text::WrapMode ToLayout(Component::TextWrapMode value)
    {
        return static_cast<Text::WrapMode>(value);
    }

    inline Text::AlignX ToLayout(Component::TextAlignX value)
    {
        return static_cast<Text::AlignX>(value);
    }

    inline Text::AlignY ToLayout(Component::TextAlignY value)
    {
        return static_cast<Text::AlignY>(value);
    }

    // 레이아웃을 바꾸는 저작 값이다. 색·그리기 순서처럼 레이아웃과 무관한 값은 여기 없다.
    struct TextBlockSettings
    {
        TextId           text;
        // 게임 문자열 표의 키다(D-226). 비어 있지 않으면 `text` 대신 이 키의 글자를 보인다: 지금 로케일 → 폴백 로케일 → 키 그대로.
        // 표는 이 모듈 사본에 묶인 `ILocalization`(호스트의 것)에서 찾는다. 묶인 것이 없으면 키 그대로다.
        TextId           textKey;
        AssetId          fontId;
        AssetHandle      font;             // fontId 에서 해석된 것(Font 또는 FontFamily, D-225). fontId 가 비면 프로젝트의 첫 폰트를 쓴다(D-213)
        float            fontSize = 32.0f; // em 크기(글자 픽셀)
        float            boxWidth = 0.0f;  // 글자 픽셀. 0 이면 그 방향으로 제한이 없다
        float            boxHeight = 0.0f;
        Text::Overflow   overflow = Text::Overflow::Wrap;
        Text::WrapMode   wrapMode = Text::WrapMode::Word;
        Text::AlignX     alignX = Text::AlignX::Left;
        Text::AlignY     alignY = Text::AlignY::Baseline;
        float            lineSpacing = 1.0f;
        float            letterSpacing = 0.0f;
        bool             autoSize = false;
        float            minFontSize = 8.0f;
        float            maxFontSize = 72.0f;
        bool             pixelSnap = false;
        bool             richText = false;
    };

    class TextBlock final
    {
    public:
        // 한 텍스트가 쓰는 face 의 최대 수다: 자기 폰트 하나와 프로젝트 폴백들.
        static constexpr std::uint32_t MaxFaces = 8;

        enum class UpdateResult : std::uint8_t
        {
            Unchanged, // 캐시를 그대로 쓴다
            Relaid,    // 다시 레이아웃했다
            NoFont,    // 쓸 수 있는 폰트가 없다. 쿼드가 비었다
        };

        // 폰트를 모으고 입력이 바뀌었으면 다시 레이아웃한다(새 글리프가 아틀라스에 든다). 렌더러 프레임 밖에서 부른다.
        UpdateResult Update(TextLibrary& library, const TextBlockSettings& settings);

        ArrayView<const GlyphQuad> GetQuads() const;
        // 쿼드의 `face` 번호가 가리키는 폰트다. 페이지 텍스처는 `TextLibrary::GetPageTexture(GetFont(face), page)` 다.
        AssetHandle GetFont(std::uint32_t face) const;
        std::uint32_t GetFontCount() const;
        float GetPixelsPerUnit() const;
        TextureFilter GetFilter() const;
        bool IsSdf() const;
        std::uint32_t GetSdfSpread() const;
        // 마지막으로 레이아웃한 글자 크기(em 픽셀)다. 자동 크기면 찾은 크기다.
        float GetFittedSize() const;
        // 블록 사각형(글자 픽셀, 정렬 기준점 원점, y 위쪽)이다. 레이아웃이 없으면 거짓이다.
        bool GetBounds(float& minX, float& minY, float& maxX, float& maxY) const;

        static std::uint64_t MakeOptionsKey(const TextBlockSettings& settings);

    private:
        // faces 안에서 굵게·기울임·굵은 기울임 face 의 번호다(패밀리일 때, D-225). 없으면 `Text::LayoutOptions::NoStyleFace` 다.
        struct StyleFaces
        {
            std::uint16_t bold = Text::LayoutOptions::NoStyleFace;
            std::uint16_t italic = Text::LayoutOptions::NoStyleFace;
            std::uint16_t boldItalic = Text::LayoutOptions::NoStyleFace;
        };

        bool GatherFonts(TextLibrary& library, const TextBlockSettings& settings, AssetHandle* handles, FontView* views,
            std::uint32_t& count, StyleFaces& styles) const;
        void Relayout(const TextBlockSettings& settings, const AssetHandle* handles, const FontView* views, std::uint32_t count,
            const StyleFaces& styles);

        // 보일 글자다. 키가 있으면 표에서 찾은 것(없으면 키), 없으면 `text` 의 것이다. 찾은 글자는 호스트 메모리를 가리키므로 곧바로 쓴다.
        static ArrayView<const char> ResolveText(const TextBlockSettings& settings);
        static std::uint32_t LocalizationRevision(const TextBlockSettings& settings);

        // 이 캐시를 만든 입력이다. 하나라도 다르면 다시 레이아웃한다.
        TextId           m_text;
        std::uint32_t    m_textRevision = 0;
        TextId           m_textKey;
        std::uint32_t    m_textKeyRevision = 0;
        // 키가 있을 때만 본다. 키가 없으면 0 이다 - 로케일이 바뀌어도 키 없는 텍스트는 다시 레이아웃하지 않는다.
        std::uint32_t    m_localizationRevision = 0;
        // 앞이 기본 폰트, 뒤가 폴백이다.
        AssetHandle      m_fonts[MaxFaces];
        std::uint32_t    m_fontGenerations[MaxFaces] = {};
        std::uint32_t    m_atlasGenerations[MaxFaces] = {};
        std::uint32_t    m_fontCount = 0;
        std::uint64_t    m_optionsKey = 0;
        StyleFaces       m_styles;
        Text::TextLayout m_layout;
        Array<GlyphQuad> m_quads;
        float            m_pixelsPerUnit = DefaultPixelsPerUnit;
        TextureFilter    m_filter = TextureFilter::Nearest;
        bool             m_sdf = false;
        std::uint32_t    m_sdfSpread = 8;
        float            m_fittedSize = 0.0f;
        bool             m_hasBounds = false;
    };
}
