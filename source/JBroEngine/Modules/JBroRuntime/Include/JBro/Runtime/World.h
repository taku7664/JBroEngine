#pragma once

#include <JBro/Core/ECS/ComponentRegistry.h>
#include <JBro/Core/ECS/ComponentStorage.h>
#include <JBro/Core/ECS/EntityRegistry.h>

#include <stdexcept>
#include <vector>

namespace JBro::Engine
{
    inline constexpr ComponentTypeId NameComponentType{ 0x1001 };
    inline constexpr ComponentTypeId ActiveComponentType{ 0x1002 };
    inline constexpr ComponentTypeId HierarchyComponentType{ 0x1003 };

    struct NameComponent
    {
        char value[64]{};
    };

    struct ActiveComponent
    {
        bool active = true;
    };

    struct HierarchyComponent
    {
        Entity parent = InvalidEntity;
        std::vector<Entity> children;
    };

    class CWorld
    {
    public:
        explicit CWorld(JAllocator allocator);
        ~CWorld();

        CWorld(const CWorld&) = delete;
        CWorld& operator=(const CWorld&) = delete;

        Entity CreateEntity(const char* name = nullptr);
        bool DestroyEntity(Entity entity);
        void FlushCommands();
        void Clear();
        bool IsAlive(Entity entity) const;
        std::size_t GetEntityCount() const;
        const char* GetName(Entity entity) const;
        bool SetName(Entity entity, const char* name);
        bool SetActive(Entity entity, bool active);
        bool IsActiveSelf(Entity entity) const;
        bool IsActiveInHierarchy(Entity entity) const;
        bool SetParent(Entity child, Entity parent);
        bool ClearParent(Entity child);
        Entity GetParent(Entity child) const;

        template <typename T>
        bool RegisterComponent(ComponentTypeId typeId)
        {
            if (false == m_componentRegistry.Register({ typeId, sizeof(T), alignof(T) }))
            {
                return false;
            }
            if (IComponentStorage* storage = FindStorage(typeId))
            {
                return dynamic_cast<TComponentStorage<T>*>(storage) != nullptr;
            }

            m_storages.push_back(MakeOwnerPtr<TComponentStorage<T>>(typeId, m_allocator));
            return true;
        }

        template <typename T, typename... Args>
        T* AddComponent(Entity entity, ComponentTypeId typeId, Args&&... args)
        {
            if (false == IsAlive(entity)) return nullptr;
            TComponentStorage<T>* storage = FindTypedStorage<T>(typeId);
            return storage == nullptr ? nullptr : storage->Add(entity, std::forward<Args>(args)...);
        }

        template <typename T>
        T* GetComponent(Entity entity, ComponentTypeId typeId)
        {
            TComponentStorage<T>* storage = FindTypedStorage<T>(typeId);
            return storage == nullptr ? nullptr : storage->Get(entity);
        }

        template <typename T>
        const T* GetComponent(Entity entity, ComponentTypeId typeId) const
        {
            const TComponentStorage<T>* storage = FindTypedStorage<T>(typeId);
            return storage == nullptr ? nullptr : storage->Get(entity);
        }

        template <typename T>
        bool RemoveComponent(Entity entity, ComponentTypeId typeId)
        {
            TComponentStorage<T>* storage = FindTypedStorage<T>(typeId);
            return storage != nullptr && storage->Remove(entity);
        }

        template <typename T, typename Fn>
        void Query(ComponentTypeId typeId, Fn&& function)
        {
            if (TComponentStorage<T>* storage = FindTypedStorage<T>(typeId))
            {
                storage->ForEach(std::forward<Fn>(function));
            }
        }

        template <typename A, typename B, typename Fn>
        void Query(ComponentTypeId firstType, ComponentTypeId secondType, Fn&& function)
        {
            TComponentStorage<A>* first = FindTypedStorage<A>(firstType);
            TComponentStorage<B>* second = FindTypedStorage<B>(secondType);
            if (first == nullptr || second == nullptr) return;
            first->ForEach([second, &function](Entity entity, A& firstComponent)
            {
                if (B* secondComponent = second->Get(entity))
                {
                    function(entity, firstComponent, *secondComponent);
                }
            });
        }

    private:
        IComponentStorage* FindStorage(ComponentTypeId typeId);
        const IComponentStorage* FindStorage(ComponentTypeId typeId) const;
        void DestroyEntityImmediate(Entity entity);
        bool IsDescendantOf(Entity entity, Entity possibleAncestor) const;

        template <typename T>
        TComponentStorage<T>* FindTypedStorage(ComponentTypeId typeId)
        {
            return dynamic_cast<TComponentStorage<T>*>(FindStorage(typeId));
        }

        template <typename T>
        const TComponentStorage<T>* FindTypedStorage(ComponentTypeId typeId) const
        {
            return dynamic_cast<const TComponentStorage<T>*>(FindStorage(typeId));
        }

        JAllocator m_allocator;
        CEntityRegistry m_entities;
        CComponentRegistry m_componentRegistry;
        std::vector<OwnerPtr<IComponentStorage>> m_storages;
        std::vector<Entity> m_pendingDestroy;
        bool m_flushingCommands = false;
    };
}
