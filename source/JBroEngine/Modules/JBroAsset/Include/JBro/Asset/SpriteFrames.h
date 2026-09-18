#pragma once

#include <JBro/AssetTypes/AssetTypes.h>
#include <JBro/Types/Array.h>

namespace JBro
{
    // 텍스처 크기와 임포트 옵션에서 프레임 목록을 만든다. 순수 함수다.
    //
    // `None` 은 이미지 전체 하나다. `CellCount` 는 행·열 개수로, `CellSize` 는 셀 픽셀 크기로 균일하게 자르되
    // 바깥 여백(`margin`)과 셀 사이 간격(`gap`)을 뺀다. 행 우선(왼쪽 위에서 오른쪽으로, 다음 행)이다.
    // 이미지 안에 온전히 들어가지 않는 칸은 만들지 않는다. 결과가 비면 false 다 - 셀이 이미지보다 크거나 크기가 0 이다.
    bool BuildSpriteFrames(
        std::uint32_t textureWidth,
        std::uint32_t textureHeight,
        const SpriteImportOptions& options,
        Array<SpriteFrame>& frames);
}
