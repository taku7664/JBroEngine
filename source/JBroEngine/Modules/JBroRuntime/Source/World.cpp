#include <JBro/Runtime/World.h>

#include <algorithm>
#include <cstring>

namespace JBro::Engine
{
    CWorld::CWorld(JAllocator allocator)
        : m_allocator(allocator)
        , m_entities(allocator)
    {
        RegisterComponent<NameComponent>(NameComponentType);
        RegisterComponent<ActiveComponent>(ActiveComponentType);
        RegisterComponent<HierarchyComponent>(HierarchyComponentType);
    }

    CWorld::~CWorld()
    {
        Clear();
    }

    Entity CWorld::CreateEntity(const char* name)
    {
        const Entity entity = m_entities.Create();
        if (entity == InvalidEntity) return InvalidEntity;
        if (AddComponent<NameComponent>(entity, NameComponentType) == nullptr ||
            AddComponent<ActiveComponent>(entity, ActiveComponentType) == nullptr ||
            AddComponent<HierarchyComponent>(entity, HierarchyComponentType) == nullptr)
        {
            DestroyEntityImmediate(entity);
            return InvalidEntity;
        }
        SetName(entity, name == nullptr ? "Entity" : name);
        return entity;
    }

    bool CWorld::DestroyEntity(Entity entity)
    {
        if (false == IsAlive(entity)) return false;
        if (std::find(m_pendingDestroy.begin(), m_pendingDestroy.end(), entity) == m_pendingDestroy.end())
        {
            m_pendingDestroy.push_back(entity);
        }
        return true;
    }

    void CWorld::FlushCommands()
    {
        if (m_flushingCommands) return;
        m_flushingCommands = true;
        std::vector<Entity> pending;
        pending.swap(m_pendingDestroy);
        for (Entity entity : pending)
        {
            if (false == m_entities.IsAlive(entity)) continue;
            DestroyEntityImmediate(entity);
        }
        m_flushingCommands = false;
    }

    void CWorld::Clear()
    {
        m_pendingDestroy.clear();
        for (OwnerPtr<IComponentStorage>& storage : m_storages) storage->Clear();
        m_entities.Clear();
    }

    bool CWorld::IsAlive(Entity entity) const
    {
        return m_entities.IsAlive(entity);
    }

    std::size_t CWorld::GetEntityCount() const
    {
        return m_entities.GetLiveCount();
    }

    const char* CWorld::GetName(Entity entity) const
    {
        const NameComponent* component = GetComponent<NameComponent>(entity, NameComponentType);
        return component == nullptr ? nullptr : component->value;
    }

    bool CWorld::SetName(Entity entity, const char* name)
    {
        NameComponent* component = GetComponent<NameComponent>(entity, NameComponentType);
        if (component == nullptr) return false;
        const char* source = name == nullptr ? "" : name;
        const std::size_t length = std::min(std::strlen(source), sizeof(component->value) - 1);
        std::memcpy(component->value, source, length);
        component->value[length] = '\0';
        return true;
    }

    bool CWorld::SetActive(Entity entity, bool active)
    {
        ActiveComponent* component = GetComponent<ActiveComponent>(entity, ActiveComponentType);
        if (component == nullptr) return false;
        component->active = active;
        return true;
    }

    bool CWorld::IsActiveSelf(Entity entity) const
    {
        const ActiveComponent* component = GetComponent<ActiveComponent>(entity, ActiveComponentType);
        return component != nullptr && component->active;
    }

    bool CWorld::IsActiveInHierarchy(Entity entity) const
    {
        if (false == IsActiveSelf(entity)) return false;
        Entity current = GetParent(entity);
        while (IsAlive(current))
        {
            if (false == IsActiveSelf(current)) return false;
            current = GetParent(current);
        }
        return true;
    }

    bool CWorld::SetParent(Entity child, Entity parent)
    {
        if (false == IsAlive(child) || false == IsAlive(parent) || child == parent || IsDescendantOf(parent, child)) return false;
        ClearParent(child);
        HierarchyComponent* childHierarchy = GetComponent<HierarchyComponent>(child, HierarchyComponentType);
        HierarchyComponent* parentHierarchy = GetComponent<HierarchyComponent>(parent, HierarchyComponentType);
        if (childHierarchy == nullptr || parentHierarchy == nullptr) return false;
        childHierarchy->parent = parent;
        parentHierarchy->children.push_back(child);
        return true;
    }

    bool CWorld::ClearParent(Entity child)
    {
        HierarchyComponent* childHierarchy = GetComponent<HierarchyComponent>(child, HierarchyComponentType);
        if (childHierarchy == nullptr) return false;
        if (IsAlive(childHierarchy->parent))
        {
            if (HierarchyComponent* parentHierarchy = GetComponent<HierarchyComponent>(childHierarchy->parent, HierarchyComponentType))
            {
                std::erase(parentHierarchy->children, child);
            }
        }
        childHierarchy->parent = InvalidEntity;
        return true;
    }

    Entity CWorld::GetParent(Entity child) const
    {
        const HierarchyComponent* component = GetComponent<HierarchyComponent>(child, HierarchyComponentType);
        return component == nullptr ? InvalidEntity : component->parent;
    }

    bool CWorld::IsDescendantOf(Entity entity, Entity possibleAncestor) const
    {
        Entity current = GetParent(entity);
        while (IsAlive(current))
        {
            if (current == possibleAncestor) return true;
            current = GetParent(current);
        }
        return false;
    }

    void CWorld::DestroyEntityImmediate(Entity entity)
    {
        if (false == IsAlive(entity)) return;
        const HierarchyComponent* hierarchy = GetComponent<HierarchyComponent>(entity, HierarchyComponentType);
        const std::vector<Entity> children = hierarchy == nullptr ? std::vector<Entity>{} : hierarchy->children;
        for (Entity child : children) DestroyEntityImmediate(child);
        ClearParent(entity);
        for (OwnerPtr<IComponentStorage>& storage : m_storages) storage->Remove(entity);
        m_entities.Destroy(entity);
    }

    IComponentStorage* CWorld::FindStorage(ComponentTypeId typeId)
    {
        const auto found = std::find_if(m_storages.begin(), m_storages.end(), [typeId](const OwnerPtr<IComponentStorage>& storage)
        {
            return storage->GetTypeId() == typeId;
        });
        return found == m_storages.end() ? nullptr : found->get();
    }

    const IComponentStorage* CWorld::FindStorage(ComponentTypeId typeId) const
    {
        const auto found = std::find_if(m_storages.begin(), m_storages.end(), [typeId](const OwnerPtr<IComponentStorage>& storage)
        {
            return storage->GetTypeId() == typeId;
        });
        return found == m_storages.end() ? nullptr : found->get();
    }
}
