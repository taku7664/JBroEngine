#pragma once

#include <type_traits>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>

// 화면 가장자리에서 가려지는 띠의 두께다(표면 픽셀).
//
// **왜 필요한가**: 휴대 기기의 화면은 직사각형이 아니다. 노치·펀치홀·둥근 모서리·홈 표시줄이
// 가장자리를 먹는다. 그림은 화면 끝까지 그리는 것이 맞지만, **글자와 버튼은 그 띠 안으로
// 들어가면 안 된다** - 가려지거나, 시스템의 쓸어 넘기기와 손짓이 겹친다.
//
// 그래서 값을 둘로 나눠 생각한다. 그리는 영역은 창 전체이고, 사람이 눌러야 하는 것이 놓이는
// 영역은 여기 적힌 두께만큼 줄어든 안쪽이다.
//
// **데스크톱은 전부 0 이다.** 기본값이 그래서 0 이고, 0 이면 안쪽 영역과 전체 영역이 같다.
// 창 크기가 바뀌거나 기기를 돌리면 값이 바뀌므로, 들고 있지 말고 프레임마다 다시 받는다.
namespace JBro
{
    struct SafeAreaInsets
    {
        Float left = 0.0f;
        Float top = 0.0f;
        Float right = 0.0f;
        Float bottom = 0.0f;

        // 하나라도 0 이 아닌가. 데스크톱에서는 늘 거짓이라 계산을 통째로 건너뛸 수 있다.
        constexpr Bool IsAny() const noexcept
        {
            return left > 0.0f || top > 0.0f || right > 0.0f || bottom > 0.0f;
        }
    };

    static_assert(sizeof(SafeAreaInsets) == sizeof(float) * 4);
    static_assert(std::is_standard_layout_v<SafeAreaInsets>);
    static_assert(std::is_trivially_copyable_v<SafeAreaInsets>);
}
