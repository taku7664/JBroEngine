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

#include <cmath>

namespace
{
	// 명령은 오브젝트를 불투명 id(EntityId == CGameObject::GetId)로 보관한다.
	// 실제 조작 시점에 활성 씬에서 다시 해석한다(파괴/리로드 후에도 안전한 모델).
	CGameObject* Resolve(const SafePtr<CScene>& scene, EntityId id)
	{
		return scene.IsValid() ? scene->FindObjectById(id) : nullptr;
	}
}

CAddComponentCommand::CAddComponentCommand(SafePtr<CScene> scene, EntityId entity, TypeId componentTypeId)
	: m_scene(scene)
	, m_entity(entity)
	, m_componentTypeId(componentTypeId)
{
}

const char* CAddComponentCommand::GetName() const
{
	return "Add Component";
}

bool CAddComponentCommand::Execute()
{
	CGameObject* object = Resolve(m_scene, m_entity);
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
	m_addedComponent = nullptr;

	if (false == m_added)
	{
		CSystemLog::Warning("Failed to add reflected component.");
	}
	return m_added;
}

void CAddComponentCommand::Undo()
{
	CGameObject* object = Resolve(m_scene, m_entity);
	if (false == m_added || nullptr == object || false == Core::Reflection.IsValid())
	{
		return;
	}

	Core::Reflection->RemoveComponent(*m_scene, *object, m_componentTypeId);
	m_addedComponent = nullptr;
	m_added = false;
}

void CAddComponentCommand::Redo()
{
	if (false == m_added)
	{
		Execute();
	}
}

CAddScriptComponentCommand::CAddScriptComponentCommand(SafePtr<CScene> scene, EntityId entity, TypeId scriptTypeId)
	: m_scene(scene)
	, m_entity(entity)
	, m_scriptTypeId(scriptTypeId)
{
}

const char* CAddScriptComponentCommand::GetName() const
{
	return "Add Script Component";
}

bool CAddScriptComponentCommand::Execute()
{
	CGameObject* object = Resolve(m_scene, m_entity);
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
	CGameObject* object = Resolve(m_scene, m_entity);
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

CSetScriptTypeCommand::CSetScriptTypeCommand(SafePtr<CScene> scene, EntityId entity, std::size_t instanceIndex, TypeId newScriptTypeId)
	: m_scene(scene)
	, m_entity(entity)
	, m_instanceIndex(instanceIndex)
	, m_newScriptTypeId(newScriptTypeId)
{
	CGameObject* object = Resolve(m_scene, m_entity);
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
	CGameObject* object = Resolve(m_scene, m_entity);
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
                                                   EntityId parent)
	: m_scene(scene)
	, m_name(name ? name : "GameObject")
	, m_parent(parent)
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
	m_entity  = m_created ? gameObject->GetId() : INVALID_ENTITY_ID;

	// parent 지정 시 자식으로 등록.
	if (m_created && m_parent != INVALID_ENTITY_ID)
	{
		if (CGameObject* parent = m_scene->FindObjectById(m_parent))
		{
			gameObject->SetParent(*parent);
		}
	}

	return m_created;
}

void CCreateGameObjectCommand::Undo()
{
	CGameObject* object = Resolve(m_scene, m_entity);
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

EntityId CCreateGameObjectCommand::GetEntity() const
{
	return m_entity;
}

CSetComponentPropertyCommand::CSetComponentPropertyCommand(
	SafePtr<CScene> scene,
	EntityId entity,
	TypeId componentTypeId,
	std::size_t propertyOffset,
	std::vector<std::uint8_t> oldValue,
	std::vector<std::uint8_t> newValue,
	std::size_t instanceIndex)
	: m_scene(scene)
	, m_entity(entity)
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

bool CSetComponentPropertyCommand::WriteValue(const std::vector<std::uint8_t>& value)
{
	CGameObject* object = Resolve(m_scene, m_entity);
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

CSetParentCommand::CSetParentCommand(SafePtr<CScene> scene, EntityId child, EntityId newParent)
	: m_scene(scene)
	, m_child(child)
	, m_newParent(newParent)
{
	// 생성 시점에 현재 부모와 로컬 Transform 스냅샷 — Undo 복원용.
	if (CGameObject* childObject = Resolve(m_scene, child))
	{
		CGameObject* oldParent = childObject->GetParent().TryGet();
		m_oldParent = oldParent ? oldParent->GetId() : INVALID_ENTITY_ID;
		m_oldLocalTransform = childObject->GetTransform();
	}
}

const char* CSetParentCommand::GetName() const
{
	return "Set Parent";
}

namespace
{
	// child 의 부모를 newParent(0 이면 루트)로 설정. 성공 시 true.
	bool ApplyParent(CScene& scene, CGameObject& child, EntityId newParent)
	{
		if (INVALID_ENTITY_ID == newParent)
		{
			child.ClearParent();
			return true;
		}
		CGameObject* parent = scene.FindObjectById(newParent);
		return parent ? child.SetParent(*parent) : false;
	}
}

bool CSetParentCommand::Execute()
{
	CGameObject* childObject = Resolve(m_scene, m_child);
	if (nullptr == childObject)
	{
		return false;
	}
	CScene& scene = *m_scene;

	// ── WorldStay: SetParent 이전 world transform 캡처 ───────────────────────
	const Matrix3x2 childWorld = GetWorldTransform(*childObject);
	CGameObject* newParentObject = (m_newParent != INVALID_ENTITY_ID) ? scene.FindObjectById(m_newParent) : nullptr;
	const Matrix3x2 newParentWorld = newParentObject ? GetWorldTransform(*newParentObject) : Matrix3x2::Identity();

	if (false == ApplyParent(scene, *childObject, m_newParent))
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
	CGameObject* childObject = Resolve(m_scene, m_child);
	if (false == m_executed || nullptr == childObject)
	{
		return;
	}

	ApplyParent(*m_scene, *childObject, m_oldParent);
	childObject->GetTransform() = m_oldLocalTransform;
	m_executed = false;
}

void CSetParentCommand::Redo()
{
	CGameObject* childObject = Resolve(m_scene, m_child);
	if (m_executed || nullptr == childObject)
	{
		return;
	}

	ApplyParent(*m_scene, *childObject, m_newParent);
	childObject->GetTransform() = m_newLocalTransform;
	m_executed = true;
}

// ── CModifyPolygonVerticesCommand ─────────────────────────────────────────────

CModifyPolygonVerticesCommand::CModifyPolygonVerticesCommand(
	SafePtr<CScene>             scene,
	EntityId                    entity,
	std::vector<Vector2> newPoints)
	: m_scene(scene)
	, m_entity(entity)
	, m_newPoints(std::move(newPoints))
{
	// 현재 상태 스냅샷 (Undo 복원용).
	if (CGameObject* object = Resolve(m_scene, entity))
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
	CGameObject* object = Resolve(m_scene, m_entity);
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
