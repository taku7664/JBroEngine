#pragma once

#include <JBro/Core/Core.h>
#include <JBro/Core/ObjectPool.h>
#include <JBro/Core/StableTypeId.h>
#include <JBro/Runtime/Component.h>
#include <JBro/Runtime/GameScriptBase.h>
#include <JBro/Runtime/ComponentLookupStats.h>
#include <JBro/Runtime/GameObject.h>
#include <JBro/Canvas/Layer.h>
#include <JBro/Canvas/ScriptPool.h>
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

        // 이름으로 스크립트를 붙인다(H5). 타입이 DLL 안에 있어 호스트가 컴파일 시간에
        // 알 수 없으므로, 무엇을 만들지는 ScriptRegistry 가 알려 준다.
        // 등록되지 않은 이름이면 nullptr 이다.
        GameScriptBase* AttachScript(GameObject* owner, NameId scriptName);
        GameScriptBase* AttachScript(GameObject* owner, const char* scriptName);

        // 타입을 가리지 않고 살아 있는 스크립트를 전부 모은다(D-45).
        // 어느 풀이 스크립트인지는 AttachComponent<T> 시점에 컴파일 타임으로 정해지므로
        // 매 프레임 dynamic_cast 가 필요 없다(§9).
        // 결과는 정렬되지 않은 채로 나온다. 실행 순서를 세우는 것은 부르는 쪽의 일이다.
        void CollectScripts(Array<GameScriptBase*>& results);

        // 스크립트 실행 목록이 언제 헌 것이 되는지를 알리는 표다(D-45).
        //
        // 목록은 **더티 플래그로 지연 재구축한다**. 값이 바뀌었을 때만 다시 세우면 되고,
        // 그 판단을 `System::ScriptSystem` 이 이 값을 기억했다가 비교해서 한다.
        // 불리언 대신 세는 값인 이유는, 목록을 들고 있는 쪽이 여럿이어도 각자 자기가 본
        // 마지막 값과 견주면 되기 때문이다.
        //
        // 오르는 자리는 D-45 가 이름을 댄 것들이다 - 스크립트 부착·분리, `SetParent`,
        // 컴포넌트 자리 이동, 레이어 생성·파괴·이동, 오브젝트의 레이어 변경, 오브젝트 파괴.
        std::uint64_t GetScriptOrderRevision() const;

        // 순회 깊이를 세는 가드. live 배열이 순회 중에 흔들리면 바깥 순회가 무효화되므로,
        // 깊이가 0 이 아닌 동안의 파괴 요청은 큐로 간다(§8, 구 엔진 ScriptIterationGuard).
        //
        // **공개인 이유**: §8 은 이 가드를 `ForEach<T>` 뿐 아니라 **스크립트 실행 목록
        // 순회**에도 적용하라고 한다. 그 순회는 `System::ScriptSystem` 에 있어 Canvas 밖이다.
        // private 으로 두었을 때 그 자리가 맨몸으로 돌았고, 스크립트가 훅 안에서 오브젝트를
        // 지우면 파괴자가 지나간 객체 위에서 다음 훅이 불렸다. Canvas 는 Tier E 라
        // 스크립트 타깃의 include 경로에 없으므로 사용자에게는 이 이름이 보이지 않는다.
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


    private:
        struct IComponentBucket
        {
            virtual ~IComponentBucket() = default;
            virtual bool Destroy(ComponentBase* component) = 0;
            // 스크립트 풀만 자기 원소를 여기에 쏟는다. 나머지는 아무 일도 하지 않는다.
            virtual void AppendScripts(Array<GameScriptBase*>& results) = 0;
            // 파괴할 때 실행 목록을 헌 것으로 표시할지 가른다. 타입은 컴파일 타임에 안다.
            virtual bool HoldsScripts() const = 0;
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

            void AppendScripts(Array<GameScriptBase*>& results) override
            {
                if constexpr (std::is_base_of_v<GameScriptBase, T>)
                {
                    Pool.ForEachLive([&results](T& script)
                    {
                        results.Add(static_cast<GameScriptBase*>(&script));
                    });
                }
                else
                {
                    (void)results;
                }
            }

            bool HoldsScripts() const override
            {
                return std::is_base_of_v<GameScriptBase, T>;
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
        // 스크립트 실행 목록을 헌 것으로 표시한다. GameObject 는 Tier S 라 Canvas 를
        // 알 수 없으므로 파괴와 같은 방식으로 함수 포인터를 타고 건너온다.
        void MarkScriptOrderDirty();
        static void MarkScriptOrderDirtyFromObject(Canvas* canvas);

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
        // 이름으로 붙인 스크립트의 저장소다. 타입마다 하나씩 늦게 만든다.
        Table<NameId, OwnerPtr<ScriptPool>>             m_scriptPools;
        Array<SafePtr<GameObject>>                      m_pendingDestroyObjects;
        Array<SafePtr<ComponentBase>>                   m_pendingDestroyComponents;
        std::size_t                                     m_iterationDepth = 0;
        // 0 은 "아직 아무것도 본 적 없음" 을 뜻하는 쪽이 쓰므로 1 에서 시작한다.
        std::uint64_t                                   m_scriptOrderRevision = 1;
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

        // 타입 이름을 이름표에 넣어 둔다. 컴포넌트는 타입 **id** 만 들고 다니므로,
        // 저장과 인스펙터가 그 id 에서 이름을 되찾으려면 원문이 어딘가에 있어야 한다.
        //
        // 여기서 하지 않으면 프로퍼티를 등록한 타입만 이름이 남는다 — 그러면 저장이
        // 등록에 딸려 오는 부수 효과에 기대게 되고, 등록하지 않은 타입을 만났을 때
        // 오류 메시지가 정작 그 타입을 말하지 못한다.
        //
        // 캐시하지 않는다. 이름표는 스크립트 DLL 이 로드될 때 바뀔 수 있고, 그때 캐시가
        // 남아 있으면 원문이 없어진 표를 가리킨다. 이미 있는 키를 넣는 것은 조회 한 번이고
        // 부착은 매 프레임 도는 경로가 아니다.
        NameTable::Get().Intern(T::StaticTypeName());

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

        // **스크립트일 때만 올린다.** 매 프레임 스폰이 컴포넌트를 붙이는데, 그때마다
        // 목록을 헌 것으로 만들면 더티 플래그를 둔 뜻이 없어진다. 스크립트인지는
        // 타입에서 컴파일 타임에 갈린다(§9).
        if constexpr (std::is_base_of_v<GameScriptBase, T>)
        {
            MarkScriptOrderDirty();
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
