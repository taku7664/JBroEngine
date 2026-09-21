#include <JBro/Canvas/Canvas.h>

#include <JBro/Core/InstanceIdGenerator.h>
#include <JBro/Internal/InstanceRegistry.h>

#include <cassert>
#include <utility>

namespace JBro
{
    namespace
    {
        InstanceIdGenerator& GetCanvasInstanceIdGenerator()
        {
            static InstanceIdGenerator generator;
            return generator;
        }
    }

    Canvas::Canvas(JAllocator allocator)
        : m_allocator(allocator)
        , m_objects(MakeOwnerPtr<TObjectPool<GameObject>>(allocator))
    {
        GetCanvasInstanceIdGenerator().BeginFrame();
        CreateLayer("Default");
    }

    Canvas::~Canvas()
    {
        m_systems.RemoveAllSystems(*this);
        while (m_objects->GetLiveCount() != 0)
        {
            GameObject* object = nullptr;
            m_objects->ForEachLive([&object](GameObject& candidate)
            {
                if (object == nullptr)
                {
                    object = &candidate;
                }
            });

            if (object == nullptr || false == DestroyObject(object))
            {
                assert(false && "Canvas object teardown lost ownership state");
                break;
            }
        }
    }

    void Canvas::BeginFrame()
    {
        GetCanvasInstanceIdGenerator().BeginFrame();
    }

    SystemScheduler& Canvas::GetSystems()
    {
        return m_systems;
    }

    GameObject* Canvas::CreateObject(const char* name)
    {
        GameObject* object = m_objects->Create();
        if (object == nullptr)
        {
            return nullptr;
        }

        const InstanceId instanceId = GenerateCanvasInstanceId();
        Internal::InstanceRegistry& registry = Internal::InstanceRegistry::Get();
        const InstanceHandle handle = registry.Register(
            instanceId,
            InvalidInstanceId,
            RefCategory::Object,
            object);
        if (false == handle.IsSet())
        {
            m_objects->Destroy(object);
            return nullptr;
        }

        object->SetInstanceIdentity(instanceId, handle);
        object->BindCanvas(
            this,
            &Canvas::DestroyObjectFromHandle,
            &Canvas::MarkScriptOrderDirtyFromObject);
        object->SetTag(name);

        Layer* defaultLayer = FindLayer(m_defaultLayer);
        if (defaultLayer != nullptr)
        {
            object->SetLayer(
                FindLayerReference(m_defaultLayer),
                defaultLayer->GetId());
        }
        return object;
    }

    bool Canvas::DestroyObject(GameObject* object)
    {
        if (object == nullptr
            || object->m_canvas != this
            || object->m_destroying)
        {
            return false;
        }

        // 순회 중이면 요청만 받아 두고 안전 지점에서 수행한다(D-45).
        if (IsIterating())
        {
            SafePtr<GameObject> pending = object->SafeFromThis();
            if (false == pending.IsValid())
            {
                return false;
            }
            object->m_destroying = true;
            m_pendingDestroyObjects.Add(std::move(pending));
            return true;
        }
        return DestroyObjectNow(object);
    }

    bool Canvas::DestroyObjectNow(GameObject* object)
    {
        Internal::InstanceRegistry& registry = Internal::InstanceRegistry::Get();
        if (registry.Resolve(object->m_handle, RefCategory::Object) != object)
        {
            return false;
        }
        object->m_destroying = true;

        while (false == object->m_children.IsEmpty())
        {
            SafePtr<GameObject> childRef = object->m_children.Last();
            GameObject* child = childRef.TryGet();
            if (child == nullptr)
            {
                object->m_children.RemoveAtSwap(object->m_children.Size() - 1);
                continue;
            }

            // 이미 즉시 파괴 경로에 들어와 있다. 자식을 다시 큐로 보내면 부모가 먼저 사라진다.
            if (false == DestroyObjectNow(child))
            {
                child->SetParent(nullptr);
            }
        }

        while (false == object->m_components.IsEmpty())
        {
            SafePtr<ComponentBase> componentRef = object->m_components.Last().reference;
            ComponentBase* component = componentRef.TryGet();
            if (component == nullptr)
            {
                object->m_components.RemoveAtSwap(object->m_components.Size() - 1);
                continue;
            }

            if (false == DestroyComponentNow(component))
            {
                object->m_destroying = false;
                return false;
            }
        }

        object->SetParent(nullptr);
        const InstanceHandle handle = object->m_handle;
        if (false == registry.Unregister(handle))
        {
            object->m_destroying = false;
            return false;
        }

        object->BindCanvas(nullptr, nullptr, nullptr);
        object->SetLayer({}, 0);
        object->SetInstanceIdentity(InvalidInstanceId, {});
        return m_objects->Destroy(object);
    }

    std::size_t Canvas::GetObjectCount() const
    {
        return m_objects->GetLiveCount();
    }

    void Canvas::GetRootObjects(Array<GameObject*>& result)
    {
        // **죽었거나 더 이상 뿌리가 아닌 것을 먼저 뺀다.** 순서를 지키며 빼야 한다 -
        // 마지막 것으로 덮으면 부모를 하나 바꾼 것만으로 남은 뿌리들의 차례가 흐트러진다.
        m_rootOrder.RemoveAll([](const SafePtr<GameObject>& reference)
        {
            const GameObject* object = reference.TryGet();
            return object == nullptr || object->GetParent() != nullptr;
        });

        m_rootSeen.Clear();
        for (std::size_t index = 0; index < m_rootOrder.Size(); ++index)
        {
            if (const GameObject* object = m_rootOrder[index].TryGet())
            {
                m_rootSeen.TryAdd(object, std::uint8_t{1});
            }
        }

        // 새로 뿌리가 된 것은 뒤에 붙는다. 풀 순회 순서는 **여기서만** 쓰인다 -
        // 한 번 붙고 나면 그 뒤로는 이 목록이 순서다.
        ForEachObject([this](GameObject& object)
        {
            if (object.GetParent() != nullptr || m_rootSeen.Contains(&object))
            {
                return;
            }
            SafePtr<GameObject> reference = object.SafeFromThis();
            if (reference.IsValid())
            {
                m_rootSeen.TryAdd(&object, std::uint8_t{1});
                m_rootOrder.Add(std::move(reference));
            }
        });

        result.Clear();
        for (std::size_t index = 0; index < m_rootOrder.Size(); ++index)
        {
            if (GameObject* object = m_rootOrder[index].TryGet())
            {
                result.Add(object);
            }
        }
    }

    bool Canvas::FindRootIndex(const GameObject* object, std::size_t& index)
    {
        if (object == nullptr || object->GetParent() != nullptr)
        {
            return false;
        }
        Array<GameObject*> roots;
        GetRootObjects(roots);
        for (std::size_t at = 0; at < roots.Size(); ++at)
        {
            if (roots[at] == object)
            {
                index = at;
                return true;
            }
        }
        return false;
    }

    bool Canvas::SetRootIndex(GameObject* object, std::size_t index)
    {
        std::size_t current = 0;
        if (false == FindRootIndex(object, current))
        {
            return false;
        }
        // `GetRootObjects` 가 방금 목록을 맞췄으므로 `m_rootOrder` 에서 죽은 자리를
        // 다시 걸러 낼 필요가 없다. 살아 있는 뿌리만 남아 있다.
        const std::size_t count = m_rootOrder.Size();
        if (count == 0)
        {
            return false;
        }
        const std::size_t target = index < count ? index : count - 1;
        if (target == current)
        {
            return true;
        }
        SafePtr<GameObject> moved = m_rootOrder[current];
        m_rootOrder.RemoveAt(current);
        m_rootOrder.Insert(target, std::move(moved));
        return true;
    }

    Layer& Canvas::CreateLayer(const char* name)
    {
        OwnerPtr<Layer> layer = MakeOwnerPtr<Layer>(
            m_nextLayer++,
            name == nullptr ? "Layer" : name);
        Layer& result = *layer;
        m_layers.Add(std::move(layer));
        ReindexLayers();
        if (m_defaultLayer == InvalidLayerId)
        {
            m_defaultLayer = result.GetId();
        }
        return result;
    }

    bool Canvas::DestroyLayer(LayerId layer)
    {
        if (m_layers.Size() <= 1)
        {
            return false;
        }

        const std::size_t index = m_layers.IndexOfBy(
            [layer](const OwnerPtr<Layer>& item)
            {
                return item->GetId() == layer;
            });
        if (index == decltype(m_layers)::InvalidIndex)
        {
            return false;
        }

        SafePtr<Layer> replacementRef;
        for (const OwnerPtr<Layer>& candidate : m_layers)
        {
            if (candidate->GetId() != layer)
            {
                replacementRef = candidate.GetSafePtr();
                break;
            }
        }
        Layer* replacement = replacementRef.TryGet();
        assert(replacement != nullptr);

        if (layer == m_defaultLayer)
        {
            m_defaultLayer = replacement->GetId();
        }
        else
        {
            Layer* defaultLayer = FindLayer(m_defaultLayer);
            if (defaultLayer != nullptr)
            {
                replacement = defaultLayer;
                replacementRef = FindLayerReference(m_defaultLayer);
            }
        }

        m_objects->ForEachLive([layer, replacement, replacementRef](GameObject& object)
        {
            if (object.GetLayerId() == layer)
            {
                object.SetLayer(
                    replacementRef,
                    replacement->GetId());
            }
        });
        m_layers.RemoveAt(index);
        ReindexLayers();
        return true;
    }

    bool Canvas::MoveLayer(LayerId layer, std::size_t newIndex)
    {
        const std::size_t index = m_layers.IndexOfBy(
            [layer](const OwnerPtr<Layer>& item)
            {
                return item->GetId() == layer;
            });
        if (index == decltype(m_layers)::InvalidIndex || newIndex >= m_layers.Size())
        {
            return false;
        }

        OwnerPtr<Layer> owner = std::move(m_layers[index]);
        m_layers.RemoveAt(index);
        m_layers.Insert(newIndex, std::move(owner));
        ReindexLayers();
        return true;
    }

    bool Canvas::SetObjectLayer(GameObject* object, LayerId layer)
    {
        if (object == nullptr || object->GetCanvas() != this)
        {
            return false;
        }

        Layer* target = FindLayer(layer);
        if (target == nullptr)
        {
            return false;
        }
        object->SetLayer(FindLayerReference(layer), target->GetId());
        // 오브젝트가 다른 레이어로 가면 실행 차례의 가장 바깥 키가 바뀐다(D-45).
        MarkScriptOrderDirty();
        return true;
    }

    Layer* Canvas::FindLayer(LayerId layer)
    {
        const std::size_t index = m_layers.IndexOfBy(
            [layer](const OwnerPtr<Layer>& item)
            {
                return item->GetId() == layer;
            });
        if (index == decltype(m_layers)::InvalidIndex)
        {
            return nullptr;
        }
        return m_layers[index].Get();
    }

    void Canvas::ForEachComponentPool(
        const std::function<void(const ComponentPoolUsage&)>& visit) const
    {
        if (false == static_cast<bool>(visit))
        {
            return;
        }
        for (auto iterator = m_componentBuckets.begin();
            iterator != m_componentBuckets.end(); ++iterator)
        {
            const IComponentBucket* bucket = iterator->MappedValue.Get();
            if (bucket == nullptr)
            {
                continue;
            }
            ComponentPoolUsage usage;
            usage.typeId = iterator->KeyValue;
            usage.live = bucket->GetLiveCount();
            usage.capacity = bucket->GetCapacity();
            visit(usage);
        }
    }

    void Canvas::GetObjectPoolUsage(std::size_t& live, std::size_t& capacity) const
    {
        live = 0;
        capacity = 0;
        if (m_objects.Get() == nullptr)
        {
            return;
        }
        live = m_objects->GetLiveCount();
        capacity = m_objects->GetCapacity();
    }

    std::size_t Canvas::GetLayerCount() const
    {
        return m_layers.Size();
    }

    Layer* Canvas::GetLayerAt(std::size_t index)
    {
        if (index >= m_layers.Size())
        {
            return nullptr;
        }
        return m_layers[index].Get();
    }

    LayerId Canvas::GetDefaultLayer() const
    {
        return m_defaultLayer;
    }

    bool Canvas::DestroyComponent(ComponentBase* component)
    {
        if (component == nullptr)
        {
            return false;
        }

        GameObject* owner = component->GetOwnerObject();
        if (owner == nullptr || owner->GetCanvas() != this)
        {
            return false;
        }

        if (IsIterating())
        {
            SafePtr<ComponentBase> pending = component->SafeFromThis();
            if (false == pending.IsValid())
            {
                return false;
            }
            m_pendingDestroyComponents.Add(std::move(pending));
            return true;
        }
        return DestroyComponentNow(component);
    }

    bool Canvas::DestroyComponentNow(ComponentBase* component)
    {
        GameObject* owner = component->GetOwnerObject();
        if (owner == nullptr || owner->GetCanvas() != this)
        {
            return false;
        }

        // 타입 풀에 없으면 이름으로 붙인 스크립트다. 둘 중 하나에는 반드시 있어야 한다 —
        // 여기서 못 찾으면 오브젝트를 파괴할 수 없고 Canvas 해체가 멈춘다.
        OwnerPtr<IComponentBucket>* bucket =
            m_componentBuckets.Find(component->GetTypeId());
        OwnerPtr<ScriptPool>* scriptPool = nullptr;
        if (bucket == nullptr)
        {
            // NameId 와 ComponentTypeId 는 같은 문자열에 같은 해시를 건 값이다
            // (MakeNameId 가 MakeStableTypeId 다). ScriptRegistry 가 그 동일성을 확인한다.
            scriptPool = m_scriptPools.Find(component->GetTypeId());
        }
        if (bucket == nullptr && scriptPool == nullptr)
        {
            return false;
        }

        component->OnDetached();
        if (false == owner->DetachComponent(component))
        {
            return false;
        }

        if (false == UnregisterComponentInstance(component))
        {
            owner->AttachComponent(component);
            return false;
        }
        if (bucket != nullptr)
        {
            if ((*bucket)->HoldsScripts())
            {
                MarkScriptOrderDirty();
            }
            return (*bucket)->Destroy(component);
        }
        // 이름으로 붙인 것은 전부 스크립트다.
        MarkScriptOrderDirty();
        return (*scriptPool)->Destroy(static_cast<GameScriptBase*>(component));
    }

    bool Canvas::RegisterComponentInstance(
        GameObject* owner,
        ComponentBase* component,
        RefCategory category)
    {
        const InstanceId componentId = GenerateCanvasInstanceId();
        Internal::InstanceRegistry& registry = Internal::InstanceRegistry::Get();
        const InstanceHandle handle = registry.Register(
            owner->GetInstanceId(),
            componentId,
            category,
            component);
        if (false == handle.IsSet())
        {
            return false;
        }
        component->SetInstanceIdentity(componentId, handle);
        return true;
    }

    bool Canvas::UnregisterComponentInstance(ComponentBase* component)
    {
        Internal::InstanceRegistry& registry = Internal::InstanceRegistry::Get();
        if (false == registry.Unregister(component->GetHandle()))
        {
            return false;
        }
        component->SetInstanceIdentity(InvalidInstanceId, {});
        return true;
    }

    SafePtr<Layer> Canvas::FindLayerReference(LayerId layer)
    {
        const std::size_t index = m_layers.IndexOfBy(
            [layer](const OwnerPtr<Layer>& item)
            {
                return item->GetId() == layer;
            });
        if (index == decltype(m_layers)::InvalidIndex)
        {
            return {};
        }
        return m_layers[index].GetSafePtr();
    }

    void Canvas::ReindexLayers()
    {
        for (std::size_t index = 0; index < m_layers.Size(); ++index)
        {
            m_layers[index]->SetOrder(static_cast<LayerOrder>(index));
        }
        // 레이어 생성·파괴·이동이 전부 여기를 거친다(D-46). 합성 순서가 실행 순서의
        // 가장 바깥 키이므로 한곳에서 올린다(D-45).
        MarkScriptOrderDirty();
    }

    std::uint64_t Canvas::GetScriptOrderRevision() const
    {
        return m_scriptOrderRevision;
    }

    void Canvas::MarkScriptOrderDirty()
    {
        ++m_scriptOrderRevision;
    }

    void Canvas::MarkScriptOrderDirtyFromObject(Canvas* canvas)
    {
        if (canvas == nullptr)
        {
            return;
        }
        canvas->MarkScriptOrderDirty();
    }

    bool Canvas::IsIterating() const
    {
        return m_iterationDepth != 0;
    }

    std::size_t Canvas::GetPendingDestroyCount() const
    {
        return m_pendingDestroyComponents.Size() + m_pendingDestroyObjects.Size();
    }

    // 컴포넌트를 먼저 걷는다. 오브젝트 파괴가 자기 컴포넌트를 이미 정리하므로 순서를 뒤집으면
    // 큐에 남은 컴포넌트가 죽은 대상을 가리킨다. SafePtr 이 그것을 걸러 주지만 무의미한 일을 하게 된다.
    GameScriptBase* Canvas::AttachScript(GameObject* owner, const char* scriptName)
    {
        return AttachScript(owner, MakeNameId(scriptName));
    }

    GameScriptBase* Canvas::AttachScript(GameObject* owner, NameId scriptName)
    {
        if (owner == nullptr || owner->GetCanvas() != this || scriptName == InvalidNameId)
        {
            return nullptr;
        }
        const ScriptTypeInfo* type = ScriptRegistry::Get().Find(scriptName);
        if (type == nullptr)
        {
            return nullptr;
        }

        OwnerPtr<ScriptPool>* existing = m_scriptPools.Find(scriptName);
        if (existing == nullptr)
        {
            OwnerPtr<ScriptPool> pool = MakeOwnerPtr<ScriptPool>();
            if (false == pool->Initialize(*type, m_allocator))
            {
                return nullptr;
            }
            if (false == m_scriptPools.TryAdd(scriptName, std::move(pool)))
            {
                return nullptr;
            }
            existing = m_scriptPools.Find(scriptName);
        }
        if (existing == nullptr || existing->Get() == nullptr)
        {
            return nullptr;
        }

        ScriptPool& pool = *existing->Get();
        GameScriptBase* script = pool.Create();
        if (script == nullptr)
        {
            return nullptr;
        }
        script->CacheTypeId();

        if (false == RegisterComponentInstance(owner, script, RefCategory::Script))
        {
            pool.Destroy(script);
            return nullptr;
        }
        try
        {
            owner->AttachComponent(script);
        }
        catch (...)
        {
            UnregisterComponentInstance(script);
            pool.Destroy(script);
            throw;
        }
        MarkScriptOrderDirty();
        if (script->GetOwnerObject() != owner)
        {
            UnregisterComponentInstance(script);
            pool.Destroy(script);
            return nullptr;
        }

        // 소유 오브젝트와 식별자가 모두 확정된 뒤에 부른다(D-48).
        script->OnAttached();
        return script;
    }

    void Canvas::CollectScripts(Array<GameScriptBase*>& results)
    {
        results.Clear();
        // 모으는 동안 파괴가 배열을 흔들면 안 된다. 가드가 그 동안의 파괴를 큐로 보낸다.
        IterationGuard guard(*this);
        for (auto& entry : m_componentBuckets)
        {
            if (IComponentBucket* bucket = entry.MappedValue.Get())
            {
                bucket->AppendScripts(results);
            }
        }
        // 이름으로 붙인 것들도 같은 목록에 들어간다. 스케줄러가 둘을 구분할 이유가 없다.
        for (auto& entry : m_scriptPools)
        {
            if (ScriptPool* pool = entry.MappedValue.Get())
            {
                pool->ForEachLive([&results](GameScriptBase& script)
                {
                    results.Add(&script);
                });
            }
        }
    }

    void Canvas::FlushPendingDestroy()
    {
        if (IsIterating())
        {
            return;
        }

        while (false == m_pendingDestroyComponents.IsEmpty())
        {
            SafePtr<ComponentBase> pending = m_pendingDestroyComponents.Last();
            m_pendingDestroyComponents.RemoveAt(m_pendingDestroyComponents.Size() - 1);
            if (ComponentBase* component = pending.TryGet())
            {
                DestroyComponentNow(component);
            }
        }

        while (false == m_pendingDestroyObjects.IsEmpty())
        {
            SafePtr<GameObject> pending = m_pendingDestroyObjects.Last();
            m_pendingDestroyObjects.RemoveAt(m_pendingDestroyObjects.Size() - 1);
            // 부모가 먼저 파괴되면서 이 항목이 이미 죽었을 수 있다. SafePtr 이 그것을 걸러 준다.
            if (GameObject* object = pending.TryGet())
            {
                object->m_destroying = false;
                DestroyObjectNow(object);
            }
        }
    }

    bool Canvas::DestroyObjectFromHandle(Canvas* canvas, GameObject* object)
    {
        if (canvas == nullptr)
        {
            return false;
        }
        return canvas->DestroyObject(object);
    }

    InstanceId Canvas::GenerateCanvasInstanceId()
    {
        return GetCanvasInstanceIdGenerator().Generate();
    }
}
