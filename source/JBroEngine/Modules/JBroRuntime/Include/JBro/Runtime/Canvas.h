#pragma once

#include <JBro/Core/Core.h>
#include <JBro/Core/ObjectPool.h>
#include <JBro/Core/StableTypeId.h>
#include <JBro/Runtime/Component.h>
#include <JBro/Runtime/GameObject.h>
#include <JBro/Runtime/Layer.h>
#include <JBro/Runtime/SystemScheduler.h>
#include <JBro/Types/Array.h>
#include <JBro/Types/Table.h>

#include <cstddef>
#include <type_traits>
#include <utility>

namespace JBro
{
    // 최상위 실행 단위. 오브젝트 풀, 타입별 컴포넌트 풀, 레이어를 직접 소유한다.
    class Canvas final
    {
    public:
        explicit Canvas(JAllocator allocator);
        ~Canvas();

        Canvas(const Canvas&)            = delete;
        Canvas& operator=(const Canvas&) = delete;

        // 호출 프레임의 시간을 한 번만 읽어 이후 InstanceId 생성을 정수 증가로 제한한다.
        void BeginFrame();
        SystemScheduler& GetSystems();

        // 오브젝트
        GameObject* CreateObject(const char* name = nullptr);
        bool        DestroyObject(GameObject* object);
        std::size_t GetObjectCount() const;

        template<typename Fn>
        void ForEachObject(Fn&& function);

        // 레이어
        Layer&      CreateLayer(const char* name = nullptr);
        bool        DestroyLayer(LayerIndex layer);
        bool        MoveLayer(LayerIndex layer, std::size_t newIndex);
        bool        SetObjectLayer(GameObject* object, LayerIndex layer);
        Layer*      FindLayer(LayerIndex layer);
        std::size_t GetLayerCount() const;
        Layer*      GetLayerAt(std::size_t index);
        LayerIndex  GetDefaultLayer() const;

        // 같은 타입을 여러 개 붙일 수 있다. 시스템 순회는 타입 풀을 직접 순회한다.
        template<typename T>
        T* AttachComponent(GameObject* owner);

        template<typename T>
        bool DetachComponent(GameObject* owner, T* component);

        template<typename T>
        T* GetComponent(GameObject* owner);

        // Replaces results in attachment order. Pointers are short-lived borrowed references.
        // Reserve results during setup to avoid allocation on repeated queries.
        template<typename T>
        void GetComponents(GameObject* owner, Array<T*>& results);

        template<typename T, typename Fn>
        void ForEach(Fn&& function);

    private:
        struct IComponentBucket
        {
            virtual ~IComponentBucket() = default;
            virtual bool Destroy(ComponentBase* component) = 0;
        };

        template<typename T>
        struct TComponentBucket final : IComponentBucket
        {
            explicit TComponentBucket(JAllocator allocator)
                : Pool(allocator)
            {
            }

            bool Destroy(ComponentBase* component) override
            {
                return Pool.Destroy(static_cast<T*>(component));
            }

            TObjectPool<T> Pool;
        };

        template<typename T>
        TComponentBucket<T>* GetOrCreateBucket();

        template<typename T>
        TComponentBucket<T>* FindBucket();

        bool DestroyComponent(ComponentBase* component);
        bool RegisterComponentInstance(
            GameObject* owner,
            ComponentBase* component,
            RefCategory category);
        bool UnregisterComponentInstance(ComponentBase* component);
        SafePtr<Layer> FindLayerReference(LayerIndex layer);
        static InstanceId GenerateCanvasInstanceId();

        JAllocator                                      m_allocator;
        OwnerPtr<TObjectPool<GameObject>>               m_objects;
        Array<OwnerPtr<Layer>>                          m_layers;
        LayerIndex                                      m_defaultLayer = InvalidLayerIndex;
        LayerIndex                                      m_nextLayer = 0;
        Table<ComponentTypeId, OwnerPtr<IComponentBucket>> m_componentBuckets;
        SystemScheduler m_systems;
    };

    template<typename Fn>
    void Canvas::ForEachObject(Fn&& function)
    {
        m_objects->ForEachLive(std::forward<Fn>(function));
    }

    template<typename T>
    T* Canvas::AttachComponent(GameObject* owner)
    {
        static_assert(std::is_base_of_v<ComponentBase, T>);
        if (owner == nullptr || owner->GetCanvas() != this)
        {
            return nullptr;
        }

        TComponentBucket<T>* bucket = GetOrCreateBucket<T>();
        if (bucket == nullptr)
        {
            return nullptr;
        }

        T* component = bucket->Pool.Create();
        if (component == nullptr)
        {
            return nullptr;
        }

        if (false == RegisterComponentInstance(
            owner,
            component,
            RefCategoryOf<T>::value))
        {
            bucket->Pool.Destroy(component);
            return nullptr;
        }

        try
        {
            owner->AttachComponent(component);
        }
        catch (...)
        {
            UnregisterComponentInstance(component);
            bucket->Pool.Destroy(component);
            throw;
        }

        if (component->GetOwner() != owner)
        {
            UnregisterComponentInstance(component);
            bucket->Pool.Destroy(component);
            return nullptr;
        }
        return component;
    }

    template<typename T>
    bool Canvas::DetachComponent(GameObject* owner, T* component)
    {
        static_assert(std::is_base_of_v<ComponentBase, T>);
        if (owner == nullptr
            || component == nullptr
            || owner->GetCanvas() != this
            || component->GetOwner() != owner)
        {
            return false;
        }
        return DestroyComponent(component);
    }

    template<typename T>
    T* Canvas::GetComponent(GameObject* owner)
    {
        static_assert(std::is_base_of_v<ComponentBase, T>);
        if (owner == nullptr || owner->GetCanvas() != this)
        {
            return nullptr;
        }

        static constexpr ComponentTypeId TypeId = MakeStableTypeId(T::StaticTypeName());
        for (const SafePtr<ComponentBase>& componentRef : owner->m_components)
        {
            ComponentBase* component = componentRef.TryGet();
            if (component != nullptr && component->GetTypeId() == TypeId)
            {
                return static_cast<T*>(component);
            }
        }
        return nullptr;
    }

    template<typename T>
    void Canvas::GetComponents(GameObject* owner, Array<T*>& results)
    {
        static_assert(std::is_base_of_v<ComponentBase, T>);
        results.Clear();
        if (owner == nullptr || owner->GetCanvas() != this)
        {
            return;
        }

        static constexpr ComponentTypeId TypeId = MakeStableTypeId(T::StaticTypeName());
        for (const SafePtr<ComponentBase>& componentRef : owner->m_components)
        {
            ComponentBase* component = componentRef.TryGet();
            if (component != nullptr && component->GetTypeId() == TypeId)
            {
                results.Add(static_cast<T*>(component));
            }
        }
    }

    template<typename T, typename Fn>
    void Canvas::ForEach(Fn&& function)
    {
        static_assert(std::is_base_of_v<ComponentBase, T>);
        TComponentBucket<T>* bucket = FindBucket<T>();
        if (bucket == nullptr)
        {
            return;
        }
        bucket->Pool.ForEachLive(std::forward<Fn>(function));
    }

    template<typename T>
    Canvas::TComponentBucket<T>* Canvas::GetOrCreateBucket()
    {
        static constexpr ComponentTypeId TypeId = MakeStableTypeId(T::StaticTypeName());
        OwnerPtr<IComponentBucket>* existing = m_componentBuckets.Find(TypeId);
        if (existing != nullptr)
        {
            return static_cast<TComponentBucket<T>*>(existing->Get());
        }

        OwnerPtr<TComponentBucket<T>> created =
            MakeOwnerPtr<TComponentBucket<T>>(m_allocator);
        TComponentBucket<T>* result = created.Get();
        OwnerPtr<IComponentBucket> erased(std::move(created));
        if (false == m_componentBuckets.TryAdd(TypeId, std::move(erased)))
        {
            return nullptr;
        }
        return result;
    }

    template<typename T>
    Canvas::TComponentBucket<T>* Canvas::FindBucket()
    {
        static constexpr ComponentTypeId TypeId = MakeStableTypeId(T::StaticTypeName());
        OwnerPtr<IComponentBucket>* existing = m_componentBuckets.Find(TypeId);
        if (existing == nullptr)
        {
            return nullptr;
        }
        return static_cast<TComponentBucket<T>*>(existing->Get());
    }
}
