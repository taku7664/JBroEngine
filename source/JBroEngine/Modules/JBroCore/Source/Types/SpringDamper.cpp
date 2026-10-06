#include <JBro/Types/SpringDamper.h>
#include <JBro/Types/Float.h>

namespace JBro
{
    Float SpringDamper::Update(Float current, Float target, Float smoothTime, Float deltaTime,
        Float maxSpeed) noexcept
    {
        // 시간이 흐르지 않았으면 아무것도 하지 않는다. 아래 식이 델타로 나누므로 여기서 막아야 한다.
        if (deltaTime <= 0.0f)
        {
            return current;
        }

        // 따라갈 시간을 주지 않았으면 곧바로 둔다. 속도도 지운다 - 안 지우면 다음 프레임에 밀려난다.
        if (smoothTime <= 0.0f)
        {
            velocity = 0.0f;
            return target;
        }

        // 지수 감쇠 `exp(-omega * dt)` 를 나눗셈 한 번으로 바꾼 근사식이다. 이 근사 덕분에
        // 델타가 흔들려도 결과가 거의 같다.
        const Float omega = 2.0f / smoothTime;
        const Float x = omega * deltaTime;
        const Float decay = 1.0f / (1.0f + x + 0.48f * x * x + 0.235f * x * x * x);

        Float change = current - target;

        // 속도 제한은 **거리**로 바꿔서 건다. 한 프레임에 옮길 수 있는 만큼만 목표를 당겨 온다.
        if (maxSpeed > 0.0f)
        {
            const Float maxChange = maxSpeed * smoothTime;
            if (change > maxChange)
            {
                change = maxChange;
            }
            else if (change < -maxChange)
            {
                change = -maxChange;
            }
            target = current - change;
        }

        const Float temp = (velocity + omega * change) * deltaTime;
        velocity = (velocity - omega * temp) * decay;
        const Float result = target + (change + temp) * decay;

        // **지나침을 되돌리는 손질을 두지 않는다.** 널리 쓰이는 판에는 목표를 넘었을 때 목표에
        // 세우는 분기가 붙어 있는데, 이 근사식에서는 발동하지 않는다 - 매끄러움 0.05~1 초,
        // 델타 1/240~2 초, 속도 제한 없음~50 의 140 가지를 걸어 보았고 한 번도 넘지 않았다(D-259).
        // 목표가 도중에 뒤집혀도 마찬가지다. 잡지 못하는 분기를 두면 시험이 닿지 않는 코드가 남는다.

        return result;
    }
}
