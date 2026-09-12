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
        object->BindCanvas(this, &Canvas::DestroyObjectFromHandle);
        object->SetTag(name);

        Layer* defaultLayer = FindLayer(m_defaultLayer);
        if (defaultLayer != nullptr)
        {
            object->SetLayer(
                FindLayerReference(m_defaultLayer),
                defaultLayer->GetIndex());
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
            SafePtr<ComponentBase> componentRef = object->m_components.Last();
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

        object->BindCanvas(nullptr, nullptr);
        object->SetLayer({}, 0);
        object->SetInstanceIdentity(InvalidInstanceId, {});
        return m_objects->Destroy(object);
    }

    std::size_t Canvas::GetObjectCount() const
    {
        return m_objects->GetLiveCount();
    }

    Layer& Canvas::CreateLayer(const char* name)
    {
        OwnerPtr<Layer> layer = MakeOwnerPtr<Layer>(
            m_nextLayer++,
            name == nullptr ? "Layer" : name);
        Layer& result = *layer;
        m_layers.Add(std::move(layer));
        if (m_defaultLayer == InvalidLayerIndex)
        {
            m_defaultLayer = result.GetIndex();
        }
        return result;
    }

    bool Canvas::DestroyLayer(LayerIndex layer)
    {
        if (m_layers.Size() <= 1)
        {
            return false;
        }

        const std::size_t index = m_layers.IndexOfBy(
            [layer](const OwnerPtr<Layer>& item)
            {
                return item->GetIndex() == layer;
            });
        if (index == decltype(m_layers)::InvalidIndex)
        {
            return false;
        }

        SafePtr<Layer> replacementRef;
        for (const OwnerPtr<Layer>& candidate : m_layers)
        {
            if (candidate->GetIndex() != layer)
            {
                replacementRef = candidate.GetSafePtr();
                break;
            }
        }
        Layer* replacement = replacementRef.TryGet();
        assert(replacement != nullptr);

        if (layer == m_defaultLayer)
        {
            m_defaultLayer = replacement->GetIndex();
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
            if (object.GetLayerIndex() == layer)
            {
                object.SetLayer(
                    replacementRef,
                    replacement->GetIndex());
            }
        });
        m_layers.RemoveAt(index);
        return true;
    }

    bool Canvas::MoveLayer(LayerIndex layer, std::size_t newIndex)
    {
        const std::size_t index = m_layers.IndexOfBy(
            [layer](const OwnerPtr<Layer>& item)
            {
                return item->GetIndex() == layer;
            });
        if (index == decltype(m_layers)::InvalidIndex || newIndex >= m_layers.Size())
        {
            return false;
        }

        OwnerPtr<Layer> owner = std::move(m_layers[index]);
        m_layers.RemoveAt(index);
        m_layers.Insert(newIndex, std::move(owner));
        return true;
    }

    bool Canvas::SetObjectLayer(GameObject* object, LayerIndex layer)
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
        object->SetLayer(FindLayerReference(layer), target->GetIndex());
        return true;
    }

    Layer* Canvas::FindLayer(LayerIndex layer)
    {
        const std::size_t index = m_layers.IndexOfBy(
            [layer](const OwnerPtr<Layer>& item)
            {
                return item->GetIndex() == layer;
            });
        if (index == decltype(m_layers)::InvalidIndex)
        {
            return nullptr;
        }
        return m_layers[index].Get();
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

    LayerIndex Canvas::GetDefaultLayer() const
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

        OwnerPtr<IComponentBucket>* bucket =
            m_componentBuckets.Find(component->GetTypeId());
        if (bucket == nullptr)
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
        return (*bucket)->Destroy(component);
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

    SafePtr<Layer> Canvas::FindLayerReference(LayerIndex layer)
    {
        const std::size_t index = m_layers.IndexOfBy(
            [layer](const OwnerPtr<Layer>& item)
            {
                return item->GetIndex() == layer;
            });
        if (index == decltype(m_layers)::InvalidIndex)
        {
            return {};
        }
        return m_layers[index].GetSafePtr();
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
