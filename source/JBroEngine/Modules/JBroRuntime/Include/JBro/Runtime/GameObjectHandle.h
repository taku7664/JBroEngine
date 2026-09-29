#pragma once

#include <JBro/Core/Core.h>
#include <JBro/Core/StableTypeId.h>
#include <JBro/Runtime/Ref.h>

#include <type_traits>

namespace JBro
{
    namespace Object
    {
        class GameObject;
    }

    namespace Internal
    {
        struct GameObjectHandleAccess;
    }
}

// 스크립트가 보는 핸들이다(D-271). 이름은 엔진 타입과 같고, 프렐류드가 이 네임스페이스를 연다.
namespace JBro::Handle
{
    // 스크립트에 노출하는 GameObject 전용 16B 핸들. 포인터 연산자는 의도적으로 없다.
    class GameObject final
    {
    public:
        GameObject() = default;

        bool IsValid() const;
        explicit operator bool() const;

        void Destroy();
        void SetActive(bool active);
        bool IsActive() const;

        template<typename T>
        Ref<T> GetComponent() const;

        InstanceId GetInstanceId() const;

    private:
        friend class Object::GameObject;
        friend struct Internal::GameObjectHandleAccess;

        explicit GameObject(const Object::GameObject* object);
        Object::GameObject* Resolve() const;
        InstanceRef FindComponentReference(ComponentTypeId typeId) const;
        static void ReportInvalidAccess(const char* operation, InstanceId instanceId);

        mutable InstanceHandle m_cached;
        InstanceId             m_instanceId = InvalidInstanceId;
    };

    static_assert(sizeof(GameObject) == 16,
        "Handle::GameObject must remain a 16-byte script value");
    static_assert(std::is_standard_layout_v<GameObject>,
        "Handle::GameObject must remain standard layout");
    static_assert(std::is_trivially_copyable_v<GameObject>,
        "Handle::GameObject must remain trivially copyable");

    template<typename T>
    Ref<T> GameObject::GetComponent() const
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
