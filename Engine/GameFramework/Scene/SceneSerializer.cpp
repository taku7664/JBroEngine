#include "pch.h"
#include "SceneSerializer.h"

#include "Core/Core.h"
#include "GameFramework/Component/Camera2D.h"
#include "GameFramework/Component/Light2D.h"
#include "GameFramework/Component/Physics2DComponents.h"
#include "GameFramework/Component/PrefabInstance.h"
#include "GameFramework/Component/ScriptComponent.h"
#include "GameFramework/Component/SpriteRenderer2D.h"
#include "GameFramework/Component/Transform2D.h"
#include "GameFramework/Object/GameObject.h"
#include "GameFramework/Component/GameObject.h"
#include "GameFramework/Reflection/ReflectionRegistry.h"
#include "GameFramework/Scene/Scene.h"
#include "GameFramework/Scene/SceneSnapshot.h"
#include "yaml-cpp/yaml.h"

#include <cstring>
#include <algorithm>
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

	void AddReferencedAsset(std::vector<AssetGuid>& referencedAssets, const File::Guid& guid)
	{
		if (guid.IsNull())
		{
			return;
		}

		if (std::find(referencedAssets.begin(), referencedAssets.end(), guid) == referencedAssets.end())
		{
			referencedAssets.push_back(guid);
		}
	}

	YAML::Node WriteReferencedAssets(const std::vector<AssetGuid>& referencedAssets)
	{
		YAML::Node node(YAML::NodeType::Sequence);
		for (const AssetGuid& guid : referencedAssets)
		{
			if (false == guid.IsNull())
			{
				node.push_back(guid.generic_string());
			}
		}
		return node;
	}

	std::vector<AssetGuid> ReadReferencedAssets(const YAML::Node& node)
	{
		std::vector<AssetGuid> referencedAssets;
		if (!node || false == node.IsSequence())
		{
			return referencedAssets;
		}

		for (const YAML::Node& assetNode : node)
		{
			try
			{
				AddReferencedAsset(referencedAssets, File::Guid(assetNode.as<std::string>()));
			}
			catch (const YAML::Exception&)
			{
			}
		}
		return referencedAssets;
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

	// ── 리플렉션 기반 제네릭 직렬화 ──────────────────────────────────────────

	// 등록된 모든 프로퍼티를 EReflectPropertyType에 따라 YAML로 직렬화합니다.
	YAML::Node WriteComponentReflected(const void* ptr, const ComponentTypeInfo& typeInfo, std::vector<AssetGuid>* referencedAssets = nullptr)
	{
		YAML::Node node(YAML::NodeType::Map);
		for (const ReflectPropertyInfo& prop : typeInfo.Properties)
		{
			const void* field = static_cast<const char*>(ptr) + prop.Offset;
			switch (prop.Type)
			{
			case EReflectPropertyType::Bool:
				node[prop.Name] = *static_cast<const bool*>(field);
				break;
			case EReflectPropertyType::Int32:
				node[prop.Name] = *static_cast<const std::int32_t*>(field);
				break;
			case EReflectPropertyType::UInt32:
				node[prop.Name] = *static_cast<const std::uint32_t*>(field);
				break;
			case EReflectPropertyType::Float:
			case EReflectPropertyType::AngleDegrees:
				node[prop.Name] = *static_cast<const float*>(field);
				break;
			case EReflectPropertyType::Vector2Float:
				node[prop.Name] = WriteVector2(*static_cast<const Vector2<float>*>(field));
				break;
			case EReflectPropertyType::ColorFloat4:
				{
					const float* c = static_cast<const float*>(field);
					float arr[4] = { c[0], c[1], c[2], c[3] };
					node[prop.Name] = WriteColor4(arr);
				}
				break;
			case EReflectPropertyType::AssetGuid:
				{
					const File::Guid& guid = *static_cast<const File::Guid*>(field);
					node[prop.Name] = guid.generic_string();
					if (referencedAssets)
					{
						AddReferencedAsset(*referencedAssets, guid);
					}
				}
				break;
			case EReflectPropertyType::Enum:
				{
					int val = 0;
					std::memcpy(&val, field, std::min(prop.Size, sizeof(int)));
					node[prop.Name] = val;
				}
				break;
			case EReflectPropertyType::Layout2D:
				node[prop.Name] = WriteLayout2D(*static_cast<const Layout2D*>(field));
				break;
			case EReflectPropertyType::String:
				node[prop.Name] = std::string(static_cast<const char*>(field));
				break;
			default:
				break;
			}
		}
		return node;
	}

	// 등록된 모든 프로퍼티를 EReflectPropertyType에 따라 YAML에서 역직렬화합니다.
	void ReadComponentReflected(const YAML::Node& node, void* ptr, const ComponentTypeInfo& typeInfo)
	{
		for (const ReflectPropertyInfo& prop : typeInfo.Properties)
		{
			if (!node[prop.Name])
				continue;
			void* field = static_cast<char*>(ptr) + prop.Offset;
			switch (prop.Type)
			{
			case EReflectPropertyType::Bool:
				ReadValue(node, prop.Name, *static_cast<bool*>(field));
				break;
			case EReflectPropertyType::Int32:
				ReadValue(node, prop.Name, *static_cast<std::int32_t*>(field));
				break;
			case EReflectPropertyType::UInt32:
				ReadValue(node, prop.Name, *static_cast<std::uint32_t*>(field));
				break;
			case EReflectPropertyType::Float:
			case EReflectPropertyType::AngleDegrees:
				ReadValue(node, prop.Name, *static_cast<float*>(field));
				break;
			case EReflectPropertyType::Vector2Float:
				ReadVector2(node[prop.Name], *static_cast<Vector2<float>*>(field));
				break;
			case EReflectPropertyType::ColorFloat4:
				{
					float tmp[4] = {};
					ReadColor4(node[prop.Name], tmp);
					std::memcpy(field, tmp, sizeof(float) * 4);
				}
				break;
			case EReflectPropertyType::AssetGuid:
				{
					std::string s;
					if (ReadValue(node, prop.Name, s))
						*static_cast<File::Guid*>(field) = File::Guid(s);
				}
				break;
			case EReflectPropertyType::Enum:
				{
					int val = 0;
					if (ReadValue(node, prop.Name, val))
						std::memcpy(field, &val, std::min(prop.Size, sizeof(int)));
				}
				break;
			case EReflectPropertyType::Layout2D:
				*static_cast<Layout2D*>(field) = ReadLayout2D(
					node[prop.Name], *static_cast<const Layout2D*>(field));
				break;
			case EReflectPropertyType::String:
				{
					std::string s;
					if (ReadValue(node, prop.Name, s))
					{
						const std::size_t cap = prop.ElementCount > 0 ? prop.ElementCount : prop.Size;
						const std::size_t len = std::min(s.size(), cap - 1);
						std::memcpy(field, s.c_str(), len);
						static_cast<char*>(field)[len] = '\0';
					}
				}
				break;
			default:
				break;
			}
		}
	}

	// 타입 이름으로 ComponentTypeInfo를 가져옵니다. Core::Reflection이 없으면 nullptr 반환.
	const ComponentTypeInfo* GetTypeInfo(const char* name)
	{
		return Core::Reflection ? Core::Reflection->FindComponentByName(name) : nullptr;
	}

	// ── 컴포넌트별 직렬화 (제네릭 + 예외 처리) ──────────────────────────────

	YAML::Node WriteTransform(const Transform2D& transform)
	{
		const ComponentTypeInfo* ti = GetTypeInfo("Transform2D");
		if (!ti) return YAML::Node(YAML::NodeType::Map);
		return WriteComponentReflected(&transform, *ti);
	}

	void ReadTransform(const YAML::Node& node, Transform2D& transform)
	{
		const ComponentTypeInfo* ti = GetTypeInfo("Transform2D");
		if (ti) ReadComponentReflected(node, &transform, *ti);
	}

	YAML::Node WriteSpriteRenderer(const SceneObjectSnapshot& object, std::vector<AssetGuid>& referencedAssets)
	{
		const ComponentTypeInfo* ti = GetTypeInfo("SpriteRenderer2D");
		if (!ti) return YAML::Node(YAML::NodeType::Map);
		// SpriteRenderer2D 는 복사 불가 멤버(OwnerPtr)를 포함하므로 직렬화 가능 필드만
		// 임시 구조체로 조립한 뒤 리플렉션 직렬화에 넘긴다.
		SpriteRenderer2D temp;
		temp.IsEnabled    = object.SpriteRendererEnabled;
		temp.SpriteGuid   = object.SpriteGuid;
		temp.MaterialGuid = object.MaterialGuid;
		temp.Size         = object.SpriteSize;
		temp.Offset       = object.SpriteOffset;
		std::memcpy(temp.Color, object.Color, sizeof(temp.Color));
		temp.SortOrder    = object.SortOrder;
		temp.LayerMask    = object.LayerMask;
		return WriteComponentReflected(&temp, *ti, &referencedAssets);
	}

	void ReadSpriteRenderer(const YAML::Node& node, SpriteRenderer2D& sprite)
	{
		const ComponentTypeInfo* ti = GetTypeInfo("SpriteRenderer2D");
		if (ti) ReadComponentReflected(node, &sprite, *ti);
	}

	YAML::Node WriteCamera(const Camera2D& camera)
	{
		const ComponentTypeInfo* ti = GetTypeInfo("Camera2D");
		if (!ti) return YAML::Node(YAML::NodeType::Map);
		return WriteComponentReflected(&camera, *ti);
	}

	void ReadCamera(const YAML::Node& node, Camera2D& camera)
	{
		const ComponentTypeInfo* ti = GetTypeInfo("Camera2D");
		if (ti) ReadComponentReflected(node, &camera, *ti);

		// 구버전 Viewport 포맷 → Layout2D 변환 (하위 호환)
		if (!node["Position"] && !node["Size"])
		{
			if (const YAML::Node viewport = node["Viewport"]; viewport && viewport.IsSequence() && viewport.size() >= 4)
			{
				camera.Position.Normalized.x = viewport[0].as<float>(0.0f);
				camera.Position.Normalized.y = viewport[1].as<float>(0.0f);
				camera.Position.Pixel        = { 0.0f, 0.0f };
				camera.Size.Normalized.x     = viewport[2].as<float>(1.0f);
				camera.Size.Normalized.y     = viewport[3].as<float>(1.0f);
				camera.Size.Pixel            = { 0.0f, 0.0f };
			}
		}
	}

	YAML::Node WriteLight(const Light2D& light)
	{
		const ComponentTypeInfo* ti = GetTypeInfo("Light2D");
		YAML::Node node = ti ? WriteComponentReflected(&light, *ti) : YAML::Node(YAML::NodeType::Map);
		// InnerAngleRadians / OuterAngleRadians: 레지스트리 미등록 필드
		node["InnerAngleRadians"] = light.InnerAngleRadians;
		node["OuterAngleRadians"] = light.OuterAngleRadians;
		return node;
	}

	void ReadLight(const YAML::Node& node, Light2D& light)
	{
		const ComponentTypeInfo* ti = GetTypeInfo("Light2D");
		if (ti) ReadComponentReflected(node, &light, *ti);
		light.InnerAngleRadians = ReadValueOr<float>(node, "InnerAngleRadians", light.InnerAngleRadians);
		light.OuterAngleRadians = ReadValueOr<float>(node, "OuterAngleRadians", light.OuterAngleRadians);
	}

	YAML::Node WriteRigidbody(const Rigidbody2D& rigidbody)
	{
		const ComponentTypeInfo* ti = GetTypeInfo("Rigidbody2D");
		if (!ti) return YAML::Node(YAML::NodeType::Map);
		return WriteComponentReflected(&rigidbody, *ti);
	}

	void ReadRigidbody(const YAML::Node& node, Rigidbody2D& rigidbody)
	{
		const ComponentTypeInfo* ti = GetTypeInfo("Rigidbody2D");
		if (ti) ReadComponentReflected(node, &rigidbody, *ti);
		// InverseMass / InverseInertia 재계산 (레지스트리 미등록 파생 필드)
		rigidbody.SetMass(rigidbody.Mass);
		rigidbody.SetInertia(rigidbody.Inertia);
	}

	YAML::Node WritePolygonCollider(const PolygonCollider2D& collider)
	{
		const ComponentTypeInfo* ti = GetTypeInfo("PolygonCollider2D");
		YAML::Node node = ti ? WriteComponentReflected(&collider, *ti) : YAML::Node(YAML::NodeType::Map);
		// LocalPoints: 레지스트리 미등록 vector 필드
		YAML::Node points(YAML::NodeType::Sequence);
		for (const Vector2<float>& pt : collider.LocalPoints)
			points.push_back(WriteVector2(pt));
		node["LocalPoints"] = points;
		return node;
	}

	void ReadPolygonCollider(const YAML::Node& node, PolygonCollider2D& collider)
	{
		const ComponentTypeInfo* ti = GetTypeInfo("PolygonCollider2D");
		if (ti) ReadComponentReflected(node, &collider, *ti);
		if (collider.VertexCount < 3)
			collider.VertexCount = 3;
		collider.LocalPoints.clear();
		if (const YAML::Node points = node["LocalPoints"]; points && points.IsSequence())
		{
			for (const YAML::Node& ptNode : points)
			{
				Vector2<float> pt;
				if (ReadVector2(ptNode, pt))
					collider.LocalPoints.push_back(pt);
			}
		}
		collider.MarkProceduralBuilt();
		collider.m_convexDirty = true;
	}

	YAML::Node WriteCircleCollider(const CircleCollider2D& collider)
	{
		const ComponentTypeInfo* ti = GetTypeInfo("CircleCollider2D");
		if (!ti) return YAML::Node(YAML::NodeType::Map);
		return WriteComponentReflected(&collider, *ti);
	}

	void ReadCircleCollider(const YAML::Node& node, CircleCollider2D& collider)
	{
		const ComponentTypeInfo* ti = GetTypeInfo("CircleCollider2D");
		if (ti) ReadComponentReflected(node, &collider, *ti);
	}

	// ── 스크립트 필드 직렬화 헬퍼 ────────────────────────────────────────────

	// REFLECT_FIELD 로 등록된 프로퍼티들을 YAML Map 으로 직렬화한다.
	// 인스턴스가 nullptr 이면 빈 노드를 반환한다.
	YAML::Node WriteScriptFields(const CGameScript* instance, const ScriptTypeInfo& typeInfo, std::vector<AssetGuid>* referencedAssets = nullptr)
	{
		YAML::Node node(YAML::NodeType::Map);
		if (nullptr == instance)
		{
			return node;
		}

		for (const ReflectPropertyInfo& prop : typeInfo.Properties)
		{
			const void* field = CReflectionRegistry::GetPropertyAddress(static_cast<const void*>(instance), prop);
			if (nullptr == field)
			{
				continue;
			}

			switch (prop.Type)
			{
			case EReflectPropertyType::Bool:
				node[prop.Name] = *static_cast<const bool*>(field);
				break;
			case EReflectPropertyType::Int32:
				node[prop.Name] = *static_cast<const std::int32_t*>(field);
				break;
			case EReflectPropertyType::UInt32:
				node[prop.Name] = *static_cast<const std::uint32_t*>(field);
				break;
			case EReflectPropertyType::Float:
			case EReflectPropertyType::AngleDegrees:
				node[prop.Name] = *static_cast<const float*>(field);
				break;
			case EReflectPropertyType::Vector2Float:
				node[prop.Name] = WriteVector2(*static_cast<const Vector2<float>*>(field));
				break;
			case EReflectPropertyType::AssetGuid:
				{
					const File::Guid& guid = *static_cast<const File::Guid*>(field);
					node[prop.Name] = guid.generic_string();
					if (referencedAssets)
					{
						AddReferencedAsset(*referencedAssets, guid);
					}
				}
				break;
			default:
				break;
			}
		}

		return node;
	}

	// YAML Map 에서 필드 값을 읽어 ScriptPendingField 목록으로 변환한다.
	// 씬 로드 또는 핫리로드 복원 시 ScriptComponent::PendingFields 에 채운다.
	void ReadScriptFields(
		const YAML::Node& node,
		std::vector<ScriptPendingField>& out,
		const ScriptTypeInfo& typeInfo,
		std::vector<AssetGuid>* referencedAssets = nullptr)
	{
		for (const ReflectPropertyInfo& prop : typeInfo.Properties)
		{
			if (!node[prop.Name])
			{
				continue;
			}

			ScriptPendingField pending;
			pending.Name = prop.Name;
			pending.Type = prop.Type;
			if (EReflectPropertyType::AssetGuid != prop.Type)
			{
				pending.Data.resize(prop.Size, 0);
			}

			try
			{
				switch (prop.Type)
				{
				case EReflectPropertyType::Bool:
				{
					bool v = node[prop.Name].as<bool>(false);
					std::memcpy(pending.Data.data(), &v, sizeof(v));
					break;
				}
				case EReflectPropertyType::Int32:
				{
					std::int32_t v = node[prop.Name].as<std::int32_t>(0);
					std::memcpy(pending.Data.data(), &v, sizeof(v));
					break;
				}
				case EReflectPropertyType::UInt32:
				{
					std::uint32_t v = node[prop.Name].as<std::uint32_t>(0);
					std::memcpy(pending.Data.data(), &v, sizeof(v));
					break;
				}
				case EReflectPropertyType::Float:
				case EReflectPropertyType::AngleDegrees:
				{
					float v = node[prop.Name].as<float>(0.0f);
					std::memcpy(pending.Data.data(), &v, sizeof(v));
					break;
				}
				case EReflectPropertyType::Vector2Float:
				{
					Vector2<float> v;
					ReadVector2(node[prop.Name], v);
					std::memcpy(pending.Data.data(), &v, sizeof(v));
					break;
				}
				case EReflectPropertyType::AssetGuid:
				{
					pending.Text = node[prop.Name].as<std::string>("");
					if (referencedAssets)
					{
						AddReferencedAsset(*referencedAssets, File::Guid(pending.Text));
					}
					break;
				}
				default:
					continue; // 지원하지 않는 타입은 기본값 유지
				}
			}
			catch (const YAML::Exception&)
			{
				continue;
			}

			out.push_back(std::move(pending));
		}
	}
}

ESceneSerializeResult CSceneSerializer::SerializeToText(const CScene& scene, std::string& outText) const
{
	SceneSnapshot snapshot;
	scene.BuildSnapshot(snapshot);
	std::vector<AssetGuid> referencedAssets;

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
			components["SpriteRenderer2D"] = WriteSpriteRenderer(object, referencedAssets);
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
			AddReferencedAsset(referencedAssets, object.SourcePrefabGuid);
			components["PrefabInstance"] = prefab;
		}

		// ── ScriptComponent 직렬화 ──────────────────────────────────────────
		// SceneSnapshot 이 스크립트를 포함하지 않으므로 씬을 직접 조회한다.
		if (Core::Reflection)
		{
			CScene& mutableScene = const_cast<CScene&>(scene);
			ScriptComponent* scriptComp = mutableScene.GetComponent<ScriptComponent>(object.Entity);
			if (scriptComp && scriptComp->ScriptTypeId != INVALID_TYPE_ID)
			{
				YAML::Node scriptNode(YAML::NodeType::Map);
				const ScriptTypeInfo* scriptInfo = Core::Reflection->FindScript(scriptComp->ScriptTypeId);
				if (scriptInfo && scriptInfo->Type.Name)
				{
					scriptNode["Type"]      = scriptInfo->Type.Name;
					scriptNode["IsEnabled"] = scriptComp->IsEnabled;

					if (!scriptInfo->Properties.empty())
					{
						// 인스턴스가 있으면 현재 값, 없으면 PendingFields 를 재직렬화
						if (scriptComp->Instance)
						{
							scriptNode["Fields"] = WriteScriptFields(scriptComp->Instance, *scriptInfo, &referencedAssets);
						}
						else if (!scriptComp->PendingFields.empty())
						{
							// 아직 인스턴스가 없는 경우(DLL 미로드 등) — PendingFields 로 보존
							YAML::Node fields(YAML::NodeType::Map);
							for (const ScriptPendingField& pf : scriptComp->PendingFields)
							{
								// 타입 정보로 올바른 YAML 값 복원
								for (const ReflectPropertyInfo& prop : scriptInfo->Properties)
								{
									if (prop.Name && pf.Name == prop.Name &&
										((EReflectPropertyType::AssetGuid == pf.Type && EReflectPropertyType::AssetGuid == prop.Type) ||
										pf.Data.size() == prop.Size))
									{
										const void* src = pf.Data.empty() ? nullptr : pf.Data.data();
										switch (prop.Type)
										{
										case EReflectPropertyType::Bool:
											fields[prop.Name] = *static_cast<const bool*>(src); break;
										case EReflectPropertyType::Int32:
											fields[prop.Name] = *static_cast<const std::int32_t*>(src); break;
										case EReflectPropertyType::UInt32:
											fields[prop.Name] = *static_cast<const std::uint32_t*>(src); break;
										case EReflectPropertyType::Float:
										case EReflectPropertyType::AngleDegrees:
											fields[prop.Name] = *static_cast<const float*>(src); break;
										case EReflectPropertyType::Vector2Float:
											fields[prop.Name] = WriteVector2(*static_cast<const Vector2<float>*>(src)); break;
										case EReflectPropertyType::AssetGuid:
											fields[prop.Name] = pf.Text;
											AddReferencedAsset(referencedAssets, File::Guid(pf.Text));
											break;
										default: break;
										}
										break;
									}
								}
							}
							scriptNode["Fields"] = fields;
						}
					}
				}
				else
				{
					// 타입 정보 없음(DLL 미로드): TypeId 해시를 문자열로 보존
					scriptNode["TypeId"]    = scriptComp->ScriptTypeId;
					scriptNode["IsEnabled"] = scriptComp->IsEnabled;
				}
				components["Script"] = scriptNode;
			}
		}

		objectNode["Components"] = components;
		objects.push_back(objectNode);
	}

	root["Objects"] = objects;
	root["ReferencedAssets"] = WriteReferencedAssets(referencedAssets);

	YAML::Emitter emitter;
	emitter << root;
	outText = emitter.c_str();
	outText.push_back('\n');
	const_cast<CScene&>(scene).SetReferencedAssets(std::move(referencedAssets));
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
	std::vector<AssetGuid> referencedAssets = ReadReferencedAssets(root["ReferencedAssets"]);

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
					AddReferencedAsset(referencedAssets, sprite->SpriteGuid);
					AddReferencedAsset(referencedAssets, sprite->MaterialGuid);
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

			// 단일 인스턴스 ECS: PolygonCollider2D 는 엔티티당 1개.
			// 기존 시퀀스 포맷("PolygonColliders") 호환을 위해 첫 항목만 사용.
			// 레거시 단일 포맷("PolygonCollider2D") 도 함께 지원.
			if (const YAML::Node seqNode = components["PolygonColliders"]; seqNode && seqNode.IsSequence() && seqNode.size() > 0)
			{
				if (PolygonCollider2D* collider = object.AddComponent<PolygonCollider2D>())
				{
					ReadPolygonCollider(seqNode[0], *collider);
				}
			}
			else if (const YAML::Node polygonNode = components["PolygonCollider2D"])
			{
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
					AddReferencedAsset(referencedAssets, prefab->SourcePrefabGuid);
				}
			}

			// ── ScriptComponent 역직렬화 ────────────────────────────────────
			if (const YAML::Node scriptNode = components["Script"])
			{
				ScriptComponent* sc = object.AddComponent<ScriptComponent>();
				if (sc)
				{
					sc->IsEnabled = ReadValueOr<bool>(scriptNode, "IsEnabled", true);

					const std::string typeName = ReadValueOr<std::string>(scriptNode, "Type", "");
					if (!typeName.empty() && Core::Reflection)
					{
						const ScriptTypeInfo* info = Core::Reflection->FindScriptByName(typeName.c_str());
						if (info)
						{
							sc->ScriptTypeId = info->Type.Id;
							// Fields 복원: 인스턴스는 ScriptSystem 에서 지연 생성되므로
							// PendingFields 에 보관했다가 생성 시 적용한다.
							if (const YAML::Node fieldsNode = scriptNode["Fields"])
							{
								ReadScriptFields(fieldsNode, sc->PendingFields, *info, &referencedAssets);
							}
						}
						else
						{
							// DLL 미로드: 이름 해시로 TypeId 를 보존해 둔다.
							// DLL 이 로드되면 같은 이름으로 등록된 타입을 찾아 쓸 수 있다.
							sc->ScriptTypeId = CReflectionRegistry::MakeTypeId(typeName.c_str());
						}
					}
					else if (scriptNode["TypeId"])
					{
						// 레거시: TypeId 숫자 직렬화 (하위 호환)
						try { sc->ScriptTypeId = scriptNode["TypeId"].as<TypeId>(INVALID_TYPE_ID); }
						catch (...) {}
					}
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

	scene.SetReferencedAssets(std::move(referencedAssets));
	return ESceneSerializeResult::Success;
}

ESceneSerializeResult CSceneSerializer::SaveToFile(const CScene& scene, const File::Path& path) const
{
	if (path.empty())
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

ESceneSerializeResult CSceneSerializer::LoadFromFile(CScene& scene, const File::Path& path) const
{
	if (path.empty())
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
