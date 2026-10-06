#pragma once

#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/UInt.h>

namespace JBro::Widget::ItemLocator
{
    // 화면 좌표의 사각형이다(ImGui 를 이 헤더에 들이지 않으려고 따로 둔다 - 호스트의 include 경로에는 imgui 가 없다).
    struct ItemRect
    {
        Float minX = 0.0f;
        Float minY = 0.0f;
        Float maxX = 0.0f;
        Float maxY = 0.0f;
    };

    // **ImGui 항목이 그려진 자리를 적는다**(D-292). ImGui 의 시험 엔진 훅(`IMGUI_ENABLE_TEST_ENGINE`)으로 받는다 - 지켜볼 Id 를 정하면 지금 ImGui
    // 문맥의 다음 프레임부터 그 Id 의 항목이 더해질 때마다 사각형을 적는다(창의 클립 영역과 겹친 보이는 부분만). 시험이 마우스로 창을 훑어 항목을
    // 찾던 것을 한 프레임으로 줄이려고 둔다. 지켜보지 않는 동안 ImGui 는 항목마다 참거짓 검사 하나만 한다.
    void Watch(UInt32 id);
    // 지켜본 항목이 보였으면 그 사각형을 주고 참이다. 지켜보기를 그만둔다.
    Bool Take(ItemRect& rect);
    // 지금 무언가를 지켜보는가. ImGui 항목이 아니라 직접 판정하는 손잡이(기즈모)가 제 사각형을 셀 것인지 정한다.
    Bool IsWatching();
    // ImGui 항목 없이 `SetHoveredID` 로 직접 가리켜지는 것(기즈모 손잡이)이 제 사각형을 알린다. 지켜보는 Id 일 때만 적는다.
    void Report(UInt32 id, const ItemRect& rect);
}
