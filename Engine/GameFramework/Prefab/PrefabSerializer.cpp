#include "pch.h"
#include "PrefabSerializer.h"

#include "Core/Core.h"
#include "GameFramework/Component/Light2D.h"
#include "GameFramework/Component/Physics2DComponents.h"
#include "GameFramework/Object/GameObject.h"
#include "GameFramework/Reflection/ReflectionRegistry.h"
#include "GameFramework/Scene/Scene.h"
#include "GameFramework/Scene/SceneSerializer.h"
#include "Utillity/File/FilePath.h"

#include <cstring>
#include <fstream>
#include <sstream>

EPrefabSerializeResult CPrefabSerializer::SerializePrefabToText(const CScene& scene, const CGameObject* root, std::string& outText) const
{
	if (nullptr == root)
	{
		return EPrefabSerializeResult::InvalidArgument;
	}

	CScene prefabScene;
	CloneHierarchy(scene, prefabScene, *root);

	CSceneSerializer serializer;
	return ConvertSceneResult(static_cast<int>(serializer.SerializeToText(prefabScene, outText)));
}

EPrefabSerializeResult CPrefabSerializer::DeserializePrefabFromText(CScene& scene, const char* text, CGameObject** outRoot) const
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

	CGameObject* firstRoot = nullptr;
	prefabScene.ForEachObject(
		[&](CGameObject& object)
		{
			if (object.GetParent().IsValid())
			{
				return;   // 루트만 클론(자식은 재귀로).
			}
			CGameObject* clonedRoot = CloneHierarchy(prefabScene, scene, object);
			if (nullptr == firstRoot)
			{
				firstRoot = clonedRoot;
			}
		});

	if (outRoot)
	{
		*outRoot = firstRoot;
	}

	return firstRoot ? EPrefabSerializeResult::Success : EPrefabSerializeResult::ParseError;
}

EPrefabSerializeResult CPrefabSerializer::SavePrefabToFile(const CScene& scene, const CGameObject* root, const File::Path& path) const
{
	if (path.empty())
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

EPrefabSerializeResult CPrefabSerializer::LoadPrefabFromFile(CScene& scene, const File::Path& path, CGameObject** outRoot) const
{
	if (path.empty())
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

CGameObject* CPrefabSerializer::CloneHierarchy(const CScene& /*sourceScene*/, CScene& targetScene, const CGameObject& sourceObject)
{
	CGameObject* targetObject = targetScene.CreateGameObject(sourceObject.GetName());
	if (nullptr == targetObject)
	{
		return nullptr;
	}

	targetObject->SetActive(sourceObject.IsActive);
	targetObject->Tag = sourceObject.Tag;
	targetObject->Flags = sourceObject.Flags;

	CopyComponents(sourceObject, *targetObject);

	for (const SafePtr<CGameObject>& childRef : sourceObject.GetChildren())
	{
		if (const CGameObject* child = childRef.TryGet())
		{
			CGameObject* clonedChild = CloneHierarchy(*targetObject->GetScene(), targetScene, *child);
			if (clonedChild)
			{
				clonedChild->SetParent(*targetObject);
			}
		}
	}

	return targetObject;
}

void CPrefabSerializer::CopyComponents(const CGameObject& sourceObject, CGameObject& targetObject)
{
	// Transform 은 오브젝트 멤버 — 직접 복사.
	targetObject.GetTransform() = sourceObject.GetTransform();

	CScene* targetScene = targetObject.GetScene();
	if (nullptr == targetScene || false == static_cast<bool>(Core::Reflection))
	{
		return;
	}

	for (const SafePtr<CComponent>& cref : sourceObject.GetComponents())
	{
		const CComponent* src = cref.TryGet();
		if (nullptr == src)
		{
			continue;
		}
		const char* name = src->GetTypeName();
		if (0 == std::strcmp(name, "ScriptComponent"))
		{
			continue;   // 프리팹은 스크립트 인스턴스를 복제하지 않는다(기존 동작 유지).
		}

		const ComponentTypeInfo* ti = Core::Reflection->FindComponentByName(name);
		if (nullptr == ti)
		{
			continue;
		}
		if (false == Core::Reflection->AddComponent(*targetScene, targetObject, ti->Type.Id))
		{
			continue;
		}
		CComponent* dst = static_cast<CComponent*>(Core::Reflection->GetComponentAddress(targetObject, ti->Type.Id));
		if (nullptr == dst)
		{
			continue;
		}

		// 등록 프로퍼티만 복사(런타임 캐시/비복사 멤버는 건드리지 않는다).
		// ⚠ `*dst = *src` 는 CComponent 베이스(Owner SafePtr / ControlBlock)까지 복사해 깨지므로 금지.
		for (const ReflectPropertyInfo& prop : ti->Properties)
		{
			const void* sf = static_cast<const char*>(static_cast<const void*>(src)) + prop.Offset;
			void*       df = static_cast<char*>(static_cast<void*>(dst)) + prop.Offset;
			if (EReflectPropertyType::AssetGuid == prop.Type)
			{
				*static_cast<File::Guid*>(df) = *static_cast<const File::Guid*>(sf);
			}
			else if (EReflectPropertyType::String == prop.Type)
			{
				if (prop.ElementCount > 1) std::memcpy(df, sf, prop.Size * prop.ElementCount);
				else *static_cast<std::string*>(df) = *static_cast<const std::string*>(sf);
			}
			else
			{
				std::memcpy(df, sf, prop.Size * (prop.ElementCount ? prop.ElementCount : 1));
			}
		}
		dst->IsEnabled = src->IsEnabled;

		// 비반영 추가 필드(직렬화 예외와 동일).
		if (0 == std::strcmp(name, "Light2D"))
		{
			static_cast<Light2D*>(dst)->InnerAngleRadians = static_cast<const Light2D*>(src)->InnerAngleRadians;
			static_cast<Light2D*>(dst)->OuterAngleRadians = static_cast<const Light2D*>(src)->OuterAngleRadians;
		}
		else if (0 == std::strcmp(name, "PolygonCollider2D"))
		{
			PolygonCollider2D* d = static_cast<PolygonCollider2D*>(dst);
			const PolygonCollider2D* s = static_cast<const PolygonCollider2D*>(src);
			d->LocalPoints = s->LocalPoints;
			d->WorldPoints.clear();
			d->m_convexDirty = true;
		}
		else if (0 == std::strcmp(name, "Rigidbody2D"))
		{
			Rigidbody2D* d = static_cast<Rigidbody2D*>(dst);
			d->SetMass(d->Mass);
			d->SetInertia(d->Inertia);
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
