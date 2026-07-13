#include "pch.h"
#include "Scene.h"

#include "Core/Asset/AssetRef.inl"   // m_loadedAssets(AssetRef) 소멸자 인스턴스화에 필요
#include "Core/Logging/LoggerInternal.h"
#include "Core/EngineCore.h"
#include "Core/Input/InputSystem.h"
#include "GameFramework/Scripting/GameScript.h"
#include "GameFramework/Physics2D/Physics2DSystem.h"
#include "GameFramework/Reflection/ReflectionRegistry.h"
#include "GameFramework/Scripting/ScriptSystem.h"
#include "GameFramework/Transform/TransformSystem.h"

#include <algorithm>
#include <cmath>
#include <new>
#include <unordered_map>
#include <vector>

CGameScene::ScriptMemoryPool::ScriptMemoryPool(TypeId typeId, std::size_t slotSize, std::size_t slotAlignment)
	: m_typeId(typeId)
	, m_slotSize(std::max<std::size_t>(slotSize, 1))
	, m_slotAlignment(std::max<std::size_t>(slotAlignment, alignof(void*)))
{
}

CGameScene::ScriptMemoryPool::~ScriptMemoryPool()
{
	Clear();
}

bool CGameScene::ScriptMemoryPool::Matches(std::size_t slotSize, std::size_t slotAlignment) const
{
	return m_slotSize == std::max<std::size_t>(slotSize, 1)
		&& m_slotAlignment == std::max<std::size_t>(slotAlignment, alignof(void*));
}

void CGameScene::ScriptMemoryPool::Reserve(std::size_t capacity)
{
	while (m_slots.size() < capacity)
	{
		void* ptr = ::operator new(m_slotSize, std::align_val_t(m_slotAlignment));
		m_slots.push_back(ptr);
		m_freeList.push_back(ptr);
	}
}

void* CGameScene::ScriptMemoryPool::Allocate()
{
	if (false == m_freeList.empty())
	{
		void* ptr = m_freeList.back();
		m_freeList.pop_back();
		return ptr;
	}

	void* ptr = ::operator new(m_slotSize, std::align_val_t(m_slotAlignment));
	m_slots.push_back(ptr);
	++m_expansionCount;
	return ptr;
}

void CGameScene::ScriptMemoryPool::Free(void* ptr)
{
	if (nullptr == ptr)
	{
		return;
	}
	m_freeList.push_back(ptr);
}

void CGameScene::ScriptMemoryPool::Clear()
{
	for (void* ptr : m_slots)
	{
		::operator delete(ptr, std::align_val_t(m_slotAlignment));
	}
	m_slots.clear();
	m_freeList.clear();
}

CGameScene::CGameScene()
{
	m_transformSystem = MakeOwnerPtr<CTransformSystem>();
	if (m_transformSystem)
	{
		m_transformSystem->Initialize(*this);
	}

	m_physicsSystem = MakeOwnerPtr<CPhysics2DSystem>();
	if (m_physicsSystem)
	{
		m_physicsSystem->Initialize(*this);
	}

	m_scriptSystem = MakeOwnerPtr<CScriptSystem>();
	if (m_scriptSystem)
	{
		m_scriptSystem->Initialize(*this);
	}
}

CGameScene::~CGameScene()
{
	Clear();
}

CGameObject* CGameScene::CreateGameObject(const char* name)
{
	const File::Guid guid = File::GenerateGuid();
	CGameObject* object = m_objectPool.Allocate(*this, name, guid);
	if (object)
	{
		object->CreationOrder = m_nextCreationOrder++;
		m_objectByGuid[guid] = object->SafeFromThis();
	}
	return object;
}

bool CGameScene::DestroyGameObject(CGameObject* gameObject)
{
	if (nullptr == gameObject || gameObject->GetScene() != this)
	{
		return false;
	}
	// 지연 파괴 — 순회 중 즉시 해제 금지. flush 에서 실제 파괴.
	m_pendingDestroyObjects.push_back(gameObject->SafeFromThis());
	return true;
}

void CGameScene::DestroyObjectRecursive(CGameObject* object)
{
	if (nullptr == object)
	{
		return;
	}

	// 자식 먼저 파괴 (리스트가 바뀌므로 복사본 순회).
	const std::vector<SafePtr<CGameObject>> children = object->GetChildren();
	for (const SafePtr<CGameObject>& child : children)
	{
		if (CGameObject* childObject = child.TryGet())
		{
			DestroyObjectRecursive(childObject);
		}
	}

	object->ClearParent();

	// 컴포넌트 해제 (복사본 순회 — DestroyComponent 가 owner 리스트에서 탈착).
	const std::vector<SafePtr<CComponent>> components = object->GetComponents();
	for (const SafePtr<CComponent>& component : components)
	{
		if (CComponent* raw = component.TryGet())
		{
			DestroyComponent(raw);
		}
	}
	m_objectByGuid.erase(object->InstanceGuid);
	m_objectPool.Free(object);
}

void CGameScene::DestroyComponent(CComponent* component)
{
	if (nullptr == component)
	{
		return;
	}

	// 파괴 훅 — 아직 컴포넌트/owner 가 유효한 시점에 자신의 정리(물리월드 등록 해제 등) 수행.
	if (CGameScript* script = dynamic_cast<CGameScript*>(component))
	{
		CGameObject* owner = component->GetOwner().TryGet();
		ScriptRuntimeState* runtime = FindScriptRuntime(script);
		if (runtime)
		{
			if (runtime->InputRegistered && runtime->InputHandler && Engine.InputSystem.IsValid())
			{
				Engine.InputSystem->UnregisterHandler(runtime->InputHandler);
			}
			if (owner)
			{
				owner->DetachComponent(component);
			}
			script->Destroy();
			SafePtrDetail::ControlBlock* block = runtime->ControlBlock;
			if (block)
			{
				block->Alive = false;
				block->Ptr = nullptr;
			}
			if (runtime->DestroyInstance)
			{
				runtime->DestroyInstance(script, runtime->HostApi);
			}
			if (block && 0 == block->SafeCount)
			{
				delete block;
			}
			std::erase_if(m_scriptRuntimeStates, [script](const ScriptRuntimeState& state)
			{
				return state.Instance == script;
			});
		}
		return;
	}

	component->OnDestroy();

	if (CGameObject* owner = component->GetOwner().TryGet())
	{
		owner->DetachComponent(component);
	}

	const std::type_index key(typeid(*component));
	auto it = std::lower_bound(m_componentPools.begin(), m_componentPools.end(), key,
		[](const PoolEntry& e, const std::type_index& k) { return e.Key < k; });
	if (it != m_componentPools.end() && it->Key == key && it->Pool)
	{
		it->Pool->FreeBase(component);
	}
}

CGameScript* CGameScene::AddScript(
	CGameObject& object,
	TypeId scriptTypeId,
	const CReflectionRegistry& registry)
{
	const ScriptTypeInfo* typeInfo = registry.FindScript(scriptTypeId);
	if (nullptr == typeInfo)
	{
		return nullptr;
	}

	ScriptInstanceHandle handle = registry.CreateScriptInstance(scriptTypeId, *this, object);
	CGameScript* script = handle.Instance;
	if (nullptr == script)
	{
		return nullptr;
	}

	SafePtrDetail::ControlBlock* block = new SafePtrDetail::ControlBlock(script, [](void*) {});
	SafePtrDetail::BindSafeFromThisControlBlock(static_cast<CComponent*>(script), block);
	script->InstanceGuid = File::GenerateGuid();
	script->Bind(*this, scriptTypeId, typeInfo->Type.Name);
	object.AttachComponent(script->SafeFromThis());

	ScriptRuntimeState state;
	state.Instance = script;
	state.Type = scriptTypeId;
	state.DestroyInstance = handle.DestroyInstance;
	state.HostApi = handle.HostApi;
	state.ControlBlock = block;
	if (typeInfo->ToInputHandler)
	{
		state.InputHandler = typeInfo->ToInputHandler(script);
	}
	m_scriptRuntimeStates.push_back(state);
	return script;
}

CGameScene::ScriptRuntimeState* CGameScene::FindScriptRuntime(CGameScript* script)
{
	const auto it = std::find_if(m_scriptRuntimeStates.begin(), m_scriptRuntimeStates.end(),
		[script](const ScriptRuntimeState& state) { return state.Instance == script; });
	return it == m_scriptRuntimeStates.end() ? nullptr : &*it;
}

const CGameScene::ScriptRuntimeState* CGameScene::FindScriptRuntime(const CGameScript* script) const
{
	const auto it = std::find_if(m_scriptRuntimeStates.begin(), m_scriptRuntimeStates.end(),
		[script](const ScriptRuntimeState& state) { return state.Instance == script; });
	return it == m_scriptRuntimeStates.end() ? nullptr : &*it;
}

SafePtr<CGameObject> CGameScene::FindByInstanceGuid(const File::Guid& guid)
{
	if (guid.IsNull())
	{
		return nullptr;
	}

	const auto it = m_objectByGuid.find(guid);
	if (it == m_objectByGuid.end())
	{
		return nullptr;
	}
	if (false == it->second.IsValid())
	{
		// 방어적: 어떤 경로로든 동기화가 어긋나 죽은 엔트리가 남았으면 제거.
		m_objectByGuid.erase(it);
		return nullptr;
	}
	return it->second;
}

void CGameScene::SetObjectInstanceGuid(CGameObject& object, const File::Guid& guid)
{
	// guid 재설정 시 맵도 rekey — 안 하면 옛 guid 로 계속 찾히거나 새 guid 가 안 찾힌다.
	if (false == object.InstanceGuid.IsNull())
	{
		m_objectByGuid.erase(object.InstanceGuid);
	}
	object.InstanceGuid = guid;
	if (false == guid.IsNull())
	{
		m_objectByGuid[guid] = object.SafeFromThis();
	}
}

void CGameScene::Update(bool isSimulationPlaying)
{
	UpdateSystems(isSimulationPlaying);
	if (isSimulationPlaying)
	{
		UpdateScripts();
	}
	FlushPendingDestroys();
}

void CGameScene::Update()
{
	Update(true);
}

void CGameScene::FixedUpdate()
{
	if (m_physicsSystem)
	{
		m_physicsSystem->FixedUpdate(*this);
	}

	for (OwnerPtr<CGameSystem>& system : m_systems)
	{
		if (system)
		{
			system->FixedUpdate(*this);
		}
	}

	if (m_scriptSystem)
	{
		m_scriptSystem->FixedUpdate(*this);
	}

	FlushPendingDestroys();
}

void CGameScene::FlushPendingDestroys()
{
	// 컴포넌트 먼저(개별 RemoveComponent), 그다음 오브젝트(자식 재귀 포함).
	// SafePtr 가 null 이면 이미 다른 경로로 파괴된 것 → 스킵.
	if (false == m_pendingDestroyComponents.empty())
	{
		std::vector<SafePtr<CComponent>> pending;
		pending.swap(m_pendingDestroyComponents);
		for (const SafePtr<CComponent>& ref : pending)
		{
			if (CComponent* component = ref.TryGet())
			{
				DestroyComponent(component);
			}
		}
	}

	if (false == m_pendingDestroyObjects.empty())
	{
		std::vector<SafePtr<CGameObject>> pending;
		pending.swap(m_pendingDestroyObjects);
		for (const SafePtr<CGameObject>& ref : pending)
		{
			if (CGameObject* object = ref.TryGet())
			{
				DestroyObjectRecursive(object);
			}
		}
	}
}

void CGameScene::UpdateSystems(bool isSimulationPlaying)
{
	if (m_transformSystem)
	{
		m_transformSystem->Update(*this);
	}

	for (OwnerPtr<CGameSystem>& system : m_systems)
	{
		if (system && (isSimulationPlaying || system->ShouldUpdateInEditMode()))
		{
			system->Update(*this);
		}
	}
}

void CGameScene::UpdateScripts()
{
	if (m_scriptSystem)
	{
		m_scriptSystem->Update(*this);
	}
}

void CGameScene::NotifySimulationStop()
{
	for (OwnerPtr<CGameSystem>& system : m_systems)
	{
		if (system)
		{
			system->SimulationStop(*this);
		}
	}
}

void CGameScene::DestroyScriptInstances()
{
	std::vector<CGameScript*> scripts;
	for (const ScriptRuntimeState& state : m_scriptRuntimeStates)
	{
		if (state.Instance) scripts.push_back(state.Instance);
	}
	for (CGameScript* script : scripts)
	{
		DestroyComponent(script);
	}
	ClearScriptMemoryPools();
}

void CGameScene::ClearScriptMemoryPools()
{
	CReflectionRegistry::ForgetScriptAllocationsForScene(*this);
	m_scriptMemoryPools.clear();
	++m_scriptAllocationGeneration;
	if (0 == m_scriptAllocationGeneration)
	{
		m_scriptAllocationGeneration = 1;
	}
}

void CGameScene::ReserveScriptMemoryForCurrentScripts(const CReflectionRegistry& registry, float capacityMultiplier)
{
	std::unordered_map<TypeId, std::size_t> counts;
	for (const ScriptRuntimeState& script : m_scriptRuntimeStates)
	{
		if (INVALID_TYPE_ID != script.Type)
		{
			++counts[script.Type];
		}
	}

	const float multiplier = std::max(capacityMultiplier, 1.0f);
	for (const auto& [scriptTypeId, count] : counts)
	{
		const ScriptTypeInfo* typeInfo = registry.FindScript(scriptTypeId);
		if (nullptr == typeInfo)
		{
			continue;
		}

		const std::size_t capacity = static_cast<std::size_t>(std::ceil(static_cast<float>(count) * multiplier));
		if (capacity > 0)
		{
			void* ptr = AllocateScriptMemory(scriptTypeId, typeInfo->Type.Size, typeInfo->Type.Alignment);
			FreeScriptMemory(scriptTypeId, ptr, typeInfo->Type.Size, typeInfo->Type.Alignment);

			auto it = std::lower_bound(
				m_scriptMemoryPools.begin(),
				m_scriptMemoryPools.end(),
				scriptTypeId,
				[](const OwnerPtr<ScriptMemoryPool>& pool, TypeId typeId)
				{
					return pool && pool->GetTypeId() < typeId;
				});
			if (it != m_scriptMemoryPools.end() && *it && (*it)->GetTypeId() == scriptTypeId)
			{
				(*it)->Reserve(capacity);
			}
		}
	}
}

void CGameScene::ReserveScriptMemory(TypeId scriptTypeId, std::size_t size, std::size_t alignment, std::size_t capacity)
{
	if (INVALID_TYPE_ID == scriptTypeId || 0 == capacity)
	{
		return;
	}

	const std::size_t effectiveSize = std::max<std::size_t>(size, 1);
	const std::size_t effectiveAlignment = std::max<std::size_t>(alignment, alignof(void*));
	auto it = std::lower_bound(m_scriptMemoryPools.begin(), m_scriptMemoryPools.end(), scriptTypeId,
		[](const OwnerPtr<ScriptMemoryPool>& pool, TypeId typeId)
		{
			return pool && pool->GetTypeId() < typeId;
		});
	if (it == m_scriptMemoryPools.end() || false == static_cast<bool>(*it) || (*it)->GetTypeId() != scriptTypeId)
	{
		it = m_scriptMemoryPools.insert(it, MakeOwnerPtr<ScriptMemoryPool>(scriptTypeId, effectiveSize, effectiveAlignment));
	}
	else if (false == (*it)->Matches(effectiveSize, effectiveAlignment))
	{
		*it = MakeOwnerPtr<ScriptMemoryPool>(scriptTypeId, effectiveSize, effectiveAlignment);
	}
	if (*it)
	{
		(*it)->Reserve(capacity);
	}
}

std::vector<CGameScene::ScriptMemoryPoolStats> CGameScene::GetScriptMemoryPoolStats() const
{
	std::vector<ScriptMemoryPoolStats> result;
	result.reserve(m_scriptMemoryPools.size());
	for (const OwnerPtr<ScriptMemoryPool>& pool : m_scriptMemoryPools)
	{
		if (pool)
		{
			result.push_back({ pool->GetTypeId(), pool->GetCapacity(), pool->GetUsedCount(), pool->GetExpansionCount() });
		}
	}
	return result;
}

void* CGameScene::AllocateScriptMemory(TypeId scriptTypeId, std::size_t size, std::size_t alignment)
{
	const std::size_t effectiveSize = std::max<std::size_t>(size, 1);
	const std::size_t effectiveAlignment = std::max<std::size_t>(alignment, alignof(void*));

	auto it = std::lower_bound(
		m_scriptMemoryPools.begin(),
		m_scriptMemoryPools.end(),
		scriptTypeId,
		[](const OwnerPtr<ScriptMemoryPool>& pool, TypeId typeId)
		{
			return pool && pool->GetTypeId() < typeId;
		});

	if (it != m_scriptMemoryPools.end() && *it && (*it)->GetTypeId() == scriptTypeId)
	{
		if (false == (*it)->Matches(effectiveSize, effectiveAlignment))
		{
			// Live compile can change size/alignment for the same script name.
			// Instances are destroyed before reload; replacing an empty pool is the safe path.
			CSystemLog::Info(std::format(
				"Script memory pool replaced after script layout changed. scene='{}', typeId={}, oldCapacity={}",
				GetName(),
				static_cast<unsigned long long>(scriptTypeId),
				(*it)->GetCapacity()));
			*it = MakeOwnerPtr<ScriptMemoryPool>(scriptTypeId, effectiveSize, effectiveAlignment);
		}
		const std::size_t beforeCapacity = (*it)->GetCapacity();
		void* ptr = (*it)->Allocate();
		if ((*it)->GetCapacity() > beforeCapacity)
		{
			CSystemLog::Warning(std::format(
				"Script memory pool expanded at runtime. scene='{}', typeId={}, capacity={} -> {}. Consider increasing preallocation.",
				GetName(),
				static_cast<unsigned long long>(scriptTypeId),
				beforeCapacity,
				(*it)->GetCapacity()));
		}
		return ptr;
	}

	OwnerPtr<ScriptMemoryPool> pool = MakeOwnerPtr<ScriptMemoryPool>(scriptTypeId, effectiveSize, effectiveAlignment);
	void* ptr = pool ? pool->Allocate() : nullptr;
	m_scriptMemoryPools.insert(it, std::move(pool));
	return ptr;
}

void CGameScene::FreeScriptMemory(TypeId scriptTypeId, void* ptr, std::size_t size, std::size_t alignment)
{
	if (nullptr == ptr)
	{
		return;
	}

	auto it = std::lower_bound(
		m_scriptMemoryPools.begin(),
		m_scriptMemoryPools.end(),
		scriptTypeId,
		[](const OwnerPtr<ScriptMemoryPool>& pool, TypeId typeId)
		{
			return pool && pool->GetTypeId() < typeId;
		});

	if (it != m_scriptMemoryPools.end() && *it && (*it)->GetTypeId() == scriptTypeId
		&& (*it)->Matches(size, alignment))
	{
		(*it)->Free(ptr);
		return;
	}

	CSystemLog::Warning(std::format(
		"Script memory was freed outside its owning pool. scene='{}', typeId={}, size={}, alignment={}.",
		GetName(),
		static_cast<unsigned long long>(scriptTypeId),
		static_cast<unsigned long long>(size),
		static_cast<unsigned long long>(alignment)));
	::operator delete(ptr, std::align_val_t(std::max<std::size_t>(alignment, alignof(void*))));
}

void CGameScene::DispatchSurfaceEventToScripts(const SurfaceEvent& surfaceEvent)
{
	ForEachObjectInHierarchyOrder([&surfaceEvent](CGameObject& object)
	{
		for (const SafePtr<CComponent>& component : object.GetComponents())
		{
			CGameScript* instance = dynamic_cast<CGameScript*>(component.TryGet());
			if (nullptr == instance) continue;
		switch (surfaceEvent.Type)
		{
		case ESurfaceEventType::FocusGained: instance->ApplicationFocusGained();         break;
		case ESurfaceEventType::FocusLost:   instance->ApplicationFocusLost();           break;
		case ESurfaceEventType::Resized:     instance->SurfaceResized(surfaceEvent.ClientSize); break;
		}
		}
	});
}

CPhysics2DSystem* CGameScene::GetPhysics2DSystem()
{
	return m_physicsSystem.Get();
}

const CPhysics2DSystem* CGameScene::GetPhysics2DSystem() const
{
	return m_physicsSystem.Get();
}

void CGameScene::SetReferencedAssets(std::vector<AssetGuid> referencedAssets)
{
	m_referencedAssets = std::move(referencedAssets);
}

const std::vector<AssetGuid>& CGameScene::GetReferencedAssets() const
{
	return m_referencedAssets;
}

void CGameScene::ClearObjects()
{
	DestroyScriptInstances();

	m_pendingDestroyComponents.clear();
	m_pendingDestroyObjects.clear();

	// 컴포넌트 풀 먼저 해제 → 오브젝트 풀 해제(상호참조 없이 안전).
	for (PoolEntry& entry : m_componentPools)
	{
		if (entry.Pool)
		{
			entry.Pool->Clear();
		}
	}
	m_componentPools.clear();
	m_scriptRuntimeStates.clear();
	m_objectByGuid.clear();
	m_objectPool.Clear();
	m_referencedAssets.clear();
}

void CGameScene::Clear()
{
	for (OwnerPtr<CGameSystem>& system : m_systems)
	{
		if (system)
		{
			system->Finalize(*this);
		}
	}
	m_systems.clear();

	if (m_transformSystem)
	{
		m_transformSystem->Finalize(*this);
		m_transformSystem->Initialize(*this);
	}
	if (m_physicsSystem)
	{
		m_physicsSystem->Finalize(*this);
		m_physicsSystem->Initialize(*this);
	}
	if (m_scriptSystem)
	{
		m_scriptSystem->Finalize(*this);
		m_scriptSystem->Initialize(*this);
	}

	ClearObjects();
}
