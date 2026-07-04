#include "pch.h"
#include "Scene.h"

#include "Core/Asset/AssetRef.inl"   // m_loadedAssets(AssetRef) 소멸자 인스턴스화에 필요
#include "GameFramework/Component/ScriptComponent.h"
#include "GameFramework/Physics2D/Physics2DSystem.h"
#include "GameFramework/Scripting/ScriptSystem.h"
#include "GameFramework/Transform/TransformSystem.h"

#include <vector>

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
	component->OnDestroy();

	if (CGameObject* owner = component->GetOwner())
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
	ForEach<ScriptComponent>([](ScriptComponent& script)
	{
		script.ResetInstance();
	});
}

void CGameScene::DispatchSurfaceEventToScripts(const SurfaceEvent& surfaceEvent)
{
	ForEach<ScriptComponent>([&surfaceEvent](ScriptComponent& script)
	{
		CGameScript* instance = script.Instance;
		if (nullptr == instance)
		{
			return;
		}
		switch (surfaceEvent.Type)
		{
		case ESurfaceEventType::FocusGained: instance->ApplicationFocusGained();         break;
		case ESurfaceEventType::FocusLost:   instance->ApplicationFocusLost();           break;
		case ESurfaceEventType::Resized:     instance->SurfaceResized(surfaceEvent.ClientSize); break;
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
