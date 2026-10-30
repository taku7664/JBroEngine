#pragma once

#include <JBro/Types/Float.h>

namespace JBro
{
    struct PropertyTable;

    // **픽셀 기본값 나누기**(D-354·D-355). `Pixels()` 어트리뷰트가 붙은 필드(`PropertyEditInfo.pixels`)는 화면 레이어의 픽셀이고 월드 레이어에서는
    // 유닛이다 - 맨 컴포넌트를 월드 레이어의 오브젝트에 붙일 때 그 필드의 `Float` 잎사귀(`Vector2` 는 x·y)를 모두 `factor` 배 한다. 에디터의 컴포넌트 추가와
    // 스크립트의 `AddComponent` 가 같은 것을 부른다. 값을 들고 붙이는 길(붙여넣기)은 부르지 않는다 - 그 값은 이미 그 레이어의 것이다.
    void ScalePixelFields(const PropertyTable& table, void* instance, Float factor);
}
