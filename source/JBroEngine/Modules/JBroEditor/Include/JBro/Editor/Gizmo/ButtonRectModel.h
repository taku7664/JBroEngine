#pragma once

#include <JBro/Types/Math2D.h>

#include <cstdint>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>

// 캔버스 뷰의 `Button2D` 누름 사각형 편집(D-351 1 단계)에서 화면을 모르는 부분이다. `LightGizmoModel` 처럼 ImGui 없이 오브젝트 로컬 숫자로 잰다.
// 사각형은 `offset` 을 가운데에 둔 `size` 이고(`Button2DSystem::HitTest` 와 같은 뜻, 크기는 절댓값), 손잡이는 네 변의 가운데와 사각형의 가운데다.
// 변 손잡이는 그 변만 옮긴다 - 맞은편 변은 그대로라 `size` 와 `offset` 이 함께 바뀐다. 가운데 손잡이는 `offset` 만 옮긴다.
namespace JBro::ButtonRectModel
{
    // 손잡이를 잡는 반지름(화면 픽셀).
    inline constexpr Float HandlePickRadius = 7.0f;

    enum class Handle : std::uint8_t
    {
        None,
        Left,
        Right,
        Bottom,
        Top,
        Center,
    };

    // 사각형의 로컬 모서리 넷이다(왼쪽 아래부터 반시계).
    void Corners(Vector2 offset, Vector2 size, Vector2 corners[4]);
    // 손잡이의 로컬 자리다. 변 손잡이는 그 변의 가운데, `Center` 는 `offset` 이다.
    Vector2 HandleLocal(Vector2 offset, Vector2 size, Handle handle);

    // 끈 결과다. 잡은 자리에서 마우스가 간 만큼(로컬) 그 변을 옮긴다 - 잡은 자리가 손잡이와 조금 어긋나도 튀지 않는다.
    // 변은 맞은편 변을 넘지 않는다(크기가 0 아래로 가지 않는다). 가운데 손잡이는 사각형을 통째로 옮긴다.
    void Drag(Handle handle, Vector2 startOffset, Vector2 startSize, Vector2 grabLocal, Vector2 mouseLocal, Vector2& offset, Vector2& size);

    // 그림에 맞춘다: 크기는 그림 크기의 절댓값이고, 자리는 피벗이 가운데가 아닌 만큼 옮긴 것이다 - `offset = (0.5 - pivot) x 크기`.
    // 크기가 0 인 축이 있으면 거짓이고 결과를 건드리지 않는다.
    Bool FitToSprite(Vector2 spriteSize, Vector2 pivot, Vector2& size, Vector2& offset);

    // 손잡이의 ImGui Id 다.
    const char* HandleId(Handle handle);
}
