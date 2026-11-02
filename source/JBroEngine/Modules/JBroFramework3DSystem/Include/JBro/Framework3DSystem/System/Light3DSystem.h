#pragma once

#include <JBro/Canvas/GameSystem.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/Int.h>

namespace JBro
{
    class RenderWorld3D;
}

namespace JBro::System
{
    // 켜진 `Light3D` 를 렌더 월드에 뜬다(D-366). 자리와 방향은 오브젝트의 `Transform3D` 에서 온다 - 없으면 `Ambient` 만 뜬다.
    class Light3DSystem final : public GameSystem
    {
    public:
        Int32 GetExecutionOrder() const override;
        void SetRenderWorld(RenderWorld3D* renderWorld);

    protected:
        void OnUpdate(Canvas& canvas, Float deltaTime) override;

    private:
        RenderWorld3D* m_renderWorld = nullptr;
    };
}
