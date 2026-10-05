#pragma once

#include <JBro/Editor/Widget/Common.h>

namespace JBro::Widget
{
    // 바탕이 없는 버튼이다. 눌리는 자리는 보통 버튼과 같고 테두리와 바탕색만 없다.
    //
    // 목록의 삭제 표시처럼 **글자 하나가 곧 버튼**인 자리에 쓴다. 보통 버튼을 쓰면
    // 글자 하나 주위로 상자가 생겨 줄이 시끄러워진다. 올려놓으면 글자색이 바뀐다.
    //
    // 이름이 글자 하나(코드 포인트 하나, 곧 아이콘)면 그 글리프의 잉크를 버튼 한가운데 둔다(`DrawGlyphCentered`).
    bool TextButton(
        const char* label,
        const ImVec2& size = ImVec2(0.0f, 0.0f),
        const ImVec2& offset = ImVec2(0.0f, 0.0f),
        ImGuiButtonFlags flags = ImGuiButtonFlags_None);

    // 글자가 코드 포인트 하나뿐인가. 아이콘 글리프가 그렇다.
    bool IsSingleGlyph(const char* text);

    // 글리프 하나의 **잉크**(실제로 칠해지는 부분)를 `center` 칸에 놓을 글자의 왼쪽 위 자리다(D-277).
    // 가로는 칸의 가운데, 세로는 **같은 칸에 놓인 본문 글자의 가운데**(대문자 `H` 의 잉크 가운데)다.
    //
    // 글자처럼 줄 상자로 가운데를 잡으면 아이콘이 어긋난다 - 합친 아이콘 글꼴의 글리프는 본문 글꼴의 기준선에 앉고,
    // 그림이 em 상자 안 어디에 있는지는 글리프마다 다르다. 구운 글리프의 사각형도 잉크에 꼭 맞지 않아(래스터화 여백)
    // 아틀라스의 픽셀로 잉크를 잰다. 그래서 아이콘을 바꾸어도, 글꼴과 크기가 바뀌어도 숫자를 다시 맞추지 않는다.
    // 지금 글꼴(`PushFont`) 기준이고, `fontSize` 가 0 이면 지금 크기다(에셋 칸의 큰 아이콘은 크기를 준다).
    ImVec2 GlyphCenteredPosition(const char* glyph, const ImVec2& center, float fontSize = 0.0f);

    // 글리프 하나를 `min..max` 칸의 한가운데 그린다. 자리를 잡지 않는다(그리기만 한다).
    void DrawGlyphCentered(const char* glyph, const ImVec2& min, const ImVec2& max, ImU32 color, float fontSize = 0.0f);

    // 누르지 않는 아이콘 하나다. 줄 높이의 정사각형 자리를 잡고 그 한가운데 그린다(콤보 앞의 표시 같은 것, D-278).
    void Icon(const char* glyph);

    // **글자 앞의 작은 아이콘**이다(D-278). 글줄 높이의 정사각형 자리를 잡아 그리고 같은 줄에 이어 둔다 - 다음 항목(글자·
    // 고르는 줄)이 그 뒤에서 시작한다. 색을 주지 않으면 지금 글자색이다. 글자와 같은 높이에 선다(`DrawGlyphCentered`).
    void InlineIcon(const char* glyph);
    void InlineIcon(const char* glyph, ImU32 color);
}
