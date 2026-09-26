#include <JBro/TextRendering/GlyphMesh.h>

#include <algorithm>
#include <cmath>

namespace JBro
{
    std::uint32_t GlyphPixelSize(float fontSize)
    {
        const long rounded = std::isfinite(fontSize) ? std::lround(fontSize) : 0;
        return static_cast<std::uint32_t>(std::clamp<long>(rounded, 1, static_cast<long>(Text::GlyphAtlas::MaxPixelSize)));
    }

    void BuildGlyphQuads(const Text::TextLayout& layout, const FontView* views, std::uint32_t viewCount,
        const GlyphMeshOptions& options, Array<GlyphQuad>& out)
    {
        out.Clear();
        if (views == nullptr || viewCount == 0)
        {
            return;
        }
        const float clipLeft = layout.GetMinX();
        const float clipRight = layout.GetMaxX();
        const float clipBottom = layout.GetMinY();
        const float clipTop = layout.GetMaxY();
        for (const Text::PositionedGlyph& glyph : layout.GetGlyphs())
        {
            // 글리프는 그것을 고른 face 의 아틀라스에 든다. 폴백 폰트의 글자는 그 폰트의 페이지로 그린다.
            const std::uint32_t faceIndex = glyph.face < viewCount ? glyph.face : 0;
            const FontView& glyphFont = views[faceIndex];
            if (glyphFont.atlas == nullptr || glyphFont.face == nullptr)
            {
                continue;
            }
            const float pageSize = static_cast<float>(glyphFont.atlas->GetPageSize());
            // 글자마다 크기가 다를 수 있다(리치 텍스트). SDF 는 거리장 한 벌을 그 크기로 키우고, 비트맵은 그 정수 크기로 뜬다.
            const float glyphSize = glyph.size > 0.0f ? glyph.size : 1.0f;
            const float cellScale = options.sdf ? glyphSize / static_cast<float>(options.sdfSize) : 1.0f;
            Text::AtlasGlyph cell;
            const Text::AtlasError placed = options.sdf
                ? glyphFont.atlas->EnsureSdf(*glyphFont.face, options.sdfSize, options.sdfSpread, glyph.glyph, cell)
                : glyphFont.atlas->Ensure(*glyphFont.face, GlyphPixelSize(glyphSize), glyph.glyph, cell);
            if (placed != Text::AtlasError::None || cell.empty)
            {
                continue;
            }
            GlyphQuad quad;
            const float originX = options.pixelSnap ? std::round(glyph.x) : glyph.x;
            const float originY = options.pixelSnap ? std::round(glyph.y) : glyph.y;
            quad.left = originX + static_cast<float>(cell.left) * cellScale;
            quad.top = originY + static_cast<float>(cell.top) * cellScale;
            quad.width = static_cast<float>(cell.width) * cellScale;
            quad.height = static_cast<float>(cell.height) * cellScale;
            float u0 = static_cast<float>(cell.x) / pageSize;
            float v0 = static_cast<float>(cell.y) / pageSize;
            float u1 = static_cast<float>(cell.x + cell.width) / pageSize;
            float v1 = static_cast<float>(cell.y + cell.height) / pageSize;
            if (options.clip)
            {
                float right = quad.left + quad.width;
                float bottom = quad.top - quad.height;
                if (right <= clipLeft || quad.left >= clipRight || bottom >= clipTop || quad.top <= clipBottom)
                {
                    continue;
                }
                if (quad.left < clipLeft)
                {
                    u0 += (u1 - u0) * (clipLeft - quad.left) / quad.width;
                    quad.left = clipLeft;
                }
                if (right > clipRight)
                {
                    u1 -= (u1 - u0) * (right - clipRight) / (right - quad.left);
                    right = clipRight;
                }
                if (quad.top > clipTop)
                {
                    v0 += (v1 - v0) * (quad.top - clipTop) / quad.height;
                    quad.top = clipTop;
                }
                if (bottom < clipBottom)
                {
                    v1 -= (v1 - v0) * (clipBottom - bottom) / (quad.top - bottom);
                    bottom = clipBottom;
                }
                quad.width = right - quad.left;
                quad.height = quad.top - bottom;
            }
            quad.uvRect[0] = u0;
            quad.uvRect[1] = v0;
            quad.uvRect[2] = u1 - u0;
            quad.uvRect[3] = v1 - v0;
            quad.page = cell.page;
            quad.face = static_cast<std::uint8_t>(faceIndex);
            quad.sdfPerTextPixel = options.sdf ? static_cast<float>(options.sdfSize) / glyphSize : 1.0f;
            if (glyph.hasColor)
            {
                quad.hasTint = true;
                for (int channel = 0; channel < 4; ++channel)
                {
                    quad.tint[channel] = static_cast<std::uint8_t>((glyph.color >> (channel * 8)) & 0xFFu);
                }
            }
            out.Add(quad);
        }
    }

    float SdfOutlineEdge(float outlineWidth, float sdfPerTextPixel, std::uint32_t sdfSpread)
    {
        if (false == (outlineWidth > 0.0f) || sdfSpread <= 1)
        {
            return 0.5f;
        }
        const float spread = static_cast<float>(sdfSpread);
        const float width = std::min(outlineWidth * sdfPerTextPixel, spread - 1.0f);
        return 0.5f - width * (0.5f / spread);
    }
}
