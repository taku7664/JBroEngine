#include <JBro/Runtime/GameObject.h>
#include <JBro/Runtime/Component.h>

#include <algorithm>

namespace JBro
{
    GameObject::GameObject()  = default;
    GameObject::~GameObject() = default;

    InstanceId     GameObject::GetInstanceId() const { return m_instanceId; }
    InstanceHandle GameObject::GetHandle()     const { return m_handle; }

    Canvas* GameObject::GetCanvas() const { return m_canvas; }
    void    GameObject::SetCanvas(Canvas* canvas) { m_canvas = canvas; }

    GameObject* GameObject::GetParent() const { return m_parent; }
    void        GameObject::SetParent(GameObject* parent)
    {
        if (m_parent == parent) return;
        if (m_parent != nullptr)
        {
            auto& siblings = m_parent->m_children;
            siblings.erase(std::remove(siblings.begin(), siblings.end(), this), siblings.end());
        }
        m_parent = parent;
        if (m_parent != nullptr) m_parent->m_children.push_back(this);
    }
    const std::vector<GameObject*>& GameObject::GetChildren() const { return m_children; }

    std::uint32_t GameObject::GetLayerIndex() const { return m_layerIndex; }
    void          GameObject::SetLayerIndex(std::uint32_t layerIndex) { m_layerIndex = layerIndex; }

    bool GameObject::IsActiveSelf() const { return m_active; }
    bool GameObject::IsActiveInHierarchy() const
    {
        for (const GameObject* node = this; node != nullptr; node = node->m_parent)
        {
            if (false == node->m_active) return false;
        }
        return true;
    }
    void GameObject::SetActive(bool active) { m_active = active; }

    const char*   GameObject::GetTag()   const { return m_tag; }
    void          GameObject::SetTag(const char* tag) { m_tag = tag; }
    std::uint32_t GameObject::GetFlags() const { return m_flags; }
    void          GameObject::SetFlags(std::uint32_t flags) { m_flags = flags; }

    const std::vector<ComponentBase*>& GameObject::GetComponents() const { return m_components; }
    void GameObject::AttachComponent(ComponentBase* component)
    {
        if (component == nullptr) return;
        m_components.push_back(component);
        component->SetOwner(this);
    }
    bool GameObject::DetachComponent(ComponentBase* component)
    {
        const auto found = std::find(m_components.begin(), m_components.end(), component);
        if (found == m_components.end()) return false;
        (*found)->SetOwner(nullptr);
        m_components.erase(found);
        return true;
    }
}
