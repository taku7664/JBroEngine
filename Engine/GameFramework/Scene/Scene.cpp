#include "pch.h"
#include "Scene.h"

#include "GameFramework/Component/Transform2D.h"
#include "GameFramework/Component/Camera2D.h"
#include "GameFramework/Component/Light2D.h"
#include "GameFramework/Component/Physics2DComponents.h"
#include "GameFramework/Component/PrefabInstance.h"
#include "GameFramework/Component/SpriteRenderer2D.h"
#include "GameFramework/Component/TransformHierarchy2D.h"
#include "GameFramework/Object/GameObject.h"
#include "GameFramework/Component/GameObject.h"
#include "GameFramework/Physics2D/Physics2DSystem.h"
#include "GameFramework/Scene/SceneSnapshot.h"
#include "GameFramework/Scripting/ScriptSystem.h"

CScene::CScene()
{
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

CScene::~CScene()
{
	Clear();
}

EntityId CScene::CreateEntity()
{
	return m_entityManager.CreateEntity();
}

bool CScene::DestroyEntity(EntityId entity)
{
	if (false == IsAlive(entity))
	{
		return false;
	}

	DestroyEntityHierarchy(entity);
	if (false == m_entityManager.DestroyEntity(entity))
	{
		return false;
	}

	for (auto& pair : m_componentPools)
	{
		if (pair.second)
		{
			pair.second->RemoveEntity(entity);
		}
	}

	return true;
}

bool CScene::IsAlive(EntityId entity) const
{
	return m_entityManager.IsAlive(entity);
}

std::size_t CScene::GetAliveEntityCount() const
{
	return m_entityManager.GetAliveCount();
}

CGameObject CScene::CreateGameObject(const char* name)
{
	const EntityId entity = CreateEntity();
	AddComponent<GameObject>(entity, name);
	AddComponent<Transform2D>(entity);
	AddComponent<TransformHierarchy2D>(entity);
	return CGameObject(*this, entity);
}

bool CScene::DestroyGameObject(const CGameObject& gameObject)
{
	if (gameObject.GetScene() != this)
	{
		return false;
	}

	return DestroyEntity(gameObject.GetEntityId());
}

void CScene::BuildSnapshot(SceneSnapshot& snapshot) const
{
	snapshot.Clear();

	ForEach<GameObject>(
		[this, &snapshot](EntityId entity, const GameObject& gameObject)
		{
			SceneObjectSnapshot object;
			object.Entity = entity;
			gameObject.CopyNameTo(object.Name, GameObject::MAX_NAME_LENGTH + 1);
			object.IsActive = gameObject.IsActive;
			object.Layer = gameObject.Layer;
			object.Parent = GetParent(entity);

			if (const Transform2D* transform = GetComponent<Transform2D>(entity))
			{
				object.HasTransform = true;
				object.Transform = *transform;
			}

			if (const SpriteRenderer2D* sprite = GetComponent<SpriteRenderer2D>(entity))
			{
				object.HasSpriteRenderer = true;
				object.SpriteGuid = sprite->SpriteGuid;
				object.MaterialGuid = sprite->MaterialGuid;
				object.Color[0] = sprite->Color[0];
				object.Color[1] = sprite->Color[1];
				object.Color[2] = sprite->Color[2];
				object.Color[3] = sprite->Color[3];
				object.SortOrder = sprite->SortOrder;
				object.LayerMask = sprite->LayerMask;
			}

			if (const Camera2D* camera = GetComponent<Camera2D>(entity))
			{
				object.HasCamera = true;
				object.Camera = *camera;
			}

			if (const Light2D* light = GetComponent<Light2D>(entity))
			{
				object.HasLight = true;
				object.Light = *light;
			}

			if (const Rigidbody2D* rigidbody = GetComponent<Rigidbody2D>(entity))
			{
				object.HasRigidbody = true;
				object.Rigidbody = *rigidbody;
			}

			if (const PolygonCollider2D* polygonCollider = GetComponent<PolygonCollider2D>(entity))
			{
				object.HasPolygonCollider = true;
				object.PolygonCollider = *polygonCollider;
				object.PolygonCollider.WorldPoints.clear();
			}

			if (const CircleCollider2D* circleCollider = GetComponent<CircleCollider2D>(entity))
			{
				object.HasCircleCollider = true;
				object.CircleCollider = *circleCollider;
			}

			if (const PrefabInstance* prefabInstance = GetComponent<PrefabInstance>(entity))
			{
				object.HasPrefabInstance = true;
				object.SourcePrefabGuid = prefabInstance->SourcePrefabGuid;
			}

			snapshot.Objects.push_back(object);
		});
}

bool CScene::SetParent(EntityId child, EntityId parent)
{
	if (false == IsAlive(child))
	{
		return false;
	}

	if (INVALID_ENTITY_ID == parent)
	{
		return ClearParent(child);
	}

	if (child == parent || false == IsAlive(parent) || IsDescendantOf(parent, child))
	{
		return false;
	}

	if (nullptr == GetComponent<TransformHierarchy2D>(child))
	{
		AddComponent<TransformHierarchy2D>(child);
	}
	if (nullptr == GetComponent<TransformHierarchy2D>(parent))
	{
		AddComponent<TransformHierarchy2D>(parent);
	}

	DetachFromParent(child);
	return AttachToParent(child, parent);
}

bool CScene::ClearParent(EntityId child)
{
	if (false == IsAlive(child))
	{
		return false;
	}

	DetachFromParent(child);
	return true;
}

EntityId CScene::GetParent(EntityId entity) const
{
	const TransformHierarchy2D* hierarchy = GetComponent<TransformHierarchy2D>(entity);
	return hierarchy ? hierarchy->Parent : INVALID_ENTITY_ID;
}

EntityId CScene::GetFirstChild(EntityId entity) const
{
	const TransformHierarchy2D* hierarchy = GetComponent<TransformHierarchy2D>(entity);
	return hierarchy ? hierarchy->FirstChild : INVALID_ENTITY_ID;
}

EntityId CScene::GetNextSibling(EntityId entity) const
{
	const TransformHierarchy2D* hierarchy = GetComponent<TransformHierarchy2D>(entity);
	return hierarchy ? hierarchy->NextSibling : INVALID_ENTITY_ID;
}

EntityId CScene::GetPrevSibling(EntityId entity) const
{
	const TransformHierarchy2D* hierarchy = GetComponent<TransformHierarchy2D>(entity);
	return hierarchy ? hierarchy->PrevSibling : INVALID_ENTITY_ID;
}

bool CScene::IsDescendantOf(EntityId entity, EntityId possibleAncestor) const
{
	EntityId current = GetParent(entity);
	while (INVALID_ENTITY_ID != current)
	{
		if (current == possibleAncestor)
		{
			return true;
		}

		current = GetParent(current);
	}

	return false;
}

void CScene::Update()
{
	UpdateSystems();
	UpdateScripts();
}

void CScene::UpdateSystems()
{
	if (m_physicsSystem)
	{
		m_physicsSystem->Update(*this);
	}

	for (OwnerPtr<CGameSystem>& system : m_systems)
	{
		if (system)
		{
			system->Update(*this);
		}
	}
}

void CScene::UpdateScripts()
{
	if (m_scriptSystem)
	{
		m_scriptSystem->Update(*this);
	}
}

void CScene::Clear()
{
	for (OwnerPtr<CGameSystem>& system : m_systems)
	{
		if (system)
		{
			system->Finalize(*this);
		}
	}

	m_systems.clear();
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

void CScene::ClearObjects()
{
	m_componentPools.clear();
	m_entityManager.Clear();
}

bool CScene::AttachToParent(EntityId child, EntityId parent)
{
	TransformHierarchy2D* childHierarchy = GetComponent<TransformHierarchy2D>(child);
	TransformHierarchy2D* parentHierarchy = GetComponent<TransformHierarchy2D>(parent);
	if (nullptr == childHierarchy || nullptr == parentHierarchy)
	{
		return false;
	}

	const EntityId oldFirstChild = parentHierarchy->FirstChild;
	childHierarchy->Parent = parent;
	childHierarchy->PrevSibling = INVALID_ENTITY_ID;
	childHierarchy->NextSibling = oldFirstChild;
	parentHierarchy->FirstChild = child;

	if (TransformHierarchy2D* oldFirstChildHierarchy = GetComponent<TransformHierarchy2D>(oldFirstChild))
	{
		oldFirstChildHierarchy->PrevSibling = child;
	}

	return true;
}

void CScene::DetachFromParent(EntityId child)
{
	TransformHierarchy2D* childHierarchy = GetComponent<TransformHierarchy2D>(child);
	if (nullptr == childHierarchy || INVALID_ENTITY_ID == childHierarchy->Parent)
	{
		return;
	}

	TransformHierarchy2D* parentHierarchy = GetComponent<TransformHierarchy2D>(childHierarchy->Parent);
	if (parentHierarchy && parentHierarchy->FirstChild == child)
	{
		parentHierarchy->FirstChild = childHierarchy->NextSibling;
	}

	if (TransformHierarchy2D* prevSibling = GetComponent<TransformHierarchy2D>(childHierarchy->PrevSibling))
	{
		prevSibling->NextSibling = childHierarchy->NextSibling;
	}

	if (TransformHierarchy2D* nextSibling = GetComponent<TransformHierarchy2D>(childHierarchy->NextSibling))
	{
		nextSibling->PrevSibling = childHierarchy->PrevSibling;
	}

	childHierarchy->Parent = INVALID_ENTITY_ID;
	childHierarchy->PrevSibling = INVALID_ENTITY_ID;
	childHierarchy->NextSibling = INVALID_ENTITY_ID;
}

void CScene::DestroyEntityHierarchy(EntityId entity)
{
	TransformHierarchy2D* hierarchy = GetComponent<TransformHierarchy2D>(entity);
	while (hierarchy && INVALID_ENTITY_ID != hierarchy->FirstChild)
	{
		DestroyEntity(hierarchy->FirstChild);
		hierarchy = GetComponent<TransformHierarchy2D>(entity);
	}

	DetachFromParent(entity);
}
