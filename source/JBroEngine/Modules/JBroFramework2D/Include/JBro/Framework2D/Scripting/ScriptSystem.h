#pragma once

#include <JBro/Runtime/GameSystem.h>

namespace JBro::Engine
{
    class GameScript;

    class ScriptSystem final : public GameSystem
    {
    public:
        int GetExecutionOrder() const override;

    protected:
        void OnInitialize(CWorld& world) override;
        void OnFixedUpdate(CWorld& world, float fixedDeltaTime) override;
        void OnUpdate(CWorld& world, float deltaTime) override;
        void OnShutdown(CWorld& world) override;

    private:
        void StartPendingScripts(CWorld& world);
        void UpdateActiveScripts(CWorld& world, float deltaTime);
        void FixedUpdateActiveScripts(CWorld& world, float fixedDeltaTime);
        void DestroyPendingScripts(CWorld& world);
        bool ShouldRun(const GameScript& script) const;
    };
}
