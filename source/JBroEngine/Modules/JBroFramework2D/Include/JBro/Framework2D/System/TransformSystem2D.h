#pragma once

#include <JBro/Core/Core.h>
#include <JBro/Framework2D/Math2D.h>
#include <JBro/Runtime/GameSystem.h>

namespace JBro::Engine
{
    class TransformSystem2D final : public GameSystem
    {
    public:
        int GetExecutionOrder() const override;

    protected:
        void OnUpdate(CWorld& world, float deltaTime) override;

    private:
        void UpdateRoot(CWorld& world, Entity root);
        void UpdateHierarchy(CWorld& world, Entity entity, const Matrix3x2& parentWorld);
        Matrix3x2 BuildLocalMatrix(CWorld& world, Entity entity) const;
    };
}
