#include "pch.h"
#include "Canvas.h"

#include "Core/Asset/AssetRef.inl"   // m_loadedAssets(AssetRef) 소멸자 인스턴스화에 필요
#include "Core/Logging/LoggerInternal.h"
#include "Core/EngineCore.h"
#include "Core/Input/InputSystem.h"
#include "Core/Time/Time.h"   // 코루틴 tick 의 스케일/언스케일 dt
#include "GameFramework/Scripting/GameScript.h"
#include "GameFramework/Physics2D/Physics2DSystem.h"
#include "GameFramework/Reflection/ReflectionRegistry.h"
#include "GameFramework/Canvas/GameLayer.h"
#include "GameFramework/Canvas/CanvasViewProjection.h"   // ComputeCanvasViewProjection — 역투영 공용 계산
#include "GameFramework/Scripting/ScriptSystem.h"
#include "GameFramework/Transform/TransformSystem.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <typeinfo>
#include <new>
#include <unordered_map>
#include <vector>

namespace
{
	// File::Guid(fs::path) → Guid128. 오브젝트 생성/파괴/rekey 등 cold path 에서만 쓴다
	// (문자열은 ASCII hex 라 fs::path::string() 왕복이 안전). hot path 는 Guid128::FromText(char*).
	Guid128 ToGuid128(const File::Guid& guid)
	{
		return Guid128::FromText(guid.string().c_str());
	}

	// m_objectByGuid 공통 조회 — 죽은 엔트리는 방어적으로 제거한다.
	SafePtr<CGameObject> LookupByGuid128(
		Table<Guid128, SafePtr<CGameObject>>& objectByGuid, const Guid128& id)
	{
		if (id.IsNull())
		{
			return nullptr;
		}
		SafePtr<CGameObject>* object = objectByGuid.Find(id);
		if (nullptr == object)
		{
			return nullptr;
		}
		if (false == object->IsValid())
		{
			objectByGuid.Remove(id);
			return nullptr;
		}
		return *object;
	}
}

CGameCanvas::ScriptMemoryPool::ScriptMemoryPool(TypeId typeId, std::size_t slotSize, std::size_t slotAlignment)
	: m_typeId(typeId)
	, m_slotSize(std::max<std::size_t>(slotSize, 1))
	, m_slotAlignment(std::max<std::size_t>(slotAlignment, alignof(void*)))
{
}

CGameCanvas::ScriptMemoryPool::~ScriptMemoryPool()
{
	Clear();
}

bool CGameCanvas::ScriptMemoryPool::Matches(std::size_t slotSize, std::size_t slotAlignment) const
{
	return m_slotSize == std::max<std::size_t>(slotSize, 1)
		&& m_slotAlignment == std::max<std::size_t>(slotAlignment, alignof(void*));
}

void CGameCanvas::ScriptMemoryPool::Reserve(std::size_t capacity)
{
	while (m_slots.size() < capacity)
	{
		void* ptr = ::operator new(m_slotSize, std::align_val_t(m_slotAlignment));
		m_slots.push_back(ptr);
		m_freeList.push_back(ptr);
	}
}

void* CGameCanvas::ScriptMemoryPool::Allocate()
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

void CGameCanvas::ScriptMemoryPool::Free(void* ptr)
{
	if (nullptr == ptr)
	{
		return;
	}
	m_freeList.push_back(ptr);
}

void CGameCanvas::ScriptMemoryPool::Clear()
{
	for (void* ptr : m_slots)
	{
		::operator delete(ptr, std::align_val_t(m_slotAlignment));
	}
	m_slots.clear();
	m_freeList.clear();
}

CGameCanvas::CGameCanvas()
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

CGameCanvas::~CGameCanvas()
{
	Clear();
}

CGameLayer* CGameCanvas::CreateLayer(const char* name)
{
	std::string autoName;
	if (nullptr == name || '\0' == name[0])
	{
		autoName = "Layer " + std::to_string(m_layers.size() + 1);
		name = autoName.c_str();
	}
	OwnerPtr<CGameLayer> layer = MakeOwnerPtr<CGameLayer>(name, File::GenerateGuid());
	CGameLayer* raw = layer.Get();
	m_layers.push_back(std::move(layer));
	ReindexLayers();
	MarkScriptExecutionOrderDirty();
	return raw;
}

void CGameCanvas::ReindexLayers()
{
	for (std::size_t i = 0; i < m_layers.size(); ++i)
	{
		if (m_layers[i])
		{
			m_layers[i]->m_index = static_cast<std::uint16_t>(i);
		}
	}
}

bool CGameCanvas::DestroyLayer(CGameLayer* layer)
{
	if (nullptr == layer || m_layers.size() <= 1 || GetLayerIndex(layer) < 0)
	{
		return false;
	}
	// 소속 루트 오브젝트 파괴 예약(자식은 재귀 파괴가 따라감 — 자식=부모 레이어 불변식).
	ForEachObject([this, layer](CGameObject& object)
	{
		if (object.GetLayer().TryGet() == layer && false == object.GetParent().IsValid())
		{
			m_pendingDestroyObjects.Add(object.SafeFromThis());
		}
	});
	m_pendingDestroyLayers.Add(layer->SafeFromThis());
	return true;
}

SafePtr<CGameLayer> CGameCanvas::FindLayerByName(const char* name) const
{
	if (nullptr == name)
	{
		return nullptr;
	}
	for (const OwnerPtr<CGameLayer>& layer : m_layers)
	{
		if (layer && layer->Name == name)
		{
			return layer.GetSafePtr();
		}
	}
	return nullptr;
}

SafePtr<CGameLayer> CGameCanvas::FindLayerBySourceAsset(const File::Guid& sourceAssetGuid) const
{
	if (sourceAssetGuid.IsNull())
	{
		return nullptr;
	}
	for (const OwnerPtr<CGameLayer>& layer : m_layers)
	{
		if (layer && layer->SourceAssetGuid == sourceAssetGuid)
		{
			return layer.GetSafePtr();
		}
	}
	return nullptr;
}

SafePtr<CGameLayer> CGameCanvas::FindLayerByInstanceGuid(const File::Guid& guid) const
{
	if (guid.IsNull())
	{
		return nullptr;
	}
	for (const OwnerPtr<CGameLayer>& layer : m_layers)
	{
		if (layer && layer->GetInstanceGuid() == guid)
		{
			return layer.GetSafePtr();
		}
	}
	return nullptr;
}

CGameLayer* CGameCanvas::GetLayerAt(std::size_t index) const
{
	return index < m_layers.size() ? m_layers[index].Get() : nullptr;
}

int CGameCanvas::GetLayerIndex(const CGameLayer* layer) const
{
	if (nullptr == layer)
	{
		return -1;
	}
	for (std::size_t i = 0; i < m_layers.size(); ++i)
	{
		if (m_layers[i].Get() == layer)
		{
			return static_cast<int>(i);
		}
	}
	return -1;
}

bool CGameCanvas::MoveLayer(CGameLayer* layer, std::size_t newIndex)
{
	const int currentIndex = GetLayerIndex(layer);
	if (currentIndex < 0)
	{
		return false;
	}
	OwnerPtr<CGameLayer> moved = std::move(m_layers[static_cast<std::size_t>(currentIndex)]);
	m_layers.erase(m_layers.begin() + currentIndex);
	newIndex = std::min(newIndex, m_layers.size());
	m_layers.insert(m_layers.begin() + static_cast<std::ptrdiff_t>(newIndex), std::move(moved));
	ReindexLayers();
	MarkScriptExecutionOrderDirty();
	return true;
}

CGameLayer* CGameCanvas::GetDefaultLayer()
{
	if (m_layers.empty())
	{
		return CreateLayer(nullptr);
	}
	return m_layers.front().Get();
}

bool CGameCanvas::MoveObjectToLayer(CGameObject& object, CGameLayer& layer)
{
	// 루트 서브트리 단위만 — 비루트 이동은 "자식=부모 레이어" 불변식을 깬다.
	if (object.GetCanvas() != this || object.GetParent().IsValid() || GetLayerIndex(&layer) < 0)
	{
		return false;
	}
	object.SetLayerRecursive(layer.SafeFromThis());
	MarkScriptExecutionOrderDirty();
	return true;
}

void CGameCanvas::SetLayerInstanceGuid(CGameLayer& layer, const File::Guid& guid)
{
	layer.SetInstanceGuid(guid);
}

CanvasViewport* CGameCanvas::CreateViewport(const char* name)
{
	return InsertViewport(m_viewports.size(), name);
}

CanvasViewport* CGameCanvas::InsertViewport(std::size_t index, const char* name)
{
	CanvasViewport viewport;
	viewport.Name = (name && '\0' != name[0]) ? name : ("Viewport " + std::to_string(m_viewports.size() + 1));

	const std::size_t position = std::min(index, m_viewports.size());
	const auto it = m_viewports.insert(
		m_viewports.begin() + static_cast<std::ptrdiff_t>(position), std::move(viewport));
	return &(*it);
}

bool CGameCanvas::DestroyViewport(std::size_t index)
{
	if (index >= m_viewports.size())
	{
		return false;
	}
	// 마지막 하나는 거부 — 렌더 수집(CollectGameRenderViewports)이 매 프레임
	// GetOrCreateDefaultViewport 로 빈 목록을 채우므로, 지워도 다음 프레임에 "Default" 가
	// 되살아난다. 그 상태에서 undo 하면 되살아난 것 + 복원한 것 = 2개가 되어 화면이 어긋난다.
	if (1 == m_viewports.size())
	{
		return false;
	}
	m_viewports.erase(m_viewports.begin() + static_cast<std::ptrdiff_t>(index));
	return true;
}

CanvasViewport* CGameCanvas::GetViewportAt(std::size_t index)
{
	return index < m_viewports.size() ? &m_viewports[index] : nullptr;
}

const CanvasViewport* CGameCanvas::GetViewportAt(std::size_t index) const
{
	return index < m_viewports.size() ? &m_viewports[index] : nullptr;
}

CanvasViewport* CGameCanvas::FindViewportByName(const char* name)
{
	if (nullptr == name)
	{
		return nullptr;
	}
	for (CanvasViewport& viewport : m_viewports)
	{
		if (viewport.Name == name)
		{
			return &viewport;
		}
	}
	return nullptr;
}

CanvasViewport& CGameCanvas::GetOrCreateDefaultViewport()
{
	if (m_viewports.empty())
	{
		// 기본 뷰포트 = 풀스크린 + 카메라 미지정(폴백). 스플릿을 안 쓰는 게임은 이 하나로
		// 끝나고 뷰포트라는 개념을 볼 일이 없다.
		CreateViewport("Default");
	}
	return m_viewports.front();
}

bool CGameCanvas::SetViewportCamera(std::size_t viewportIndex, CGameObject* cameraObject)
{
	if (viewportIndex >= m_viewports.size())
	{
		return false;
	}

	CanvasViewport& viewport = m_viewports[viewportIndex];
	if (nullptr == cameraObject)
	{
		// 카메라 미지정으로 되돌림 — guid 와 캐시를 함께 비운다. 렌더는 폴백 카메라를 쓴다.
		viewport.CameraObjectGuid = File::Guid();
		viewport.ResolvedCamera = nullptr;
		return true;
	}

	// guid(영속 키)와 SafePtr(프레임 캐시)를 함께 대입한다 — 캐시를 갱신하지 않으면 렌더가
	// 살아 있는 옛 캐시를 그대로 써서 이번 변경이 무시된다(필드 직접 대입이 조용히 깨지는 이유).
	viewport.CameraObjectGuid = cameraObject->GetInstanceGuid();
	viewport.ResolvedCamera = cameraObject->SafeFromThis();
	return true;
}

const Camera2D* CGameCanvas::FindFallbackCamera() const
{
	// "첫 활성 카메라" — 순서 규약은 레이어 순서 → 레이어 내 하이라키(생성) 순서다. 스크립트
	// 실행 순서와 같은 축이라 예측 가능하고, 풀 순회 순서(비결정)에 의존하지 않는다.
	const Camera2D* best = nullptr;
	std::uint16_t bestLayerIndex = 0;
	std::uint64_t bestCreationOrder = 0;
	ForEach<Camera2D>(
		[&](const Camera2D& camera)
		{
			if (false == IsActiveComponent(camera))
			{
				return;
			}
			const CGameObject* owner = camera.GetOwner().TryGet();
			if (nullptr == owner)
			{
				return;
			}
			const std::uint16_t layerIndex = owner->GetLayerIndex();
			const std::uint64_t creationOrder = owner->GetCreationOrder();
			if (nullptr == best
				|| layerIndex < bestLayerIndex
				|| (layerIndex == bestLayerIndex && creationOrder < bestCreationOrder))
			{
				best = &camera;
				bestLayerIndex = layerIndex;
				bestCreationOrder = creationOrder;
			}
		});
	return best;
}

bool CGameCanvas::ScreenToWorld(float screenX, float screenY, Vector2& outWorld) const
{
	if (m_lastRenderWidth < 1.0f || m_lastRenderHeight < 1.0f)
	{
		return false;   // 아직 렌더된 적이 없다 — 뷰포트 렉트를 픽셀로 되돌릴 기준이 없다.
	}

	// 폴백 카메라는 뷰포트와 무관하게 캔버스당 하나로 정해진다("첫 활성 카메라") — 루프 안에서
	// 부르면 카메라 미지정 뷰포트 수만큼 Camera2D 풀 전체를 다시 순회한다. 스크립트가 매 프레임
	// 마우스 피킹으로 부르는 경로라, 실제로 필요해진 시점에 한 번만 해석하고 재사용한다.
	const Camera2D* fallbackCamera = nullptr;
	bool fallbackCameraResolved = false;

	// 위에 있는 뷰포트가 클릭을 먹는다(그리는 순서 = 목록 순서, 뒤 항목이 화면 위) → 역순 탐색.
	for (std::size_t reverse = m_viewports.size(); reverse > 0; --reverse)
	{
		const CanvasViewport& viewport = m_viewports[reverse - 1];
		if (false == viewport.Active)
		{
			continue;
		}

		// 카메라 해석은 렌더가 채운 ResolvedCamera 캐시를 그대로 쓴다(같은 프레임 = 같은 카메라).
		// 캐시가 비어 있으면(카메라 미지정 뷰포트 = 가장 흔한 경우) 폴백으로 해석한다.
		const Camera2D* camera = nullptr;
		if (CGameObject* cameraObject = viewport.ResolvedCamera.TryGet())
		{
			camera = cameraObject->GetComponent<Camera2D>();
		}
		if (nullptr == camera)
		{
			if (false == fallbackCameraResolved)
			{
				fallbackCamera = FindFallbackCamera();
				fallbackCameraResolved = true;
			}
			camera = fallbackCamera;
		}
		if (nullptr == camera)
		{
			continue;   // 눈이 없으면 이 뷰포트로는 역투영할 수 없다.
		}
		const CGameObject* owner = camera->GetOwner().TryGet();
		if (nullptr == owner)
		{
			continue;
		}

		const CanvasViewProjection view = ComputeCanvasViewProjection(viewport, *camera, *owner, m_lastRenderWidth, m_lastRenderHeight);
		if (false == CanvasViewProjectionContainsScreen(view, screenX, screenY))
		{
			continue;   // 스크린점이 이 뷰포트 렉트 밖 — 다음(아래) 뷰포트를 본다.
		}

		outWorld = CanvasViewProjectionScreenToWorld(view, screenX, screenY);
		return true;
	}

	return false;
}

void CGameCanvas::AttachRuntimeLayerAssets(const File::Guid& layerGuid, std::vector<AssetRef<IAsset>> assets)
{
	if (layerGuid.IsNull())
	{
		return;
	}
	m_runtimeLayerAssets[ToGuid128(layerGuid)] = std::move(assets);
}

void CGameCanvas::SetBackgroundColor(float r, float g, float b, float a)
{
	m_backgroundColor[0] = r;
	m_backgroundColor[1] = g;
	m_backgroundColor[2] = b;
	m_backgroundColor[3] = a;
}

CGameObject* CGameCanvas::CreateGameObject(const char* name, CGameLayer* layer)
{
	const File::Guid guid = File::GenerateGuid();
	CGameObject* object = m_objectPool.Allocate(*this, name, guid);
	if (object)
	{
		if (nullptr == layer || GetLayerIndex(layer) < 0)
		{
			layer = GetDefaultLayer();
		}
		object->m_layer = layer->SafeFromThis();
		object->m_creationOrder = m_nextCreationOrder++;
		m_objectByGuid.InsertOrAssign(ToGuid128(guid), object->SafeFromThis());
		MarkScriptExecutionOrderDirty();
	}
	return object;
}

bool CGameCanvas::DestroyGameObject(CGameObject* gameObject)
{
	if (nullptr == gameObject || gameObject->GetCanvas() != this)
	{
		return false;
	}
	// 지연 파괴 — 순회 중 즉시 해제 금지. flush 에서 실제 파괴.
	m_pendingDestroyObjects.Add(gameObject->SafeFromThis());
	return true;
}

void CGameCanvas::DestroyObjectRecursive(CGameObject* object)
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
	m_objectByGuid.Remove(ToGuid128(object->GetInstanceGuid()));
	m_objectPool.Free(object);
}

void CGameCanvas::DestroyComponent(CComponent* component)
{
	if (nullptr == component)
	{
		return;
	}

	// 스크립트 여부는 생성 시 등록한 O(1) 인덱스로 판별한다. RTTI는 라이프사이클 및
	// 파괴 경로에서 사용하지 않는다.
	const auto runtimeIt = m_scriptRuntimeByComponent.find(component);
	if (runtimeIt != m_scriptRuntimeByComponent.end())
	{
		ScriptRuntimeState* runtime = runtimeIt->second;
		assert(nullptr != runtime && runtime->Instance == component);
		CGameScript* script = runtime->Instance;
		CGameObject* owner = component->GetOwner().TryGet();

		if (runtime->InputRegistered && runtime->InputHandler && Engine.InputSystem.IsValid())
		{
			Engine.InputSystem->UnregisterHandler(runtime->InputHandler);
		}
		if (owner)
		{
			owner->DetachComponent(component);
		}

		// 캐시가 다음 조회 전에 재구축되더라도, 현재 실행 스냅샷 안의 포인터가 파괴 후
		// 남지 않도록 즉시 제거한다.
		std::erase(m_scriptExecutionOrder, runtime);
		m_scriptRuntimeByComponent.erase(runtimeIt);
		MarkScriptExecutionOrderDirty();

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
		const std::size_t runtimeIndex = runtime->SelfIndex;
		*runtime = ScriptRuntimeState{};
		m_freeScriptRuntimeStateIndices.push_back(runtimeIndex);
		return;
	}

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

CGameScript* CGameCanvas::AddScript(
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
	script->SetInstanceGuid(File::GenerateGuid());
	script->Bind(*this, typeInfo->Type.Name);
	object.AttachComponent(script->SafeFromThis());

	ScriptRuntimeState* state = nullptr;
	std::size_t stateIndex = 0;
	if (false == m_freeScriptRuntimeStateIndices.empty())
	{
		stateIndex = m_freeScriptRuntimeStateIndices.back();
		m_freeScriptRuntimeStateIndices.pop_back();
		state = &m_scriptRuntimeStates[stateIndex];
	}
	else
	{
		// deque 라 emplace_back 이 기존 원소 주소를 무효화하지 않는다 — 재인덱싱 불필요.
		stateIndex = m_scriptRuntimeStates.size();
		m_scriptRuntimeStates.emplace_back();
		state = &m_scriptRuntimeStates.back();
	}

	state->Instance = script;
	state->Type = scriptTypeId;
	state->DestroyInstance = handle.DestroyInstance;
	state->HostApi = handle.HostApi;
	state->ControlBlock = block;
	state->SelfIndex = stateIndex;
	if (typeInfo->ToInputHandler)
	{
		state->InputHandler = typeInfo->ToInputHandler(script);
	}
	m_scriptRuntimeByComponent.emplace(script, state);
	MarkScriptExecutionOrderDirty();
	return script;
}

void CGameCanvas::MarkScriptExecutionOrderDirty()
{
	m_scriptExecutionOrderDirty = true;
}

void CGameCanvas::EnsureScriptExecutionOrder()
{
	// 순회 중(가드 활성)에는 재빌드하지 않는다 — clear/refill 이 바깥 순회의 iterator·
	// 인덱스를 무효화(UB)한다. dirty 는 유지되어 순회 밖 다음 호출에서 반영된다.
	if (m_scriptIterationDepth > 0)
	{
		return;
	}
	if (m_scriptExecutionOrderDirty)
	{
		RebuildScriptExecutionOrder();
	}
}

void CGameCanvas::RebuildScriptExecutionOrder()
{
	m_scriptExecutionOrder.clear();
	m_scriptRangesByObject.clear();
	m_scriptExecutionOrder.reserve(m_scriptRuntimeStates.size());
	m_scriptRangesByObject.reserve(m_scriptRuntimeStates.size());

	std::vector<CGameObject*> roots;
	roots.reserve(GetObjectCount());
	ForEachObject([&roots](CGameObject& object)
	{
		if (false == object.GetParent().IsValid())
		{
			roots.push_back(&object);
		}
	});
	// 실행 순서 = 레이어 순서(컴포짓 순서와 동일 축) → 레이어 내 하이라키 순서.
	// 레이어 포인터→인덱스 맵은 재빌드(cold path)당 1회만 구성한다.
	std::unordered_map<const CGameLayer*, std::size_t> layerIndexOf;
	layerIndexOf.reserve(m_layers.size());
	for (std::size_t i = 0; i < m_layers.size(); ++i)
	{
		layerIndexOf.emplace(m_layers[i].Get(), i);
	}
	auto layerIndexOfObject = [&layerIndexOf](const CGameObject* object)
	{
		const auto it = layerIndexOf.find(object->GetLayer().TryGet());
		// 레이어 미배정(방어)은 맨 뒤로.
		return it != layerIndexOf.end() ? it->second : layerIndexOf.size();
	};
	std::sort(roots.begin(), roots.end(),
		[&layerIndexOfObject](const CGameObject* lhs, const CGameObject* rhs)
	{
		const std::size_t lhsLayer = layerIndexOfObject(lhs);
		const std::size_t rhsLayer = layerIndexOfObject(rhs);
		if (lhsLayer != rhsLayer)
		{
			return lhsLayer < rhsLayer;
		}
		return lhs->GetCreationOrder() < rhs->GetCreationOrder();
	});

	for (CGameObject* root : roots)
	{
		if (root)
		{
			AppendObjectScriptsInHierarchyOrder(*root);
		}
	}
	m_scriptExecutionOrderDirty = false;
}

void CGameCanvas::AppendObjectScriptsInHierarchyOrder(CGameObject& object)
{
	const std::size_t begin = m_scriptExecutionOrder.size();
	for (const SafePtr<CComponent>& componentRef : object.GetComponents())
	{
		CComponent* component = componentRef.TryGet();
		const auto runtimeIt = m_scriptRuntimeByComponent.find(component);
		if (runtimeIt != m_scriptRuntimeByComponent.end() && runtimeIt->second)
		{
			m_scriptExecutionOrder.push_back(runtimeIt->second);
		}
	}

	const std::size_t count = m_scriptExecutionOrder.size() - begin;
	if (count > 0)
	{
		m_scriptRangesByObject.emplace(&object, ScriptObjectRange{ begin, count });
	}

	std::vector<CGameObject*> children;
	children.reserve(object.GetChildren().size());
	for (const SafePtr<CGameObject>& childRef : object.GetChildren())
	{
		if (CGameObject* child = childRef.TryGet())
		{
			children.push_back(child);
		}
	}
	std::sort(children.begin(), children.end(), [](const CGameObject* lhs, const CGameObject* rhs)
	{
		return lhs->GetCreationOrder() < rhs->GetCreationOrder();
	});
	for (CGameObject* child : children)
	{
		AppendObjectScriptsInHierarchyOrder(*child);
	}
}

SafePtr<CGameObject> CGameCanvas::FindByInstanceGuid(const File::Guid& guid)
{
	if (guid.IsNull())
	{
		return nullptr;
	}
	return LookupByGuid128(m_objectByGuid, ToGuid128(guid));
}

SafePtr<CGameObject> CGameCanvas::FindByInstanceGuidText(const char* guidText)
{
	if (nullptr == guidText || '\0' == guidText[0])
	{
		return nullptr;
	}
	return LookupByGuid128(m_objectByGuid, Guid128::FromText(guidText));
}

SafePtr<CGameObject> CGameCanvas::FindByTag(const char* tag)
{
	if (nullptr == tag || '\0' == tag[0])
	{
		return nullptr;
	}

	SafePtr<CGameObject> found;
	ForEachObject(
		[&](CGameObject& object)
		{
			if (found.IsValid() || object.Tag != tag)
			{
				return;
			}
			found = object.SafeFromThis();
		});
	return found;
}

void CGameCanvas::FindAllByTag(const char* tag, std::vector<CGameObject*>& outResults)
{
	outResults.clear();
	if (nullptr == tag || '\0' == tag[0])
	{
		return;
	}

	ForEachObject(
		[&](CGameObject& object)
		{
			if (object.Tag == tag)
			{
				outResults.push_back(&object);
			}
		});
}

SafePtr<CGameObject> CGameCanvas::FindByName(const char* name)
{
	if (nullptr == name || '\0' == name[0])
	{
		return nullptr;
	}

	SafePtr<CGameObject> found;
	ForEachObject(
		[&](CGameObject& object)
		{
			if (found.IsValid() || object.Name != name)
			{
				return;
			}
			found = object.SafeFromThis();
		});
	return found;
}

void CGameCanvas::SetObjectInstanceGuid(CGameObject& object, const File::Guid& guid)
{
	// guid 재설정 시 맵도 rekey — 안 하면 옛 guid 로 계속 찾히거나 새 guid 가 안 찾힌다.
	if (false == object.GetInstanceGuid().IsNull())
	{
		m_objectByGuid.Remove(ToGuid128(object.GetInstanceGuid()));
	}
	object.SetInstanceGuid(guid);
	if (false == guid.IsNull())
	{
		m_objectByGuid.InsertOrAssign(ToGuid128(guid), object.SafeFromThis());
	}
}

void CGameCanvas::Update(bool isSimulationPlaying)
{
	// 순서 계약: 스크립트 → 파괴 flush → 시스템(transform 전파 → 렌더 제출).
	// 스크립트가 옮긴 트랜스폼·스폰·파괴가 같은 프레임 렌더에 반영된다.
	// (구 순서: 시스템 제출이 스크립트보다 앞 → 스크립트 변경이 1프레임 늦게 그려짐.)
	// 스크립트 Update 안에서 GetWorld().Matrix 는 아직 지난 프레임 전파값 —
	// 신선한 월드값이 필요하면 CanvasTransformUtils::GetWorldTransform 을 쓴다.
	if (isSimulationPlaying)
	{
		UpdateScripts();
	}
	{
		CFrameSectionScope scope(m_frameProfiler, "FlushPendingDestroys", false);
		FlushPendingDestroys();
	}
	UpdateSystems(isSimulationPlaying);

	// 프레임의 마지막 구간이 여기서 끝난다. FixedUpdate 는 CanvasManager 가 이 앞에서
	// 0~8 번 돌리므로, 여기서 접으면 같은 프레임의 고정 스텝까지 함께 잡힌다.
	m_frameProfiler.EndFrame();
}

void CGameCanvas::Update()
{
	Update(true);
}

void CGameCanvas::FixedUpdate()
{
	// 이 함수는 프레임당 0~8 번 돈다(CanvasManager 의 고정 스텝 누산기). 그래서 계측이
	// 호출마다 접지 않고 누적한다 — 접으면 마지막 스텝만 남아 물리가 과소보고된다.
	if (m_physicsSystem)
	{
		CFrameSectionScope scope(m_frameProfiler, "CPhysics2DSystem", true);
		m_physicsSystem->FixedUpdate(*this);
	}

	// 현재 이 목록에서 OnFixedUpdate 를 구현한 시스템은 없다(전부 기본 no-op).
	// 그래도 하나로 묶어 재 둔다 — 누가 구현했는데 아무도 모르는 상황을 막는다.
	{
		CFrameSectionScope scope(m_frameProfiler, "Systems", true);
		for (OwnerPtr<CGameSystem>& system : m_systems)
		{
			if (system)
			{
				system->FixedUpdate(*this);
			}
		}
	}

	if (m_scriptSystem)
	{
		CFrameSectionScope scope(m_frameProfiler, "CScriptSystem", true);
		m_scriptSystem->FixedUpdate(*this);
	}

	// WaitFixedUpdate 로 멈춘 코루틴을 이 고정 스텝에 재개한다(프레임당 0~N회 호출).
	// 라벨은 UpdateScripts 의 tick 과 같은 리터럴 — CPU 프로파일러가 Update/FixedUpdate 를
	// 한 풀(CoroutineScheduler)로 함께 선택·표시한다.
	{
		CFrameSectionScope scope(m_frameProfiler, "CCoroutineScheduler", true);
		m_coroutineScheduler.NotifyFixedUpdate();
	}

	{
		CFrameSectionScope scope(m_frameProfiler, "FlushPendingDestroys", true);
		FlushPendingDestroys();
	}
}

void CGameCanvas::FlushPendingDestroys()
{
	// 컴포넌트 먼저(개별 RemoveComponent), 그다음 오브젝트(자식 재귀 포함).
	// SafePtr 가 null 이면 이미 다른 경로로 파괴된 것 → 스킵.
	if (false == m_pendingDestroyComponents.IsEmpty())
	{
		Array<SafePtr<CComponent>> pending;
		pending.Swap(m_pendingDestroyComponents);
		for (const SafePtr<CComponent>& ref : pending)
		{
			if (CComponent* component = ref.TryGet())
			{
				DestroyComponent(component);
			}
		}
	}

	if (false == m_pendingDestroyObjects.IsEmpty())
	{
		Array<SafePtr<CGameObject>> pending;
		pending.Swap(m_pendingDestroyObjects);
		for (const SafePtr<CGameObject>& ref : pending)
		{
			if (CGameObject* object = ref.TryGet())
			{
				DestroyObjectRecursive(object);
			}
		}
	}

	// 레이어는 소속 오브젝트 파괴가 끝난 뒤 제거한다(순서 계약).
	if (false == m_pendingDestroyLayers.IsEmpty())
	{
		Array<SafePtr<CGameLayer>> pendingLayers;
		pendingLayers.Swap(m_pendingDestroyLayers);
		for (const SafePtr<CGameLayer>& ref : pendingLayers)
		{
			const CGameLayer* layer = ref.TryGet();
			const int index = GetLayerIndex(layer);
			if (index >= 0)
			{
				// 런타임 로드 레이어면 물고 있던 에셋을 함께 놓는다(다른 사용자가 없으면 GC 대상).
				m_runtimeLayerAssets.erase(ToGuid128(layer->GetInstanceGuid()));
				m_layers.erase(m_layers.begin() + index);
			}
		}
		ReindexLayers();
		MarkScriptExecutionOrderDirty();
	}
}

void CGameCanvas::UpdateSystems(bool isSimulationPlaying)
{
	if (m_transformSystem)
	{
		// 단일 멤버라 타입이 고정 — typeid 대신 리터럴을 쓴다(같은 TU 안이라 주소가 안정적).
		CFrameSectionScope scope(m_frameProfiler, "CTransformSystem", false);
		m_transformSystem->Update(*this);
	}

	for (std::size_t index = 0; index < m_systems.size(); ++index)
	{
		CGameSystem* system = m_systems[index].Get();
		if (nullptr == system)
		{
			continue;
		}
		if (false == isSimulationPlaying && false == system->ShouldUpdateInEditMode())
		{
			continue;
		}
		CFrameSectionScope scope(m_frameProfiler, SystemLabel(index, *system), false);
		system->Update(*this);
	}
}

// typeid 이름을 슬롯당 한 번만 뽑는다. 매 프레임 부르면 계측이 스스로를 오염시킨다
// (MSVC 의 type_info::name 은 첫 호출에서 undecorate + 캐시라 공짜가 아니다).
const char* CGameCanvas::SystemLabel(std::size_t index, const CGameSystem& system)
{
	if (m_systemLabels.size() != m_systems.size())
	{
		m_systemLabels.assign(m_systems.size(), nullptr);
	}
	if (nullptr == m_systemLabels[index])
	{
		m_systemLabels[index] = typeid(system).name();
	}
	return m_systemLabels[index];
}

void CGameCanvas::UpdateScripts()
{
	if (m_scriptSystem)
	{
		CFrameSectionScope scope(m_frameProfiler, "CScriptSystem", false);
		m_scriptSystem->Update(*this);
	}

	// 코루틴 재개(스크립트 Update 직후). UpdateScripts 는 재생 중에만 호출되므로 편집 모드엔 tick 안 됨.
	// dt 는 호스트 Engine.Time 에서 — 스케일(Seconds)·언스케일(SecondsRealtime) 둘 다 넘긴다.
	{
		CFrameSectionScope scope(m_frameProfiler, "CCoroutineScheduler", false);
		float deltaSeconds = 0.0f;
		float unscaledDeltaSeconds = 0.0f;
		if (Engine.Time.IsValid())
		{
			deltaSeconds = Engine.Time->GetDeltaSeconds();
			unscaledDeltaSeconds = Engine.Time->GetUnscaledDeltaSeconds();
		}
		m_coroutineScheduler.Tick(deltaSeconds, unscaledDeltaSeconds);
	}
}

CoroutineId CGameCanvas::StartCoroutine(Coroutine&& routine, const SafePtr<CGameScript>& owner)
{
	return m_coroutineScheduler.Start(std::move(routine), owner);
}

void CGameCanvas::StopCoroutine(CoroutineId id)
{
	m_coroutineScheduler.Stop(id);
}

void CGameCanvas::StopCoroutinesForOwner(const CGameScript* owner)
{
	m_coroutineScheduler.StopForOwner(owner);
}

void CGameCanvas::NotifySimulationStop()
{
	for (OwnerPtr<CGameSystem>& system : m_systems)
	{
		if (system)
		{
			system->SimulationStop(*this);
		}
	}
}

void CGameCanvas::DestroyScriptInstances()
{
	// 살아있는 코루틴 프레임을 먼저 일괄 파괴한다 — 소유 스크립트 인스턴스와 게임 DLL 이
	// 사라지기 전에(시뮬 정지·DLL 라이브 리컴파일·프로젝트 언로드·앱 종료·캔버스 클리어 공용 관문).
	m_coroutineScheduler.CancelAll();

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

void CGameCanvas::ClearScriptMemoryPools()
{
	CReflectionRegistry::ForgetScriptAllocationsForCanvas(*this);
	m_scriptMemoryPools.clear();
	++m_scriptAllocationGeneration;
	if (0 == m_scriptAllocationGeneration)
	{
		m_scriptAllocationGeneration = 1;
	}
}

void CGameCanvas::ReserveScriptMemoryForCurrentScripts(const CReflectionRegistry& registry, float capacityMultiplier)
{
	std::unordered_map<TypeId, std::size_t> counts;
	for (const ScriptRuntimeState& script : m_scriptRuntimeStates)
	{
		if (script.Instance && INVALID_TYPE_ID != script.Type)
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

void CGameCanvas::ReserveScriptMemory(TypeId scriptTypeId, std::size_t size, std::size_t alignment, std::size_t capacity)
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

std::vector<CGameCanvas::ScriptMemoryPoolStats> CGameCanvas::GetScriptMemoryPoolStats() const
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

void* CGameCanvas::AllocateScriptMemory(TypeId scriptTypeId, std::size_t size, std::size_t alignment)
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
				"Script memory pool replaced after script layout changed. canvas='{}', typeId={}, oldCapacity={}",
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
				"Script memory pool expanded at runtime. canvas='{}', typeId={}, capacity={} -> {}. Consider increasing preallocation.",
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

void CGameCanvas::FreeScriptMemory(TypeId scriptTypeId, void* ptr, std::size_t size, std::size_t alignment)
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
		"Script memory was freed outside its owning pool. canvas='{}', typeId={}, size={}, alignment={}.",
		GetName(),
		static_cast<unsigned long long>(scriptTypeId),
		static_cast<unsigned long long>(size),
		static_cast<unsigned long long>(alignment)));
	::operator delete(ptr, std::align_val_t(std::max<std::size_t>(alignment, alignof(void*))));
}

void CGameCanvas::DispatchSurfaceEventToScripts(const SurfaceEvent& surfaceEvent)
{
	EnsureScriptExecutionOrder();
	// 훅(유저 스크립트)이 스폰·Ref 해석으로 캐시를 재빌드하지 못하게 순회 잠금.
	ScriptIterationGuard iterationGuard(*this);
	for (ScriptRuntimeState* runtime : m_scriptExecutionOrder)
	{
		CGameScript* instance = runtime ? runtime->Instance : nullptr;
		if (nullptr == instance)
		{
			continue;
		}
		switch (surfaceEvent.Type)
		{
		case ESurfaceEventType::FocusGained: instance->ApplicationFocusGained();         break;
		case ESurfaceEventType::FocusLost:   instance->ApplicationFocusLost();           break;
		case ESurfaceEventType::Resized:     instance->SurfaceResized(surfaceEvent.ClientSize); break;
		}
	}
}

void CGameCanvas::DispatchCanvasChangedToScripts(const std::vector<SafePtr<CGameLayer>>& inheritedLayers)
{
	if (inheritedLayers.empty())
	{
		return;
	}

	EnsureScriptExecutionOrder();
	// 훅(유저 스크립트)이 스폰·Ref 해석으로 캐시를 재빌드하지 못하게 순회 잠금.
	ScriptIterationGuard iterationGuard(*this);
	for (ScriptRuntimeState* runtime : m_scriptExecutionOrder)
	{
		CGameScript* instance = runtime ? runtime->Instance : nullptr;
		if (nullptr == instance)
		{
			continue;
		}

		const CGameObject* owner = instance->GetOwner().TryGet();
		if (nullptr == owner)
		{
			continue;
		}

		// 승계 레이어 소속만 — 선형 검색이지만 전환 1회당 (스크립트 수 × 레이어 수)이고
		// 레이어는 몇 개뿐이다. 프레임 경로가 아니라 전환 경로다.
		const CGameLayer* layer = owner->GetLayer().TryGet();
		const bool isInherited = std::any_of(
			inheritedLayers.begin(),
			inheritedLayers.end(),
			[layer](const SafePtr<CGameLayer>& inherited) { return nullptr != layer && inherited.TryGet() == layer; });
		if (isInherited)
		{
			instance->CanvasChanged(*this);
		}
	}
}

CPhysics2DSystem* CGameCanvas::GetPhysics2DSystem()
{
	return m_physicsSystem.Get();
}

const CPhysics2DSystem* CGameCanvas::GetPhysics2DSystem() const
{
	return m_physicsSystem.Get();
}

void CGameCanvas::SetReferencedAssets(std::vector<AssetGuid> referencedAssets)
{
	m_referencedAssets = std::move(referencedAssets);
}

const std::vector<AssetGuid>& CGameCanvas::GetReferencedAssets() const
{
	return m_referencedAssets;
}

void CGameCanvas::ClearObjects()
{
	DestroyScriptInstances();

	m_pendingDestroyComponents.Clear();
	m_pendingDestroyObjects.Clear();

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
	m_freeScriptRuntimeStateIndices.clear();
	m_scriptRuntimeByComponent.clear();
	m_scriptExecutionOrder.clear();
	m_scriptRangesByObject.clear();
	m_scriptExecutionOrderDirty = true;
	m_objectByGuid.Clear();
	m_objectPool.Clear();
	m_referencedAssets.clear();
	// 레이어·뷰포트는 오브젝트 정리 뒤 마지막에 — 직렬화 로드가 파일 기준으로 재구성한다.
	m_pendingDestroyLayers.Clear();
	// 레이어를 통째로 버리므로(파괴 flush 를 거치지 않는다) 런타임 로드 에셋 보유도 함께 놓는다 —
	// 안 그러면 사라진 레이어의 에셋이 고아로 남아 use-count 가 떨어지지 않는다.
	m_runtimeLayerAssets.clear();
	m_layers.clear();
	m_viewports.clear();
}

void CGameCanvas::ClearViewports()
{
	m_viewports.clear();
}

void CGameCanvas::Clear()
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
