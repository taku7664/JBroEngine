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
            return;
        }

        m_parent = parent->SafeFromThis();
        SafePtr<GameObject> self = SafeFromThis();
        if (self.IsValid())
        {
            parent->m_children.Add(std::move(self));
        }
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
        for (const GameObject* node = this;
            node != nullptr;
            node = node->m_parent.TryGet())
        {
            if (false == node->m_active)
            {
                return false;
            }
        }
        return true;
    }

    void GameObject::SetActive(bool active)
    {
        m_active = active;
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
