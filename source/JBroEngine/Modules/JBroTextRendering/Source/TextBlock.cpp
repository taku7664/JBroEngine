#include <JBro/TextRendering/TextBlock.h>

#include <JBro/Types/Hash.h>

#include <algorithm>
#include <bit>
#include <cmath>

namespace JBro
{
    namespace
    {
        void Mix(std::uint64_t& key, std::uint64_t value)
        {
            std::size_t seed = static_cast<std::size_t>(key);
            HashCombine(seed, static_cast<std::size_t>(value));
            key = static_cast<std::uint64_t>(seed);
        }

        std::uint64_t Bits(float value)
        {
            return std::bit_cast<std::uint32_t>(value);
        }
    }

    std::uint64_t TextBlock::MakeOptionsKey(const TextBlockSettings& settings)
    {
        // 크기는 값 그대로 넣는다. SDF 는 소수 크기로 레이아웃하므로 반올림한 픽셀이 같아도 다시 해야 한다.
        std::uint64_t key = Bits(settings.fontSize);
        Mix(key, Bits(settings.boxWidth));
        Mix(key, Bits(settings.boxHeight));
        Mix(key, static_cast<std::uint64_t>(settings.overflow));
        Mix(key, static_cast<std::uint64_t>(settings.wrapMode));
        Mix(key, static_cast<std::uint64_t>(settings.alignX));
        Mix(key, static_cast<std::uint64_t>(settings.alignY));
        Mix(key, Bits(settings.lineSpacing));
        Mix(key, Bits(settings.letterSpacing));
        Mix(key, settings.autoSize ? 1u : 0u);
        Mix(key, Bits(settings.minFontSize));
        Mix(key, Bits(settings.maxFontSize));
        Mix(key, settings.pixelSnap ? 1u : 0u);
        Mix(key, settings.richText ? 1u : 0u);
        return key;
    }

    bool TextBlock::GatherFonts(TextLibrary& library, const TextBlockSettings& settings, AssetHandle* handles, FontView* views,
        std::uint32_t& count, StyleFaces& styles) const
    {
        styles = {};
        // **`fontId` 가 비면 프로젝트의 첫 폰트다**(D-200 (6)). 아이디를 적었는데 그 폰트가 없으면 대신 기본 폰트로 그리지 않는다 -
        // 고른 폰트가 깨졌다는 것이 보여야 한다(경고가 뜬다).
        count = 0;
        const ArrayView<const AssetHandle> project = library.GetProjectFonts();
        AssetHandle primary = settings.font;
        if (settings.fontId.IsNull())
        {
            primary = project.Size() > 0 ? project[0] : AssetHandle{};
        }
        // **패밀리면 Regular 가 기본 face 이고 나머지 칸이 스타일 face 다**(D-224). Regular 칸이 비면 처음 찬 칸이 기본이다.
        AssetHandle family[4];
        const bool isFamily = library.GetFamilyFonts(primary, family);
        if (isFamily)
        {
            primary = AssetHandle{};
            for (const AssetHandle slot : family)
            {
                if (slot.generation != 0)
                {
                    primary = slot;
                    break;
                }
            }
        }
        if (false == library.Acquire(primary, views[0]))
        {
            return false;
        }
        handles[0] = primary;
        count = 1;
        if (isFamily)
        {
            std::uint16_t* targets[3] = { &styles.bold, &styles.italic, &styles.boldItalic };
            for (std::size_t slot = 1; slot < 4 && count < MaxFaces; ++slot)
            {
                const AssetHandle font = family[slot];
                if (font.generation == 0)
                {
                    continue;
                }
                // 같은 폰트가 이미 모였으면 그 번호를 쓴다(Regular 와 같은 폰트를 굵게 칸에 둔 패밀리).
                std::uint32_t found = count;
                for (std::uint32_t face = 0; face < count; ++face)
                {
                    if (handles[face].index == font.index && handles[face].generation == font.generation)
                    {
                        found = face;
                        break;
                    }
                }
                if (found == count)
                {
                    if (false == library.Acquire(font, views[count]))
                    {
                        continue;
                    }
                    handles[count] = font;
                    ++count;
                }
                *targets[slot - 1] = static_cast<std::uint16_t>(found);
            }
        }
        // 나머지 프로젝트 폰트가 폴백이다. 기본 폰트와 같은 것은 건너뛰고, 열리지 않는 것은 빼고 간다.
        for (std::size_t index = 0; index < project.Size() && count < MaxFaces; ++index)
        {
            const AssetHandle fallback = project[index];
            bool already = false;
            for (std::uint32_t face = 0; face < count; ++face)
            {
                already = already || (handles[face].index == fallback.index && handles[face].generation == fallback.generation);
            }
            if (already)
            {
                continue;
            }
            if (library.Acquire(fallback, views[count]))
            {
                handles[count] = fallback;
                ++count;
            }
        }
        return true;
    }

    TextBlock::UpdateResult TextBlock::Update(TextLibrary& library, const TextBlockSettings& settings)
    {
        AssetHandle handles[MaxFaces];
        FontView views[MaxFaces];
        std::uint32_t count = 0;
        StyleFaces styles;
        if (false == GatherFonts(library, settings, handles, views, count, styles))
        {
            // 폰트가 돌아오면 face 수가 달라 다시 레이아웃된다.
            m_fontCount = 0;
            m_quads.Clear();
            m_hasBounds = false;
            return UpdateResult::NoFont;
        }
        const TextStore& store = TextStore::Get();
        bool stale = m_text.index != settings.text.index
            || m_text.generation != settings.text.generation
            || m_textRevision != store.GetRevision(settings.text)
            || m_fontCount != count
            || m_optionsKey != MakeOptionsKey(settings)
            || m_styles.bold != styles.bold || m_styles.italic != styles.italic || m_styles.boldItalic != styles.boldItalic;
        for (std::uint32_t face = 0; false == stale && face < count; ++face)
        {
            stale = m_fonts[face].index != handles[face].index
                || m_fonts[face].generation != handles[face].generation
                || m_fontGenerations[face] != views[face].dataGeneration
                || m_atlasGenerations[face] != views[face].atlasGeneration;
        }
        if (false == stale)
        {
            return UpdateResult::Unchanged;
        }
        Relayout(settings, handles, views, count, styles);
        return UpdateResult::Relaid;
    }

    void TextBlock::Relayout(const TextBlockSettings& settings, const AssetHandle* handles, const FontView* views,
        std::uint32_t count, const StyleFaces& styles)
    {
        const FontView& font = views[0];
        // **SDF 는 크기를 반올림하지 않는다**(4 단계). 거리장 한 벌을 키우고 줄이므로 크기가 조금씩 바뀌는 연출(트윈)에 새 글리프가
        // 생기지 않는다 - 비트맵은 정수 크기마다 새로 떠 아틀라스가 크기 수만큼 자랐다(text-plan §7).
        const bool sdf = font.renderMode == FontRenderMode::Sdf;
        const float maxSize = static_cast<float>(Text::GlyphAtlas::MaxPixelSize);
        const auto sizeOf = [&](float requested) {
            return sdf ? std::clamp(std::isfinite(requested) ? requested : 1.0f, 1.0f, maxSize)
                       : static_cast<float>(GlyphPixelSize(requested));
        };
        float layoutSize = sizeOf(settings.fontSize);
        Text::LayoutOptions options;
        options.fontSize = layoutSize;
        options.boxWidth = std::max(0.0f, settings.boxWidth);
        options.boxHeight = std::max(0.0f, settings.boxHeight);
        options.overflow = settings.overflow;
        options.wrapMode = settings.wrapMode;
        options.alignX = settings.alignX;
        options.alignY = settings.alignY;
        options.lineSpacing = settings.lineSpacing;
        options.letterSpacing = settings.letterSpacing;
        options.richText = settings.richText;
        // 비트맵은 정수 크기마다 뜨므로 `<size>` 도 정수로 잰다.
        options.wholePixelMarkup = false == sdf;
        options.boldFace = styles.bold;
        options.italicFace = styles.italic;
        options.boldItalicFace = styles.boldItalic;
        m_styles = styles;

        m_text = settings.text;
        m_textRevision = TextStore::Get().GetRevision(settings.text);
        m_fontCount = count;
        for (std::uint32_t face = 0; face < count; ++face)
        {
            m_fonts[face] = handles[face];
            m_fontGenerations[face] = views[face].dataGeneration;
            m_atlasGenerations[face] = views[face].atlasGeneration;
        }
        m_optionsKey = MakeOptionsKey(settings);
        m_pixelsPerUnit = font.pixelsPerUnit;
        m_sdf = sdf;
        m_sdfSpread = font.sdfSpread;
        m_filter = font.filter;
        m_quads.Clear();
        m_hasBounds = false;

        const Text::FontFace* faces[MaxFaces] = {};
        for (std::uint32_t face = 0; face < count; ++face)
        {
            faces[face] = views[face].face;
        }
        const ArrayView<const Text::FontFace* const> faceView(faces, count);
        const ArrayView<const char> utf8 = TextStore::Get().GetText(settings.text);
        // 자동 크기는 상자가 있을 때만 뜻이 있다. 크기를 먼저 찾고, 그 크기로 레이아웃이 남는다.
        const bool fitToBox = settings.autoSize && (options.boxWidth > 0.0f || options.boxHeight > 0.0f);
        Text::LayoutError built = Text::LayoutError::None;
        if (fitToBox)
        {
            float chosen = layoutSize;
            built = m_layout.BuildToFit(utf8, faceView, options, sizeOf(std::min(settings.minFontSize, settings.maxFontSize)),
                sizeOf(std::max(settings.minFontSize, settings.maxFontSize)), sdf ? 0.0f : 1.0f, chosen);
            layoutSize = chosen;
        }
        else
        {
            built = m_layout.Build(utf8, faceView, options);
        }
        m_fittedSize = layoutSize;
        if (built != Text::LayoutError::None)
        {
            return;
        }
        GlyphMeshOptions mesh;
        mesh.sdf = sdf;
        mesh.sdfSize = font.sdfSize;
        mesh.sdfSpread = font.sdfSpread;
        mesh.pixelSnap = settings.pixelSnap;
        // Clip 은 상자 밖으로 나간 글리프를 잘라 낸다 - 레이아웃은 줄만 버렸고, 반쯤 걸친 글리프의 사각형과 UV 를 줄인다.
        mesh.clip = settings.overflow == Text::Overflow::Clip && options.boxWidth > 0.0f && options.boxHeight > 0.0f;
        BuildGlyphQuads(m_layout, views, count, mesh, m_quads);
        m_hasBounds = true;
    }

    ArrayView<const GlyphQuad> TextBlock::GetQuads() const
    {
        return ArrayView<const GlyphQuad>(m_quads.Data(), m_quads.Size());
    }

    AssetHandle TextBlock::GetFont(std::uint32_t face) const
    {
        return face < m_fontCount ? m_fonts[face] : AssetHandle{};
    }

    std::uint32_t TextBlock::GetFontCount() const
    {
        return m_fontCount;
    }

    float TextBlock::GetPixelsPerUnit() const
    {
        return m_pixelsPerUnit;
    }

    TextureFilter TextBlock::GetFilter() const
    {
        return m_filter;
    }

    bool TextBlock::IsSdf() const
    {
        return m_sdf;
    }

    std::uint32_t TextBlock::GetSdfSpread() const
    {
        return m_sdfSpread;
    }

    float TextBlock::GetFittedSize() const
    {
        return m_fittedSize;
    }

    bool TextBlock::GetBounds(float& minX, float& minY, float& maxX, float& maxY) const
    {
        if (false == m_hasBounds)
        {
            return false;
        }
        minX = m_layout.GetMinX();
        minY = m_layout.GetMinY();
        maxX = m_layout.GetMaxX();
        maxY = m_layout.GetMaxY();
        return true;
    }
}
