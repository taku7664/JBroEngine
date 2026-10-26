#pragma once

#include <JBro/Editor/Widget/Common.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/UInt.h>

namespace JBro::Widget
{
    // **구간 막대**다(렌더 그래프 창의 텍스처 수명, D-340). 칸 `count` 개를 남은 폭에 고르게 펴고 `[first, last]` 칸을 `color` 로 칠한다.
    // `marker` 칸에는 세로 선을 긋는다(고른 패스) - 여러 줄에 같은 칸으로 그으면 줄을 가로지르는 선이 된다. `marker` 가 `count` 이상이면
    // 긋지 않는다. `strong` 이면 막대를 진하게 칠한다(고른 패스가 쓰는 텍스처). 마우스를 올리면 참이다 - 부르는 쪽이 툴팁을 띄운다.
    Bool SpanBar(const char* id, UInt32 first, UInt32 last, UInt32 count, const ImVec4& color, UInt32 marker, Bool strong,
        Float height = 0.0f);
}
