#pragma once

#include <JBro/Text/TextLayout.h>
#include <JBro/TextRendering/TextLibrary.h>
#include <JBro/Types/Array.h>

#include <cstdint>

// 레이아웃한 글리프를 아틀라스 칸의 사각형으로 바꾼다(D-222). 2D 텍스트와 3D 텍스트가 같이 쓴다 - 사각형은 텍스트 로컬의 **글자 픽셀**
// (블록 기준점 원점, y 위쪽)이고, 월드로 옮기는 것(PPU·변환)은 각 프레임워크의 일이다.
namespace JBro
{
    struct GlyphQuad
    {
        // 왼쪽 위 모서리와 크기(글자 픽셀)다.
        float         left = 0.0f;
        float         top = 0.0f;
        float         width = 0.0f;
        float         height = 0.0f;
        float         uvRect[4] = { 0.0f, 0.0f, 1.0f, 1.0f };
        std::uint16_t page = 0;
        std::uint8_t  face = 0;          // views 안의 번호. 폴백 face 의 글리프는 그 폰트의 아틀라스에 있다
        bool          hasTint = false;   // 리치 텍스트의 `<color>` 가 붙은 글자다
        std::uint8_t  tint[4] = { 255, 255, 255, 255 };
        // 거리장 픽셀 / 글자 픽셀이다. 외곽선 폭(글자 픽셀)을 문턱으로 바꿀 때 쓴다. 글자 크기가 섞이면 글자마다 다르다.
        float         sdfPerTextPixel = 1.0f;
    };

    struct GlyphMeshOptions
    {
        bool          sdf = false;
        std::uint32_t sdfSize = 48;     // 기본 폰트의 거리장 크기. 폴백 글자도 이 크기로 뜬다(한 텍스트는 한 셰이더)
        std::uint32_t sdfSpread = 8;
        bool          pixelSnap = false; // 글리프 원점을 정수 글자 픽셀로 반올림한다
        bool          clip = false;      // 블록 밖으로 나간 조각을 사각형과 UV 째 잘라 낸다(Overflow::Clip)
    };

    // 비트맵은 정수 크기로 뜬다. 레이아웃도 같은 크기로 해야 글자 사이가 비트맵과 맞는다. 1 ~ `GlyphAtlas::MaxPixelSize` 로 자른다.
    std::uint32_t GlyphPixelSize(float fontSize);

    // 글리프마다 아틀라스 칸을 잡고(없으면 뜬다) 사각형을 쌓는다. views[0] 이 기본 폰트다. 칸을 못 잡은 글리프·빈 글리프는 빠진다.
    // out 은 비우고 채운다(용량은 다시 쓴다).
    void BuildGlyphQuads(const Text::TextLayout& layout, const FontView* views, std::uint32_t viewCount,
        const GlyphMeshOptions& options, Array<GlyphQuad>& out);

    // 외곽선 폭(글자 픽셀)을 거리장의 문턱으로 바꾼다. 거리값은 외곽선에서 0.5 이고 거리장 한 칸마다 0.5 / 퍼짐씩 준다. 폭은 퍼짐보다
    // 한 칸 안쪽까지로 자른다 - 문턱이 0 에 닿으면 글자 칸 전체가 외곽선이 된다(text-plan §1.2 의 7 번). 외곽선이 없으면 0.5 다.
    float SdfOutlineEdge(float outlineWidth, float sdfPerTextPixel, std::uint32_t sdfSpread);
}
