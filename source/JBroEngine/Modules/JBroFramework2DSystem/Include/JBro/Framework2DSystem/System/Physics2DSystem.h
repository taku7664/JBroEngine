#pragma once

#include <JBro/Framework2D/Component/Physics2D.h>
#include <JBro/Framework2D/System/IPhysics2DSystem.h>
#include <JBro/Canvas/GameSystem.h>
#include <JBro/Types/Array.h>

namespace JBro
{
    class GameObject;
}

namespace JBro::System
{
    class Physics2DSystem final : public GameSystem, public IPhysics2DSystem
    {
    public:
        int  GetExecutionOrder() const override;
        void SetGravity(Vec2 gravity);
        Vec2 GetGravity() const;

        bool Raycast(
            Vec2 origin,
            Vec2 direction,
            float distance,
            Collision2D& hit) const override;
        void OverlapBox(
            const Rect& area,
            Array<GameObjectHandle>& results) const override;

    protected:
        void OnInitialize (Canvas& canvas) override;
        void OnFixedUpdate(Canvas& canvas, float fixedDeltaTime) override;
        void OnShutdown   (Canvas& canvas) override;

    private:
        Canvas* m_canvas = nullptr;
        Vec2 m_gravity{ 0.0f, -9.81f };
    };
}
