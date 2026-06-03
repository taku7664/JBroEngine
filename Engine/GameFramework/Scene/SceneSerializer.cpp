#include "pch.h"
#include "SceneSerializer.h"

#include "Core/Core.h"
#include "GameFramework/Component/Camera2D.h"
#include "GameFramework/Component/Light2D.h"
#include "GameFramework/Component/AudioComponents.h"
#include "GameFramework/Component/Physics2DComponents.h"
#include "GameFramework/Component/PrefabInstance.h"
#include "GameFramework/Component/ScriptComponent.h"
#include "GameFramework/Component/SpriteRenderer2D.h"
#include "GameFramework/Component/Transform2D.h"
#include "GameFramework/Object/GameObject.h"
#include "GameFramework/Reflection/ReflectionRegistry.h"
#include "GameFramework/Scene/Scene.h"
#include "Utillity/Math/RectT.h"
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

	YAML::Node WriteVector2(const Vector2& value)
	{
		YAML::Node node(YAML::NodeType::Sequence);
		node.push_back(value.x);
		node.push_back(value.y);
		return node;
	}

	bool ReadVector2(const YAML::Node& node, Vector2& outValue)
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

	YAML::Node WriteRect(const Rect& value)
	{
		YAML::Node node(YAML::NodeType::Sequence);
		node.push_back(value.Left);
		node.push_back(value.Top);
		node.push_back(value.Right);
		node.push_back(value.Bottom);
		return node;
	}

	bool ReadRect(const YAML::Node& node, Rect& outValue)
	{
		if (!node || false == node.IsSequence() || node.size() < 4)
		{
			return false;
		}

		try
		{
			outValue.Left = node[0].as<float>();
			outValue.Top = node[1].as<float>();
			outValue.Right = node[2].as<float>();
			outValue.Bottom = node[3].as<float>();
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

	std::string ReadStringField(const void* field, const ReflectPropertyInfo& prop)
	{
		if (prop.ElementCount > 1)
		{
			return std::string(static_cast<const char*>(field));
		}
		return *static_cast<const std::string*>(field);
	}

	void WriteStringField(void* field, const ReflectPropertyInfo& prop, const std::string& value)
	{
		if (prop.ElementCount > 1)
		{
			const std::size_t cap = prop.ElementCount;
			const std::size_t len = std::min(value.size(), cap - 1);
			std::memcpy(field, value.c_str(), len);
			static_cast<char*>(field)[len] = '\0';
			return;
		}
		*static_cast<std::string*>(field) = value;
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

	// ── Ref 직렬화 ───────────────────────────────────────────────────────────
	// 컴포넌트/스크립트 Ref 는 (오브젝트 guid + 컴포넌트 guid) 쌍을 { Guid, ComponentGuid }
	// 맵으로 저장한다. 컴포넌트 guid 가 비어 있으면(오브젝트/에셋 Ref) 단일 스칼라로 저장해
	// 사람이 읽기 쉽게 하고 구포맷과도 호환한다.
	YAML::Node WriteRef(const RefBase& ref)
	{
		if ('\0' == ref.ComponentGuid[0])
		{
			return YAML::Node(std::string(ref.GuidText()));
		}
		YAML::Node node(YAML::NodeType::Map);
		node["Guid"]          = std::string(ref.GuidText());
		node["ComponentGuid"] = std::string(ref.ComponentGuidText());
		return node;
	}

	void ReadRef(const YAML::Node& node, RefBase& ref)
	{
		if (node.IsMap())
		{
			ref.SetGuidText(node["Guid"] ? node["Guid"].as<std::string>("").c_str() : "");
			ref.SetComponentGuidText(node["ComponentGuid"] ? node["ComponentGuid"].as<std::string>("").c_str() : "");
		}
		else
		{
			// 스칼라(오브젝트/에셋 Ref 또는 구포맷): 주 guid 만, 컴포넌트 guid 는 비운다.
			ref.SetGuidText(node.as<std::string>("").c_str());
			ref.SetComponentGuidText("");
		}
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
			case EReflectPropertyType::Int64:
				node[prop.Name] = *static_cast<const std::int64_t*>(field);
				break;
			case EReflectPropertyType::UInt32:
				node[prop.Name] = *static_cast<const std::uint32_t*>(field);
				break;
			case EReflectPropertyType::UInt64:
				node[prop.Name] = *static_cast<const std::uint64_t*>(field);
				break;
			case EReflectPropertyType::Float:
			case EReflectPropertyType::Degree:
			case EReflectPropertyType::Radian:
			case EReflectPropertyType::AngleDegrees:
				node[prop.Name] = *static_cast<const float*>(field);
				break;
			case EReflectPropertyType::Vector2Float:
				node[prop.Name] = WriteVector2(*static_cast<const Vector2*>(field));
				break;
			case EReflectPropertyType::RectFloat:
				node[prop.Name] = WriteRect(*static_cast<const Rect*>(field));
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
			case EReflectPropertyType::Ref:
				{
					// Ref<T> 의 저장부는 RefBase 의 고정 길이 char 버퍼(POD).
					// 컴포넌트 guid 가 있으면(컴포넌트/스크립트 Ref) { Guid, ComponentGuid } 맵으로,
					// 없으면(오브젝트/에셋) 단일 문자열로 저장한다.
					const RefBase* ref = static_cast<const RefBase*>(field);
					node[prop.Name] = WriteRef(*ref);
					// 에셋 참조일 때만 referencedAssets 에 등록(오브젝트/컴포넌트/스크립트는 InstanceGuid).
					if (referencedAssets && ERefCategory::Asset == prop.RefCategory)
					{
						AddReferencedAsset(*referencedAssets, File::Guid(ref->GuidText()));
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
				node[prop.Name] = ReadStringField(field, prop);
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
			case EReflectPropertyType::Int64:
				ReadValue(node, prop.Name, *static_cast<std::int64_t*>(field));
				break;
			case EReflectPropertyType::UInt32:
				ReadValue(node, prop.Name, *static_cast<std::uint32_t*>(field));
				break;
			case EReflectPropertyType::UInt64:
				ReadValue(node, prop.Name, *static_cast<std::uint64_t*>(field));
				break;
			case EReflectPropertyType::Float:
			case EReflectPropertyType::Degree:
			case EReflectPropertyType::Radian:
			case EReflectPropertyType::AngleDegrees:
				ReadValue(node, prop.Name, *static_cast<float*>(field));
				break;
			case EReflectPropertyType::Vector2Float:
				ReadVector2(node[prop.Name], *static_cast<Vector2*>(field));
				break;
			case EReflectPropertyType::RectFloat:
				ReadRect(node[prop.Name], *static_cast<Rect*>(field));
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
			case EReflectPropertyType::Ref:
				{
					if (const YAML::Node refNode = node[prop.Name])
					{
						ReadRef(refNode, *static_cast<RefBase*>(field));
					}
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
						WriteStringField(field, prop, s);
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

	// Transform2D 는 더 이상 컴포넌트가 아니라 CGameObject 멤버 → 레지스트리 없이 직접 직렬화.
	YAML::Node WriteTransform(const Transform2D& transform)
	{
		YAML::Node node(YAML::NodeType::Map);
		node["Position"]        = WriteVector2(transform.Position);
		node["RotationRadians"] = transform.RotationRadians;
		node["Scale"]           = WriteVector2(transform.Scale);
		return node;
	}

	void ReadTransform(const YAML::Node& node, Transform2D& transform)
	{
		if (!node) return;
		ReadVector2(node["Position"], transform.Position);
		ReadValue(node, "RotationRadians", transform.RotationRadians);
		ReadVector2(node["Scale"], transform.Scale);
	}

	YAML::Node WriteSpriteRenderer(const SpriteRenderer2D& sprite, std::vector<AssetGuid>& referencedAssets)
	{
		const ComponentTypeInfo* ti = GetTypeInfo("SpriteRenderer2D");
		if (!ti) return YAML::Node(YAML::NodeType::Map);
		// 등록 프로퍼티(직렬화 대상)만 기록 — 비복사 런타임 캐시(Mesh/Material/AssetRef)는 미등록이라 무시.
		return WriteComponentReflected(&sprite, *ti, &referencedAssets);
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

	YAML::Node WriteAudioPlayer(const AudioPlayer& audioPlayer, std::vector<AssetGuid>& referencedAssets)
	{
		const ComponentTypeInfo* ti = GetTypeInfo("AudioPlayer");
		if (!ti) return YAML::Node(YAML::NodeType::Map);
		// 리플렉션은 등록 프로퍼티(AudioGuid/EffectGuid/Volume...)만 직렬화 — 런타임 캐시 멤버는 무시.
		return WriteComponentReflected(&audioPlayer, *ti, &referencedAssets);
	}

	void ReadAudioPlayer(const YAML::Node& node, AudioPlayer& audioPlayer)
	{
		const ComponentTypeInfo* ti = GetTypeInfo("AudioPlayer");
		if (ti) ReadComponentReflected(node, &audioPlayer, *ti);
	}

	YAML::Node WriteAudioListener(const AudioListener& audioListener)
	{
		const ComponentTypeInfo* ti = GetTypeInfo("AudioListener");
		if (!ti) return YAML::Node(YAML::NodeType::Map);
		return WriteComponentReflected(&audioListener, *ti);
	}

	void ReadAudioListener(const YAML::Node& node, AudioListener& audioListener)
	{
		const ComponentTypeInfo* ti = GetTypeInfo("AudioListener");
		if (ti) ReadComponentReflected(node, &audioListener, *ti);
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
		for (const Vector2& pt : collider.LocalPoints)
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
				Vector2 pt;
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
			if (false == prop.Serialize)
			{
				continue;   // JPROP(NoSerialize) — 씬 파일에 저장하지 않는다.
			}
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
			case EReflectPropertyType::Int64:
				node[prop.Name] = *static_cast<const std::int64_t*>(field);
				break;
			case EReflectPropertyType::UInt32:
				node[prop.Name] = *static_cast<const std::uint32_t*>(field);
				break;
			case EReflectPropertyType::UInt64:
				node[prop.Name] = *static_cast<const std::uint64_t*>(field);
				break;
			case EReflectPropertyType::Float:
			case EReflectPropertyType::Degree:
			case EReflectPropertyType::Radian:
			case EReflectPropertyType::AngleDegrees:
				node[prop.Name] = *static_cast<const float*>(field);
				break;
			case EReflectPropertyType::Vector2Float:
				node[prop.Name] = WriteVector2(*static_cast<const Vector2*>(field));
				break;
			case EReflectPropertyType::RectFloat:
				node[prop.Name] = WriteRect(*static_cast<const Rect*>(field));
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
			case EReflectPropertyType::Ref:
				{
					const RefBase* ref = static_cast<const RefBase*>(field);
					node[prop.Name] = WriteRef(*ref);
					if (referencedAssets && ERefCategory::Asset == prop.RefCategory)
					{
						AddReferencedAsset(*referencedAssets, File::Guid(ref->GuidText()));
					}
				}
				break;
			case EReflectPropertyType::String:
				node[prop.Name] = *static_cast<const std::string*>(field);
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
			if (false == prop.Serialize)
			{
				continue;   // JPROP(NoSerialize) — 씬에서 복원하지 않는다.
			}
			if (!node[prop.Name])
			{
				continue;
			}

			ScriptPendingField pending;
			pending.Name = prop.Name;
			pending.Type = prop.Type;
			// AssetGuid/Ref(File::Guid)·String 은 Text 로 보존 — raw Data 미사용.
			if (EReflectPropertyType::AssetGuid != prop.Type
				&& EReflectPropertyType::Ref != prop.Type
				&& EReflectPropertyType::String != prop.Type)
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
				case EReflectPropertyType::Int64:
				{
					std::int64_t v = node[prop.Name].as<std::int64_t>(0);
					std::memcpy(pending.Data.data(), &v, sizeof(v));
					break;
				}
				case EReflectPropertyType::UInt32:
				{
					std::uint32_t v = node[prop.Name].as<std::uint32_t>(0);
					std::memcpy(pending.Data.data(), &v, sizeof(v));
					break;
				}
				case EReflectPropertyType::UInt64:
				{
					std::uint64_t v = node[prop.Name].as<std::uint64_t>(0);
					std::memcpy(pending.Data.data(), &v, sizeof(v));
					break;
				}
				case EReflectPropertyType::Float:
				case EReflectPropertyType::Degree:
				case EReflectPropertyType::Radian:
				case EReflectPropertyType::AngleDegrees:
				{
					float v = node[prop.Name].as<float>(0.0f);
					std::memcpy(pending.Data.data(), &v, sizeof(v));
					break;
				}
				case EReflectPropertyType::Vector2Float:
				{
					Vector2 v;
					ReadVector2(node[prop.Name], v);
					std::memcpy(pending.Data.data(), &v, sizeof(v));
					break;
				}
				case EReflectPropertyType::RectFloat:
				{
					Rect v;
					ReadRect(node[prop.Name], v);
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
				case EReflectPropertyType::Ref:
				{
					pending.Text = node[prop.Name].as<std::string>("");
					if (referencedAssets && ERefCategory::Asset == prop.RefCategory)
					{
						AddReferencedAsset(*referencedAssets, File::Guid(pending.Text));
					}
					break;
				}
				case EReflectPropertyType::String:
				{
					pending.Text = node[prop.Name].as<std::string>("");
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
	std::vector<AssetGuid> referencedAssets;

	// 결정적 순서 + 부모 인덱스 매핑(ParentIndex 직렬화용).
	std::vector<CGameObject*> objectList;
	const_cast<CScene&>(scene).ForEachObject([&objectList](CGameObject& o){ objectList.push_back(&o); });
	std::unordered_map<const CGameObject*, int> indexOf;
	for (std::size_t i = 0; i < objectList.size(); ++i)
	{
		indexOf.emplace(objectList[i], static_cast<int>(i));
	}

	// 스크립트 컴포넌트 노드 — 인스턴스 현재값 우선, 없으면 PendingFields 재직렬화.
	auto writeScript = [&](ScriptComponent* sc) -> YAML::Node
	{
		YAML::Node scriptNode(YAML::NodeType::Map);
		if (nullptr == sc || INVALID_TYPE_ID == sc->ScriptTypeId || false == static_cast<bool>(Core::Reflection))
		{
			return scriptNode;
		}
		const ScriptTypeInfo* scriptInfo = Core::Reflection->FindScript(sc->ScriptTypeId);
		if (scriptInfo && scriptInfo->Type.Name)
		{
			// 스크립트 타입명은 ScriptType 키에 둔다. 공통 "Type" 키는 디스패치용("Script"),
			// "IsEnabled"/"InstanceGuid" 도 호출부 공통 로직이 부착한다.
			scriptNode["ScriptType"] = scriptInfo->Type.Name;
			if (!scriptInfo->Properties.empty())
			{
				if (sc->Instance)
				{
					scriptNode["Fields"] = WriteScriptFields(sc->Instance, *scriptInfo, &referencedAssets);
				}
				else if (!sc->PendingFields.empty())
				{
					YAML::Node fields(YAML::NodeType::Map);
					for (const ScriptPendingField& pf : sc->PendingFields)
					{
						for (const ReflectPropertyInfo& prop : scriptInfo->Properties)
						{
							if (prop.Name && pf.Name == prop.Name &&
								((EReflectPropertyType::AssetGuid == pf.Type && EReflectPropertyType::AssetGuid == prop.Type) ||
								(EReflectPropertyType::Ref == pf.Type && EReflectPropertyType::Ref == prop.Type) ||
								pf.Data.size() == prop.Size))
							{
								const void* src = pf.Data.empty() ? nullptr : pf.Data.data();
								switch (prop.Type)
								{
								case EReflectPropertyType::Bool:   fields[prop.Name] = *static_cast<const bool*>(src); break;
								case EReflectPropertyType::Int32:  fields[prop.Name] = *static_cast<const std::int32_t*>(src); break;
								case EReflectPropertyType::Int64:  fields[prop.Name] = *static_cast<const std::int64_t*>(src); break;
								case EReflectPropertyType::UInt32: fields[prop.Name] = *static_cast<const std::uint32_t*>(src); break;
								case EReflectPropertyType::UInt64: fields[prop.Name] = *static_cast<const std::uint64_t*>(src); break;
								case EReflectPropertyType::Float:
								case EReflectPropertyType::Degree:
								case EReflectPropertyType::Radian:
								case EReflectPropertyType::AngleDegrees:
									fields[prop.Name] = *static_cast<const float*>(src); break;
								case EReflectPropertyType::Vector2Float:
									fields[prop.Name] = WriteVector2(*static_cast<const Vector2*>(src)); break;
								case EReflectPropertyType::RectFloat:
									fields[prop.Name] = WriteRect(*static_cast<const Rect*>(src)); break;
								case EReflectPropertyType::AssetGuid:
									fields[prop.Name] = pf.Text;
									AddReferencedAsset(referencedAssets, File::Guid(pf.Text));
									break;
								case EReflectPropertyType::Ref:
									fields[prop.Name] = pf.Text;
									if (ERefCategory::Asset == prop.RefCategory)
									{
										AddReferencedAsset(referencedAssets, File::Guid(pf.Text));
									}
									break;
								case EReflectPropertyType::String:
									fields[prop.Name] = pf.Text;
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
			scriptNode["TypeId"]    = sc->ScriptTypeId;
			scriptNode["IsEnabled"] = sc->IsEnabled;
		}
		return scriptNode;
	};

	YAML::Node objects(YAML::NodeType::Sequence);

	for (CGameObject* obj : objectList)
	{
		YAML::Node objectNode(YAML::NodeType::Map);
		objectNode["Name"] = obj->Name;
		if (false == obj->InstanceGuid.IsNull())
		{
			objectNode["InstanceGuid"] = obj->InstanceGuid.generic_string();
		}
		objectNode["Active"] = obj->IsActive;
		objectNode["Layer"]  = obj->Layer;

		CGameObject* parent = obj->GetParent().TryGet();
		const auto parentIt = parent ? indexOf.find(parent) : indexOf.end();
		objectNode["ParentIndex"] = (parentIt != indexOf.end()) ? parentIt->second : -1;

		// Transform 은 컴포넌트가 아니라 오브젝트 멤버 → 객체 노드에 직접 기록.
		objectNode["Transform2D"] = WriteTransform(obj->Local);

		// 컴포넌트는 다형성 — GetTypeName 으로 분기. 등록 프로퍼티는 리플렉션 직렬화,
		// 비반영 추가 필드(Light 각도/Polygon 포인트 등)는 per-type 헬퍼가 처리.
		// 멀티 컴포넌트: 같은 타입이 여럿일 수 있으므로 시퀀스로 저장한다. 각 원소는
		// { Type, InstanceGuid, IsEnabled, ...타입별 필드 } 맵이다.
		YAML::Node components(YAML::NodeType::Sequence);
		for (const SafePtr<CComponent>& cref : obj->GetComponents())
		{
			CComponent* c = cref.TryGet();
			if (nullptr == c)
			{
				continue;
			}
			const char* tn = c->GetTypeName();

			YAML::Node cn;
			const char* serializedType = tn;
			if (0 == std::strcmp(tn, "ScriptComponent"))
			{
				ScriptComponent* sc = static_cast<ScriptComponent*>(c);
				if (sc->ScriptTypeId == INVALID_TYPE_ID)
				{
					continue;   // 타입 미지정 스크립트는 저장하지 않는다.
				}
				cn = writeScript(sc);
				serializedType = "Script";   // 역직렬화 디스패치 키(기존 호환).
			}
			else if (0 == std::strcmp(tn, "SpriteRenderer2D")) cn = WriteSpriteRenderer(*static_cast<SpriteRenderer2D*>(c), referencedAssets);
			else if (0 == std::strcmp(tn, "Camera2D"))         cn = WriteCamera(*static_cast<Camera2D*>(c));
			else if (0 == std::strcmp(tn, "Light2D"))          cn = WriteLight(*static_cast<Light2D*>(c));
			else if (0 == std::strcmp(tn, "AudioPlayer"))      cn = WriteAudioPlayer(*static_cast<AudioPlayer*>(c), referencedAssets);
			else if (0 == std::strcmp(tn, "AudioListener"))    cn = WriteAudioListener(*static_cast<AudioListener*>(c));
			else if (0 == std::strcmp(tn, "Rigidbody2D"))      cn = WriteRigidbody(*static_cast<Rigidbody2D*>(c));
			else if (0 == std::strcmp(tn, "CircleCollider2D")) cn = WriteCircleCollider(*static_cast<CircleCollider2D*>(c));
			else if (0 == std::strcmp(tn, "PolygonCollider2D")) cn = WritePolygonCollider(*static_cast<PolygonCollider2D*>(c));
			else
			{
				const ComponentTypeInfo* ti = GetTypeInfo(tn);
				cn = ti ? WriteComponentReflected(c, *ti, &referencedAssets) : YAML::Node(YAML::NodeType::Map);
			}

			cn["Type"]         = serializedType;
			cn["IsEnabled"]    = c->IsEnabled;   // 공통 enable 플래그(CComponent 베이스).
			if (false == c->InstanceGuid.IsNull())
			{
				cn["InstanceGuid"] = c->InstanceGuid.generic_string();
			}
			components.push_back(cn);
		}

		objectNode["Components"] = components;
		objects.push_back(objectNode);
	}

	// ReferencedAssets 를 파일 상단(Version 바로 뒤, Objects 앞)에 배치한다 — 사람이
	// 씬 파일을 열었을 때 어떤 에셋을 참조하는지 바로 확인할 수 있도록.
	YAML::Node root(YAML::NodeType::Map);
	root["Version"] = SCENE_FILE_VERSION;
	root["ReferencedAssets"] = WriteReferencedAssets(referencedAssets);
	root["Objects"] = objects;

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

	std::vector<CGameObject*> objects;
	std::vector<int> parentIndices;
	for (const YAML::Node& objectNode : objectsNode)
	{
		if (!objectNode || false == objectNode.IsMap())
		{
			return ESceneSerializeResult::ParseError;
		}

		const std::string name = ReadValueOr<std::string>(objectNode, "Name", "GameObject");
		CGameObject* object = scene.CreateGameObject(name.c_str());
		if (nullptr == object)
		{
			return ESceneSerializeResult::ParseError;
		}
		object->SetActive(ReadValueOr<bool>(objectNode, "Active", true));
		object->Layer = ReadValueOr<std::uint32_t>(objectNode, "Layer", 0);
		// 저장된 안정 식별자가 있으면 복원(없으면 CreateGameObject 가 발급한 새 GUID 유지).
		{
			const std::string instanceGuid = ReadValueOr<std::string>(objectNode, "InstanceGuid", "");
			if (false == instanceGuid.empty())
			{
				object->InstanceGuid = File::Guid(instanceGuid);
			}
		}

		// Transform 은 객체 노드에 직접 기록되거나(신 포맷) Components 시퀀스 밖에 있다.
		const YAML::Node transformNode = objectNode["Transform2D"];
		if (transformNode)
		{
			ReadTransform(transformNode, object->GetTransform());
		}

		// 컴포넌트 = 시퀀스. 각 원소의 Type 으로 디스패치하고 InstanceGuid/IsEnabled 를 복원한다.
		// 같은 타입이 여러 번 나오면 그대로 여러 개 부착된다(멀티 컴포넌트).
		const YAML::Node components = objectNode["Components"];
		if (components && components.IsSequence())
		{
			for (const YAML::Node& cn : components)
			{
				if (!cn || false == cn.IsMap())
				{
					continue;
				}
				const std::string type = ReadValueOr<std::string>(cn, "Type", "");
				if (type.empty())
				{
					continue;
				}

				CComponent* added = nullptr;
				if (type == "SpriteRenderer2D")
				{
					if (SpriteRenderer2D* sprite = object->AddComponent<SpriteRenderer2D>())
					{
						ReadSpriteRenderer(cn, *sprite);
						AddReferencedAsset(referencedAssets, sprite->SpriteGuid);
						AddReferencedAsset(referencedAssets, sprite->MaterialGuid);
						added = sprite;
					}
				}
				else if (type == "Camera2D")
				{
					if (Camera2D* camera = object->AddComponent<Camera2D>()) { ReadCamera(cn, *camera); added = camera; }
				}
				else if (type == "Light2D")
				{
					if (Light2D* light = object->AddComponent<Light2D>()) { ReadLight(cn, *light); added = light; }
				}
				else if (type == "AudioPlayer")
				{
					if (AudioPlayer* audioPlayer = object->AddComponent<AudioPlayer>())
					{
						ReadAudioPlayer(cn, *audioPlayer);
						AddReferencedAsset(referencedAssets, audioPlayer->AudioGuid);
						AddReferencedAsset(referencedAssets, audioPlayer->EffectGuid);
						added = audioPlayer;
					}
				}
				else if (type == "AudioListener")
				{
					if (AudioListener* audioListener = object->AddComponent<AudioListener>()) { ReadAudioListener(cn, *audioListener); added = audioListener; }
				}
				else if (type == "Rigidbody2D")
				{
					if (Rigidbody2D* rigidbody = object->AddComponent<Rigidbody2D>()) { ReadRigidbody(cn, *rigidbody); added = rigidbody; }
				}
				else if (type == "PolygonCollider2D")
				{
					if (PolygonCollider2D* collider = object->AddComponent<PolygonCollider2D>()) { ReadPolygonCollider(cn, *collider); added = collider; }
				}
				else if (type == "CircleCollider2D")
				{
					if (CircleCollider2D* collider = object->AddComponent<CircleCollider2D>()) { ReadCircleCollider(cn, *collider); added = collider; }
				}
				else if (type == "PrefabInstance")
				{
					if (PrefabInstance* prefab = object->AddComponent<PrefabInstance>())
					{
						prefab->SourcePrefabGuid = File::Guid(ReadValueOr<std::string>(cn, "SourcePrefabGuid", ""));
						AddReferencedAsset(referencedAssets, prefab->SourcePrefabGuid);
						added = prefab;
					}
				}
				else if (type == "Script")
				{
					if (ScriptComponent* sc = object->AddComponent<ScriptComponent>())
					{
						const std::string typeName = ReadValueOr<std::string>(cn, "ScriptType", ReadValueOr<std::string>(cn, "TypeName", ""));
						if (!typeName.empty() && Core::Reflection)
						{
							const ScriptTypeInfo* info = Core::Reflection->FindScriptByName(typeName.c_str());
							if (info)
							{
								sc->ScriptTypeId = info->Type.Id;
								if (const YAML::Node fieldsNode = cn["Fields"])
								{
									ReadScriptFields(fieldsNode, sc->PendingFields, *info, &referencedAssets);
								}
							}
							else
							{
								// DLL 미로드: 이름 해시로 TypeId 보존(로드되면 같은 이름으로 매칭).
								sc->ScriptTypeId = CReflectionRegistry::MakeTypeId(typeName.c_str());
							}
						}
						else if (cn["TypeId"])
						{
							try { sc->ScriptTypeId = cn["TypeId"].as<TypeId>(INVALID_TYPE_ID); }
							catch (...) {}
						}
						added = sc;
					}
				}
				else
				{
					// 빌트인 외 리플렉션 등록 컴포넌트 — 레지스트리로 추가 후 반영 필드 복원.
					const ComponentTypeInfo* ti = GetTypeInfo(type.c_str());
					if (ti && Core::Reflection && Core::Reflection->AddComponent(scene, *object, ti->Type.Id))
					{
						const std::vector<CComponent*> all = object->GetComponents<CComponent>();
						added = all.empty() ? nullptr : all.back();   // 방금 부착된 것 = 마지막
						if (added)
						{
							ReadComponentReflected(cn, added, *ti);
						}
					}
				}

				if (added)
				{
					added->IsEnabled = ReadValueOr<bool>(cn, "IsEnabled", true);
					const std::string compGuid = ReadValueOr<std::string>(cn, "InstanceGuid", "");
					if (false == compGuid.empty())
					{
						added->InstanceGuid = File::Guid(compGuid);
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

		if (false == objects[i]->SetParent(*objects[static_cast<std::size_t>(parentIndex)]))
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

std::vector<AssetGuid> CSceneSerializer::ReadReferencedAssetsFromFile(const File::Path& path) const
{
	std::vector<AssetGuid> result;
	if (path.empty())
	{
		return result;
	}

	YAML::Node root;
	try
	{
		root = YAML::LoadFile(path.string());
	}
	catch (const YAML::Exception&)
	{
		return result;
	}

	if (!root || false == root.IsMap())
	{
		return result;
	}

	return ReadReferencedAssets(root["ReferencedAssets"]);
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
