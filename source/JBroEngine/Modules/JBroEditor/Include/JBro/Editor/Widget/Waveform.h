#pragma once

#include <JBro/Editor/Widget/Common.h>
#include <JBro/Types/ArrayView.h>

namespace JBro::Widget
{
    // 오디오의 파형과 재생 위치다(D-197, 기존 `ImAudioVisualizer` 의 파형 자리). 봉우리는 칸마다 0..1 의 최대 절댓값이고
    // 가운데 줄을 기준으로 위아래로 그린다. `progress` 가 0..1 이면 그 자리에 세로줄을 긋고 지난 쪽을 진하게 칠한다.
    //
    // 파형을 누르거나 끌면 참이고 `seekFraction` 에 그 자리(0..1)가 온다 - 부르는 쪽이 그 자리로 옮긴다.
    // 봉우리가 없으면 빈 칸으로 자리만 지킨다 - 파형을 재는 동안 아래 줄이 프레임마다 움직이지 않게.
    bool Waveform(const char* id, ArrayView<const float> peaks, float progress, float height, float& seekFraction);
}
