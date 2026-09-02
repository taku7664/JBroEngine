#pragma once

#include <JBro/Runtime/GameSystem.h>

#include <memory>
#include <vector>

namespace JBro::Engine
{
    class SystemScheduler
    {
    public:
        template <typename T, typename... Args>
        T& AddSystem(Args&&... args);

        template <typename T>
        T* FindSystem();

        void RemoveAllSystems(CWorld& world);
        void Initialize(CWorld& world);
        void FixedUpdate(CWorld& world, float fixedDeltaTime);
        void Update(CWorld& world, float deltaTime);
        void ExtractRender(CWorld& world, RenderWorld2D& renderWorld);
        void Shutdown(CWorld& world);
        void SortByExecutionOrder();

        std::size_t GetSystemCount() const;
        GameSystem* GetSystem(std::size_t index);

    private:
        std::vector<std::unique_ptr<GameSystem>> mSystems;
        bool mInitialized = false;
    };
}
