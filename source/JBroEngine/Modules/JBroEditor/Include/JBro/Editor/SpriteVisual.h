#pragma once

#include <JBro/Types/Math2D.h>

namespace JBro
{
    class AssetSystem;
    class Layer;
    namespace Component
    {
        class SpriteRenderer2D;
    }

    // **스프라이트가 그려지는 크기와 피벗**이다(오브젝트 로컬). 그리는 쪽(`SpriteRender2DSystem`)과 같은 규칙이다 - `sizeMode` 가 `FromSprite` 면
    // 칸의 픽셀을 그 에셋의 PPU 로 나눈 값이고 화면 레이어는 픽셀 그대로다(D-117·D-237), 아니면 컴포넌트의 `size` 다. 피벗도 같은 식으로 고른다.
    // `flip` 은 크기의 부호다. 캔버스 뷰의 집기 사각형과 버튼의 `스프라이트에 맞추기` 가 함께 쓴다 - 두 곳이 따로 세면 어긋난다.
    struct SpriteVisual
    {
        Vector2 size;
        Vector2 pivot{ 0.5f, 0.5f };
    };

    SpriteVisual ResolveSpriteVisual(const AssetSystem* assets, const Component::SpriteRenderer2D& sprite, const Layer* layer);
}
