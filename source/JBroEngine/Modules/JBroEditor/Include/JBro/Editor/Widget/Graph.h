#pragma once

#include <JBro/Editor/Widget/Common.h>
#include <JBro/Types/ArrayView.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>

#include <cstdint>

namespace JBro::Widget
{
    // 그래프 축의 눈금 하나다. `label` 이 비면 줄만 긋는다. 글자는 값이라 번역하지 않는다(`1k`, `-12`).
    struct GraphTick
    {
        Float value = 0.0f;
        const char* label = nullptr;
    };

    struct GraphAxis
    {
        Float min = 0.0f;
        Float max = 1.0f;
        // 몇 자릿수에 걸친 축(주파수)이다. `min` 은 0 보다 커야 한다.
        Bool logarithmic = false;
        ArrayView<const GraphTick> ticks;
    };

    enum class GraphStyle : std::uint8_t
    {
        // 강조색 선이다.
        Line,
        // 강조색 선과, 선에서 `baseline` 까지 옅게 칠한 면이다.
        Area,
        // `baseline` 에서 선 세로 막대와 끝의 점이다(메아리의 되풀이처럼 띄엄띄엄한 값).
        Stems,
        // 흐린 참고선이다(원음·꺼졌을 때의 모양).
        Guide,
    };

    struct GraphSeries
    {
        ArrayView<const Float> xs;
        ArrayView<const Float> ys;
        GraphStyle style = GraphStyle::Line;
        // `Area`·`Stems` 의 바닥이다(세로 축의 값).
        Float baseline = 0.0f;
    };

    struct GraphDesc
    {
        GraphAxis x;
        GraphAxis y;
        ArrayView<const GraphSeries> series;
        // 0 이면 줄 높이의 여섯 배다.
        Float height = 0.0f;
        // 세로 표시줄이다(가로 축의 값) - 차단 주파수·문턱처럼 지금 고치는 자리를 짚는다.
        ArrayView<const Float> markers;
        // 그림 칸 위의 제 줄에 흐리게 적는 제목이다(무엇을 그렸는지, 축의 단위). 부르는 쪽이 번역해 넘긴다.
        const char* title = nullptr;
    };

    // **작은 응답 그래프다**(D-309). 남은 폭을 다 쓴다. 바탕·눈금과 그 글자·시리즈·표시줄을 그린다. 누르는 것은 없다 -
    // 값은 아래 슬라이더가 고치고 그래프는 그 결과를 보인다. 시리즈의 점은 축 범위로 자른다.
    void Graph(const char* id, const GraphDesc& desc);
}
