#include "pch.h"
#include "PrefabSerializer.h"

#include "GameFramework/Component/Camera2D.h"
#include "GameFramework/Component/GameObject.h"
#include "GameFramework/Component/Light2D.h"
#include "GameFramework/Component/Physics2DComponents.h"
#include "GameFramework/Component/PrefabInstance.h"
#include "GameFramework/Component/SpriteRenderer2D.h"
#include "GameFramework/Component/Transform2D.h"
#include "GameFramework/Object/GameObject.h"
#include "GameFramework/Scene/Scene.h"
#include "GameFramework/Scene/SceneSerializer.h"
#include "GameFramework/Scene/SceneTypes.h"

#include <fstream>
#include <sstream>

EPrefabSerializeResult CPrefabSerializer::SerializePrefabToText(const CScene& scene, EntityId root, std::string& outText) const
{
	if (false == scene.IsAlive(root))
	{
		return EPrefabSerializeResult::InvalidArgument;
	}

	CScene prefabScene;
	CloneHierarchy(scene, prefabScene, root);

	CSceneSerializer serializer;
	return ConvertSceneResult(static_cast<int>(serializer.SerializeToText(prefabScene, outText)));
}

EPrefabSerializeResult CPrefabSerializer::DeserializePrefabFromText(CScene& scene, const char* text, CGameObject* outRoot) const
{
	if (nullptr == text)
	{
		return EPrefabSerializeResult::InvalidArgument;
	}

	CScene prefabScene;
	CSceneSerializer serializer;
	const ESceneSerializeResult sceneResult = serializer.DeserializeFromText(prefabScene, text);
	if (ESceneSerializeResult::Success != sceneResult)
	{
		return ConvertSceneResult(static_cast<int>(sceneResult));
	}

	CGameObject firstRoot;
	prefabScene.ForEach<GameObject>(
		[&](EntityId entity, const GameObject&)
		{
			if (INVALID_ENTITY_ID != prefabScene.GetParent(entity))
			{
				return;
			}

			CGameObject clonedRoot = CloneHierarchy(prefabScene, scene, entity);
			if (false == firstRoot.IsValid())
			{
				firstRoot = clonedRoot;
			}
		});

	if (outRoot)
	{
		*outRoot = firstRoot;
	}

	return firstRoot.IsValid() ? EPrefabSerializeResult::Success : EPrefabSerializeResult::ParseError;
}

EPrefabSerializeResult CPrefabSerializer::SavePrefabToFile(const CScene& scene, EntityId root, const char* path) const
{
	if (nullptr == path)
	{
		return EPrefabSerializeResult::InvalidArgument;
	}

	std::string text;
	const EPrefabSerializeResult serializeResult = SerializePrefabToText(scene, root, text);
	if (EPrefabSerializeResult::Success != serializeResult)
	{
		return serializeResult;
	}

	std::ofstream file(path, std::ios::out | std::ios::trunc);
	if (false == file.is_open())
	{
		return EPrefabSerializeResult::IoError;
	}

	file << text;
	return EPrefabSerializeResult::Success;
}

EPrefabSerializeResult CPrefabSerializer::LoadPrefabFromFile(CScene& scene, const char* path, CGameObject* outRoot) const
{
	if (nullptr == path)
	{
		return EPrefabSerializeResult::InvalidArgument;
	}

	std::ifstream file(path);
	if (false == file.is_open())
	{
		return EPrefabSerializeResult::IoError;
	}

	std::stringstream buffer;
	buffer << file.rdbuf();
	return DeserializePrefabFromText(scene, buffer.str().c_str(), outRoot);
}

CGameObject CPrefabSerializer::CloneHierarchy(const CScene& sourceScene, CScene& targetScene, EntityId sourceEntity)
{
	const GameObject* sourceGameObject = sourceScene.GetComponent<GameObject>(sourceEntity);
	CGameObject targetObject = targetScene.CreateGameObject(sourceGameObject ? sourceGameObject->Name : nullptr);
	if (false == targetObject.IsValid())
	{
		return targetObject;
	}

	if (sourceGameObject)
	{
		targetObject.SetActive(sourceGameObject->IsActive);
		targetObject.SetLayer(sourceGameObject->Layer);
	}

	CopyComponents(sourceScene, sourceEntity, targetObject);

	EntityId child = sourceScene.GetFirstChild(sourceEntity);
	while (INVALID_ENTITY_ID != child)
	{
		const EntityId nextSibling = sourceScene.GetNextSibling(child);
		CGameObject clonedChild = CloneHierarchy(sourceScene, targetScene, child);
		if (clonedChild.IsValid())
		{
			clonedChild.SetParent(targetObject);
		}
		child = nextSibling;
	}

	return targetObject;
}

void CPrefabSerializer::CopyComponents(const CScene& sourceScene, EntityId sourceEntity, CGameObject& targetObject)
{
	if (const Transform2D* source = sourceScene.GetComponent<Transform2D>(sourceEntity))
	{
		if (Transform2D* target = targetObject.GetTransform())
		{
			*target = *source;
		}
	}

	if (const SpriteRenderer2D* source = sourceScene.GetComponent<SpriteRenderer2D>(sourceEntity))
	{
		if (SpriteRenderer2D* target = targetObject.AddComponent<SpriteRenderer2D>())
		{
			target->SpriteGuid = source->SpriteGuid;
			target->MaterialGuid = source->MaterialGuid;
			target->Color[0] = source->Color[0];
			target->Color[1] = source->Color[1];
			target->Color[2] = source->Color[2];
			target->Color[3] = source->Color[3];
			target->SortOrder = source->SortOrder;
			target->LayerMask = source->LayerMask;
		}
	}

	if (const Camera2D* source = sourceScene.GetComponent<Camera2D>(sourceEntity))
	{
		if (Camera2D* target = targetObject.AddComponent<Camera2D>())
		{
			*target = *source;
		}
	}

	if (const Light2D* source = sourceScene.GetComponent<Light2D>(sourceEntity))
	{
		if (Light2D* target = targetObject.AddComponent<Light2D>())
		{
			*target = *source;
		}
	}

	if (const Rigidbody2D* source = sourceScene.GetComponent<Rigidbody2D>(sourceEntity))
	{
		if (Rigidbody2D* target = targetObject.AddComponent<Rigidbody2D>())
		{
			*target = *source;
		}
	}

	if (const PolygonCollider2D* source = sourceScene.GetComponent<PolygonCollider2D>(sourceEntity))
	{
		if (PolygonCollider2D* target = targetObject.AddComponent<PolygonCollider2D>())
		{
			*target = *source;
			target->WorldPoints.clear();
		}
	}

	if (const CircleCollider2D* source = sourceScene.GetComponent<CircleCollider2D>(sourceEntity))
	{
		if (CircleCollider2D* target = targetObject.AddComponent<CircleCollider2D>())
		{
			*target = *source;
		}
	}

	if (const PrefabInstance* source = sourceScene.GetComponent<PrefabInstance>(sourceEntity))
	{
		if (PrefabInstance* target = targetObject.AddComponent<PrefabInstance>())
		{
			*target = *source;
		}
	}
}

EPrefabSerializeResult CPrefabSerializer::ConvertSceneResult(int sceneResult)
{
	switch (static_cast<ESceneSerializeResult>(sceneResult))
	{
	case ESceneSerializeResult::Success:
		return EPrefabSerializeResult::Success;
	case ESceneSerializeResult::InvalidArgument:
		return EPrefabSerializeResult::InvalidArgument;
	case ESceneSerializeResult::IoError:
		return EPrefabSerializeResult::IoError;
	case ESceneSerializeResult::ParseError:
	default:
		return EPrefabSerializeResult::ParseError;
	}
}
