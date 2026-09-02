#pragma once

#include <JBro/Runtime/GameSystem.h>

#include <memory>
#include <vector>

namespace JBro
{
    class SystemScheduler
    {
    public:
        template <typename T, typename... Args>
        T& AddSystem(Args&&... args);

        template <typename T>
        T* FindSystem();

        void Initialize (Canvas& canvas);
        void FixedUpdate(Canvas& canvas, float fixedDeltaTime);
        void Update     (Canvas& canvas, float deltaTime);
        void Shutdown   (Canvas& canvas);

        void RemoveAllSystems(Canvas& canvas);
        void SortByExecutionOrder();

        std::size_t GetSystemCount() const;
        GameSystem* GetSystem(std::size_t index);

    private:
        std::vector<std::unique_ptr<GameSystem>> m_systems;
        bool m_initialized = false;
    };
}
