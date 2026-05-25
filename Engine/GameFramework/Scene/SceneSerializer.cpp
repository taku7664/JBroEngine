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
#include "yaml-cpp/yaml.h"

#include <fstream>
#include <sstream>
#include <unordered_map>

namespace
{
	constexpr std::uint32_t SCENE_FILE_VERSION = 1;

	template<typename T>
	bool ReadValue(const YAML::Node& node, const char* key, T& outValue)
	{
		if (!node[key])
		{
			return false;
		}

		try
		{
			outValue = node[key].as<T>();
			return true;
		}
		catch (const YAML::Exception&)
		{
			return false;
		}
	}

	template<typename T>
	T ReadValueOr(const YAML::Node& node, const char* key, const T& defaultValue)
	{
		T value = defaultValue;
		ReadValue(node, key, value);
		return value;
	}

	YAML::Node WriteVector2(const Vector2<float>& value)
	{
		YAML::Node node(YAML::NodeType::Sequence);
		node.push_back(value.x);
		node.push_back(value.y);
		return node;
	}

	bool ReadVector2(const YAML::Node& node, Vector2<float>& outValue)
	{
		if (!node || false == node.IsSequence() || node.size() < 2)
		{
			return false;
		}

		try
		{
			outValue.x = node[0].as<float>();
			outValue.y = node[1].as<float>();
			return true;
		}
		catch (const YAML::Exception&)
		{
			return false;
		}
	}

	YAML::Node WriteColor4(const float (&color)[4])
	{
		YAML::Node node(YAML::NodeType::Sequence);
		node.push_back(color[0]);
		node.push_back(color[1]);
		node.push_back(color[2]);
		node.push_back(color[3]);
		return node;
	}

	bool ReadColor4(const YAML::Node& node, float (&outColor)[4])
	{
		if (!node || false == node.IsSequence() || node.size() < 4)
		{
			return false;
		}

		try
		{
			for (std::size_t i = 0; i < 4; ++i)
			{
				outColor[i] = node[i].as<float>();
			}
			return true;
		}
		catch (const YAML::Exception&)
		{
			return false;
		}
	}

	YAML::Node WriteTransform(const Transform2D& transform)
	{
		YAML::Node node(YAML::NodeType::Map);
		node["Position"] = WriteVector2(transform.Position);
		node["RotationRadians"] = transform.RotationRadians;
		node["Scale"] = WriteVector2(transform.Scale);
		return node;
	}

	void ReadTransform(const YAML::Node& node, Transform2D& transform)
	{
		ReadVector2(node["Position"], transform.Position);
		transform.RotationRadians = ReadValueOr<float>(node, "RotationRadians", transform.RotationRadians);
		ReadVector2(node["Scale"], transform.Scale);
	}

	YAML::Node WriteSpriteRenderer(const SceneObjectSnapshot& object)
	{
		YAML::Node node(YAML::NodeType::Map);
		node["IsEnabled"] = object.SpriteRendererEnabled;
		node["SpriteGuid"] = object.SpriteGuid.generic_string();
		node["MaterialGuid"] = object.MaterialGuid.generic_string();
		node["Size"] = WriteVector2(object.SpriteSize);
		node["Offset"] = WriteVector2(object.SpriteOffset);
		node["Color"] = WriteColor4(object.Color);
		node["SortOrder"] = object.SortOrder;
		node["LayerMask"] = object.LayerMask;
		return node;
	}

	void ReadSpriteRenderer(const YAML::Node& node, SpriteRenderer2D& sprite)
	{
		sprite.IsEnabled = ReadValueOr<bool>(node, "IsEnabled", sprite.IsEnabled);
		sprite.SpriteGuid = File::Guid(ReadValueOr<std::string>(node, "SpriteGuid", ""));
		sprite.MaterialGuid = File::Guid(ReadValueOr<std::string>(node, "MaterialGuid", ""));
		ReadVector2(node["Size"], sprite.Size);
		ReadVector2(node["Offset"], sprite.Offset);
		ReadColor4(node["Color"], sprite.Color);
		sprite.SortOrder = ReadValueOr<std::int32_t>(node, "SortOrder", sprite.SortOrder);
		sprite.LayerMask = ReadValueOr<RenderLayerMask>(node, "LayerMask", sprite.LayerMask);
	}

	// Layout2D 직렬화 헬퍼
	YAML::Node WriteLayout2D(const Layout2D& layout)
	{
		YAML::Node node(YAML::NodeType::Map);
		{
			YAML::Node norm(YAML::NodeType::Sequence);
			norm.push_back(layout.Normalized.x);
			norm.push_back(layout.Normalized.y);
			node["Normalized"] = norm;
		}
		{
			YAML::Node pix(YAML::NodeType::Sequence);
			pix.push_back(layout.Pixel.x);
			pix.push_back(layout.Pixel.y);
			node["Pixel"] = pix;
		}
		return node;
	}

	Layout2D ReadLayout2D(const YAML::Node& node, const Layout2D& defaultVal)
	{
		if (!node || !node.IsMap())
		{
			return defaultVal;
		}
		Layout2D result = defaultVal;
		if (const YAML::Node norm = node["Normalized"]; norm && norm.IsSequence() && norm.size() >= 2)
		{
			result.Normalized.x = norm[0].as<float>(defaultVal.Normalized.x);
			result.Normalized.y = norm[1].as<float>(defaultVal.Normalized.y);
		}
		if (const YAML::Node pix = node["Pixel"]; pix && pix.IsSequence() && pix.size() >= 2)
		{
			result.Pixel.x = pix[0].as<float>(defaultVal.Pixel.x);
			result.Pixel.y = pix[1].as<float>(defaultVal.Pixel.y);
		}
		return result;
	}

	YAML::Node WriteCamera(const Camera2D& camera)
	{
		YAML::Node node(YAML::NodeType::Map);
		node["IsEnabled"] = camera.IsEnabled;
		node["ProjectionMode"] = static_cast<int>(camera.ProjectionMode);
		node["OrthographicSize"] = camera.OrthographicSize;
		node["PerspectiveFovDegrees"] = camera.PerspectiveFovDegrees;
		node["NearClip"] = camera.NearClip;
		node["FarClip"] = camera.FarClip;
		node["Position"] = WriteLayout2D(camera.Position);
		node["Size"]     = WriteLayout2D(camera.Size);
		node["ClearColor"] = WriteColor4(camera.ClearColor);
		node["LayerMask"] = camera.LayerMask;
		node["Priority"] = camera.Priority;
		node["IsMainCamera"] = camera.IsMainCamera;
		return node;
	}

	void ReadCamera(const YAML::Node& node, Camera2D& camera)
	{
		camera.IsEnabled = ReadValueOr<bool>(node, "IsEnabled", camera.IsEnabled);
		camera.ProjectionMode = static_cast<ECameraProjectionMode2D>(ReadValueOr<int>(node, "ProjectionMode", static_cast<int>(camera.ProjectionMode)));
		camera.OrthographicSize = ReadValueOr<float>(node, "OrthographicSize", camera.OrthographicSize);
		camera.PerspectiveFovDegrees = ReadValueOr<float>(node, "PerspectiveFovDegrees", camera.PerspectiveFovDegrees);
		camera.NearClip = ReadValueOr<float>(node, "NearClip", camera.NearClip);
		camera.FarClip = ReadValueOr<float>(node, "FarClip", camera.FarClip);

		// 신규 Layout2D 포맷
		if (node["Position"])
		{
			camera.Position = ReadLayout2D(node["Position"], camera.Position);
		}
		if (node["Size"])
		{
			camera.Size = ReadLayout2D(node["Size"], camera.Size);
		}
		// 구버전 Viewport 포맷 → Layout2D 변환 (하위 호환)
		else if (const YAML::Node viewport = node["Viewport"]; viewport && viewport.IsSequence() && viewport.size() >= 4)
		{
			camera.Position.Normalized.x = viewport[0].as<float>(0.0f);
			camera.Position.Normalized.y = viewport[1].as<float>(0.0f);
			camera.Position.Pixel        = { 0.0f, 0.0f };
			camera.Size.Normalized.x     = viewport[2].as<float>(1.0f);
			camera.Size.Normalized.y     = viewport[3].as<float>(1.0f);
			camera.Size.Pixel            = { 0.0f, 0.0f };
		}

		ReadColor4(node["ClearColor"], camera.ClearColor);
		camera.LayerMask = ReadValueOr<std::uint32_t>(node, "LayerMask", camera.LayerMask);
		camera.Priority = ReadValueOr<std::int32_t>(node, "Priority", camera.Priority);
		camera.IsMainCamera = ReadValueOr<bool>(node, "IsMainCamera", camera.IsMainCamera);
	}

	YAML::Node WriteLight(const Light2D& light)
	{
		YAML::Node node(YAML::NodeType::Map);
		node["Type"] = static_cast<int>(light.Type);
		node["Color"] = WriteColor4(light.Color);
		node["Intensity"] = light.Intensity;
		node["Range"] = light.Range;
		node["InnerAngleRadians"] = light.InnerAngleRadians;
		node["OuterAngleRadians"] = light.OuterAngleRadians;
		node["LayerMask"] = light.LayerMask;
		node["CastShadows"] = light.CastShadows;
		return node;
	}

	void ReadLight(const YAML::Node& node, Light2D& light)
	{
		light.Type = static_cast<ELight2DType>(ReadValueOr<int>(node, "Type", static_cast<int>(light.Type)));
		ReadColor4(node["Color"], light.Color);
		light.Intensity = ReadValueOr<float>(node, "Intensity", light.Intensity);
		light.Range = ReadValueOr<float>(node, "Range", light.Range);
		light.InnerAngleRadians = ReadValueOr<float>(node, "InnerAngleRadians", light.InnerAngleRadians);
		light.OuterAngleRadians = ReadValueOr<float>(node, "OuterAngleRadians", light.OuterAngleRadians);
		light.LayerMask = ReadValueOr<std::uint32_t>(node, "LayerMask", light.LayerMask);
		light.CastShadows = ReadValueOr<bool>(node, "CastShadows", light.CastShadows);
	}

	YAML::Node WriteRigidbody(const Rigidbody2D& rigidbody)
	{
		YAML::Node node(YAML::NodeType::Map);
		node["IsEnabled"] = rigidbody.IsEnabled;
		node["BodyType"] = static_cast<int>(rigidbody.BodyType);
		node["Velocity"] = WriteVector2(rigidbody.Velocity);
		node["Force"] = WriteVector2(rigidbody.Force);
		node["AngularVelocity"] = rigidbody.AngularVelocity;
		node["Torque"] = rigidbody.Torque;
		node["Mass"] = rigidbody.Mass;
		node["Inertia"] = rigidbody.Inertia;
		node["Friction"] = rigidbody.Friction;
		node["Restitution"] = rigidbody.Restitution;
		node["LinearDamping"] = rigidbody.LinearDamping;
		node["AngularDamping"] = rigidbody.AngularDamping;
		node["GravityScale"] = rigidbody.GravityScale;
		node["UseGravity"] = rigidbody.UseGravity;
		node["FreezePositionX"] = rigidbody.FreezePositionX;
		node["FreezePositionY"] = rigidbody.FreezePositionY;
		node["FreezeRotation"] = rigidbody.FreezeRotation;
		node["StabilizeRestingContacts"] = rigidbody.StabilizeRestingContacts;
		node["RestingLinearVelocityThreshold"] = rigidbody.RestingLinearVelocityThreshold;
		node["RestingAngularVelocityThreshold"] = rigidbody.RestingAngularVelocityThreshold;
		return node;
	}

	void ReadRigidbody(const YAML::Node& node, Rigidbody2D& rigidbody)
	{
		rigidbody.IsEnabled = ReadValueOr<bool>(node, "IsEnabled", rigidbody.IsEnabled);
		rigidbody.BodyType = static_cast<EPhysics2DBodyType>(ReadValueOr<int>(node, "BodyType", static_cast<int>(rigidbody.BodyType)));
		ReadVector2(node["Velocity"], rigidbody.Velocity);
		ReadVector2(node["Force"], rigidbody.Force);
		rigidbody.AngularVelocity = ReadValueOr<float>(node, "AngularVelocity", rigidbody.AngularVelocity);
		rigidbody.Torque = ReadValueOr<float>(node, "Torque", rigidbody.Torque);
		rigidbody.Mass = ReadValueOr<float>(node, "Mass", rigidbody.Mass);
		rigidbody.SetMass(rigidbody.Mass);
		rigidbody.Inertia = ReadValueOr<float>(node, "Inertia", rigidbody.Inertia);
		rigidbody.SetInertia(rigidbody.Inertia);
		rigidbody.Friction = ReadValueOr<float>(node, "Friction", rigidbody.Friction);
		rigidbody.Restitution = ReadValueOr<float>(node, "Restitution", rigidbody.Restitution);
		rigidbody.LinearDamping = ReadValueOr<float>(node, "LinearDamping", rigidbody.LinearDamping);
		rigidbody.AngularDamping = ReadValueOr<float>(node, "AngularDamping", rigidbody.AngularDamping);
		rigidbody.GravityScale = ReadValueOr<float>(node, "GravityScale", rigidbody.GravityScale);
		rigidbody.UseGravity = ReadValueOr<bool>(node, "UseGravity", rigidbody.UseGravity);
		rigidbody.FreezePositionX = ReadValueOr<bool>(node, "FreezePositionX", rigidbody.FreezePositionX);
		rigidbody.FreezePositionY = ReadValueOr<bool>(node, "FreezePositionY", rigidbody.FreezePositionY);
		rigidbody.FreezeRotation = ReadValueOr<bool>(node, "FreezeRotation", rigidbody.FreezeRotation);
		rigidbody.StabilizeRestingContacts = ReadValueOr<bool>(node, "StabilizeRestingContacts", rigidbody.StabilizeRestingContacts);
		rigidbody.RestingLinearVelocityThreshold = ReadValueOr<float>(node, "RestingLinearVelocityThreshold", rigidbody.RestingLinearVelocityThreshold);
		rigidbody.RestingAngularVelocityThreshold = ReadValueOr<float>(node, "RestingAngularVelocityThreshold", rigidbody.RestingAngularVelocityThreshold);
	}

	YAML::Node WritePolygonCollider(const PolygonCollider2D& collider)
	{
		YAML::Node node(YAML::NodeType::Map);
		node["IsEnabled"] = collider.IsEnabled;
		node["LocalCenter"] = WriteVector2(collider.LocalCenter);
		node["VertexCount"] = collider.VertexCount;
		node["Size"] = WriteVector2(collider.Size);
		node["RotationRadians"] = collider.RotationRadians;
		node["IsTrigger"] = collider.IsTrigger;
		node["IsConvex"] = collider.IsConvex;
		YAML::Node points(YAML::NodeType::Sequence);
		for (const Vector2<float>& point : collider.LocalPoints)
		{
			points.push_back(WriteVector2(point));
		}
		node["LocalPoints"] = points;
		return node;
	}

	void ReadPolygonCollider(const YAML::Node& node, PolygonCollider2D& collider)
	{
		collider.IsEnabled = ReadValueOr<bool>(node, "IsEnabled", collider.IsEnabled);
		ReadVector2(node["LocalCenter"], collider.LocalCenter);
		collider.VertexCount = ReadValueOr<std::uint32_t>(node, "VertexCount", collider.VertexCount);
		if (collider.VertexCount < 3)
		{
			collider.VertexCount = 3;
		}
		if (false == ReadVector2(node["Size"], collider.Size))
		{
			const float legacyRadius = ReadValueOr<float>(node, "Radius", 0.0f);
			if (legacyRadius > 0.0f)
			{
				collider.Size = Vector2<float>(legacyRadius * 2.0f, legacyRadius * 2.0f);
			}
		}
		collider.RotationRadians = ReadValueOr<float>(node, "RotationRadians", collider.RotationRadians);
		collider.IsTrigger = ReadValueOr<bool>(node, "IsTrigger", collider.IsTrigger);
		collider.IsConvex = ReadValueOr<bool>(node, "IsConvex", collider.IsConvex);
		collider.LocalPoints.clear();
		if (const YAML::Node points = node["LocalPoints"]; points && points.IsSequence())
		{
			for (const YAML::Node& pointNode : points)
			{
				Vector2<float> point;
				if (ReadVector2(pointNode, point))
				{
					collider.LocalPoints.push_back(point);
				}
			}
		}
	}

	YAML::Node WriteCircleCollider(const CircleCollider2D& collider)
	{
		YAML::Node node(YAML::NodeType::Map);
		node["IsEnabled"] = collider.IsEnabled;
		node["LocalCenter"] = WriteVector2(collider.LocalCenter);
		node["Radius"] = collider.Radius;
		node["IsTrigger"] = collider.IsTrigger;
		return node;
	}

	void ReadCircleCollider(const YAML::Node& node, CircleCollider2D& collider)
	{
		collider.IsEnabled = ReadValueOr<bool>(node, "IsEnabled", collider.IsEnabled);
		ReadVector2(node["LocalCenter"], collider.LocalCenter);
		collider.Radius = ReadValueOr<float>(node, "Radius", collider.Radius);
		collider.IsTrigger = ReadValueOr<bool>(node, "IsTrigger", collider.IsTrigger);
	}
}

ESceneSerializeResult CSceneSerializer::SerializeToText(const CScene& scene, std::string& outText) const
{
	SceneSnapshot snapshot;
	scene.BuildSnapshot(snapshot);

	std::unordered_map<EntityId, std::size_t> entityToIndex;
	for (std::size_t i = 0; i < snapshot.Objects.size(); ++i)
	{
		entityToIndex.emplace(snapshot.Objects[i].Entity, i);
	}

	YAML::Node root(YAML::NodeType::Map);
	root["Version"] = SCENE_FILE_VERSION;
	YAML::Node objects(YAML::NodeType::Sequence);

	for (const SceneObjectSnapshot& object : snapshot.Objects)
	{
		YAML::Node objectNode(YAML::NodeType::Map);
		objectNode["Name"] = object.Name;
		objectNode["Active"] = object.IsActive;
		objectNode["Layer"] = object.Layer;

		const auto parentIt = entityToIndex.find(object.Parent);
		objectNode["ParentIndex"] = parentIt == entityToIndex.end() ? -1 : static_cast<int>(parentIt->second);

		YAML::Node components(YAML::NodeType::Map);
		if (object.HasTransform)
		{
			components["Transform2D"] = WriteTransform(object.Transform);
		}
		if (object.HasSpriteRenderer)
		{
			components["SpriteRenderer2D"] = WriteSpriteRenderer(object);
		}
		if (object.HasCamera)
		{
			components["Camera2D"] = WriteCamera(object.Camera);
		}
		if (object.HasLight)
		{
			components["Light2D"] = WriteLight(object.Light);
		}
		if (object.HasRigidbody)
		{
			components["Rigidbody2D"] = WriteRigidbody(object.Rigidbody);
		}
		if (false == object.PolygonColliders.empty())
		{
			YAML::Node collidersSeq(YAML::NodeType::Sequence);
			for (const PolygonCollider2D& collider : object.PolygonColliders)
			{
				collidersSeq.push_back(WritePolygonCollider(collider));
			}
			components["PolygonColliders"] = collidersSeq;
		}
		if (object.HasCircleCollider)
		{
			components["CircleCollider2D"] = WriteCircleCollider(object.CircleCollider);
		}
		if (object.HasPrefabInstance)
		{
			YAML::Node prefab(YAML::NodeType::Map);
			prefab["SourcePrefabGuid"] = object.SourcePrefabGuid.generic_string();
			components["PrefabInstance"] = prefab;
		}
		objectNode["Components"] = components;
		objects.push_back(objectNode);
	}

	root["Objects"] = objects;

	YAML::Emitter emitter;
	emitter << root;
	outText = emitter.c_str();
	outText.push_back('\n');
	return ESceneSerializeResult::Success;
}

ESceneSerializeResult CSceneSerializer::DeserializeFromText(CScene& scene, const char* text) const
{
	if (nullptr == text)
	{
		return ESceneSerializeResult::InvalidArgument;
	}

	YAML::Node root;
	try
	{
		root = YAML::Load(text);
	}
	catch (const YAML::Exception&)
	{
		return ESceneSerializeResult::ParseError;
	}

	if (!root || false == root.IsMap())
	{
		return ESceneSerializeResult::ParseError;
	}

	const std::uint32_t version = ReadValueOr<std::uint32_t>(root, "Version", 0);
	if (SCENE_FILE_VERSION != version)
	{
		return ESceneSerializeResult::ParseError;
	}

	const YAML::Node objectsNode = root["Objects"];
	if (!objectsNode || false == objectsNode.IsSequence())
	{
		return ESceneSerializeResult::ParseError;
	}

	scene.ClearObjects();

	std::vector<CGameObject> objects;
	std::vector<int> parentIndices;
	for (const YAML::Node& objectNode : objectsNode)
	{
		if (!objectNode || false == objectNode.IsMap())
		{
			return ESceneSerializeResult::ParseError;
		}

		const std::string name = ReadValueOr<std::string>(objectNode, "Name", "GameObject");
		CGameObject object = scene.CreateGameObject(name.c_str());
		object.SetActive(ReadValueOr<bool>(objectNode, "Active", true));
		object.SetLayer(ReadValueOr<std::uint32_t>(objectNode, "Layer", 0));

		const YAML::Node components = objectNode["Components"];
		if (components && components.IsMap())
		{
			if (const YAML::Node transformNode = components["Transform2D"])
			{
				if (Transform2D* transform = object.GetTransform())
				{
					ReadTransform(transformNode, *transform);
				}
			}

			if (const YAML::Node spriteNode = components["SpriteRenderer2D"])
			{
				if (SpriteRenderer2D* sprite = object.AddComponent<SpriteRenderer2D>())
				{
					ReadSpriteRenderer(spriteNode, *sprite);
				}
			}

			if (const YAML::Node cameraNode = components["Camera2D"])
			{
				if (Camera2D* camera = object.AddComponent<Camera2D>())
				{
					ReadCamera(cameraNode, *camera);
				}
			}

			if (const YAML::Node lightNode = components["Light2D"])
			{
				if (Light2D* light = object.AddComponent<Light2D>())
				{
					ReadLight(lightNode, *light);
				}
			}

			if (const YAML::Node rigidbodyNode = components["Rigidbody2D"])
			{
				if (Rigidbody2D* rigidbody = object.AddComponent<Rigidbody2D>())
				{
					ReadRigidbody(rigidbodyNode, *rigidbody);
				}
			}

			// New format: "PolygonColliders" is a sequence supporting multiple instances.
			// Legacy format: "PolygonCollider2D" is a single map (backward compat).
			if (const YAML::Node seqNode = components["PolygonColliders"]; seqNode && seqNode.IsSequence())
			{
				for (const YAML::Node& colliderNode : seqNode)
				{
					// First instance uses AddComponent (idempotent); subsequent ones use
					// AddNewComponent so duplicates are preserved faithfully.
					const bool alreadyHasOne = (nullptr != object.GetComponent<PolygonCollider2D>());
					PolygonCollider2D* collider = alreadyHasOne
						? object.AddNewComponent<PolygonCollider2D>()
						: object.AddComponent<PolygonCollider2D>();
					if (collider)
					{
						ReadPolygonCollider(colliderNode, *collider);
					}
				}
			}
			else if (const YAML::Node polygonNode = components["PolygonCollider2D"])
			{
				// Legacy single-collider format.
				if (PolygonCollider2D* collider = object.AddComponent<PolygonCollider2D>())
				{
					ReadPolygonCollider(polygonNode, *collider);
				}
			}

			if (const YAML::Node circleNode = components["CircleCollider2D"])
			{
				if (CircleCollider2D* collider = object.AddComponent<CircleCollider2D>())
				{
					ReadCircleCollider(circleNode, *collider);
				}
			}

			if (const YAML::Node prefabNode = components["PrefabInstance"])
			{
				if (PrefabInstance* prefab = object.AddComponent<PrefabInstance>())
				{
					prefab->SourcePrefabGuid = File::Guid(ReadValueOr<std::string>(prefabNode, "SourcePrefabGuid", ""));
				}
			}
		}

		objects.push_back(object);
		parentIndices.push_back(ReadValueOr<int>(objectNode, "ParentIndex", -1));
	}

	for (std::size_t i = 0; i < objects.size(); ++i)
	{
		const int parentIndex = parentIndices[i];
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
