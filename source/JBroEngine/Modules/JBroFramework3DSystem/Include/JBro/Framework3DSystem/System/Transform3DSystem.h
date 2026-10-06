#pragma once

#include <JBro/Canvas/GameSystem.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/Int.h>

namespace JBro::System
{
    // 뿌리에서 내려가며 `Transform3D` 의 월드 캐시를 채운다. 2D 와 같은 걸음이다.
    class Transform3DSystem final : public GameSystem
    {
    public:
        Int32 GetExecutionOrder() const override;

    protected:
        void OnUpdate(Canvas& canvas, Float deltaTime) override;
    };
}
