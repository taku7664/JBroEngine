#pragma once

#include <JBro/Framework2D/Component/Physics2D.h>
#include <JBro/Runtime/GameSystem.h>

#include <vector>

namespace JBro::Engine
{
    class Physics2DSystem final : public GameSystem
    {
    public:
        int GetExecutionOrder() const override;
        void SetGravity(Vec2 gravity);
        Vec2 GetGravity() const;

        bool Raycast(CWorld& world, Vec2 origin, Vec2 direction, float distance, Collision2D& hit) const;
        void OverlapBox(CWorld& world, const Rect& area, std::vector<Entity>& results) const;

    protected:
        void OnInitialize(CWorld& world) override;
        void OnFixedUpdate(CWorld& world, float fixedDeltaTime) override;
        void OnShutdown(CWorld& world) override;

    private:
        void CreateBody(CWorld& world, Entity entity);
        void DestroyMissingBodies(CWorld& world);
        void StepSimulation(float fixedDeltaTime);
        void WriteBackTransforms(CWorld& world);

        Vec2 mGravity{ 0.0f, -9.81f };
    };
}
