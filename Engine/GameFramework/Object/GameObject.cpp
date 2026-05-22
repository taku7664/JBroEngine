#include "pch.h"
#include "GameObject.h"

CGameObject::CGameObject(CScene& scene, EntityId entity)
	: m_scene(&scene)
	, m_entity(entity)
{
}

bool CGameObject::IsValid() const
{
	return nullptr != m_scene && m_scene->IsAlive(m_entity);
}

void CGameObject::Destroy()
{
	if (IsValid())
	{
		m_scene->DestroyEntity(m_entity);
	}

	m_scene = nullptr;
	m_entity = INVALID_ENTITY_ID;
}

EntityId CGameObject::GetEntityId() const
{
	return m_entity;
}

CScene* CGameObject::GetScene() const
{
	return m_scene;
}

CGameObject::operator bool() const
{
	return IsValid();
}

const char* CGameObject::GetName() const
{
	const GameObject* gameObject = GetComponent<GameObject>();
	return gameObject ? gameObject->Name : "";
}

void CGameObject::SetName(const char* name)
{
	if (GameObject* gameObject = GetComponent<GameObject>())
	{
		gameObject->SetName(name);
	}
}

bool CGameObject::IsActive() const
{
	const GameObject* gameObject = GetComponent<GameObject>();
	return gameObject ? gameObject->IsActive : false;
}

void CGameObject::SetActive(bool isActive)
{
	if (GameObject* gameObject = GetComponent<GameObject>())
	{
		gameObject->IsActive = isActive;
	}
}

std::uint32_t CGameObject::GetLayer() const
{
	const GameObject* gameObject = GetComponent<GameObject>();
	return gameObject ? gameObject->Layer : 0;
}

void CGameObject::SetLayer(std::uint32_t layer)
{
	if (GameObject* gameObject = GetComponent<GameObject>())
	{
		gameObject->Layer = layer;
	}
}

Transform2D* CGameObject::GetTransform()
{
	return GetComponent<Transform2D>();
}

const Transform2D* CGameObject::GetTransform() const
{
	return GetComponent<Transform2D>();
}

bool CGameObject::SetParent(const CGameObject& parent)
{
	if (false == IsValid() || parent.GetScene() != m_scene)
	{
		return false;
	}

	return m_scene->SetParent(m_entity, parent.GetEntityId());
}

bool CGameObject::ClearParent()
{
	if (false == IsValid())
	{
		return false;
	}

	return m_scene->ClearParent(m_entity);
}

EntityId CGameObject::GetParent() const
{
	return IsValid() ? m_scene->GetParent(m_entity) : INVALID_ENTITY_ID;
}

EntityId CGameObject::GetFirstChild() const
{
	return IsValid() ? m_scene->GetFirstChild(m_entity) : INVALID_ENTITY_ID;
}

EntityId CGameObject::GetNextSibling() const
{
	return IsValid() ? m_scene->GetNextSibling(m_entity) : INVALID_ENTITY_ID;
}

EntityId CGameObject::GetPrevSibling() const
{
	return IsValid() ? m_scene->GetPrevSibling(m_entity) : INVALID_ENTITY_ID;
}
