#pragma once

#include "Core/Asset/AssetTypes.h"
#include "GameFramework/Component/Component.h"
#include "GameFramework/Object/GameObject.h"
#include "GameFramework/Object/ObjectPool.h"
#include "GameFramework/System/GameSystem.h"
#include "Utillity/File/FilePath.h"
#include "Utillity/Pointer/SafePtr.h"

#include <algorithm>
#include <type_traits>
#include <typeindex>
#include <utility>
#include <vector>

class CPhysics2DSystem;
class CScriptSystem;
class CTransformSystem;

// CScene 은 GameScript 가 소유하는 런타임 객체다. CScene/GameObject/Component 포인터를
// 라이브 컴파일 DLL 경계로 그대로 넘기지 말 것(안전참조는 SafePtr, 직렬화는 리플렉션).
class CScene final : public EnableSafeFromThis<CScene>
{
public:
	CScene();
	~CScene();

	CScene(const CScene&) = delete;
	CScene& operator=(const CScene&) = delete;

	// ── 오브젝트 ──────────────────────────────────────────────────────────────
	CGameObject* CreateGameObject(const char* name = nullptr);
	bool         DestroyGameObject(CGameObject* gameObject);
	std::size_t  GetObjectCount() const { return m_objectPool.GetLiveCount(); }

	// InstanceGuid → 활성 오브젝트(Ref<T> 직렬화 키 해석).
	SafePtr<CGameObject> FindByInstanceGuid(const File::Guid& guid);
	void                 SetObjectInstanceGuid(CGameObject& object, const File::Guid& guid);

	template<typename Fn> void ForEachObject(Fn&& fn)       { m_objectPool.ForEachLive(std::forward<Fn>(fn)); }
	template<typename Fn> void ForEachObject(Fn&& fn) const { m_objectPool.ForEachLive(std::forward<Fn>(fn)); }

	// ── 컴포넌트 ──────────────────────────────────────────────────────────────
	// 멀티 인스턴스: 같은 타입을 여러 개 부착할 수 있다. 매 호출마다 새 컴포넌트를 만든다.
	// 각 컴포넌트는 고유 InstanceGuid 를 받아 Ref<T> 가 특정 1개를 지목할 수 있다.
	template<typename T, typename... Args>
	T* AddComponent(CGameObject& object, Args&&... args)
	{
		static_assert(std::is_base_of_v<CComponent, T>, "T must derive from CComponent.");
		T* component = GetOrCreatePool<T>().Allocate(std::forward<Args>(args)...);
		if (nullptr == component)
		{
			return nullptr;
		}
		component->Owner = object.SafeFromThis();
		if (component->InstanceGuid.IsNull())
		{
			component->InstanceGuid = File::GenerateGuid();
		}
		object.AttachComponent(component->SafeFromThis());
		component->OnCreate();
		return component;
	}

	template<typename T>
	void RemoveComponent(CGameObject& object)
	{
		static_assert(std::is_base_of_v<CComponent, T>, "T must derive from CComponent.");
		if (T* component = object.GetComponent<T>())
		{
			// 지연 파괴 — 순회 중 즉시 해제 금지(슬롯 무효화 회피). flush 에서 처리.
			m_pendingDestroyComponents.push_back(component->SafeFromThis());
		}
	}

	template<typename T>
	T* GetComponent(CGameObject& object) { return object.GetComponent<T>(); }
	template<typename T>
	const T* GetComponent(const CGameObject& object) const { return object.GetComponent<T>(); }

	// 타입별 풀 순회(시스템용). fn(T&).
	template<typename T, typename Fn>
	void ForEach(Fn&& fn)
	{
		if (TObjectPool<T>* pool = FindPool<T>())
		{
			pool->ForEachLive(std::forward<Fn>(fn));
		}
	}

	template<typename T, typename Fn>
	void ForEach(Fn&& fn) const
	{
		if (const TObjectPool<T>* pool = FindPool<T>())
		{
			pool->ForEachLive(std::forward<Fn>(fn));
		}
	}

	// 타입소거 컴포넌트 해제(GameObject 파괴 경로에서 사용). 동적 타입으로 풀 식별.
	void DestroyComponent(CComponent* component);

	// ── 시스템 / 업데이트 ─────────────────────────────────────────────────────
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
			if (TSystem* typed = dynamic_cast<TSystem*>(system.Get()))
			{
				return typed;
			}
		}
		return nullptr;
	}

	void FixedUpdate();
	void Update(bool isSimulationPlaying);
	void Update();
	void UpdateSystems(bool isSimulationPlaying);
	void UpdateScripts();
	// 지연 파괴 큐 처리 — 모든 시스템/스크립트 순회가 끝난 뒤 호출(슬롯 무효화 회피).
	void FlushPendingDestroys();
	void NotifySimulationStop();
	void DestroyScriptInstances();
	CPhysics2DSystem* GetPhysics2DSystem();
	const CPhysics2DSystem* GetPhysics2DSystem() const;
	void SetReferencedAssets(std::vector<AssetGuid> referencedAssets);
	const std::vector<AssetGuid>& GetReferencedAssets() const;
	void ClearObjects();
	void Clear();

private:
	// ── 타입별 컴포넌트 풀 (타입소거) ─────────────────────────────────────────
	class IComponentPool
	{
	public:
		virtual ~IComponentPool() = default;
		virtual void FreeBase(CComponent* component) = 0;
		virtual void Clear() = 0;
	};

	template<typename T>
	class TComponentPool final : public IComponentPool
	{
	public:
		TObjectPool<T> Pool;
		void FreeBase(CComponent* component) override { Pool.Free(static_cast<T*>(component)); }
		void Clear() override { Pool.Clear(); }
	};

	struct PoolEntry
	{
		std::type_index          Key;
		OwnerPtr<IComponentPool> Pool;
	};

	template<typename T>
	TObjectPool<T>& GetOrCreatePool()
	{
		const std::type_index key(typeid(T));
		auto it = std::lower_bound(m_componentPools.begin(), m_componentPools.end(), key,
			[](const PoolEntry& e, const std::type_index& k) { return e.Key < k; });
		if (it != m_componentPools.end() && it->Key == key)
		{
			return static_cast<TComponentPool<T>*>(it->Pool.Get())->Pool;
		}
		OwnerPtr<TComponentPool<T>> wrap = MakeOwnerPtr<TComponentPool<T>>();
		TObjectPool<T>& poolRef = wrap->Pool;
		m_componentPools.insert(it, PoolEntry{ key, OwnerPtr<IComponentPool>(std::move(wrap)) });
		return poolRef;
	}

	template<typename T>
	TObjectPool<T>* FindPool()
	{
		const std::type_index key(typeid(T));
		auto it = std::lower_bound(m_componentPools.begin(), m_componentPools.end(), key,
			[](const PoolEntry& e, const std::type_index& k) { return e.Key < k; });
		if (it == m_componentPools.end() || it->Key != key)
		{
			return nullptr;
		}
		return &static_cast<TComponentPool<T>*>(it->Pool.Get())->Pool;
	}

	template<typename T>
	const TObjectPool<T>* FindPool() const
	{
		const std::type_index key(typeid(T));
		auto it = std::lower_bound(m_componentPools.begin(), m_componentPools.end(), key,
			[](const PoolEntry& e, const std::type_index& k) { return e.Key < k; });
		if (it == m_componentPools.end() || it->Key != key)
		{
			return nullptr;
		}
		return &static_cast<const TComponentPool<T>*>(it->Pool.Get())->Pool;
	}

	void DestroyObjectRecursive(CGameObject* object);

private:
	TObjectPool<CGameObject>           m_objectPool;
	std::uint64_t                      m_nextCreationOrder = 0; // 단조 증가 — 하이라키 정렬 키
	std::vector<PoolEntry>             m_componentPools;   // sorted by Key
	OwnerPtr<CTransformSystem>         m_transformSystem;
	OwnerPtr<CPhysics2DSystem>         m_physicsSystem;
	OwnerPtr<CScriptSystem>            m_scriptSystem;
	std::vector<OwnerPtr<CGameSystem>> m_systems;
	std::vector<AssetGuid>             m_referencedAssets;

	// 지연 파괴 큐(SafePtr 보유 → 부모 재귀로 이미 죽은 항목은 TryGet null 로 스킵).
	std::vector<SafePtr<CGameObject>>  m_pendingDestroyObjects;
	std::vector<SafePtr<CComponent>>   m_pendingDestroyComponents;
};

// ── CGameObject 템플릿 메서드 정의 (CScene 완전형 필요) ──────────────────────
template<typename T, typename... Args>
T* CGameObject::AddComponent(Args&&... args)
{
	return m_scene ? m_scene->AddComponent<T>(*this, std::forward<Args>(args)...) : nullptr;
}

template<typename T>
void CGameObject::RemoveComponent()
{
	if (m_scene)
	{
		m_scene->RemoveComponent<T>(*this);
	}
}
