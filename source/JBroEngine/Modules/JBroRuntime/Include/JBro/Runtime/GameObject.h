#pragma once

#include <JBro/Core/Core.h>
#include <JBro/Core/StableTypeId.h>
#include <JBro/Runtime/Component.h>
#include <JBro/Runtime/Ref.h>
#include <JBro/Types/Array.h>
#include <JBro/Types/SafePtr.h>
#include <JBro/Types/String.h>

#include <cstdint>
#include <type_traits>

namespace JBro
{
    class Canvas;
    class GameObjectHandle;
    class Layer;

    // TObjectPool 이 소유하는 주소 안정 객체. Transform 은 멤버가 아니라 컴포넌트다.
    class GameObject final : public EnableSafeFromThis<GameObject>
    {
    public:
        GameObject();
        ~GameObject();

        GameObject(const GameObject&)            = delete;
        GameObject& operator=(const GameObject&) = delete;

        InstanceId     GetInstanceId() const;
        InstanceHandle GetHandle() const;
        GameObjectHandle GetScriptHandle() const;

        Canvas* GetCanvas() const;

        // 계층
        GameObject*                       GetParent() const;
        void                              SetParent(GameObject* parent);
        const Array<SafePtr<GameObject>>& GetChildren() const;

        // 레이어 소속. 인덱스 조회는 O(1)이다.
        Layer*        GetLayer() const;
        std::uint32_t GetLayerIndex() const;

        // 활성 상태
        bool IsActiveSelf() const;
        bool IsActiveInHierarchy() const;
        void SetActive(bool active);

        // 태그·플래그(B10)
        const char*   GetTag() const;
        void          SetTag(const char* tag);
        std::uint32_t GetFlags() const;
        void          SetFlags(std::uint32_t flags);

        // 컴포넌트 풀의 주소를 SafePtr 로만 기록한다. 조회는 캐시 친화적인 선형 순회다.
        const Array<SafePtr<ComponentBase>>& GetComponents() const;

        template<typename T>
        Ref<T> GetComponent() const;

        template<typename T>
        Array<Ref<T>> GetComponents() const;

    private:
        friend class Canvas;
        friend class GameObjectHandle;

        void SetInstanceIdentity(InstanceId instanceId, InstanceHandle handle);
        void BindCanvas(Canvas* canvas);
        void SetLayer(SafePtr<Layer> layer, std::uint32_t layerIndex);
        void AttachComponent(ComponentBase* component);
        bool DetachComponent(ComponentBase* component);
        InstanceRef FindComponentReference(ComponentTypeId typeId) const;
        bool RequestDestroy();

        InstanceId                   m_instanceId = InvalidInstanceId;
        InstanceHandle               m_handle;
        Canvas*                      m_canvas = nullptr;
        SafePtr<GameObject>           m_parent;
        Array<SafePtr<GameObject>>    m_children;
        Array<SafePtr<ComponentBase>> m_components;
        SafePtr<Layer>                m_layer;
        std::uint32_t                 m_layerIndex = 0;
        std::uint32_t                 m_flags = 0;
        bool                          m_destroying = false;
        bool                          m_active = true;
        String                        m_tag;
    };

    template<typename T>
    Ref<T> GameObject::GetComponent() const
    {
        static_assert(std::is_base_of_v<ComponentBase, T>);
        static constexpr ComponentTypeId TypeId = MakeStableTypeId(T::StaticTypeName());
        const InstanceRef found = FindComponentReference(TypeId);
        Ref<T> result;
        result.ObjectId = found.ObjectId;
        result.ComponentId = found.ComponentId;
        result.Cached = found.Cached;
        return result;
    }

    template<typename T>
    Array<Ref<T>> GameObject::GetComponents() const
    {
        static_assert(std::is_base_of_v<ComponentBase, T>);
        static constexpr ComponentTypeId TypeId = MakeStableTypeId(T::StaticTypeName());

        Array<Ref<T>> result;
        for (const SafePtr<ComponentBase>& componentRef : m_components)
        {
            ComponentBase* component = componentRef.TryGet();
            if (component != nullptr && component->GetTypeId() == TypeId)
            {
                Ref<T> reference;
                reference.ObjectId = m_instanceId;
                reference.ComponentId = component->GetInstanceId();
                reference.Cached = component->GetHandle();
                result.Add(reference);
            }
        }
        return result;
    }
}
