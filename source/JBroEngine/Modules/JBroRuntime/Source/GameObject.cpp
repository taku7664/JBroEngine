#include <JBro/Runtime/GameObject.h>

#include <JBro/Runtime/GameObjectHandle.h>

namespace JBro
{
    Object::GameObject::GameObject() = default;
    Object::GameObject::~GameObject() = default;

    InstanceId Object::GameObject::GetInstanceId() const
    {
        return m_instanceId;
    }

    InstanceHandle Object::GameObject::GetHandle() const
    {
        return m_handle;
    }

    Handle::GameObject Object::GameObject::GetScriptHandle() const
    {
        return Handle::GameObject(this);
    }

    Canvas* Object::GameObject::GetCanvas() const
    {
        return m_canvas;
    }

    Object::GameObject* Object::GameObject::GetParent() const
    {
        return m_parent.TryGet();
    }

    void Object::GameObject::SetParent(Object::GameObject* parent)
    {
        if (parent == this || m_parent.TryGet() == parent)
        {
            return;
        }
        // 계층이 바뀌면 깊이 우선 순회의 결과가 달라진다(D-45).
        MarkScriptOrderDirty();

        for (Object::GameObject* ancestor = parent;
            ancestor != nullptr;
            ancestor = ancestor->GetParent())
        {
            if (ancestor == this)
            {
                return;
            }
        }

        Object::GameObject* oldParent = m_parent.TryGet();
        if (oldParent != nullptr)
        {
            // **순서를 지키며 뺀다.** 마지막 것을 끌어다 덮으면(RemoveAllSwap)
            // 부모를 바꾸는 것만으로 남은 형제들의 차례가 흐트러진다 - 계층
            // 패널에서 눈에 보이는 순서이고, 끌어 옮긴 것을 되돌려도 제자리로
            // 돌아오지 않게 된다.
            oldParent->m_children.RemoveAll([this](const SafePtr<Object::GameObject>& child)
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
        SafePtr<Object::GameObject> self = SafeFromThis();
        if (self.IsValid())
        {
            parent->m_children.Add(std::move(self));
        }
        RefreshActiveInHierarchy();
    }

    const Array<SafePtr<Object::GameObject>>& Object::GameObject::GetChildren() const
    {
        return m_children;
    }

    Layer* Object::GameObject::GetLayer() const
    {
        return m_layer.TryGet();
    }

    std::uint32_t Object::GameObject::GetLayerId() const
    {
        return m_layerIndex;
    }

    bool Object::GameObject::IsActiveSelf() const
    {
        return m_active;
    }

    bool Object::GameObject::IsActiveInHierarchy() const
    {
        return m_activeInHierarchy;
    }

    void Object::GameObject::SetActive(bool active)
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
    void Object::GameObject::RefreshActiveInHierarchy()
    {
        const Object::GameObject* parent = m_parent.TryGet();
        const bool resolved = m_active && (parent == nullptr || parent->m_activeInHierarchy);
        if (m_activeInHierarchy == resolved)
        {
            return;
        }
        m_activeInHierarchy = resolved;

        for (const SafePtr<Object::GameObject>& childReference : m_children)
        {
            if (Object::GameObject* child = childReference.TryGet())
            {
                child->RefreshActiveInHierarchy();
            }
        }
    }

    const char* Object::GameObject::GetTag() const
    {
        return NameTable::Get().Resolve(m_tag);
    }

    void Object::GameObject::SetTag(const char* tag)
    {
        m_tag = NameTable::Get().Intern(tag);
    }

    NameId Object::GameObject::GetTagId() const
    {
        return m_tag;
    }

    void Object::GameObject::SetTagId(NameId tag)
    {
        m_tag = tag;
    }

    std::uint32_t Object::GameObject::GetFlags() const
    {
        return m_flags.Get();
    }

    void Object::GameObject::SetFlags(std::uint32_t flags)
    {
        m_flags.Set(flags);
    }

    const Array<ComponentSlot>& Object::GameObject::GetComponents() const
    {
        return m_components;
    }

    void Object::GameObject::SetInstanceIdentity(
        InstanceId instanceId,
        InstanceHandle handle)
    {
        m_instanceId = instanceId;
        m_handle = handle;
    }

    void Object::GameObject::BindCanvas(
        Canvas* canvas,
        DestroyFunction destroyFunction,
        ScriptOrderDirtyFunction scriptOrderDirtyFunction)
    {
        m_canvas = canvas;
        m_destroyFunction = destroyFunction;
        m_scriptOrderDirtyFunction = scriptOrderDirtyFunction;
    }

    void Object::GameObject::MarkScriptOrderDirty()
    {
        if (m_canvas == nullptr || m_scriptOrderDirtyFunction == nullptr)
        {
            return;
        }
        m_scriptOrderDirtyFunction(m_canvas);
    }

    void Object::GameObject::SetLayer(SafePtr<Layer> layer, std::uint32_t layerIndex)
    {
        m_layer = std::move(layer);
        m_layerIndex = layerIndex;
    }

    void Object::GameObject::AttachComponent(ComponentBase* component)
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
        // 타입 id 를 참조 옆에 둔다. 타입으로 찾을 때 후보마다 제어 블록을
        // 따라가지 않기 위해서다 — 그 추적이 매 프레임 캐시 미스였다(§3.4).
        ComponentSlot slot;
        slot.typeId = component->GetCachedTypeId();
        slot.reference = std::move(safe);
        m_components.Add(std::move(slot));
        component->SetOwner(this);
    }

    bool Object::GameObject::FindChildIndex(const Object::GameObject* child, std::size_t& index) const
    {
        for (std::size_t at = 0; at < m_children.Size(); ++at)
        {
            if (m_children[at].TryGet() == child)
            {
                index = at;
                return true;
            }
        }
        return false;
    }

    bool Object::GameObject::SetChildIndex(Object::GameObject* child, std::size_t index)
    {
        std::size_t from = 0;
        if (child == nullptr || false == FindChildIndex(child, from))
        {
            return false;
        }
        const std::size_t last = m_children.Size() - 1;
        const std::size_t to = index > last ? last : index;
        if (from == to)
        {
            return true;
        }
        // 형제 자리도 실행 차례다 - 깊이 우선 순회가 자식 배열을 그대로 내려간다(D-45).
        MarkScriptOrderDirty();
        SafePtr<Object::GameObject> moved = m_children[from];
        if (from < to)
        {
            for (std::size_t at = from; at < to; ++at)
            {
                m_children[at] = m_children[at + 1];
            }
        }
        else
        {
            for (std::size_t at = from; at > to; --at)
            {
                m_children[at] = m_children[at - 1];
            }
        }
        m_children[to] = moved;
        return true;
    }

    bool Object::GameObject::FindComponentIndex(const ComponentBase* component, std::size_t& index) const
    {
        if (component == nullptr)
        {
            return false;
        }
        for (std::size_t at = 0; at < m_components.Size(); ++at)
        {
            if (m_components[at].reference.TryGet() == component)
            {
                index = at;
                return true;
            }
        }
        return false;
    }

    bool Object::GameObject::SetComponentIndex(const ComponentBase* component, std::size_t index)
    {
        std::size_t from = 0;
        if (false == FindComponentIndex(component, from))
        {
            return false;
        }
        const std::size_t last = m_components.Size() - 1;
        const std::size_t to = index > last ? last : index;
        if (from == to)
        {
            return true;
        }
        // 오브젝트 안의 실행 차례가 이 자리다(D-45).
        MarkScriptOrderDirty();
        // 밀어서 끼운다. 마지막 것과 바꾸면 사이에 있던 것들의 차례가 흐트러진다(D-84).
        ComponentSlot moved = m_components[from];
        if (from < to)
        {
            for (std::size_t at = from; at < to; ++at)
            {
                m_components[at] = m_components[at + 1];
            }
        }
        else
        {
            for (std::size_t at = from; at > to; --at)
            {
                m_components[at] = m_components[at - 1];
            }
        }
        m_components[to] = moved;
        return true;
    }

    bool Object::GameObject::DetachComponent(ComponentBase* component)
    {
        if (component == nullptr)
        {
            return false;
        }

        const std::size_t removed = m_components.RemoveAll(
            [component](const ComponentSlot& candidate)
            {
                return candidate.reference.TryGet() == component;
            });
        if (removed == 0)
        {
            return false;
        }
        component->SetOwner(nullptr);
        return true;
    }

    InstanceRef Object::GameObject::FindComponentReference(ComponentTypeId typeId) const
    {
        for (const ComponentSlot& slot : m_components)
        {
            if (slot.typeId != typeId)
            {
                continue;
            }
            ComponentBase* component = slot.reference.TryGet();
            if (component != nullptr)
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

    bool Object::GameObject::RequestDestroy()
    {
        if (m_canvas == nullptr || m_destroyFunction == nullptr)
        {
            return false;
        }
        return m_destroyFunction(m_canvas, this);
    }
}
