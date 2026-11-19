#pragma once

#include <JBro/Editor/Widget/Common.h>
#include <JBro/RHI/RHI.h>

#include <imgui.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/UInt.h>

namespace JBro::Widget
{
    // 엔진이 그린 화면 한 장을 패널에 붙이는 위젯이다(D-413). 시뮬레이션 뷰가 `ImGui::Image`·`GetContentRegionAvail` 을
    // 직접 불러 §11.1 을 어기던 자리를 여기로 옮겼다.
    struct ViewportRect
    {
        // 패널에서 남은 자리 전체(바탕을 칠한 곳)다. 자리가 없으면 `hasPanel` 이 거짓이고 아무것도 그리지 않았다.
        Bool hasPanel = false;
        Float panelLeft = 0.0f;
        Float panelTop = 0.0f;
        Float panelWidth = 0.0f;
        Float panelHeight = 0.0f;
        // 그림이 실제로 앉은 사각형이다. 그림이 없으면 `hasImage` 가 거짓이고 크기는 0 이다.
        Bool hasImage = false;
        Float imageLeft = 0.0f;
        Float imageTop = 0.0f;
        Float imageWidth = 0.0f;
        Float imageHeight = 0.0f;
    };

    // 남은 자리를 바탕색으로 칠하고, 그림을 비율을 지켜 가운데 붙인다(레터박스). 자리를 차지하지만 **입력은 받지 않는다** -
    // 누르기·끌기를 받는 화면은 `HitArea` 를 따로 둔다. 그림이 없거나(무효 핸들) 크기가 0 이면 바탕만 칠한다.
    ViewportRect ViewportImage(TextureHandle texture, Extent2D extent);

    // 화면 위에 겹쳐 쓰는 상태 줄 하나다. `line` 번째 줄(0 부터)에 아이콘과 글자를 같은 색으로 쓴다. 자리를 잡지 않는다.
    void ViewportStatusLine(const ViewportRect& rect, UInt32 line, const char* icon, ImU32 color, const char* text);
}
