#pragma once

#include "Core/Asset/AssetTypes.h"
#include "Core/Asset/AssetRef.h"
#include "Core/Platform/PlatformTypes.h"   // SurfaceEvent
#include "GameFramework/Component/Component.h"
#include "GameFramework/Object/GameObject.h"
#include "GameFramework/Object/ObjectPool.h"
#include "GameFramework/Reflection/ReflectionTypes.h"
#include "GameFramework/Scene/CanvasViewport.h"
#include "GameFramework/System/GameSystem.h"
#include "Utillity/File/FilePath.h"
#include "Utillity/File/Guid128.h"
#include "Utillity/Pointer/SafePtr.h"

#include <algorithm>
#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <type_traits>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <vector>

class CGameLayer;
class CPhysics2DSystem;
class CReflectionRegistry;
class CSceneManager;
class CSceneRuntimeAccess;
class CSceneSerializer;
class CScriptSystem;
class CTransformSystem;
class CGameScript;
class IInputHandler;
struct GameModuleHostApi;

// CGameScene 은 GameScript 가 소유하는 런타임 객체다. CGameScene/GameObject/Component 포인터를
// 라이브 컴파일 DLL 경계로 그대로 넘기지 말 것(안전참조는 SafePtr, 직렬화는 리플렉션).
class CGameScene final : public EnableSafeFromThis<CGameScene>
{
public:
	CGameScene();
	~CGameScene();

	CGameScene(const CGameScene&) = delete;
	CGameScene& operator=(const CGameScene&) = delete;

	// ── 이름 ─────────────────────────────────────────────────────────────────
	// SceneManager 의 m_scenes 유일 키와 동일(CreateScene 이 설정). Ref<GameScene> 해석 키.
	const char* GetName() const { return m_name.c_str(); }

	// ── 레이어 ────────────────────────────────────────────────────────────────
	// 순서 = m_layers 벡터 순서 = 컴포짓(아래→위)·스크립트 실행 순서.
	// "레이어 0개 + 오브젝트 존재" 불허 — 오브젝트 생성이 기본 레이어를 보장한다.
	CGameLayer* CreateLayer(const char* name = nullptr);
	// 지연 파괴 — 소속 오브젝트를 파괴 예약하고 레이어는 flush 에서 제거.
	// 마지막 남은 레이어는 거부(false).
	bool                DestroyLayer(CGameLayer* layer);
	SafePtr<CGameLayer> FindLayerByName(const char* name) const;
	SafePtr<CGameLayer> FindLayerByInstanceGuid(const File::Guid& guid) const;
	std::size_t         GetLayerCount() const { return m_layers.size(); }
	CGameLayer*         GetLayerAt(std::size_t index) const;
	// 없으면 -1. cold path 용(직렬화/에디터) — 프레임 루프에서 반복 호출 금지.
	int                 GetLayerIndex(const CGameLayer* layer) const;
	bool                MoveLayer(CGameLayer* layer, std::size_t newIndex);
	// 첫 레이어 반환, 하나도 없으면 생성해서 보장.
	CGameLayer*         GetDefaultLayer();
	// 루트 오브젝트(부모 없음)만 허용 — 서브트리 전체를 대상 레이어로 전파.
	bool MoveObjectToLayer(CGameObject& object, CGameLayer& layer);

	// ── 뷰포트 / 캔버스 속성 ──────────────────────────────────────────────────
	// 화면 분할은 캔버스급 결정 — 카메라는 눈일 뿐이고 "어디에 그릴지"는 여기서 정한다.
	CanvasViewport*       CreateViewport(const char* name = nullptr);
	bool                  DestroyViewport(std::size_t index);
	std::size_t           GetViewportCount() const { return m_viewports.size(); }
	CanvasViewport*       GetViewportAt(std::size_t index);
	const CanvasViewport* GetViewportAt(std::size_t index) const;
	CanvasViewport*       FindViewportByName(const char* name);
	// 뷰포트가 하나도 없으면 기본(풀스크린·카메라 미지정) 1개를 만들어 보장한다.
	CanvasViewport&       GetOrCreateDefaultViewport();

	// 컴포짓 맨 아래 바탕색. 레이어 RT 는 투명으로 클리어되고, 이 색 위에 순서대로 얹힌다.
	const float* GetBackgroundColor() const { return m_backgroundColor; }
	void         SetBackgroundColor(float r, float g, float b, float a);

	// ── 오브젝트 ──────────────────────────────────────────────────────────────
	// layer 미지정(nullptr) = 기본(첫) 레이어. 레이어가 없으면 만들어서 배정한다.
	CGameObject* CreateGameObject(const char* name = nullptr, CGameLayer* layer = nullptr);
	bool         DestroyGameObject(CGameObject* gameObject);
	std::size_t  GetObjectCount() const { return m_objectPool.GetLiveCount(); }

	struct ScriptMemoryPoolStats
	{
		TypeId Type = INVALID_TYPE_ID;
		std::size_t Capacity = 0;
		std::size_t Used = 0;
		std::size_t ExpansionCount = 0;
	};

	// InstanceGuid → 활성 오브젝트(Ref<T> 직렬화 키 해석).
	SafePtr<CGameObject> FindByInstanceGuid(const File::Guid& guid);
	// hot path 전용 — guid 문자열(utf8)을 File::Guid(fs::path) 로 만들지 않고 곧장 Guid128 로
	// 파싱해 조회한다(Ref::Get 이 프레임마다 부르므로 힙 할당·트랜스코딩을 피한다).
	SafePtr<CGameObject> FindByInstanceGuidText(const char* guidText);

	template<typename Fn> void ForEachObject(Fn&& fn)       { m_objectPool.ForEachLive(std::forward<Fn>(fn)); }
	template<typename Fn> void ForEachObject(Fn&& fn) const { m_objectPool.ForEachLive(std::forward<Fn>(fn)); }
	// ── 컴포넌트 ──────────────────────────────────────────────────────────────
	// 멀티 인스턴스: 같은 타입을 여러 개 부착할 수 있다. 매 호출마다 새 컴포넌트를 만든다.
	// 각 컴포넌트는 고유 InstanceGuid 를 받아 Ref<T> 가 특정 1개를 지목할 수 있다.
	template<typename T, typename... Args>
	T* AddComponent(CGameObject& object, Args&&... args)
	{
		static_assert(std::is_base_of_v<CComponent, T>, "T must derive from CComponent.");
		static_assert(false == std::is_base_of_v<CGameScript, T>,
			"Scripts must be created through the reflected script path, not AddComponent<T>().");
		T* component = GetOrCreatePool<T>().Allocate(
			ComponentConstructionToken{ MakeStableTypeId(T::StaticTypeName()) },
			object.SafeFromThis(),
			std::forward<Args>(args)...);
		if (nullptr == component)
		{
			return nullptr;
		}
		if (component->GetInstanceGuid().IsNull())
		{
			component->SetInstanceGuid(File::GenerateGuid());
		}
		object.AttachComponent(component->SafeFromThis());
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


private:
	// 타입소거 컴포넌트 해제(GameObject 파괴 경로에서 사용). 동적 타입으로 풀 식별.
	void DestroyComponent(CComponent* component);
	void RemoveComponent(CComponent* component)
	{
		if (component)
		{
			m_pendingDestroyComponents.push_back(component->SafeFromThis());
		}
	}
	CGameScript* AddScript(CGameObject& object, TypeId scriptTypeId, const CReflectionRegistry& registry);

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

	template<typename TSystem>
	const TSystem* FindSystem() const
	{
		static_assert(std::is_base_of_v<CGameSystem, TSystem>, "TSystem must derive from CGameSystem.");
		for (const OwnerPtr<CGameSystem>& system : m_systems)
		{
			if (const TSystem* typed = dynamic_cast<const TSystem*>(system.Get()))
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
	void ClearScriptMemoryPools();
	void ReserveScriptMemoryForCurrentScripts(const CReflectionRegistry& registry, float capacityMultiplier = 1.5f);
	void ReserveScriptMemory(TypeId scriptTypeId, std::size_t size, std::size_t alignment, std::size_t capacity);
	std::vector<ScriptMemoryPoolStats> GetScriptMemoryPoolStats() const;
	std::uint64_t GetScriptAllocationGeneration() const { return m_scriptAllocationGeneration; }
	void* AllocateScriptMemory(TypeId scriptTypeId, std::size_t size, std::size_t alignment);
	void FreeScriptMemory(TypeId scriptTypeId, void* ptr, std::size_t size, std::size_t alignment);

	// 호스트가 받은 윈도우 이벤트를 이 씬의 살아있는 스크립트 인스턴스들에 전달한다.
	// 인스턴스는 시뮬레이션(재생) 중에만 존재하므로 편집 모드에선 자연히 no-op.
	void DispatchSurfaceEventToScripts(const SurfaceEvent& surfaceEvent);

public:
	CPhysics2DSystem* GetPhysics2DSystem();
	const CPhysics2DSystem* GetPhysics2DSystem() const;

private:
	void SetReferencedAssets(std::vector<AssetGuid> referencedAssets);
	const std::vector<AssetGuid>& GetReferencedAssets() const;

	// ── 로드된 리소스 보유(use-count) ─────────────────────────────────────────
	// 씬은 노드(구조)만으로도 존재할 수 있다(부트 시 전 씬 노드 선로드). 리소스(에셋)는
	// 씬이 active 가 될 때만 로드하며, 그 동안 AssetRef(strong) 로 잡아 use-count>0 을 유지해
	// 자산이 unload/GC 되지 않게 보호한다. 로드 자체는 SceneManager(AssetManager 접근 가능)가
	// 수행하고, 결과 AssetRef 묶음을 여기에 보관한다. 비active 가 되면 ClearLoadedAssets 로
	// 해제 → use-count-- → (다른 사용자가 없으면) 추후 GC 대상.
	void SetLoadedAssets(std::vector<AssetRef<IAsset>> loadedAssets) { m_loadedAssets = std::move(loadedAssets); }
	void ClearLoadedAssets() { m_loadedAssets.clear(); }
	bool HasLoadedAssets() const { return false == m_loadedAssets.empty(); }

	void ClearObjects();
	void Clear();

private:
	friend class CGameObject;
	friend class CPhysics2DSystem;
	friend class CReflectionRegistry;
	friend class CSceneManager;
	friend class CSceneRuntimeAccess;
	friend class CSceneSerializer;
	friend class CScriptSystem;

	void SetName(const char* name) { m_name = name ? name : ""; }
	void SetObjectInstanceGuid(CGameObject& object, const File::Guid& guid);
	// m_layers 순서가 바뀌는 모든 지점에서 호출 — 레이어의 인덱스 캐시(CGameLayer::m_index)를
	// 목록과 동기화한다. 렌더 수집이 이 캐시를 매 프레임 읽으므로 desync 는 곧 오정렬이다.
	void ReindexLayers();
	// 직렬화 전용 — 로드 시 파일의 레이어 guid 를 복원한다(GameInstance friend 경유).
	void SetLayerInstanceGuid(CGameLayer& layer, const File::Guid& guid);

	template<typename T>
	void ReserveComponentPool(std::size_t capacity)
	{
		static_assert(std::is_base_of_v<CComponent, T>, "T must derive from CComponent.");
		GetOrCreatePool<T>().Reserve(capacity);
	}

	struct ScriptRuntimeState
	{
		CGameScript* Instance = nullptr;
		TypeId Type = INVALID_TYPE_ID;
		void(*DestroyInstance)(CGameScript*, const GameModuleHostApi*) = nullptr;
		const GameModuleHostApi* HostApi = nullptr;
		SafePtrDetail::ControlBlock* ControlBlock = nullptr;
		IInputHandler* InputHandler = nullptr;
		bool InputRegistered = false;
		// m_scriptRuntimeStates(std::deque) 내 자기 위치. deque 는 포인터 산술이 안 되므로
		// 파괴 시 free-list 로 반환할 인덱스를 여기 담아 둔다.
		std::size_t SelfIndex = 0;
	};

	// 실행순서 캐시(m_scriptExecutionOrder / m_scriptRangesByObject) 순회 중임을 표시하는
	// RAII 가드. 순회 도중 스크립트 스폰·Ref 해석이 EnsureScriptExecutionOrder 를 재진입하면
	// RebuildScriptExecutionOrder 가 그 벡터를 clear/refill 해 바깥 순회를 무효화(UB)한다.
	// 가드가 살아있는 동안 Ensure 는 재빌드를 미룬다(dirty 는 남아 다음 프레임에 반영).
	class ScriptIterationGuard
	{
	public:
		explicit ScriptIterationGuard(CGameScene& scene) : m_scene(scene) { ++m_scene.m_scriptIterationDepth; }
		~ScriptIterationGuard() { --m_scene.m_scriptIterationDepth; }
		ScriptIterationGuard(const ScriptIterationGuard&) = delete;
		ScriptIterationGuard& operator=(const ScriptIterationGuard&) = delete;
	private:
		CGameScene& m_scene;
	};

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

	class ScriptMemoryPool final
	{
	public:
		ScriptMemoryPool(TypeId typeId, std::size_t slotSize, std::size_t slotAlignment);
		~ScriptMemoryPool();

		ScriptMemoryPool(const ScriptMemoryPool&) = delete;
		ScriptMemoryPool& operator=(const ScriptMemoryPool&) = delete;

		TypeId GetTypeId() const { return m_typeId; }
		bool Matches(std::size_t slotSize, std::size_t slotAlignment) const;
		void Reserve(std::size_t capacity);
		void* Allocate();
		void Free(void* ptr);
		void Clear();
		std::size_t GetCapacity() const { return m_slots.size(); }
		std::size_t GetExpansionCount() const { return m_expansionCount; }
		std::size_t GetUsedCount() const { return m_slots.size() - m_freeList.size(); }

	private:
		TypeId m_typeId = INVALID_TYPE_ID;
		std::size_t m_slotSize = 0;
		std::size_t m_slotAlignment = alignof(void*);
		std::vector<void*> m_slots;
		std::vector<void*> m_freeList;
		std::size_t m_expansionCount = 0;
	};

	struct ScriptObjectRange
	{
		std::size_t Begin = 0;
		std::size_t Count = 0;
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
	void MarkScriptExecutionOrderDirty();
	void EnsureScriptExecutionOrder();
	void RebuildScriptExecutionOrder();
	void AppendObjectScriptsInHierarchyOrder(CGameObject& object);

	template<typename Fn>
	void ForEachScriptOnObject(CGameObject& object, Fn&& fn)
	{
		EnsureScriptExecutionOrder();
		// 콜백(유저 스크립트)이 스폰·Ref 해석으로 캐시를 재빌드하지 못하게 순회 잠금.
		ScriptIterationGuard iterationGuard(*this);
		const auto rangeIt = m_scriptRangesByObject.find(&object);
		if (rangeIt == m_scriptRangesByObject.end())
		{
			return;
		}

		const ScriptObjectRange range = rangeIt->second;
		const std::size_t end = range.Begin + range.Count;
		for (std::size_t index = range.Begin; index < end; ++index)
		{
			ScriptRuntimeState* runtime = m_scriptExecutionOrder[index];
			if (runtime && runtime->Instance)
			{
				fn(*runtime->Instance, *runtime);
			}
		}
	}

private:
	std::string                        m_name; // SceneManager 키와 동일(CreateScene 설정)
	// 레이어 목록 — 벡터 순서가 곧 컴포짓/실행 순서. 소유는 씬, 공유는 SafePtr.
	std::vector<OwnerPtr<CGameLayer>>  m_layers;
	// 뷰포트 목록 — 순서대로 그린다(뒤 항목이 화면 위). 값 타입: 개수가 적고 참조를
	// 오래 들 일이 없다(에디터·스크립트는 인덱스/이름으로 접근).
	std::vector<CanvasViewport>        m_viewports;
	// 컴포짓 바탕색(구 Camera2D::ClearColor 의 캔버스급 승계).
	float                              m_backgroundColor[4] = { 0.08f, 0.09f, 0.11f, 1.0f };
	// 지연 파괴 레이어(소속 오브젝트 파괴 flush 후 목록에서 제거).
	std::vector<SafePtr<CGameLayer>>   m_pendingDestroyLayers;
	TObjectPool<CGameObject>           m_objectPool;
	// InstanceGuid → 오브젝트 빠른 해석. FindByInstanceGuid / Ref<T>::Get 이 매 호출
	// O(n) 풀 스캔 + path 비교를 하던 것을 O(1) 해시 조회로 바꾼다. 생성/파괴/guid 재설정
	// 지점에서 동기화한다(그 외 경로로 오브젝트가 생기지 않음 — 유일 생성자 CreateGameObject).
	// 키는 Guid128(오브젝트 InstanceGuid 의 정수 표현) — File::Guid(fs::path) 해싱/힙을 피한다.
	std::unordered_map<Guid128, SafePtr<CGameObject>> m_objectByGuid;
	std::uint64_t                      m_nextCreationOrder = 0; // 단조 증가 — 하이라키 정렬 키
	std::vector<PoolEntry>             m_componentPools;   // sorted by Key
	std::vector<OwnerPtr<ScriptMemoryPool>> m_scriptMemoryPools;
	// deque 인 이유: 순회 중 스폰(emplace_back)해도 기존 원소 주소가 불변이라
	// m_scriptExecutionOrder(포인터 벡터)·m_scriptRuntimeByComponent(포인터 값)가
	// dangling 되지 않는다(vector 는 realloc 시 전부 무효 → 재인덱싱 필요했음).
	std::deque<ScriptRuntimeState> m_scriptRuntimeStates;
	std::vector<std::size_t> m_freeScriptRuntimeStateIndices;
	std::unordered_map<const CComponent*, ScriptRuntimeState*> m_scriptRuntimeByComponent;
	std::vector<ScriptRuntimeState*> m_scriptExecutionOrder;
	std::unordered_map<const CGameObject*, ScriptObjectRange> m_scriptRangesByObject;
	bool m_scriptExecutionOrderDirty = true;
	// 실행순서 캐시 순회 깊이(>0 이면 순회 중). ScriptIterationGuard 가 증감하고
	// EnsureScriptExecutionOrder 가 이 값이 0 일 때만 재빌드한다.
	int m_scriptIterationDepth = 0;
	std::uint64_t                      m_scriptAllocationGeneration = 1;
	OwnerPtr<CTransformSystem>         m_transformSystem;
	OwnerPtr<CPhysics2DSystem>         m_physicsSystem;
	OwnerPtr<CScriptSystem>            m_scriptSystem;
	std::vector<OwnerPtr<CGameSystem>> m_systems;
	std::vector<AssetGuid>             m_referencedAssets;
	// active 인 동안 referenced 에셋을 strong 으로 잡는다(use-count>0 유지). 비active 시 clear.
	std::vector<AssetRef<IAsset>>      m_loadedAssets;

	// 지연 파괴 큐(SafePtr 보유 → 부모 재귀로 이미 죽은 항목은 TryGet null 로 스킵).
	std::vector<SafePtr<CGameObject>>  m_pendingDestroyObjects;
	std::vector<SafePtr<CComponent>>   m_pendingDestroyComponents;
};

// 사용자 대면 별칭 — 스크립트/문서는 접두사 없는 Scene 으로 쓴다.
using Scene = CGameScene;

// ── CGameObject 템플릿 메서드 정의 (CGameScene 완전형 필요) ──────────────────────
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
