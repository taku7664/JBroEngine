#include "pch.h"
#include "EditorSceneCommands.h"

#if JBRO_PLATFORM_WINDOWS && JBRO_EDITOR

#include "Engine/Core/Core.h"
#include "Engine/Core/Logging/LoggerInternal.h"
#include "Engine/GameFramework/Component/Physics2DComponents.h"
#include "Engine/GameFramework/Component/ScriptComponent.h"
#include "Engine/GameFramework/Component/Transform2D.h"
#include "Engine/GameFramework/Object/GameObject.h"
#include "Engine/GameFramework/Reflection/ReflectionRegistry.h"
#include "Engine/GameFramework/Scene/Scene.h"
#include "Engine/GameFramework/Scene/SceneTransformUtils.h"
#include "Engine/GameFramework/Serialization/ComponentSerializer.h"
#include "Engine/GameFramework/Prefab/PrefabSerializer.h"

#include <cmath>

namespace
{
	// 명령은 오브젝트를 InstanceGuid 로 보관한다(포인터/정수 id 아님).
	// 실제 조작 시점에 활성 씬에서 guid 로 다시 해석한다(파괴→재생성 후에도 안전).
	CGameObject* Resolve(const SafePtr<CScene>& scene, const File::Guid& guid)
	{
		return scene.IsValid() ? scene->FindByInstanceGuid(guid).TryGet() : nullptr;
	}

	File::Guid GuidOf(const CGameObject* object)
	{
		return object ? object->InstanceGuid : File::Guid();
	}
}

CAddComponentCommand::CAddComponentCommand(SafePtr<CScene> scene, CGameObject* object, TypeId componentTypeId)
	: m_scene(scene)
	, m_objectGuid(GuidOf(object))
	, m_componentTypeId(componentTypeId)
{
}

const char* CAddComponentCommand::GetName() const
{
	return "Add Component";
}

bool CAddComponentCommand::Execute()
{
	CGameObject* object = Resolve(m_scene, m_objectGuid);
	if (nullptr == object || false == Core::Reflection.IsValid())
	{
		return false;
	}

	CReflectionRegistry& reflection = *Core::Reflection;
	if (nullptr == reflection.FindComponent(m_componentTypeId))
	{
		return false;
	}

	// 단일 인스턴스: 이미 같은 타입이 붙어 있으면 추가 불가.
	if (reflection.HasComponent(*object, m_componentTypeId))
	{
		return false;
	}
	m_added = reflection.AddComponent(*m_scene, *object, m_componentTypeId);

	if (false == m_added)
	{
		CSystemLog::Warning("Failed to add reflected component.");
	}
	return m_added;
}

void CAddComponentCommand::Undo()
{
	CGameObject* object = Resolve(m_scene, m_objectGuid);
	if (false == m_added || nullptr == object || false == Core::Reflection.IsValid())
	{
		return;
	}

	Core::Reflection->RemoveComponent(*m_scene, *object, m_componentTypeId);
	m_added = false;
}

void CAddComponentCommand::Redo()
{
	if (false == m_added)
	{
		Execute();
	}
}

CAddScriptComponentCommand::CAddScriptComponentCommand(SafePtr<CScene> scene, CGameObject* object, TypeId scriptTypeId)
	: m_scene(scene)
	, m_objectGuid(GuidOf(object))
	, m_scriptTypeId(scriptTypeId)
{
}

const char* CAddScriptComponentCommand::GetName() const
{
	return "Add Script Component";
}

bool CAddScriptComponentCommand::Execute()
{
	CGameObject* object = Resolve(m_scene, m_objectGuid);
	if (nullptr == object || false == Core::Reflection.IsValid())
	{
		return false;
	}

	CReflectionRegistry& reflection = *Core::Reflection;
	if (nullptr == reflection.FindScript(m_scriptTypeId))
	{
		CSystemLog::Warning("Failed to add script component. Script type is not registered.");
		return false;
	}

	const ComponentTypeInfo* scriptComponentType = reflection.FindComponentByName("ScriptComponent");
	if (nullptr == scriptComponentType)
	{
		CSystemLog::Warning("Failed to add script component. ScriptComponent is not registered.");
		return false;
	}

	m_scriptComponentTypeId = scriptComponentType->Type.Id;

	// 단일 인스턴스: 이미 ScriptComponent 가 있으면 그것을 재사용, 없으면 추가.
	if (false == reflection.HasComponent(*object, m_scriptComponentTypeId))
	{
		if (false == reflection.AddComponent(*m_scene, *object, m_scriptComponentTypeId))
		{
			CSystemLog::Warning("Failed to add script component.");
			return false;
		}
	}

	m_addedComponent = reflection.GetComponentAddress(*object, m_scriptComponentTypeId);
	ScriptComponent* scriptComponent = static_cast<ScriptComponent*>(m_addedComponent);
	if (nullptr == scriptComponent)
	{
		CSystemLog::Warning("Failed to resolve added script component.");
		return false;
	}

	scriptComponent->ScriptTypeId = m_scriptTypeId;
	scriptComponent->ResetInstance();
	scriptComponent->PendingFields.clear();
	m_added = true;
	return true;
}

void CAddScriptComponentCommand::Undo()
{
	CGameObject* object = Resolve(m_scene, m_objectGuid);
	if (false == m_added || nullptr == object || false == Core::Reflection.IsValid())
	{
		return;
	}

	if (m_scriptComponentTypeId != INVALID_TYPE_ID)
	{
		Core::Reflection->RemoveComponent(*m_scene, *object, m_scriptComponentTypeId);
	}

	m_addedComponent = nullptr;
	m_added = false;
}

void CAddScriptComponentCommand::Redo()
{
	if (false == m_added)
	{
		Execute();
	}
}

CSetScriptTypeCommand::CSetScriptTypeCommand(SafePtr<CScene> scene, CGameObject* object, std::size_t instanceIndex, TypeId newScriptTypeId)
	: m_scene(scene)
	, m_objectGuid(GuidOf(object))
	, m_instanceIndex(instanceIndex)
	, m_newScriptTypeId(newScriptTypeId)
{
	if (object)
	{
		if (ScriptComponent* scriptComponent = object->GetComponent<ScriptComponent>())
		{
			m_oldScriptTypeId = scriptComponent->ScriptTypeId;
		}
	}
}

const char* CSetScriptTypeCommand::GetName() const
{
	return "Set Script Type";
}

bool CSetScriptTypeCommand::Execute()
{
	if (m_newScriptTypeId != INVALID_TYPE_ID && Core::Reflection && nullptr == Core::Reflection->FindScript(m_newScriptTypeId))
	{
		return false;
	}

	m_executed = Apply(m_newScriptTypeId);
	return m_executed;
}

void CSetScriptTypeCommand::Undo()
{
	if (m_executed)
	{
		Apply(m_oldScriptTypeId);
		m_executed = false;
	}
}

void CSetScriptTypeCommand::Redo()
{
	if (false == m_executed)
	{
		m_executed = Apply(m_newScriptTypeId);
	}
}

bool CSetScriptTypeCommand::Apply(TypeId scriptTypeId)
{
	CGameObject* object = Resolve(m_scene, m_objectGuid);
	if (nullptr == object)
	{
		return false;
	}

	ScriptComponent* scriptComponent = object->GetComponent<ScriptComponent>();
	if (nullptr == scriptComponent)
	{
		return false;
	}

	scriptComponent->ResetInstance();
	scriptComponent->ScriptTypeId = scriptTypeId;
	scriptComponent->PendingFields.clear();
	return true;
}

CCreateGameObjectCommand::CCreateGameObjectCommand(SafePtr<CScene> scene, const char* name,
                                                   CGameObject* parent)
	: m_scene(scene)
	, m_name(name ? name : "GameObject")
	, m_parentGuid(GuidOf(parent))
{
}

const char* CCreateGameObjectCommand::GetName() const
{
	return "Create GameObject";
}

bool CCreateGameObjectCommand::Execute()
{
	if (false == m_scene.IsValid())
	{
		return false;
	}

	CGameObject* gameObject = m_scene->CreateGameObject(m_name.c_str());
	m_created = (nullptr != gameObject);
	if (false == m_created)
	{
		return false;
	}

	// redo(재생성)면 첫 생성 때의 guid 를 강제 복원해 이후 명령이 동일 오브젝트로 해석되게 한다.
	if (false == m_objectGuid.IsNull())
	{
		m_scene->SetObjectInstanceGuid(*gameObject, m_objectGuid);
	}
	else
	{
		m_objectGuid = gameObject->InstanceGuid;
	}

	// parent 지정 시 자식으로 등록.
	if (false == m_parentGuid.IsNull())
	{
		if (CGameObject* parent = Resolve(m_scene, m_parentGuid))
		{
			gameObject->SetParent(*parent);
		}
	}

	return m_created;
}

void CCreateGameObjectCommand::Undo()
{
	CGameObject* object = Resolve(m_scene, m_objectGuid);
	if (m_created && object)
	{
		m_scene->DestroyGameObject(object);
		m_created = false;
	}
}

void CCreateGameObjectCommand::Redo()
{
	if (false == m_created)
	{
		Execute();
	}
}

CGameObject* CCreateGameObjectCommand::GetEntity() const
{
	return Resolve(m_scene, m_objectGuid);
}

CSetComponentPropertyCommand::CSetComponentPropertyCommand(
	SafePtr<CScene> scene,
	CGameObject* object,
	TypeId componentTypeId,
	std::size_t propertyOffset,
	std::vector<std::uint8_t> oldValue,
	std::vector<std::uint8_t> newValue,
	std::size_t instanceIndex)
	: m_scene(scene)
	, m_objectGuid(GuidOf(object))
	, m_componentTypeId(componentTypeId)
	, m_propertyOffset(propertyOffset)
	, m_instanceIndex(instanceIndex)
	, m_oldValue(std::move(oldValue))
	, m_newValue(std::move(newValue))
{
}

const char* CSetComponentPropertyCommand::GetName() const
{
	return "Set Component Property";
}

bool CSetComponentPropertyCommand::Execute()
{
	return WriteValue(m_newValue);
}

void CSetComponentPropertyCommand::Undo()
{
	WriteValue(m_oldValue);
}

void CSetComponentPropertyCommand::Redo()
{
	WriteValue(m_newValue);
}

bool CSetComponentPropertyCommand::TryMerge(const IEditorCommand& newer)
{
	const CSetComponentPropertyCommand* other = dynamic_cast<const CSetComponentPropertyCommand*>(&newer);
	if (nullptr == other)
	{
		return false;
	}
	// 같은 대상(오브젝트·컴포넌트·오프셋·인스턴스)일 때만 병합. 값 크기도 일치해야 함.
	const bool sameTarget =
		(m_objectGuid == other->m_objectGuid)
		&& (m_componentTypeId == other->m_componentTypeId)
		&& (m_propertyOffset == other->m_propertyOffset)
		&& (m_instanceIndex == other->m_instanceIndex)
		&& (m_newValue.size() == other->m_newValue.size());
	if (false == sameTarget)
	{
		return false;
	}
	// old 는 드래그 시작값 유지, new 만 최신값으로 교체 → undo 1개로 시작↔끝 복원.
	m_newValue = other->m_newValue;
	return true;
}

bool CSetComponentPropertyCommand::WriteValue(const std::vector<std::uint8_t>& value)
{
	CGameObject* object = Resolve(m_scene, m_objectGuid);
	if (value.empty() || nullptr == object || false == Core::Reflection.IsValid())
	{
		return false;
	}

	// 단일 인스턴스 — instanceIndex 는 항상 0.
	void* component = Core::Reflection->GetComponentAddress(*object, m_componentTypeId);
	if (nullptr == component)
	{
		return false;
	}

	std::memcpy(static_cast<std::uint8_t*>(component) + m_propertyOffset, value.data(), value.size());
	return true;
}

// ── CDeleteGameObjectCommand ──────────────────────────────────────────────────

CDeleteGameObjectCommand::CDeleteGameObjectCommand(SafePtr<CScene> scene, CGameObject* object)
	: m_scene(scene)
	, m_objectGuid(GuidOf(object))
{
	if (object && m_scene.IsValid())
	{
		m_parentGuid = GuidOf(object->GetParent().TryGet());
		CPrefabSerializer serializer;
		serializer.SerializePrefabToText(*m_scene, object, m_snapshot);
	}
}

const char* CDeleteGameObjectCommand::GetName() const
{
	return "Delete GameObject";
}

bool CDeleteGameObjectCommand::Execute()
{
	CGameObject* object = Resolve(m_scene, m_objectGuid);
	if (nullptr == object)
	{
		return false;
	}
	m_deleted = m_scene->DestroyGameObject(object);
	return m_deleted;
}

void CDeleteGameObjectCommand::Undo()
{
	if (false == m_deleted || false == m_scene.IsValid() || m_snapshot.empty())
	{
		return;
	}

	// 직렬화는 InstanceGuid 를 보존하므로 복원된 오브젝트는 동일 guid → 이후 redo/refs 일관.
	CPrefabSerializer serializer;
	CGameObject* root = nullptr;
	if (EPrefabSerializeResult::Success !=
	    serializer.DeserializePrefabFromText(*m_scene, m_snapshot.c_str(), &root))
	{
		return;
	}

	// 부모 복원(null = 루트).
	if (root && false == m_parentGuid.IsNull())
	{
		if (CGameObject* parent = Resolve(m_scene, m_parentGuid))
		{
			root->SetParent(*parent);
		}
	}
	m_deleted = false;
}

void CDeleteGameObjectCommand::Redo()
{
	if (false == m_deleted)
	{
		Execute();
	}
}

// ── CRemoveComponentCommand ───────────────────────────────────────────────────

CRemoveComponentCommand::CRemoveComponentCommand(SafePtr<CScene> scene, CGameObject* object, TypeId componentTypeId)
	: m_scene(scene)
	, m_objectGuid(GuidOf(object))
	, m_componentTypeId(componentTypeId)
{
	// 제거 전 스냅샷 — undo 시 재부착+값 복원.
	if (object && Core::Reflection.IsValid())
	{
		if (void* addr = Core::Reflection->GetComponentAddress(*object, m_componentTypeId))
		{
			// 컴포넌트는 CComponent 를 단일 1차 베이스(offset 0)로 상속 → reinterpret 안전.
			m_snapshot = Serialization::SerializeComponent(*reinterpret_cast<CComponent*>(addr));
		}
	}
}

const char* CRemoveComponentCommand::GetName() const
{
	return "Remove Component";
}

bool CRemoveComponentCommand::RemoveNow()
{
	CGameObject* object = Resolve(m_scene, m_objectGuid);
	if (nullptr == object || false == Core::Reflection.IsValid())
	{
		return false;
	}
	return Core::Reflection->RemoveComponent(*m_scene, *object, m_componentTypeId);
}

bool CRemoveComponentCommand::Execute()
{
	m_removed = RemoveNow();
	return m_removed;
}

void CRemoveComponentCommand::Undo()
{
	CGameObject* object = Resolve(m_scene, m_objectGuid);
	if (false == m_removed || nullptr == object || m_snapshot.empty())
	{
		return;
	}
	Serialization::DeserializeComponentInto(*object, m_snapshot.c_str());
	m_removed = false;
}

void CRemoveComponentCommand::Redo()
{
	if (false == m_removed)
	{
		m_removed = RemoveNow();
	}
}

// ── CSetObjectTransformCommand ────────────────────────────────────────────────

CSetObjectTransformCommand::CSetObjectTransformCommand(SafePtr<CScene> scene,
                                                       const std::vector<CGameObject*>& objects,
                                                       const Transform2D& delta)
	: m_scene(scene)
	, m_delta(delta)
{
	// 각 대상의 현재 Transform 을 시작값으로 캡처(병렬). guid 로 보관해 파괴/재생성에도 안전.
	for (CGameObject* obj : objects)
	{
		if (nullptr == obj)
		{
			continue;
		}
		m_objectGuids.push_back(obj->InstanceGuid);
		m_oldTransforms.push_back(obj->GetTransform());
	}
}

const char* CSetObjectTransformCommand::GetName() const
{
	return "Set Transform";
}

void CSetObjectTransformCommand::Apply(bool withDelta)
{
	for (std::size_t i = 0; i < m_objectGuids.size(); ++i)
	{
		CGameObject* object = Resolve(m_scene, m_objectGuids[i]);
		if (nullptr == object)
		{
			continue;
		}
		Transform2D r = m_oldTransforms[i];
		if (withDelta)
		{
			r.Position.x          += m_delta.Position.x;
			r.Position.y          += m_delta.Position.y;
			r.RotationRadians.Value += m_delta.RotationRadians.Value;
			r.Scale.x             += m_delta.Scale.x;
			r.Scale.y             += m_delta.Scale.y;
		}
		object->GetTransform() = r;
	}
}

bool CSetObjectTransformCommand::Execute() { Apply(true);  return false == m_objectGuids.empty(); }
void CSetObjectTransformCommand::Undo()    { Apply(false); }
void CSetObjectTransformCommand::Redo()    { Apply(true); }

bool CSetObjectTransformCommand::TryMerge(const IEditorCommand& newer)
{
	const CSetObjectTransformCommand* other = dynamic_cast<const CSetObjectTransformCommand*>(&newer);
	if (nullptr == other || m_objectGuids.size() != other->m_objectGuids.size())
	{
		return false;
	}
	for (std::size_t i = 0; i < m_objectGuids.size(); ++i)
	{
		if (false == (m_objectGuids[i] == other->m_objectGuids[i]))
		{
			return false;
		}
	}
	// 같은 대상 집합 → 델타만 누적(시작값 유지) → undo 1개로 시작↔끝.
	m_delta.Position.x          += other->m_delta.Position.x;
	m_delta.Position.y          += other->m_delta.Position.y;
	m_delta.RotationRadians.Value += other->m_delta.RotationRadians.Value;
	m_delta.Scale.x             += other->m_delta.Scale.x;
	m_delta.Scale.y             += other->m_delta.Scale.y;
	return true;
}

// ── CSetParentCommand ─────────────────────────────────────────────────────────

namespace
{
	// World matrix → Transform2D 분해. 전제: scale > 0, shear 없음.
	Transform2D DecomposeMatrix(const Matrix3x2& m)
	{
		Transform2D t;
		t.Position.x       = m.Dx;
		t.Position.y       = m.Dy;
		t.Scale.x          = std::sqrt(m.M11 * m.M11 + m.M12 * m.M12);
		t.Scale.y          = std::sqrt(m.M21 * m.M21 + m.M22 * m.M22);
		t.RotationRadians  = std::atan2(m.M12, m.M11);
		return t;
	}
} // anonymous namespace

CSetParentCommand::CSetParentCommand(SafePtr<CScene> scene, CGameObject* child, CGameObject* newParent)
	: m_scene(scene)
	, m_childGuid(GuidOf(child))
	, m_newParentGuid(GuidOf(newParent))
{
	// 생성 시점에 현재 부모와 로컬 Transform 스냅샷 — Undo 복원용.
	if (child)
	{
		CGameObject* oldParent = child->GetParent().TryGet();
		m_oldParentGuid = GuidOf(oldParent);
		m_oldLocalTransform = child->GetTransform();
	}
}

const char* CSetParentCommand::GetName() const
{
	return "Set Parent";
}

namespace
{
	// child 의 부모를 newParent(null guid 면 루트)로 설정. 성공 시 true.
	bool ApplyParent(CScene& scene, CGameObject& child, const File::Guid& newParentGuid)
	{
		if (newParentGuid.IsNull())
		{
			child.ClearParent();
			return true;
		}
		CGameObject* parent = scene.FindByInstanceGuid(newParentGuid).TryGet();
		return parent ? child.SetParent(*parent) : false;
	}
}

bool CSetParentCommand::Execute()
{
	CGameObject* childObject = Resolve(m_scene, m_childGuid);
	if (nullptr == childObject)
	{
		return false;
	}
	CScene& scene = *m_scene;

	// ── WorldStay: SetParent 이전 world transform 캡처 ───────────────────────
	const Matrix3x2 childWorld = GetWorldTransform(*childObject);
	CGameObject* newParentObject = m_newParentGuid.IsNull() ? nullptr : scene.FindByInstanceGuid(m_newParentGuid).TryGet();
	const Matrix3x2 newParentWorld = newParentObject ? GetWorldTransform(*newParentObject) : Matrix3x2::Identity();

	if (false == ApplyParent(scene, *childObject, m_newParentGuid))
	{
		return false;   // 사이클/자기자신 등 거부
	}

	// childWorld = newLocal × newParentWorld → newLocal = childWorld × inverse(newParentWorld)
	Matrix3x2 newLocal = childWorld;
	if (newParentObject)
	{
		Matrix3x2 parentInv;
		if (newParentWorld.TryInvert(parentInv))
		{
			newLocal = childWorld * parentInv;
		}
	}

	m_newLocalTransform = DecomposeMatrix(newLocal);
	childObject->GetTransform() = m_newLocalTransform;
	m_executed = true;
	return true;
}

void CSetParentCommand::Undo()
{
	CGameObject* childObject = Resolve(m_scene, m_childGuid);
	if (false == m_executed || nullptr == childObject)
	{
		return;
	}

	ApplyParent(*m_scene, *childObject, m_oldParentGuid);
	childObject->GetTransform() = m_oldLocalTransform;
	m_executed = false;
}

void CSetParentCommand::Redo()
{
	CGameObject* childObject = Resolve(m_scene, m_childGuid);
	if (m_executed || nullptr == childObject)
	{
		return;
	}

	ApplyParent(*m_scene, *childObject, m_newParentGuid);
	childObject->GetTransform() = m_newLocalTransform;
	m_executed = true;
}

// ── CModifyPolygonVerticesCommand ─────────────────────────────────────────────

CModifyPolygonVerticesCommand::CModifyPolygonVerticesCommand(
	SafePtr<CScene>             scene,
	CGameObject*                object,
	std::vector<Vector2> newPoints)
	: m_scene(scene)
	, m_objectGuid(GuidOf(object))
	, m_newPoints(std::move(newPoints))
{
	// 현재 상태 스냅샷 (Undo 복원용).
	if (object)
	{
		if (const PolygonCollider2D* poly = object->GetComponent<PolygonCollider2D>())
		{
			m_oldPoints = poly->LocalPoints;
			if (m_oldPoints.empty())
			{
				poly->BuildLocalPoints(m_oldPoints);
			}
		}
	}
}

const char* CModifyPolygonVerticesCommand::GetName() const
{
	return "Modify Polygon Vertices";
}

bool CModifyPolygonVerticesCommand::Execute()
{
	m_executed = Apply(m_newPoints);
	return m_executed;
}

void CModifyPolygonVerticesCommand::Undo()
{
	if (m_executed)
		Apply(m_oldPoints);
}

void CModifyPolygonVerticesCommand::Redo()
{
	if (!m_executed)
		m_executed = Apply(m_newPoints);
}

bool CModifyPolygonVerticesCommand::Apply(
	const std::vector<Vector2>& points)
{
	CGameObject* object = Resolve(m_scene, m_objectGuid);
	if (nullptr == object)
		return false;

	PolygonCollider2D* poly = object->GetComponent<PolygonCollider2D>();
	if (!poly)
		return false;

	poly->LocalPoints = points;
	poly->MarkProceduralBuilt();
	poly->m_convexDirty = true;
	return true;
}

#endif
