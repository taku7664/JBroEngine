#pragma once

// 스크립트는 이 헤더를 직접 집지 않는다. 오브젝트는 GameObjectHandle 로만 만진다(D-5).
// include 경로로는 막을 수 없다 — 스크립트 DLL 이 JBroRuntime 을 링크하므로 경로가 열려 있다.
// 그래서 프렐류드를 거쳤는지를 표식으로 확인한다(§9.5).
#if defined(JBRO_SCRIPT_TARGET) && !defined(JBRO_SCRIPT_PRELUDE)
#error "A script reaches game objects through <JBro/ScriptAPI.h> and GameObjectHandle, not <JBro/Runtime/GameObject.h>."
#endif

#include <JBro/Core/Core.h>
#include <JBro/Core/StableTypeId.h>
#include <JBro/Runtime/Component.h>
#include <JBro/Runtime/Ref.h>
#include <JBro/Types/Array.h>
#include <JBro/Types/SafePtr.h>
#include <JBro/Types/NameTable.h>

#include <cstdint>
#include <type_traits>

namespace JBro
{
    class Canvas;
    class GameObjectHandle;
    class Layer;

    // 오브젝트가 들고 있는 컴포넌트 하나다. 타입 id 를 참조 옆에 복사해 두어,
    // 타입으로 찾을 때 후보마다 제어 블록을 따라가지 않게 한다(§3.4).
    // id 는 컴포넌트 수명 내내 바뀌지 않으므로 이 사본이 어긋날 수 없다.
    struct ComponentSlot
    {
        SafePtr<ComponentBase> reference;
        ComponentTypeId        typeId = 0;
    };

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

        // 계층
        GameObject*                       GetParent() const;
        void                              SetParent(GameObject* parent);
        const Array<SafePtr<GameObject>>& GetChildren() const;

        // 레이어 소속. 인덱스 조회는 O(1)이다.
        Layer*        GetLayer() const;
        std::uint32_t GetLayerId() const;

        // 활성 상태. IsActiveInHierarchy 는 캐시를 읽으므로 O(1) 이다 —
        // 컴포넌트 활성 게이트가 매 프레임 이것을 부르기 때문이다(§9, D-54).
        bool IsActiveSelf() const;
        bool IsActiveInHierarchy() const;
        void SetActive(bool active);

        // 태그·플래그(B10)
        // 태그는 정수로 산다(D-51). 문자열은 NameTable 에만 있고 여기서는 되찾아 줄 뿐이다.
        const char*   GetTag() const;
        void          SetTag(const char* tag);
        NameId        GetTagId() const;
        void          SetTagId(NameId tag);
        std::uint32_t GetFlags() const;
        void          SetFlags(std::uint32_t flags);

        // 컴포넌트 풀의 주소를 SafePtr 로만 기록한다. 조회는 캐시 친화적인 선형 순회다.
        const Array<ComponentSlot>& GetComponents() const;

        template<typename T>
        Ref<T> GetComponent() const;

        template<typename T>
        Array<Ref<T>> GetComponents() const;

    private:
        friend class Canvas;
        friend class GameObjectHandle;

        // Canvas 는 오브젝트를 소유하는 실행 계층이고 이 헤더는 스크립트가 링크하는 계층이다.
        // 정의를 끌어오면 그 경계가 무너지므로, 파괴 호출만 함수 포인터로 건너간다.
        // 소유자 포인터는 불완전 타입이어도 되고, 그 정체는 Canvas 가 friend 로 직접 본다.
        using DestroyFunction = bool (*)(Canvas* canvas, GameObject* object);

        Canvas* GetCanvas() const;
        void SetInstanceIdentity(InstanceId instanceId, InstanceHandle handle);
        void BindCanvas(Canvas* canvas, DestroyFunction destroyFunction);
        void SetLayer(SafePtr<Layer> layer, std::uint32_t layerIndex);
        void AttachComponent(ComponentBase* component);
        bool DetachComponent(ComponentBase* component);
        InstanceRef FindComponentReference(ComponentTypeId typeId) const;
        bool RequestDestroy();
        void RefreshActiveInHierarchy();

        InstanceId                   m_instanceId = InvalidInstanceId;
        InstanceHandle               m_handle;
        Canvas*                      m_canvas = nullptr;
        DestroyFunction              m_destroyFunction = nullptr;
        SafePtr<GameObject>           m_parent;
        Array<SafePtr<GameObject>>    m_children;
        Array<ComponentSlot> m_components;
        SafePtr<Layer>                m_layer;
        std::uint32_t                 m_layerIndex = 0;
        std::uint32_t                 m_flags = 0;
        bool                          m_destroying = false;
        bool                          m_active = true;
        bool                          m_activeInHierarchy = true;
        NameId                        m_tag = InvalidNameId;
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
        for (const ComponentSlot& slot : m_components)
        {
            if (slot.typeId != TypeId)
            {
                continue;
            }
            ComponentBase* component = slot.reference.TryGet();
            if (component != nullptr)
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
