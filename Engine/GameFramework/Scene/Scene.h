#pragma once

#include "GameFramework/ECS/ComponentPool.h"
#include "GameFramework/ECS/EntityManager.h"
#include "GameFramework/System/GameSystem.h"
#include "Utillity/SafePtr.h"

#include <typeindex>
#include <type_traits>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

class CGameObject;
class CPhysics2DSystem;
class CScriptSystem;
struct SceneSnapshot;

// CScene is a GameCode-owned runtime object. Do not pass CScene, component
// pointers, or system pointers across live-compile DLL boundaries; expose POD
// snapshots when editor inspection is needed.
class CScene final : public EnableSafeFromThis<CScene>
{
public:
	CScene();
	~CScene();

	CScene(const CScene&) = delete;
	CScene& operator=(const CScene&) = delete;

public:
	EntityId CreateEntity();
	bool DestroyEntity(EntityId entity);
	bool IsAlive(EntityId entity) const;
	std::size_t GetAliveEntityCount() const;

	CGameObject CreateGameObject(const char* name = nullptr);
	bool DestroyGameObject(const CGameObject& gameObject);
	void BuildSnapshot(SceneSnapshot& snapshot) const;

	bool SetParent(EntityId child, EntityId parent);
	bool ClearParent(EntityId child);
	EntityId GetParent(EntityId entity) const;
	EntityId GetFirstChild(EntityId entity) const;
	EntityId GetNextSibling(EntityId entity) const;
	EntityId GetPrevSibling(EntityId entity) const;
	bool IsDescendantOf(EntityId entity, EntityId possibleAncestor) const;

	template<typename T, typename... Args>
	T* AddComponent(EntityId entity, Args&&... args)
	{
		if (false == IsAlive(entity))
		{
			return nullptr;
		}

		return GetOrCreateComponentPool<T>().Add(entity, std::forward<Args>(args)...);
	}

	template<typename T>
	void RemoveComponent(EntityId entity)
	{
		if (CComponentPool<T>* pool = FindComponentPool<T>())
		{
			pool->RemoveEntity(entity);
		}
	}

	template<typename T>
	T* GetComponent(EntityId entity)
	{
		CComponentPool<T>* pool = FindComponentPool<T>();
		return pool ? pool->Get(entity) : nullptr;
	}

	template<typename T>
	const T* GetComponent(EntityId entity) const
	{
		const CComponentPool<T>* pool = FindComponentPool<T>();
		return pool ? pool->Get(entity) : nullptr;
	}

	template<typename T>
	bool HasComponent(EntityId entity) const
	{
		return nullptr != GetComponent<T>(entity);
	}

	template<typename... TComponents, typename TFunction>
	void ForEach(TFunction&& function)
	{
		static_assert(sizeof...(TComponents) > 0, "ForEach requires at least one component type.");
		using TPrimary = std::tuple_element_t<0, std::tuple<TComponents...>>;

		CComponentPool<TPrimary>* primaryPool = FindComponentPool<TPrimary>();
		if (nullptr == primaryPool)
		{
			return;
		}

		const std::vector<EntityId>& entities = primaryPool->GetEntities();
		for (EntityId entity : entities)
		{
			if ((HasComponent<TComponents>(entity) && ...))
			{
				function(entity, *GetComponent<TComponents>(entity)...);
			}
		}
	}

	template<typename... TComponents, typename TFunction>
	void ForEach(TFunction&& function) const
	{
		static_assert(sizeof...(TComponents) > 0, "ForEach requires at least one component type.");
		using TPrimary = std::tuple_element_t<0, std::tuple<TComponents...>>;

		const CComponentPool<TPrimary>* primaryPool = FindComponentPool<TPrimary>();
		if (nullptr == primaryPool)
		{
			return;
		}

		const std::vector<EntityId>& entities = primaryPool->GetEntities();
		for (EntityId entity : entities)
		{
			if ((HasComponent<TComponents>(entity) && ...))
			{
				function(entity, *GetComponent<TComponents>(entity)...);
			}
		}
	}

	template<typename TSystem, typename... Args>
	TSystem* AddSystem(Args&&... args)
	{
		static_assert(std::is_base_of_v<CGameSystem, TSystem>, "TSystem must derive from CGameSystem.");

		OwnerPtr<TSystem> system = MakeOwnerPtr<TSystem>(std::forward<Args>(args)...);
		TSystem* rawSystem = system.Get();
		rawSystem->Initialize(*this);
		m_systems.push_back(std::move(system));
		return rawSystem;
	}

	void Update();
	void UpdateSystems();
	void UpdateScripts();
	void ClearObjects();
	void Clear();

private:
	template<typename T>
	CComponentPool<T>* FindComponentPool()
	{
		auto it = m_componentPools.find(std::type_index(typeid(T)));
		if (it == m_componentPools.end())
		{
			return nullptr;
		}

		return static_cast<CComponentPool<T>*>(it->second.Get());
	}

	template<typename T>
	const CComponentPool<T>* FindComponentPool() const
	{
		auto it = m_componentPools.find(std::type_index(typeid(T)));
		if (it == m_componentPools.end())
		{
			return nullptr;
		}

		return static_cast<const CComponentPool<T>*>(it->second.Get());
	}

	template<typename T>
	CComponentPool<T>& GetOrCreateComponentPool()
	{
		const std::type_index typeIndex(typeid(T));
		auto it = m_componentPools.find(typeIndex);
		if (it == m_componentPools.end())
		{
			OwnerPtr<CComponentPool<T>> pool = MakeOwnerPtr<CComponentPool<T>>();
			CComponentPool<T>* rawPool = pool.Get();
			m_componentPools.emplace(typeIndex, std::move(pool));
			return *rawPool;
		}

		return *static_cast<CComponentPool<T>*>(it->second.Get());
	}

private:
	bool AttachToParent(EntityId child, EntityId parent);
	void DetachFromParent(EntityId child);
	void DestroyEntityHierarchy(EntityId entity);

private:
	CEntityManager m_entityManager;
	std::unordered_map<std::type_index, OwnerPtr<IComponentPool>> m_componentPools;
	std::vector<OwnerPtr<CGameSystem>> m_systems;
	OwnerPtr<CPhysics2DSystem> m_physicsSystem;
	OwnerPtr<CScriptSystem> m_scriptSystem;
};
