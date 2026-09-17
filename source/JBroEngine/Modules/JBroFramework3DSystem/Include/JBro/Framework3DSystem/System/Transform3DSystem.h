#pragma once

#include <JBro/Canvas/GameSystem.h>

namespace JBro::System
{
    // 뿌리에서 내려가며 `Transform3D` 의 월드 캐시를 채운다. 2D 와 같은 걸음이다.
    class Transform3DSystem final : public GameSystem
    {
    public:
        int GetExecutionOrder() const override;

    protected:
        void OnUpdate(Canvas& canvas, float deltaTime) override;
    };
}
