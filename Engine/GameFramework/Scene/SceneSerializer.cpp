#include "pch.h"
#include "SceneSerializer.h"

#include "GameFramework/Component/Camera2D.h"
#include "GameFramework/Component/Light2D.h"
#include "GameFramework/Component/Physics2DComponents.h"
#include "GameFramework/Component/PrefabInstance.h"
#include "GameFramework/Component/SpriteRenderer2D.h"
#include "GameFramework/Component/Transform2D.h"
#include "GameFramework/Object/GameObject.h"
#include "GameFramework/Component/GameObject.h"
#include "GameFramework/Scene/Scene.h"
#include "GameFramework/Scene/SceneSnapshot.h"

#include <fstream>
#include <sstream>
#include <unordered_map>

ESceneSerializeResult CSceneSerializer::SerializeToText(const CScene& scene, std::string& outText) const
{
	SceneSnapshot snapshot;
	scene.BuildSnapshot(snapshot);

	std::ostringstream stream;
	stream << "JBroScene\t1\n";
	std::unordered_map<EntityId, std::size_t> entityToIndex;
	for (std::size_t i = 0; i < snapshot.Objects.size(); ++i)
	{
		entityToIndex.emplace(snapshot.Objects[i].Entity, i);
	}

	for (const SceneObjectSnapshot& object : snapshot.Objects)
	{
		const auto parentIt = entityToIndex.find(object.Parent);
		const std::int64_t parentIndex = parentIt == entityToIndex.end() ? -1 : static_cast<std::int64_t>(parentIt->second);
		stream << "Object\t" << EscapeField(object.Name) << '\t'
			<< (object.IsActive ? 1 : 0) << '\t'
			<< object.Layer << '\t'
			<< parentIndex << '\n';

		if (object.HasTransform)
		{
			stream << "Transform\t"
				<< object.Transform.Position.x << '\t'
				<< object.Transform.Position.y << '\t'
				<< object.Transform.RotationRadians << '\t'
				<< object.Transform.Scale.x << '\t'
				<< object.Transform.Scale.y << '\n';
		}

		if (object.HasSpriteRenderer)
		{
			stream << "Sprite\t"
				<< object.SpriteGuid.generic_string() << '\t'
				<< object.MaterialGuid.generic_string() << '\t'
				<< object.Color[0] << '\t'
				<< object.Color[1] << '\t'
				<< object.Color[2] << '\t'
				<< object.Color[3] << '\t'
				<< object.SortOrder << '\t'
				<< object.LayerMask << '\n';
		}

		if (object.HasCamera)
		{
			stream << "Camera\t"
				<< static_cast<int>(object.Camera.ProjectionMode) << '\t'
				<< object.Camera.OrthographicSize << '\t'
				<< object.Camera.PerspectiveFovDegrees << '\t'
				<< object.Camera.NearClip << '\t'
				<< object.Camera.FarClip << '\t'
				<< object.Camera.ViewportX << '\t'
				<< object.Camera.ViewportY << '\t'
				<< object.Camera.ViewportWidth << '\t'
				<< object.Camera.ViewportHeight << '\t'
				<< object.Camera.ClearColor[0] << '\t'
				<< object.Camera.ClearColor[1] << '\t'
				<< object.Camera.ClearColor[2] << '\t'
				<< object.Camera.ClearColor[3] << '\t'
				<< object.Camera.LayerMask << '\t'
				<< object.Camera.Priority << '\t'
				<< (object.Camera.IsMainCamera ? 1 : 0) << '\n';
		}

		if (object.HasLight)
		{
			stream << "Light\t"
				<< static_cast<int>(object.Light.Type) << '\t'
				<< object.Light.Color[0] << '\t'
				<< object.Light.Color[1] << '\t'
				<< object.Light.Color[2] << '\t'
				<< object.Light.Color[3] << '\t'
				<< object.Light.Intensity << '\t'
				<< object.Light.Range << '\t'
				<< object.Light.InnerAngleRadians << '\t'
				<< object.Light.OuterAngleRadians << '\t'
				<< object.Light.LayerMask << '\t'
				<< (object.Light.CastShadows ? 1 : 0) << '\n';
		}

		if (object.HasRigidbody)
		{
			stream << "Rigidbody2D\t"
				<< static_cast<int>(object.Rigidbody.BodyType) << '\t'
				<< object.Rigidbody.Velocity.x << '\t'
				<< object.Rigidbody.Velocity.y << '\t'
				<< object.Rigidbody.Force.x << '\t'
				<< object.Rigidbody.Force.y << '\t'
				<< object.Rigidbody.Mass << '\t'
				<< object.Rigidbody.Friction << '\t'
				<< object.Rigidbody.Restitution << '\t'
				<< object.Rigidbody.LinearDamping << '\t'
				<< object.Rigidbody.GravityScale << '\t'
				<< (object.Rigidbody.UseGravity ? 1 : 0) << '\t'
				<< (object.Rigidbody.FreezePositionX ? 1 : 0) << '\t'
				<< (object.Rigidbody.FreezePositionY ? 1 : 0) << '\n';
		}

		if (object.HasPolygonCollider)
		{
			stream << "PolygonCollider2D\t"
				<< (object.PolygonCollider.IsTrigger ? 1 : 0) << '\t'
				<< (object.PolygonCollider.IsConvex ? 1 : 0) << '\t'
				<< object.PolygonCollider.LocalPoints.size();
			for (const Vector2<float>& point : object.PolygonCollider.LocalPoints)
			{
				stream << '\t' << point.x << '\t' << point.y;
			}
			stream << '\n';
		}

		if (object.HasCircleCollider)
		{
			stream << "CircleCollider2D\t"
				<< object.CircleCollider.LocalCenter.x << '\t'
				<< object.CircleCollider.LocalCenter.y << '\t'
				<< object.CircleCollider.Radius << '\t'
				<< (object.CircleCollider.IsTrigger ? 1 : 0) << '\n';
		}

		if (object.HasPrefabInstance)
		{
			stream << "PrefabInstance\t"
				<< object.SourcePrefabGuid.generic_string() << '\n';
		}

		stream << "EndObject\n";
	}

	outText = stream.str();
	return ESceneSerializeResult::Success;
}

ESceneSerializeResult CSceneSerializer::DeserializeFromText(CScene& scene, const char* text) const
{
	if (nullptr == text)
	{
		return ESceneSerializeResult::InvalidArgument;
	}

	std::istringstream stream(text);
	std::string line;
	if (false == static_cast<bool>(std::getline(stream, line)) || line != "JBroScene\t1")
	{
		return ESceneSerializeResult::ParseError;
	}

	scene.ClearObjects();

	CGameObject currentObject;
	std::vector<CGameObject> objects;
	std::vector<std::int64_t> parentIndices;
	while (std::getline(stream, line))
	{
		if (line.empty())
		{
			continue;
		}

		std::istringstream lineStream(line);
		std::string type;
		std::getline(lineStream, type, '\t');

		if (type == "Object")
		{
			std::string name;
			std::string activeText;
			std::string layerText;
			std::string parentIndexText;
			std::getline(lineStream, name, '\t');
			std::getline(lineStream, activeText, '\t');
			std::getline(lineStream, layerText, '\t');
			std::getline(lineStream, parentIndexText, '\t');

			currentObject = scene.CreateGameObject(UnescapeField(name).c_str());
			currentObject.SetActive(activeText != "0");
			currentObject.SetLayer(static_cast<std::uint32_t>(std::stoul(layerText)));
			objects.push_back(currentObject);
			parentIndices.push_back(parentIndexText.empty() ? -1 : std::stoll(parentIndexText));
		}
		else if (type == "Transform" && currentObject)
		{
			Transform2D* transform = currentObject.GetTransform();
			if (nullptr == transform)
			{
				transform = currentObject.AddComponent<Transform2D>();
			}

			lineStream >> transform->Position.x >> transform->Position.y
				>> transform->RotationRadians
				>> transform->Scale.x >> transform->Scale.y;
		}
		else if (type == "Sprite" && currentObject)
		{
			SpriteRenderer2D* sprite = currentObject.GetComponent<SpriteRenderer2D>();
			if (nullptr == sprite)
			{
				sprite = currentObject.AddComponent<SpriteRenderer2D>();
			}

			if (nullptr != sprite)
			{
				std::string spriteGuid;
				std::string materialGuid;
				lineStream >> spriteGuid >> materialGuid
					>> sprite->Color[0] >> sprite->Color[1] >> sprite->Color[2] >> sprite->Color[3]
					>> sprite->SortOrder >> sprite->LayerMask;
				sprite->SpriteGuid = File::Guid(spriteGuid);
				sprite->MaterialGuid = File::Guid(materialGuid);
			}
		}
		else if (type == "Camera" && currentObject)
		{
			Camera2D* camera = currentObject.GetComponent<Camera2D>();
			if (nullptr == camera)
			{
				camera = currentObject.AddComponent<Camera2D>();
			}

			if (nullptr != camera)
			{
				int projectionMode = 0;
				int isMainCamera = 0;
				lineStream >> projectionMode
					>> camera->OrthographicSize
					>> camera->PerspectiveFovDegrees
					>> camera->NearClip
					>> camera->FarClip
					>> camera->ViewportX
					>> camera->ViewportY
					>> camera->ViewportWidth
					>> camera->ViewportHeight
					>> camera->ClearColor[0]
					>> camera->ClearColor[1]
					>> camera->ClearColor[2]
					>> camera->ClearColor[3]
					>> camera->LayerMask
					>> camera->Priority
					>> isMainCamera;
				camera->ProjectionMode = static_cast<ECameraProjectionMode2D>(projectionMode);
				camera->IsMainCamera = isMainCamera != 0;
			}
		}
		else if (type == "Light" && currentObject)
		{
			Light2D* light = currentObject.GetComponent<Light2D>();
			if (nullptr == light)
			{
				light = currentObject.AddComponent<Light2D>();
			}

			if (nullptr != light)
			{
				int lightType = 0;
				int castShadows = 0;
				lineStream >> lightType
					>> light->Color[0]
					>> light->Color[1]
					>> light->Color[2]
					>> light->Color[3]
					>> light->Intensity
					>> light->Range
					>> light->InnerAngleRadians
					>> light->OuterAngleRadians
					>> light->LayerMask
					>> castShadows;
				light->Type = static_cast<ELight2DType>(lightType);
				light->CastShadows = castShadows != 0;
			}
		}
		else if (type == "Rigidbody2D" && currentObject)
		{
			Rigidbody2D* rigidbody = currentObject.GetComponent<Rigidbody2D>();
			if (nullptr == rigidbody)
			{
				rigidbody = currentObject.AddComponent<Rigidbody2D>();
			}

			if (nullptr != rigidbody)
			{
				int bodyType = 0;
				int useGravity = 0;
				int freezePositionX = 0;
				int freezePositionY = 0;
				lineStream >> bodyType
					>> rigidbody->Velocity.x
					>> rigidbody->Velocity.y
					>> rigidbody->Force.x
					>> rigidbody->Force.y
					>> rigidbody->Mass
					>> rigidbody->Friction
					>> rigidbody->Restitution
					>> rigidbody->LinearDamping
					>> rigidbody->GravityScale
					>> useGravity
					>> freezePositionX
					>> freezePositionY;
				rigidbody->BodyType = static_cast<EPhysics2DBodyType>(bodyType);
				rigidbody->SetMass(rigidbody->Mass);
				rigidbody->UseGravity = useGravity != 0;
				rigidbody->FreezePositionX = freezePositionX != 0;
				rigidbody->FreezePositionY = freezePositionY != 0;
			}
		}
		else if (type == "PolygonCollider2D" && currentObject)
		{
			PolygonCollider2D* collider = currentObject.GetComponent<PolygonCollider2D>();
			if (nullptr == collider)
			{
				collider = currentObject.AddComponent<PolygonCollider2D>();
			}

			if (nullptr != collider)
			{
				int isTrigger = 0;
				int isConvex = 0;
				std::size_t pointCount = 0;
				lineStream >> isTrigger >> isConvex >> pointCount;
				collider->IsTrigger = isTrigger != 0;
				collider->IsConvex = isConvex != 0;
				collider->LocalPoints.clear();
				collider->LocalPoints.reserve(pointCount);
				for (std::size_t i = 0; i < pointCount; ++i)
				{
					Vector2<float> point;
					lineStream >> point.x >> point.y;
					collider->LocalPoints.push_back(point);
				}
			}
		}
		else if (type == "CircleCollider2D" && currentObject)
		{
			CircleCollider2D* collider = currentObject.GetComponent<CircleCollider2D>();
			if (nullptr == collider)
			{
				collider = currentObject.AddComponent<CircleCollider2D>();
			}

			if (nullptr != collider)
			{
				int isTrigger = 0;
				lineStream >> collider->LocalCenter.x
					>> collider->LocalCenter.y
					>> collider->Radius
					>> isTrigger;
				collider->IsTrigger = isTrigger != 0;
			}
		}
		else if (type == "PrefabInstance" && currentObject)
		{
			PrefabInstance* prefabInstance = currentObject.GetComponent<PrefabInstance>();
			if (nullptr == prefabInstance)
			{
				prefabInstance = currentObject.AddComponent<PrefabInstance>();
			}

			if (nullptr != prefabInstance)
			{
				std::string sourcePrefabGuid;
				lineStream >> sourcePrefabGuid;
				prefabInstance->SourcePrefabGuid = File::Guid(sourcePrefabGuid);
			}
		}
		else if (type == "EndObject")
		{
			currentObject = CGameObject();
		}
		else
		{
			return ESceneSerializeResult::ParseError;
		}
	}

	for (std::size_t i = 0; i < objects.size(); ++i)
	{
		const std::int64_t parentIndex = parentIndices[i];
		if (parentIndex < 0)
		{
			continue;
		}

		if (static_cast<std::size_t>(parentIndex) >= objects.size())
		{
			return ESceneSerializeResult::ParseError;
		}

		if (false == objects[i].SetParent(objects[static_cast<std::size_t>(parentIndex)]))
		{
			return ESceneSerializeResult::ParseError;
		}
	}

	return ESceneSerializeResult::Success;
}

ESceneSerializeResult CSceneSerializer::SaveToFile(const CScene& scene, const char* path) const
{
	if (nullptr == path)
	{
		return ESceneSerializeResult::InvalidArgument;
	}

	std::string text;
	SerializeToText(scene, text);

	std::ofstream file(path, std::ios::out | std::ios::trunc);
	if (false == file.is_open())
	{
		return ESceneSerializeResult::IoError;
	}

	file << text;
	return ESceneSerializeResult::Success;
}

ESceneSerializeResult CSceneSerializer::LoadFromFile(CScene& scene, const char* path) const
{
	if (nullptr == path)
	{
		return ESceneSerializeResult::InvalidArgument;
	}

	std::ifstream file(path);
	if (false == file.is_open())
	{
		return ESceneSerializeResult::IoError;
	}

	std::stringstream buffer;
	buffer << file.rdbuf();
	return DeserializeFromText(scene, buffer.str().c_str());
}

std::string CSceneSerializer::EscapeField(const char* value)
{
	std::string escaped;
	if (nullptr == value)
	{
		return escaped;
	}

	for (const char* cursor = value; '\0' != *cursor; ++cursor)
	{
		if ('\\' == *cursor || '\t' == *cursor || '\n' == *cursor || '\r' == *cursor)
		{
			escaped.push_back('\\');
		}

		switch (*cursor)
		{
		case '\t':
			escaped.push_back('t');
			break;
		case '\n':
			escaped.push_back('n');
			break;
		case '\r':
			escaped.push_back('r');
			break;
		default:
			escaped.push_back(*cursor);
			break;
		}
	}

	return escaped;
}

std::string CSceneSerializer::UnescapeField(const std::string& value)
{
	std::string unescaped;
	for (std::size_t i = 0; i < value.size(); ++i)
	{
		if ('\\' != value[i] || i + 1 >= value.size())
		{
			unescaped.push_back(value[i]);
			continue;
		}

		++i;
		switch (value[i])
		{
		case 't':
			unescaped.push_back('\t');
			break;
		case 'n':
			unescaped.push_back('\n');
			break;
		case 'r':
			unescaped.push_back('\r');
			break;
		default:
			unescaped.push_back(value[i]);
			break;
		}
	}

	return unescaped;
}
