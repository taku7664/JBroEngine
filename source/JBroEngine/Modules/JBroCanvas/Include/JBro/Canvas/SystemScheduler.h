#pragma once

#include <JBro/Core/Core.h>
#include <JBro/Canvas/GameSystem.h>
#include <JBro/Types/Array.h>

#include <stdexcept>
#include <type_traits>
#include <utility>

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
        struct Entry
        {
            OwnerPtr<GameSystem> system;
            const void* typeKey = nullptr;
            std::size_t registrationOrder = 0;
        };

        template<typename T>
        static const void* TypeKey()
        {
            // Engine-local identity only; never serialized or passed across the script DLL boundary.
            static unsigned char token = 0;
            return &token;
        }

        Array<Entry> m_systems;
        bool m_initialized = false;
        bool m_executing = false;
    };

    template<typename T, typename... Args>
    T& SystemScheduler::AddSystem(Args&&... args)
    {
        static_assert(std::is_base_of_v<GameSystem, T>);
        if (m_initialized || m_executing)
        {
            throw std::logic_error("Systems can only be registered before initialization");
        }
        auto owner = MakeOwnerPtr<T>(std::forward<Args>(args)...);
        T& result = *owner;
        Entry entry;
        entry.system = std::move(owner);
        entry.typeKey = TypeKey<T>();
        entry.registrationOrder = m_systems.Size();
        m_systems.Add(std::move(entry));
        return result;
    }

    template<typename T>
    T* SystemScheduler::FindSystem()
    {
        static_assert(std::is_base_of_v<GameSystem, T>);
        for (auto& entry : m_systems)
        {
            if (entry.typeKey == TypeKey<T>())
            {
                return static_cast<T*>(entry.system.Get());
            }
        }
        return nullptr;
    }
}
