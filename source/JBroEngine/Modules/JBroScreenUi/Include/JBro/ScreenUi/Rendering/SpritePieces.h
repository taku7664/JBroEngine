#pragma once

#include <JBro/Types/Float.h>
#include <JBro/Types/UInt.h>

// 스프라이트 하나를 조각으로 편다(D-351, ui-plan §6.2 2 단계). 9-슬라이스·타일·채우기는 렌더러를 모르는 이 셈으로 끝나고,
// 렌더 브리지가 조각마다 `SpriteSubmit` 을 낸다 - 렌더 월드의 아이템 수·정렬·선택 마스크는 그대로다.
namespace JBro
{
    struct SpriteRenderItem;

    // 한 조각이다. `g*` 는 그려지는 사각형 안의 비율(0..1, 왼쪽 아래 원점), `t*` 는 칸 안의 텍스처 비율(0..1, 왼쪽 아래 원점)이다.
    // 같은 비율이면 그 조각은 늘지 않고(모서리), 다르면 그 비율만큼 늘거나 줄어든다(변·가운데).
    struct SpritePiece
    {
        Float gu0 = 0.0f;
        Float gu1 = 1.0f;
        Float gv0 = 0.0f;
        Float gv1 = 1.0f;
        Float tu0 = 0.0f;
        Float tu1 = 1.0f;
        Float tv0 = 0.0f;
        Float tv1 = 1.0f;
    };

    // 온 조각이다 - `Simple` 과 같다.
    constexpr SpritePiece WholeSpritePiece()
    {
        return SpritePiece{};
    }

    // 아이템 하나가 낼 수 있는 조각의 상한이다. 타일은 여기서 끊긴다(왼쪽 아래부터).
    inline constexpr UInt32 MaxSpritePieces = 256;

    // `drawMode` 대로 아이템을 조각으로 편다. 돌려주는 값은 조각 수이고 `capacity` 를 넘지 않는다.
    //   Simple          온 조각 하나.
    //   Sliced          테두리(`border`, 크기 단위)로 9 조각. 테두리 합이 크기를 넘으면 비례로 줄이고, 넓이가 0 인 조각은 내지 않는다.
    //                   칸 크기(`tileWidth`·`tileHeight`)가 0 이면 온 조각이다(풀리지 않은 스프라이트).
    //   Tiled           칸 크기로 되풀이한다. 마지막 타일은 쿼드와 텍스처를 같은 비율로 자른다. 칸 크기가 0 이면 온 조각이다.
    //   Filled          `fillAmount`(0..1) 만큼 `fillOrigin` 쪽에서부터 한 조각. 0 이면 조각이 없다.
    UInt32 BuildSpritePieces(const SpriteRenderItem& item, SpritePiece* out, UInt32 capacity);
}
