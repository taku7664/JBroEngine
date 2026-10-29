#pragma once

#include <JBro/Canvas/GameSystem.h>

#include <JBro/Types/Float.h>
#include <JBro/Types/Int.h>

namespace JBro::System
{
    // **자식을 칸에 놓는다**(D-352 6 단계). 위젯 시스템(50) 뒤, 트랜스폼 전파(100) 앞에 돌아 같은 프레임에 보인다.
    // 자식의 `position` 을 매 프레임 쓴다 - 손으로 옮긴 자리는 남지 않는다.
    class Layout2DSystem final : public GameSystem
    {
    public:
        static constexpr Int32 ExecutionOrder = 60;

        Int32 GetExecutionOrder() const override;

    protected:
        void OnUpdate(Canvas& canvas, Float deltaTime) override;
    };
}
