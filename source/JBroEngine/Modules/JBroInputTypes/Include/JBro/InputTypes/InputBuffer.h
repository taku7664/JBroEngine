#pragma once

#include <limits>
#include <type_traits>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>

namespace JBro
{
    // 선입력(버퍼)과 코요테 타임에 쓰는 작은 기억이다(D-218). 신호가 마지막으로 참이었던 뒤로 흐른 시간만 든다.
    //
    // **엔진은 지난 입력을 들고 있지 않는다.** 스크립트가 제 뷰에서 읽은 값을 매 프레임 넣는다 - 그래서 위의 레이어가 막거나 소비한
    // 누름은 여기에도 들어오지 않는다. 엔진이 과거 입력을 따로 들면 UI 가 가져간 누름이 나중에 점프로 나온다.
    //
    //     m_jump.Feed(input.IsActionPressed(Jump), deltaTime);   // 땅에 닿기 직전의 누름을 0.15 초 기억한다
    //     m_ground.Feed(IsGrounded(), deltaTime);                 // 땅을 떠난 뒤 0.1 초는 아직 뛸 수 있다(코요테)
    //     if (m_jump.Peek(0.15f) && m_ground.Peek(0.1f))
    //     {
    //         m_jump.Clear();
    //         m_ground.Clear();
    //         Jump();
    //     }
    //
    // 둘을 함께 보는 자리는 `Peek` 뒤에 `Clear` 한다. `Take` 를 차례로 부르면 앞의 것만 비워지고 뒤의 것이 거짓일 때 누름을 잃는다.
    // 값 하나라 컴포넌트 필드로 두어도 되고, 복사해도 된다. 메인 스레드에서 쓴다.
    struct InputBuffer
    {
        static constexpr Float Never = std::numeric_limits<float>::infinity();

        // 신호가 마지막으로 참이었던 뒤로 흐른 초다. 한 번도 없었거나 비웠으면 `Never` 다.
        Float age = Never;

        // 한 프레임(또는 고정 스텝)에 한 번 부른다. 참이면 지금부터 다시 센다. 부르는 쪽이 넣는 시간을 고른다 - `OnFixedUpdate` 는 고정 간격을 넣는다.
        void Feed(Bool signal, Float deltaTime)
        {
            if (signal)
            {
                age = 0.0f;
            }
            else if (age != Never)
            {
                age += deltaTime;
            }
        }

        // 지난 `window` 초 안에 신호가 있었는가. 비우지 않는다.
        Bool Peek(Float window) const
        {
            return age <= window;
        }

        // 있었으면 참을 돌려주고 비운다 - 한 번 누른 것이 두 번 뛰지 않는다.
        Bool Take(Float window)
        {
            if (age <= window)
            {
                age = Never;
                return true;
            }
            return false;
        }

        void Clear()
        {
            age = Never;
        }
    };

    static_assert(std::is_trivially_copyable_v<InputBuffer>, "InputBuffer is a plain value a component may hold");
}
