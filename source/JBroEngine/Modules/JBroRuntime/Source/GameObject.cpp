#include <JBro/Runtime/GameObject.h>

#include <JBro/Runtime/GameObjectHandle.h>

namespace JBro
{
    GameObject::GameObject() = default;
    GameObject::~GameObject() = default;

    InstanceId GameObject::GetInstanceId() const
    {
        return m_instanceId;
    }

    InstanceHandle GameObject::GetHandle() const
    {
        return m_handle;
    }

    GameObjectHandle GameObject::GetScriptHandle() const
    {
        return GameObjectHandle(this);
    }

    Canvas* GameObject::GetCanvas() const
    {
        return m_canvas;
    }

    GameObject* GameObject::GetParent() const
    {
        return m_parent.TryGet();
    }

    void GameObject::SetParent(GameObject* parent)
    {
        if (parent == this || m_parent.TryGet() == parent)
        {
            return;
        }

        for (GameObject* ancestor = parent;
            ancestor != nullptr;
            ancestor = ancestor->GetParent())
        {
            if (ancestor == this)
            {
                return;
            }
        }

        GameObject* oldParent = m_parent.TryGet();
        if (oldParent != nullptr)
        {
            oldParent->m_children.RemoveAllSwap([this](const SafePtr<GameObject>& child)
            {
                return child.TryGet() == this;
            });
        }

        if (parent == nullptr)
        {
            m_parent.Reset();
            // 부모가 바뀌면 이 부분 트리의 상속 활성값도 바뀐다.
            RefreshActiveInHierarchy();
            return;
        }

        m_parent = parent->SafeFromThis();
        SafePtr<GameObject> self = SafeFromThis();
        if (self.IsValid())
        {
            parent->m_children.Add(std::move(self));
        }
        RefreshActiveInHierarchy();
    }

    const Array<SafePtr<GameObject>>& GameObject::GetChildren() const
    {
        return m_children;
    }

    Layer* GameObject::GetLayer() const
    {
        return m_layer.TryGet();
    }

    std::uint32_t GameObject::GetLayerIndex() const
    {
        return m_layerIndex;
    }

    bool GameObject::IsActiveSelf() const
    {
        return m_active;
    }

    bool GameObject::IsActiveInHierarchy() const
    {
        return m_activeInHierarchy;
    }

    void GameObject::SetActive(bool active)
    {
        if (m_active == active)
        {
            return;
        }
        m_active = active;
        RefreshActiveInHierarchy();
    }

    // 자기 값과 부모의 캐시로 결과를 정하고, 바뀐 경우에만 자식으로 내려간다.
    // 비용은 실제로 상태가 뒤집힌 부분 트리에만 든다.
    void GameObject::RefreshActiveInHierarchy()
    {
        const GameObject* parent = m_parent.TryGet();
        const bool resolved = m_active && (parent == nullptr || parent->m_activeInHierarchy);
        if (m_activeInHierarchy == resolved)
        {
            return;
        }
        m_activeInHierarchy = resolved;

        for (const SafePtr<GameObject>& childReference : m_children)
        {
            if (GameObject* child = childReference.TryGet())
            {
                child->RefreshActiveInHierarchy();
            }
        }
    }

    const char* GameObject::GetTag() const
    {
        return m_tag.c_str();
    }

    void GameObject::SetTag(const char* tag)
    {
        m_tag = tag == nullptr ? "" : tag;
    }

    std::uint32_t GameObject::GetFlags() const
    {
        return m_flags;
    }

    void GameObject::SetFlags(std::uint32_t flags)
    {
        m_flags = flags;
    }

    const Array<SafePtr<ComponentBase>>& GameObject::GetComponents() const
    {
        return m_components;
    }

    void GameObject::SetInstanceIdentity(
        InstanceId instanceId,
        InstanceHandle handle)
    {
        m_instanceId = instanceId;
        m_handle = handle;
    }

    void GameObject::BindCanvas(Canvas* canvas, DestroyFunction destroyFunction)
    {
        m_canvas = canvas;
        m_destroyFunction = destroyFunction;
    }

    void GameObject::SetLayer(SafePtr<Layer> layer, std::uint32_t layerIndex)
    {
        m_layer = std::move(layer);
        m_layerIndex = layerIndex;
    }

    void GameObject::AttachComponent(ComponentBase* component)
    {
        if (component == nullptr)
        {
            return;
        }

        SafePtr<ComponentBase> safe = component->SafeFromThis();
        if (false == safe.IsValid())
        {
            return;
        }
        m_components.Add(std::move(safe));
        component->SetOwner(this);
    }

    bool GameObject::DetachComponent(ComponentBase* component)
    {
        if (component == nullptr)
        {
            return false;
        }

        const std::size_t removed = m_components.RemoveAll(
            [component](const SafePtr<ComponentBase>& candidate)
            {
                return candidate.TryGet() == component;
            });
        if (removed == 0)
        {
            return false;
        }
        component->SetOwner(nullptr);
        return true;
    }

    InstanceRef GameObject::FindComponentReference(ComponentTypeId typeId) const
    {
        for (const SafePtr<ComponentBase>& componentRef : m_components)
        {
            ComponentBase* component = componentRef.TryGet();
            if (component != nullptr && component->GetTypeId() == typeId)
            {
                InstanceRef result;
                result.ObjectId = m_instanceId;
                result.ComponentId = component->GetInstanceId();
                result.Cached = component->GetHandle();
                return result;
            }
        }
        return {};
    }

    bool GameObject::RequestDestroy()
    {
        if (m_canvas == nullptr || m_destroyFunction == nullptr)
        {
            return false;
        }
        return m_destroyFunction(m_canvas, this);
    }
}
