#pragma once

#include <JBro/Types/Math2D.h>
#include <JBro/Types/Math3D.h>

namespace JBro
{
    // 값을 목표로 **부드럽게 따라가게** 하는 감쇠기다(D-256).
    //
    // `Ease` 와 무엇이 다른가: 이징은 시작과 끝이 정해진 길을 진행도로 걷는다. 도중에 목표가
    // 바뀌면 길을 새로 잡아야 하고, 그 순간 속도가 튄다. 이쪽은 길이 없다 - 속도를 들고 있다가
    // **목표가 언제 바뀌어도 이어서** 따라간다. 카메라가 인물을 쫓거나 인스펙터 값이 흐르는
    // 자리가 이쪽이다.
    //
    // 임계 감쇠(critically damped)라 **목표를 지나치지 않는다.** 흔들리며 다가가는 모양이
    // 필요하면 이것이 아니라 `EaseKind::Elastic` 이다.
    //
    // **프레임 시간이 흔들려도 결과가 거의 같다.** 한 프레임에 한 번 부르든 반씩 두 번 부르든
    // 같은 자리에 온다 - 지수 감쇠를 근사한 식이라서 그렇다. 그래서 델타를 고정하지 않아도 된다.
    //
    // POD 다. 컴포넌트 칸이나 경계를 넘는 구조체에 그대로 둘 수 있다.
    struct SpringDamper
    {
        // 지금 속도다. 손으로 건드리지 않는다 - `Update` 가 들고 간다.
        float velocity = 0.0f;

        // 목표 쪽으로 한 걸음 옮긴 값을 돌려준다.
        //
        // `smoothTime` 은 목표에 닿기까지 걸리는 **대략의 시간(초)** 이다. 작을수록 빠르다.
        // 0 이하면 곧바로 목표에 둔다.
        // `maxSpeed` 는 초당 옮길 수 있는 최대 거리다. **0 이하면 제한하지 않는다** -
        // 구조체 칸에 두었을 때의 기본값 0 이 "제한 없음" 이 되게 한 것이다.
        // `deltaTime` 이 0 이하면 아무것도 하지 않고 `current` 를 돌려준다.
        float Update(float current, float target, float smoothTime, float deltaTime,
            float maxSpeed = 0.0f) noexcept;

        // 속도를 지운다. 대상을 순간이동시킨 뒤에 부른다 - 안 부르면 옛 속도가 남아 밀린다.
        void Reset() noexcept
        {
            velocity = 0.0f;
        }
    };

    // 성분마다 하나씩 든다. 성분별로 도는 것은 널리 쓰이는 방식과 같다 -
    // 방향이 아니라 축마다 따라가므로 대각선으로 갈 때 곡선이 살짝 휜다.
    struct SpringDamper2D
    {
        SpringDamper x;
        SpringDamper y;

        Vector2 Update(const Vector2& current, const Vector2& target, float smoothTime, float deltaTime,
            float maxSpeed = 0.0f) noexcept
        {
            Vector2 result;
            result.x = x.Update(current.x, target.x, smoothTime, deltaTime, maxSpeed);
            result.y = y.Update(current.y, target.y, smoothTime, deltaTime, maxSpeed);
            return result;
        }

        void Reset() noexcept
        {
            x.Reset();
            y.Reset();
        }
    };

    struct SpringDamper3D
    {
        SpringDamper x;
        SpringDamper y;
        SpringDamper z;

        Vector3 Update(const Vector3& current, const Vector3& target, float smoothTime, float deltaTime,
            float maxSpeed = 0.0f) noexcept
        {
            Vector3 result;
            result.x = x.Update(current.x, target.x, smoothTime, deltaTime, maxSpeed);
            result.y = y.Update(current.y, target.y, smoothTime, deltaTime, maxSpeed);
            result.z = z.Update(current.z, target.z, smoothTime, deltaTime, maxSpeed);
            return result;
        }

        void Reset() noexcept
        {
            x.Reset();
            y.Reset();
            z.Reset();
        }
    };
}
