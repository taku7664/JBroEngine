#pragma once

#include <JBro/Editor/Widget/Common.h>

namespace JBro::Widget
{
    // 바탕이 없는 버튼이다. 눌리는 자리는 보통 버튼과 같고 테두리와 바탕색만 없다.
    //
    // 목록의 삭제 표시처럼 **글자 하나가 곧 버튼**인 자리에 쓴다. 보통 버튼을 쓰면
    // 글자 하나 주위로 상자가 생겨 줄이 시끄러워진다. 올려놓으면 글자색이 바뀐다.
    bool TextButton(
        const char* label,
        const ImVec2& size = ImVec2(0.0f, 0.0f),
        const ImVec2& offset = ImVec2(0.0f, 0.0f),
        ImGuiButtonFlags flags = ImGuiButtonFlags_None);
}
