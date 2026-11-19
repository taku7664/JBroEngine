#pragma once

#include <JBro/Canvas/GameSystem.h>

#include <JBro/Types/Float.h>
#include <JBro/Types/Int.h>

namespace JBro::System
{
    // **트윈을 돌린다**(D-357). 위젯 시스템(50) 뒤, 레이아웃(60) 앞에 돌아 같은 프레임에 보인다. 같은 오브젝트의 트랜스폼·스프라이트에 값을 쓴다 -
    // 돌아가는 동안 손으로 옮긴 자리는 남지 않는다.
    class Tween2DSystem final : public GameSystem
    {
    public:
        static constexpr Int32 ExecutionOrder = 55;

        Int32 GetExecutionOrder() const override;

    protected:
        void OnUpdate(Canvas& canvas, Float deltaTime) override;
    };
}
