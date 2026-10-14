#pragma once

#include <JBro/Editor/Widget/Common.h>
#include <JBro/Types/ArrayView.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Int.h>

namespace JBro::Widget
{
    // **하나를 고르는 칩 줄이다**(D-309). 고를 것이 열 개 안팎이고 한눈에 보여야 할 때 콤보 대신 쓴다 - 콤보는 지금 것 하나만 보여
    // 무엇이 더 있는지 열어 봐야 안다. 이름마다 둥근 칩 하나이고 폭이 모자라면 다음 줄로 접는다. 고른 칩은 강조색이다.
    //
    // 눌러 바꿨으면 참이고 `selected` 가 그 번호다. 칩의 Id 는 `id` 아래의 번호 아래 `##chip` 이다.
    Bool ChoiceChips(const char* id, ArrayView<const char* const> names, Int32& selected);
}
