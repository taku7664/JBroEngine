#pragma once

#include <JBro/Core/Core.h>
#include <JBro/Core/StableTypeId.h>
#include <JBro/Runtime/Ref.h>

#include <type_traits>

namespace JBro
{
    class GameObject;

    // 스크립트에 노출하는 GameObject 전용 16B 핸들. 포인터 연산자는 의도적으로 없다.
    class GameObjectHandle final
    {
    public:
        GameObjectHandle() = default;

        bool IsValid() const;
        explicit operator bool() const;

        void Destroy();
        void SetActive(bool active);
        bool IsActive() const;

        template<typename T>
        Ref<T> GetComponent() const;

        InstanceId GetInstanceId() const;

    private:
        friend class GameObject;

        explicit GameObjectHandle(const GameObject* object);
        GameObject* Resolve() const;
        InstanceRef FindComponentReference(ComponentTypeId typeId) const;
        static void ReportInvalidAccess(const char* operation, InstanceId instanceId);

        mutable InstanceHandle m_cached;
        InstanceId             m_instanceId = InvalidInstanceId;
    };

    static_assert(sizeof(GameObjectHandle) == 16,
        "GameObjectHandle must remain a 16-byte script value");
    static_assert(std::is_standard_layout_v<GameObjectHandle>,
        "GameObjectHandle must remain standard layout");
    static_assert(std::is_trivially_copyable_v<GameObjectHandle>,
        "GameObjectHandle must remain trivially copyable");

    template<typename T>
    Ref<T> GameObjectHandle::GetComponent() const
    {
        static constexpr ComponentTypeId TypeId = MakeStableTypeId(T::StaticTypeName());
        const InstanceRef found = FindComponentReference(TypeId);
        Ref<T> result;
        result.ObjectId = found.ObjectId;
        result.ComponentId = found.ComponentId;
        result.Cached = found.Cached;
        return result;
    }
}
