#pragma once

#include "Vector2T.h"

// Layout2D
// ────────────────────────────────────────────────────────────────────────────
// Normalized + Pixel 두 채널로 화면 위치 / 크기를 표현합니다.
//
//   실제값 = Normalized * (resolutionW, resolutionH) + Pixel
//
// 표기 규칙 (Normalized.x, Normalized.y)(Pixel.x, Pixel.y)
//   Position 기본값 : (0,0)(0,0)  → 좌상단 원점
//   Size     기본값 : (1,1)(0,0)  → 전체 해상도 크기
// ────────────────────────────────────────────────────────────────────────────
struct Layout2D
{
    Vector2<float> Normalized;  // [0,1] 정규화 비율
    Vector2<float> Pixel;       // 픽셀 오프셋 (더하기)

    Layout2D() = default;
    Layout2D(Vector2<float> normalized, Vector2<float> pixel)
        : Normalized(normalized), Pixel(pixel)
    {}

    // resW × resH 해상도 기준으로 실제 픽셀 값을 계산합니다.
    Vector2<float> Resolve(float resW, float resH) const
    {
        return Vector2<float>(
            Normalized.x * resW + Pixel.x,
            Normalized.y * resH + Pixel.y
        );
    }

    bool operator==(const Layout2D& rhs) const
    {
        return Normalized == rhs.Normalized && Pixel == rhs.Pixel;
    }
    bool operator!=(const Layout2D& rhs) const { return !(*this == rhs); }
};
