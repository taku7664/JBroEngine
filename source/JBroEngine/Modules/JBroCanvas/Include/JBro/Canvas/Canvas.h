#pragma once

#include <JBro/Core/Core.h>
#include <JBro/Core/ObjectPool.h>
#include <JBro/Core/StableTypeId.h>
#include <JBro/Runtime/Component.h>
#include <JBro/Runtime/ComponentLookupStats.h>
#include <JBro/Runtime/GameObject.h>
#include <JBro/Canvas/Layer.h>
#include <JBro/Canvas/SystemScheduler.h>
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

        // 순회 중 요청된 파괴를 실제로 수행한다(D-45). Framework 가 FixedUpdate 묶음 뒤와
        // Update 뒤 두 지점에서 부른다. 순회 중에 부르면 아무 일도 하지 않는다.
        void        FlushPendingDestroy();
        std::size_t GetPendingDestroyCount() const;
        bool        IsIterating() const;

        // 레이어
        Layer&      CreateLayer(const char* name = nullptr);
        bool        DestroyLayer(LayerId layer);
        bool        MoveLayer(LayerId layer, std::size_t newIndex);
        bool        SetObjectLayer(GameObject* object, LayerId layer);
        Layer*      FindLayer(LayerId layer);
        std::size_t GetLayerCount() const;
        Layer*      GetLayerAt(std::size_t index);
        LayerId  GetDefaultLayer() const;

        // 같은 타입을 여러 개 붙일 수 있다. 시스템 순회는 타입 풀을 직접 순회한다.
        template<typename T>
        T* AttachComponent(GameObject* owner);

        template<typename T>
        bool DetachComponent(GameObject* owner, T* component);

        template<typename T>
        T* FindComponentRaw(GameObject* owner);

        // Replaces results in attachment order. Pointers are short-lived borrowed references.
        // Reserve results during setup to avoid allocation on repeated queries.
        template<typename T>
        void FindComponentsRaw(GameObject* owner, Array<T*>& results);

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
        SafePtr<Layer> FindLayerReference(LayerId layer);
        // m_layers 의 순서가 바뀌는 모든 지점에서 부른다. 레이어의 순서 캐시를 갱신하는
        // 유일한 주체다(D-46).
        void ReindexLayers();

        // 순회 깊이를 세는 가드. live 배열이 순회 중에 흔들리면 바깥 순회가 무효화되므로,
        // 깊이가 0 이 아닌 동안의 파괴 요청은 큐로 간다(§8, 구 엔진 ScriptIterationGuard).
        class IterationGuard final
        {
        public:
            explicit IterationGuard(Canvas& canvas)
                : m_canvas(canvas)
            {
                ++m_canvas.m_iterationDepth;
            }

            ~IterationGuard()
            {
                --m_canvas.m_iterationDepth;
            }

            IterationGuard(const IterationGuard&)            = delete;
            IterationGuard& operator=(const IterationGuard&) = delete;

        private:
            Canvas& m_canvas;
        };

        bool DestroyObjectNow(GameObject* object);
        bool DestroyComponentNow(ComponentBase* component);
        // GameObject::RequestDestroy 가 건너오는 지점. GameObject 헤더는 Canvas 정의를 알지 않는다.
        static bool DestroyObjectFromHandle(Canvas* canvas, GameObject* object);
        static InstanceId GenerateCanvasInstanceId();

        JAllocator                                      m_allocator;
        OwnerPtr<TObjectPool<GameObject>>               m_objects;
        Array<OwnerPtr<Layer>>                          m_layers;
        LayerId                                      m_defaultLayer = InvalidLayerId;
        LayerId                                      m_nextLayer = 0;
        Table<ComponentTypeId, OwnerPtr<IComponentBucket>> m_componentBuckets;
        Array<SafePtr<GameObject>>                      m_pendingDestroyObjects;
        Array<SafePtr<ComponentBase>>                   m_pendingDestroyComponents;
        std::size_t                                     m_iterationDepth = 0;
        SystemScheduler m_systems;
    };

    template<typename Fn>
    void Canvas::ForEachObject(Fn&& function)
    {
        IterationGuard guard(*this);
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
        // 타입은 이 순간 이후로 바뀌지 않는다. 조회가 원소마다 가상 호출을 하지 않도록 캐시한다.
        component->CacheTypeId();

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

        if (component->GetOwnerObject() != owner)
        {
            UnregisterComponentInstance(component);
            bucket->Pool.Destroy(component);
            return nullptr;
        }

        // 소유 오브젝트와 식별자가 모두 확정된 뒤에 부른다(D-48).
        component->OnAttached();
        return component;
    }

    template<typename T>
    bool Canvas::DetachComponent(GameObject* owner, T* component)
    {
        static_assert(std::is_base_of_v<ComponentBase, T>);
        if (owner == nullptr
            || component == nullptr
            || owner->GetCanvas() != this
            || component->GetOwnerObject() != owner)
        {
            return false;
        }
        return DestroyComponent(component);
    }

    template<typename T>
    T* Canvas::FindComponentRaw(GameObject* owner)
    {
        static_assert(std::is_base_of_v<ComponentBase, T>);
        if (owner == nullptr || owner->GetCanvas() != this)
        {
            return nullptr;
        }

        static constexpr ComponentTypeId TypeId = MakeStableTypeId(T::StaticTypeName());
        ++Diagnostics::ComponentLookupCounters::Get().lookups;
        for (const ComponentSlot& slot : owner->m_components)
        {
            // 타입 비교는 슬롯 안에서 끝난다. 맞는 것 하나만 따라간다.
            if (slot.typeId != TypeId)
            {
                continue;
            }
            ++Diagnostics::ComponentLookupCounters::Get().dereferences;
            if (ComponentBase* component = slot.reference.TryGet())
            {
                return static_cast<T*>(component);
            }
        }
        return nullptr;
    }

    template<typename T>
    void Canvas::FindComponentsRaw(GameObject* owner, Array<T*>& results)
    {
        static_assert(std::is_base_of_v<ComponentBase, T>);
        results.Clear();
        if (owner == nullptr || owner->GetCanvas() != this)
        {
            return;
        }

        static constexpr ComponentTypeId TypeId = MakeStableTypeId(T::StaticTypeName());
        ++Diagnostics::ComponentLookupCounters::Get().lookups;
        for (const ComponentSlot& slot : owner->m_components)
        {
            if (slot.typeId != TypeId)
            {
                continue;
            }
            ++Diagnostics::ComponentLookupCounters::Get().dereferences;
            if (ComponentBase* component = slot.reference.TryGet())
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
        IterationGuard guard(*this);
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
