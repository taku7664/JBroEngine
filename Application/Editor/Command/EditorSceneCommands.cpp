#include "pch.h"
#include "EditorSceneCommands.h"

#if JBRO_PLATFORM_WINDOWS && JBRO_EDITOR

#include "Engine/Core/Core.h"
#include "Engine/Core/Logging/LoggerInternal.h"
#include "Engine/GameFramework/Component/Transform2D.h"
#include "Engine/GameFramework/Object/GameObject.h"
#include "Engine/GameFramework/Reflection/ReflectionRegistry.h"
#include "Engine/GameFramework/Scene/Scene.h"
#include "Engine/GameFramework/Scene/SceneTransformUtils.h"

#include <cmath>

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
	if (false == m_scene.IsValid() || false == Core::Reflection.IsValid() || false == m_scene->IsAlive(m_entity))
	{
		return false;
	}

	CReflectionRegistry& reflection = *Core::Reflection;
	const ComponentTypeInfo* typeInfo = reflection.FindComponent(m_componentTypeId);
	if (nullptr == typeInfo)
	{
		return false;
	}

	if (typeInfo->AllowDuplicates)
	{
		// Always create a fresh instance; track the pointer for targeted undo.
		m_added = reflection.AddNewComponent(*m_scene, m_entity, m_componentTypeId);
		if (m_added)
		{
			std::vector<void*> all;
			reflection.GetAllComponentAddresses(*m_scene, m_entity, m_componentTypeId, all);
			m_addedComponent = all.empty() ? nullptr : all.back();
		}
	}
	else
	{
		if (reflection.HasComponent(*m_scene, m_entity, m_componentTypeId))
		{
			return false;
		}
		m_added = reflection.AddComponent(*m_scene, m_entity, m_componentTypeId);
		m_addedComponent = nullptr;
	}

	if (false == m_added)
	{
		CSystemLog::Warning("Failed to add reflected component.");
	}
	return m_added;
}

void CAddComponentCommand::Undo()
{
	if (false == m_added || false == m_scene.IsValid() || false == Core::Reflection.IsValid() || false == m_scene->IsAlive(m_entity))
	{
		return;
	}

	if (m_addedComponent)
	{
		Core::Reflection->RemoveSpecificComponent(*m_scene, m_entity, m_componentTypeId, m_addedComponent);
		m_addedComponent = nullptr;
	}
	else
	{
		Core::Reflection->RemoveComponent(*m_scene, m_entity, m_componentTypeId);
	}
	m_added = false;
}

void CAddComponentCommand::Redo()
{
	if (false == m_added && m_scene && Core::Reflection && m_scene->IsAlive(m_entity))
	{
		Execute();
	}
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

	CGameObject gameObject = m_scene->CreateGameObject(m_name.c_str());
	m_entity  = gameObject.GetEntityId();
	m_created = gameObject.IsValid();

	// parent 지정 시 자식으로 등록 (새 오브젝트는 항등 Transform이므로 WorldStay 불필요)
	if (m_created && m_parent != INVALID_ENTITY_ID && m_scene->IsAlive(m_parent))
	{
		m_scene->SetParent(m_entity, m_parent);
	}

	return m_created;
}

void CCreateGameObjectCommand::Undo()
{
	if (m_created && m_scene && m_scene->IsAlive(m_entity))
	{
		m_scene->DestroyEntity(m_entity);
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
	if (value.empty() || false == m_scene.IsValid() || false == Core::Reflection.IsValid() || false == m_scene->IsAlive(m_entity))
	{
		return false;
	}

	void* component = nullptr;
	if (m_instanceIndex == 0)
	{
		component = Core::Reflection->GetComponentAddress(*m_scene, m_entity, m_componentTypeId);
	}
	else
	{
		std::vector<void*> all;
		Core::Reflection->GetAllComponentAddresses(*m_scene, m_entity, m_componentTypeId, all);
		component = m_instanceIndex < all.size() ? all[m_instanceIndex] : nullptr;
	}

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
	// World matrix → Transform2D 분해.
	// 전제: scale > 0, shear 없음 (일반적인 2D 게임 오브젝트 조건).
	// M = Scale * Rotation * Translation 구조:
	//   M11 = sx·cos(r),  M12 = sx·sin(r)
	//   M21 = -sy·sin(r), M22 = sy·cos(r)
	//   Dx = tx,          Dy = ty
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
	// 생성 시점에 현재 부모와 로컬 Transform 스냅샷 — Undo 복원용
	if (m_scene.IsValid() && m_scene->IsAlive(child))
	{
		m_oldParent = m_scene->GetParent(child);
		const Transform2D* t = m_scene->GetComponent<Transform2D>(child);
		if (t)
		{
			m_oldLocalTransform = *t;
		}
	}
}

const char* CSetParentCommand::GetName() const
{
	return "Set Parent";
}

bool CSetParentCommand::Execute()
{
	if (!m_scene.IsValid() || !m_scene->IsAlive(m_child))
	{
		return false;
	}
	CScene& scene = *m_scene;

	// ── WorldStay ────────────────────────────────────────────────────────────
	// SetParent 이전에 world transform 을 캡처한다.
	// SetParent 이후에는 캐시(WorldTransform2D)가 갱신되지 않아 잘못된 값을 반환할 수 있다.
	const Matrix3x2 childWorld     = GetWorldTransform(scene, m_child);
	const Matrix3x2 newParentWorld = (m_newParent != INVALID_ENTITY_ID)
	                                 ? GetWorldTransform(scene, m_newParent)
	                                 : Matrix3x2::Identity();

	// ── 부모 관계 변경 ────────────────────────────────────────────────────────
	if (!scene.SetParent(m_child, m_newParent))
	{
		// 자기 자신 또는 사이클 등 CScene 내부에서 거부
		return false;
	}

	// ── 새 로컬 Transform 계산 ────────────────────────────────────────────────
	// childWorld = newLocal × newParentWorld
	// → newLocal = childWorld × inverse(newParentWorld)
	Matrix3x2 newLocal;
	if (m_newParent != INVALID_ENTITY_ID)
	{
		Matrix3x2 parentInv;
		if (newParentWorld.TryInvert(parentInv))
		{
			newLocal = childWorld * parentInv;
		}
		else
		{
			// 부모 행렬이 특이행렬(scale=0 등) — 안전 폴백: 월드 위치 그대로
			newLocal = childWorld;
		}
	}
	else
	{
		// 루트로 이동: 로컬 = 월드
		newLocal = childWorld;
	}

	m_newLocalTransform = DecomposeMatrix(newLocal);

	// ── Transform2D 기록 ──────────────────────────────────────────────────────
	Transform2D* transform = scene.GetComponent<Transform2D>(m_child);
	if (transform)
	{
		*transform = m_newLocalTransform;
	}

	m_executed = true;
	return true;
}

void CSetParentCommand::Undo()
{
	if (!m_executed || !m_scene.IsValid() || !m_scene->IsAlive(m_child))
	{
		return;
	}
	CScene& scene = *m_scene;

	// 부모 관계 복원 (WorldStay 없이 — 구 로컬 Transform 을 그대로 쓴다)
	scene.SetParent(m_child, m_oldParent);

	Transform2D* transform = scene.GetComponent<Transform2D>(m_child);
	if (transform)
	{
		*transform = m_oldLocalTransform;
	}

	m_executed = false;
}

void CSetParentCommand::Redo()
{
	if (m_executed || !m_scene.IsValid() || !m_scene->IsAlive(m_child))
	{
		return;
	}
	CScene& scene = *m_scene;

	scene.SetParent(m_child, m_newParent);

	Transform2D* transform = scene.GetComponent<Transform2D>(m_child);
	if (transform)
	{
		*transform = m_newLocalTransform;
	}

	m_executed = true;
}

#endif
