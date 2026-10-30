#pragma once

#include <JBro/Canvas/GameSystem.h>

#include <JBro/Types/Float.h>
#include <JBro/Types/Int.h>

namespace JBro::System
{
    // **마스크의 사각형을 레이어 좌표로 센다**(D-351 3 단계). 트랜스폼 전파(100) 뒤, 렌더 추출(400)과 글자(410) 앞에 돈다.
    // `Mask2D` 의 로컬 사각형 네 꼭짓점을 월드 행렬로 보내 둘러싼 축 정렬 사각형을 `clip*` 에 적는다 - 돌린 마스크는 근사다.
    class Mask2DSystem final : public GameSystem
    {
    public:
        static constexpr Int32 ExecutionOrder = 120;

        Int32 GetExecutionOrder() const override;

    protected:
        void OnUpdate(Canvas& canvas, Float deltaTime) override;
    };
}
