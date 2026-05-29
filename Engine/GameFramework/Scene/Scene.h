#pragma once

#include "Core/Asset/AssetTypes.h"
#include "GameFramework/ECS/ComponentPool.h"
#include "GameFramework/ECS/EntityManager.h"
#include "GameFramework/System/GameSystem.h"
#include "Utillity/SafePtr.h"

#include <algorithm>
#include <typeindex>
#include <type_traits>
#include <tuple>
#include <utility>
#include <vector>

class CGameObject;
class CPhysics2DSystem;
class CScriptSystem;
class CTransformSystem;
struct SceneSnapshot;

// CScene is a GameScript-owned runtime object. Do not pass CScene, component
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

	// Add or return existing (singleton-per-entity semantics).
	template<typename T, typename... Args>
	T* AddComponent(EntityId entity, Args&&... args)
	{
		if (false == IsAlive(entity))
		{
			return nullptr;
		}

		return GetOrCreateComponentPool<T>().Add(entity, std::forward<Args>(args)...);
	}

	// Always creates a new instance (multi-component semantics).
	template<typename T, typename... Args>
	T* AddNewComponent(EntityId entity, Args&&... args)
	{
		if (false == IsAlive(entity))
		{
			return nullptr;
		}

		return GetOrCreateComponentPool<T>().AddNew(entity, std::forward<Args>(args)...);
	}

	// Removes ALL instances of T for this entity.
	template<typename T>
	void RemoveComponent(EntityId entity)
	{
		if (CComponentPool<T>* pool = FindComponentPool<T>())
		{
			pool->RemoveEntity(entity);
		}
	}

	// Removes a specific instance by pointer.
	template<typename T>
	bool RemoveSpecificComponent(EntityId entity, T* component)
	{
		CComponentPool<T>* pool = FindComponentPool<T>();
		return pool ? pool->RemoveSpecific(entity, component) : false;
	}

	// Returns the first (oldest) instance, or nullptr.
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

	// Returns all instances of T for this entity.
	template<typename T>
	std::vector<T*> GetAllComponents(EntityId entity)
	{
		std::vector<T*> result;
		if (CComponentPool<T>* pool = FindComponentPool<T>())
		{
			pool->GetAll(entity, result);
		}
		return result;
	}

	template<typename T>
	std::vector<const T*> GetAllComponents(EntityId entity) const
	{
		std::vector<const T*> result;
		if (const CComponentPool<T>* pool = FindComponentPool<T>())
		{
			pool->GetAll(entity, result);
		}
		return result;
	}

	template<typename T>
	bool HasComponent(EntityId entity) const
	{
		return nullptr != GetComponent<T>(entity);
	}

	// ForEach iterates every live (entity, primaryComp, ...) tuple.
	// The *primary* component is taken directly from the pool (covers multi-instance).
	// Secondary components are looked up via GetComponent (returns first instance).
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

		ForEachImpl<TComponents...>(*primaryPool, std::forward<TFunction>(function));
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

		ForEachImpl<TComponents...>(*primaryPool, std::forward<TFunction>(function));
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

	template<typename TSystem>
	TSystem* FindSystem()
	{
		static_assert(std::is_base_of_v<CGameSystem, TSystem>, "TSystem must derive from CGameSystem.");

		for (OwnerPtr<CGameSystem>& system : m_systems)
		{
			if (TSystem* typedSystem = dynamic_cast<TSystem*>(system.Get()))
			{
				return typedSystem;
			}
		}
		return nullptr;
	}

	template<typename TSystem>
	const TSystem* FindSystem() const
	{
		static_assert(std::is_base_of_v<CGameSystem, TSystem>, "TSystem must derive from CGameSystem.");

		for (const OwnerPtr<CGameSystem>& system : m_systems)
		{
			if (const TSystem* typedSystem = dynamic_cast<const TSystem*>(system.Get()))
			{
				return typedSystem;
			}
		}
		return nullptr;
	}

	void FixedUpdate();
	void Update(bool isSimulationPlaying);
	void Update();
	void UpdateSystems(bool isSimulationPlaying);
	void UpdateScripts();
	void DestroyScriptInstances();
	void FlushPendingRemovesAllPools();
	CPhysics2DSystem* GetPhysics2DSystem();
	const CPhysics2DSystem* GetPhysics2DSystem() const;
	void SetReferencedAssets(std::vector<AssetGuid> referencedAssets);
	const std::vector<AssetGuid>& GetReferencedAssets() const;
	void ClearObjects();
	void Clear();

private:
	// ── ForEach implementation ─────────────────────────────────────────────
	// Splits TComponents into TFirst (iterated directly from pool) and TRest
	// (looked up by GetComponent for each entity).

	template<typename TFirst, typename... TRest, typename TFunction>
	void ForEachImpl(CComponentPool<TFirst>& pool, TFunction&& function)
	{
		if constexpr (sizeof...(TRest) == 0)
		{
			// Single-component path: no secondary lookup needed.
			pool.ForEachActive([&](EntityId entity, TFirst& first)
			{
				function(entity, first);
			});
		}
		else
		{
			// Cache secondary pool pointers once outside the loop.
			// This avoids 2 * sizeof...(TRest) hash-map lookups per entity.
			auto secondaryPools = std::make_tuple(FindComponentPool<TRest>()...);

			// Early-out: if any required component type has no pool, nothing matches.
			if (!((std::get<CComponentPool<TRest>*>(secondaryPools) != nullptr) && ...))
			{
				return;
			}

			pool.ForEachActive([&](EntityId entity, TFirst& first)
			{
				// HasEntity is O(1) sparse-array check — no hash-map lookup.
				if ((std::get<CComponentPool<TRest>*>(secondaryPools)->HasEntity(entity) && ...))
				{
					function(entity, first, *std::get<CComponentPool<TRest>*>(secondaryPools)->Get(entity)...);
				}
			});
		}
	}

	template<typename TFirst, typename... TRest, typename TFunction>
	void ForEachImpl(const CComponentPool<TFirst>& pool, TFunction&& function) const
	{
		if constexpr (sizeof...(TRest) == 0)
		{
			pool.ForEachActive([&](EntityId entity, const TFirst& first)
			{
				function(entity, first);
			});
		}
		else
		{
			auto secondaryPools = std::make_tuple(FindComponentPool<TRest>()...);

			if (!((std::get<const CComponentPool<TRest>*>(secondaryPools) != nullptr) && ...))
			{
				return;
			}

			pool.ForEachActive([&](EntityId entity, const TFirst& first)
			{
				if ((std::get<const CComponentPool<TRest>*>(secondaryPools)->HasEntity(entity) && ...))
				{
					function(entity, first, *std::get<const CComponentPool<TRest>*>(secondaryPools)->Get(entity)...);
				}
			});
		}
	}

	// ── Pool management ────────────────────────────────────────────────────
	//
	// m_componentPools is a sorted flat vector of (type_index, pool) pairs.
	// Binary search gives O(log N) lookup with contiguous memory — no hashing.
	// Typical component-type count per scene is < 30, so cache-friendliness
	// of a flat vector beats the hash-table overhead.

	template<typename T>
	CComponentPool<T>* FindComponentPool()
	{
		const std::type_index key(typeid(T));
		auto it = std::lower_bound(m_componentPools.begin(), m_componentPools.end(), key,
			[](const PoolEntry& e, const std::type_index& k) { return e.Key < k; });
		if (it == m_componentPools.end() || it->Key != key)
		{
			return nullptr;
		}
		return static_cast<CComponentPool<T>*>(it->Pool.Get());
	}

	template<typename T>
	const CComponentPool<T>* FindComponentPool() const
	{
		const std::type_index key(typeid(T));
		auto it = std::lower_bound(m_componentPools.begin(), m_componentPools.end(), key,
			[](const PoolEntry& e, const std::type_index& k) { return e.Key < k; });
		if (it == m_componentPools.end() || it->Key != key)
		{
			return nullptr;
		}
		return static_cast<const CComponentPool<T>*>(it->Pool.Get());
	}

	template<typename T>
	CComponentPool<T>& GetOrCreateComponentPool()
	{
		const std::type_index key(typeid(T));
		auto it = std::lower_bound(m_componentPools.begin(), m_componentPools.end(), key,
			[](const PoolEntry& e, const std::type_index& k) { return e.Key < k; });
		if (it != m_componentPools.end() && it->Key == key)
		{
			return *static_cast<CComponentPool<T>*>(it->Pool.Get());
		}

		OwnerPtr<CComponentPool<T>> pool = MakeOwnerPtr<CComponentPool<T>>();
		CComponentPool<T>* rawPool = pool.Get();
		m_componentPools.insert(it, PoolEntry{ key, std::move(pool) });
		return *rawPool;
	}

private:
	bool AttachToParent(EntityId child, EntityId parent);
	void DetachFromParent(EntityId child);
	void DestroyEntityHierarchy(EntityId entity);

private:
	// Sorted by Key — binary search for O(log N) pool lookup.
	struct PoolEntry
	{
		std::type_index          Key;
		OwnerPtr<IComponentPool> Pool;
	};

	CEntityManager             m_entityManager;
	std::vector<PoolEntry>     m_componentPools;  // sorted flat vector (Fix #2)
	std::vector<OwnerPtr<CGameSystem>> m_systems;
	OwnerPtr<CTransformSystem> m_transformSystem; // runs first — caches WorldTransform2D
	OwnerPtr<CPhysics2DSystem> m_physicsSystem;
	OwnerPtr<CScriptSystem>    m_scriptSystem;
	std::vector<AssetGuid>     m_referencedAssets;
};
