#pragma once

#include <cstdint>

namespace JBro
{
    // 0 에서 1 로 가는 진행도를 **어떤 모양으로** 갈지 고르는 표다(D-259).
    //
    // 값을 옮기는 일은 어디에나 있다 - 패널이 미끄러져 들어오고, 카메라가 목표로 따라가고,
    // 색이 바뀐다. 그때마다 곡선을 손으로 적으면 자리마다 다른 모양이 되고, 무엇보다
    // **에디터에서 고르게 할 수 없다.** 종류를 열거형 하나로 모으면 직렬화도 드롭다운도 따라온다.
    //
    // 이름은 Robert Penner 의 관행을 따른다. `In` 은 시작이 느리고, `Out` 은 끝이 느리며,
    // `InOut` 은 양끝이 느리다. 화면에 들어오는 것은 보통 `Out`, 나가는 것은 `In` 이다.
    //
    // **`Back` 과 `Elastic` 은 0..1 을 벗어나는 값을 돌려준다.** 되돌아왔다가 가거나 지나쳤다가
    // 돌아오는 모양이라서 그렇다 - 자르지 않는다. 자르면 그 모양이 사라진다.
    enum class EaseKind : std::uint8_t
    {
        Linear = 0,

        SineIn,      SineOut,      SineInOut,
        QuadIn,      QuadOut,      QuadInOut,
        CubicIn,     CubicOut,     CubicInOut,
        ExpoIn,      ExpoOut,      ExpoInOut,
        BackIn,      BackOut,      BackInOut,
        ElasticIn,   ElasticOut,   ElasticInOut,
        BounceIn,    BounceOut,    BounceInOut,

        Count
    };

    // 진행도 `t` 를 고른 모양으로 옮긴다.
    //
    // **`t` 는 0..1 로 자른다.** 부르는 쪽이 경과 시간을 길이로 나누다 보면 마지막 프레임에
    // 1 을 넘기 마련이고, 그때마다 자르는 것을 잊으면 `Expo` 나 `Elastic` 이 크게 튄다.
    // 돌려주는 값은 자르지 않는다(위의 `Back`·`Elastic` 참고).
    //
    // 모르는 값이 오면 `Linear` 로 본다 - 파일에서 읽은 열거형이 범위를 벗어날 수 있고,
    // 그때 화면이 멈추는 것보다 곧게 움직이는 편이 낫다.
    float Ease(EaseKind kind, float t) noexcept;

    // 두 값 사이를 고른 모양으로 오간다. `Ease` 를 부른 뒤 섞는 것과 같다.
    float EaseBetween(EaseKind kind, float from, float to, float t) noexcept;
}
