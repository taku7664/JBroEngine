#pragma once

#include <JBro/Editor/Widget/Common.h>
#include <JBro/Types/ArrayView.h>

namespace JBro::Widget
{
    // 소리 크기 막대다(D-203, 기존 `ImAudioVisualizer` 의 미터 자리). `level` 은 0..1 의 최대 절댓값이고 막대는 데시벨로
    // 그린다(-60 dB 가 빈 막대, 0 dB 가 가득). 1 을 넘으면(클리핑) 끝을 경고 색으로 칠한다. 남은 폭을 다 쓴다.
    void LevelMeter(const char* id, float level, float height = 0.0f);

    // 스펙트럼 막대들이다. `bands` 는 칸마다 0..1 이고 왼쪽이 낮은 소리다. 남은 폭을 다 쓴다.
    void Spectrum(const char* id, ArrayView<const float> bands, float height);
}
