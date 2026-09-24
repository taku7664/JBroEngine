#pragma once

#include <JBro/Core/Core.h>
#include <JBro/Canvas/GameSystem.h>
#include <JBro/Types/Array.h>

#include <cstring>
#include <stdexcept>
#include <type_traits>
#include <typeinfo>
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
            // **프로파일러에 보일 이름**이다(D-194). `typeid(T).name()` 은 그 타입의 자료를
            // 가리키므로 프로그램이 도는 동안 주소가 바뀌지 않는다 - 프로파일러가 이름을
            // 복사하지 않고 주소로 묶는 데 그 성질이 필요하다. 매 프레임 글자를 짓지 않는다.
            // 마지막 마디만 가리킨다(`class JBro::Camera2DSystem` → `Camera2DSystem`).
            // 같은 글자 안을 가리키는 것이라 주소는 그대로 안정하고, 창은 받은 것을 그냥 적는다.
            const char* profileName = nullptr;
        };

        // `class JBro::Camera2DSystem` 에서 마지막 마디만 가리킨다. 복사하지 않는다.
        static const char* ShortTypeName(const char* name)
        {
            if (name == nullptr)
            {
                return nullptr;
            }
            const char* lastColon = std::strrchr(name, ':');
            return lastColon != nullptr && *(lastColon + 1) != '\0' ? lastColon + 1 : name;
        }

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
        entry.profileName = ShortTypeName(typeid(T).name());
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
