#include "pch.h"
#include "GameFramework/Serialization/ComponentSerializer.h"

#include "Core/ScriptCore.h"
#include "GameFramework/Component/Camera2D.h"
#include "GameFramework/Component/Light2D.h"
#include "GameFramework/Component/AudioComponents.h"
#include "GameFramework/Component/Physics2DComponents.h"
#include "GameFramework/Component/PrefabInstance.h"
#include "GameFramework/Scripting/GameScript.h"
#include "GameFramework/Component/ShapeRenderers2D.h"
#include "GameFramework/Component/SpriteRenderer2D.h"
#include "GameFramework/Component/Text2D.h"
#include "GameFramework/Component/Transform2D.h"
#include "GameFramework/Component/Component.h"
#include "GameFramework/Object/GameObject.h"
#include "GameFramework/Object/Ref.h"
#include "GameFramework/Reflection/ReflectionRegistry.h"
#include "GameFramework/Canvas/Canvas.h"
#include "GameFramework/Canvas/CanvasRuntimeAccess.h"
#include "Utillity/Math/RectT.h"
#include "yaml-cpp/yaml.h"

#include <cstring>
#include <algorithm>
#include <sstream>

namespace Serialization
{
namespace
{
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

	// 색은 스칼라 4개 시퀀스로 쓴다 — Color 타입이 생기기 전 `float[4]` 시절과 같은 포맷이라
	// 기존 .jcanvas 가 그대로 읽힌다.
	YAML::Node WriteColor4(const Color& color)
	{
		YAML::Node node(YAML::NodeType::Sequence);
		node.push_back(color.R);
		node.push_back(color.G);
		node.push_back(color.B);
		node.push_back(color.A);
		return node;
	}

	bool ReadColor4(const YAML::Node& node, Color& outColor)
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

	YAML::Node WriteValueByDescriptor(const void* value, const ReflectTypeDesc& descriptor)
	{
		switch (descriptor.Type)
		{
		case EReflectPropertyType::Bool: return YAML::Node(*static_cast<const bool*>(value));
		case EReflectPropertyType::Int32: return YAML::Node(*static_cast<const std::int32_t*>(value));
		case EReflectPropertyType::Int64: return YAML::Node(*static_cast<const std::int64_t*>(value));
		case EReflectPropertyType::UInt32: return YAML::Node(*static_cast<const std::uint32_t*>(value));
		case EReflectPropertyType::UInt64: return YAML::Node(*static_cast<const std::uint64_t*>(value));
		case EReflectPropertyType::Float:
		case EReflectPropertyType::Degree:
		case EReflectPropertyType::Radian:
			return YAML::Node(*static_cast<const float*>(value));
		case EReflectPropertyType::String:
			return YAML::Node(*static_cast<const std::string*>(value));
		case EReflectPropertyType::Vector2Float:
			return WriteVector2(*static_cast<const Vector2*>(value));
		case EReflectPropertyType::RectFloat:
			return WriteRect(*static_cast<const Rect*>(value));
		case EReflectPropertyType::AssetGuid:
			return YAML::Node(static_cast<const File::Guid*>(value)->generic_string());
		case EReflectPropertyType::Ref:
			return WriteRef(*static_cast<const RefBase*>(value));
		default:
			return YAML::Node();
		}
	}

	bool ReadValueByDescriptor(const YAML::Node& node, void* value, const ReflectTypeDesc& descriptor)
	{
		try
		{
			switch (descriptor.Type)
			{
			case EReflectPropertyType::Bool: *static_cast<bool*>(value) = node.as<bool>(); return true;
			case EReflectPropertyType::Int32: *static_cast<std::int32_t*>(value) = node.as<std::int32_t>(); return true;
			case EReflectPropertyType::Int64: *static_cast<std::int64_t*>(value) = node.as<std::int64_t>(); return true;
			case EReflectPropertyType::UInt32: *static_cast<std::uint32_t*>(value) = node.as<std::uint32_t>(); return true;
			case EReflectPropertyType::UInt64: *static_cast<std::uint64_t*>(value) = node.as<std::uint64_t>(); return true;
			case EReflectPropertyType::Float:
			case EReflectPropertyType::Degree:
			case EReflectPropertyType::Radian:
				*static_cast<float*>(value) = node.as<float>(); return true;
			case EReflectPropertyType::String:
				*static_cast<std::string*>(value) = node.as<std::string>(); return true;
			case EReflectPropertyType::Vector2Float:
				ReadVector2(node, *static_cast<Vector2*>(value)); return true;
			case EReflectPropertyType::RectFloat:
				ReadRect(node, *static_cast<Rect*>(value)); return true;
			case EReflectPropertyType::AssetGuid:
				*static_cast<File::Guid*>(value) = File::Guid(node.as<std::string>()); return true;
			case EReflectPropertyType::Ref:
				ReadRef(node, *static_cast<RefBase*>(value)); return true;
			default:
				return false;
			}
		}
		catch (const YAML::Exception&)
		{
			return false;
		}
	}

	YAML::Node WriteArrayValue(const void* field, const ReflectTypeDesc& descriptor)
	{
		YAML::Node result(YAML::NodeType::Sequence);
		if (nullptr == descriptor.ArrayOps || nullptr == descriptor.Element)
		{
			return result;
		}
		const std::size_t count = descriptor.ArrayOps->GetSize(field);
		for (std::size_t index = 0; index < count; ++index)
		{
			const void* element = descriptor.ArrayOps->GetConstElement(field, index);
			if (element)
			{
				result.push_back(WriteValueByDescriptor(element, *descriptor.Element));
			}
		}
		return result;
	}

	bool ReadArrayValue(const YAML::Node& node, void* field, const ReflectTypeDesc& descriptor)
	{
		if (false == node.IsSequence() || nullptr == descriptor.ArrayOps || nullptr == descriptor.Element)
		{
			return false;
		}
		descriptor.ArrayOps->Clear(field);
		for (const YAML::Node& elementNode : node)
		{
			descriptor.ArrayOps->AddDefault(field);
			const std::size_t index = descriptor.ArrayOps->GetSize(field) - 1;
			void* element = descriptor.ArrayOps->GetElement(field, index);
			if (nullptr == element || false == ReadValueByDescriptor(elementNode, element, *descriptor.Element))
			{
				descriptor.ArrayOps->RemoveAt(field, index);
				return false;
			}
		}
		return true;
	}

	// Table 은 {Key, Value} 맵의 **시퀀스**로 적는다. YAML 매핑(`{1: x}`)이 더 짧아 보이지만
	// 그러면 키 타입마다 표현이 갈리고(문자열 키만 자연스럽다), 나중에 키가 구조체로 확장되면
	// 형식이 통째로 바뀐다. 시퀀스는 키 타입과 무관하게 형태가 하나로 유지된다.
	//
	// 순서는 보장하지 않는다 — Table 은 순서가 없고 슬롯 순회 결과는 해시/삽입 이력에 달렸다.
	// 같은 내용이라도 저장할 때마다 줄 순서가 달라질 수 있다.
	YAML::Node WriteTableValue(const void* field, const ReflectTypeDesc& descriptor)
	{
		YAML::Node result(YAML::NodeType::Sequence);
		if (nullptr == descriptor.TableOps || nullptr == descriptor.Key || nullptr == descriptor.Value)
		{
			return result;
		}

		const ReflectTableOps& ops = *descriptor.TableOps;
		for (std::size_t slot = ops.BeginSlot(field);
			InvalidTableSlot != slot;
			slot = ops.NextSlot(field, slot))
		{
			const void* key   = ops.GetKeyAt(field, slot);
			const void* value = ops.GetConstValueAt(field, slot);
			if (nullptr == key || nullptr == value)
			{
				continue;
			}

			YAML::Node entry(YAML::NodeType::Map);
			entry["Key"]   = WriteValueByDescriptor(key, *descriptor.Key);
			entry["Value"] = WriteValueByDescriptor(value, *descriptor.Value);
			result.push_back(entry);
		}
		return result;
	}

	bool ReadTableValue(const YAML::Node& node, void* field, const ReflectTypeDesc& descriptor)
	{
		if (false == node.IsSequence() || nullptr == descriptor.TableOps
			|| nullptr == descriptor.Key || nullptr == descriptor.Value)
		{
			return false;
		}

		const ReflectTableOps& ops = *descriptor.TableOps;
		if (nullptr == ops.CreateKey || nullptr == ops.DestroyKey || nullptr == ops.FindValue)
		{
			return false;
		}

		ops.Clear(field);

		// 키는 타입이 소거돼 있어 스택에 못 만든다 — ops 가 만들어 주는 것 하나를 재활용한다.
		void* key = ops.CreateKey();
		if (nullptr == key)
		{
			return false;
		}

		bool succeeded = true;
		for (const YAML::Node& entryNode : node)
		{
			if (false == entryNode.IsMap()
				|| false == entryNode["Key"].IsDefined()
				|| false == entryNode["Value"].IsDefined())
			{
				succeeded = false;
				break;
			}
			if (false == ReadValueByDescriptor(entryNode["Key"], key, *descriptor.Key))
			{
				succeeded = false;
				break;
			}
			// 중복 키는 파일이 깨진 것이다. InsertDefault 가 false 를 주면 값을 못 채우므로
			// 조용히 넘기지 않고 실패로 본다.
			if (false == ops.InsertDefault(field, key))
			{
				succeeded = false;
				break;
			}

			// 삽입이 리해시했을 수 있어 슬롯 커서로는 방금 넣은 자리를 못 잡는다. 키로 찾는다.
			void* value = ops.FindValue(field, key);
			if (nullptr == value || false == ReadValueByDescriptor(entryNode["Value"], value, *descriptor.Value))
			{
				succeeded = false;
				break;
			}
		}

		ops.DestroyKey(key);
		if (false == succeeded)
		{
			ops.Clear(field);
		}
		return succeeded;
	}

	// Table 값에 들어 있는 에셋 guid 를 패키징용 참조 목록에 모은다.
	// 값만 본다 — 키는 조회용 식별자라 에셋을 키로 삼는 건 설계상 의미가 없고, 실제로 그렇게
	// 쓰이면 값 쪽에 두는 게 맞다. (Array 쪽 수집과 같은 역할이다.)
	void CollectTableAssetReferences(const void* field, const ReflectTypeDesc& descriptor,
		std::vector<AssetGuid>* referencedAssets)
	{
		if (nullptr == referencedAssets || nullptr == descriptor.TableOps || nullptr == descriptor.Value
			|| EReflectPropertyType::AssetGuid != descriptor.Value->Type)
		{
			return;
		}

		const ReflectTableOps& ops = *descriptor.TableOps;
		for (std::size_t slot = ops.BeginSlot(field);
			InvalidTableSlot != slot;
			slot = ops.NextSlot(field, slot))
		{
			const void* value = ops.GetConstValueAt(field, slot);
			if (nullptr != value)
			{
				AddReferencedAsset(*referencedAssets, *static_cast<const AssetGuid*>(value));
			}
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
				node[prop.Name] = *static_cast<const float*>(field);
				break;
			case EReflectPropertyType::Vector2Float:
				node[prop.Name] = WriteVector2(*static_cast<const Vector2*>(field));
				break;
			case EReflectPropertyType::RectFloat:
				node[prop.Name] = WriteRect(*static_cast<const Rect*>(field));
				break;
			case EReflectPropertyType::ColorFloat4:
				node[prop.Name] = WriteColor4(*static_cast<const Color*>(field));
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
					// 메타가 있으면 이름 문자열로 저장(값 재배치에 견고). 없으면 정수 폴백.
					if (prop.Enum && prop.Enum->ToName)
					{
						if (const char* name = prop.Enum->ToName(field, prop.Size))
						{
							node[prop.Name] = std::string(name);
							break;
						}
					}
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
			case EReflectPropertyType::Array:
				if (prop.Descriptor)
				{
					node[prop.Name] = WriteArrayValue(field, *prop.Descriptor);
					if (referencedAssets && prop.Descriptor->Element
						&& EReflectPropertyType::AssetGuid == prop.Descriptor->Element->Type
						&& prop.Descriptor->ArrayOps)
					{
						const std::size_t count = prop.Descriptor->ArrayOps->GetSize(field);
						for (std::size_t index = 0; index < count; ++index)
						{
							const void* element = prop.Descriptor->ArrayOps->GetConstElement(field, index);
							if (element) AddReferencedAsset(*referencedAssets, *static_cast<const AssetGuid*>(element));
						}
					}
				}
				break;
			case EReflectPropertyType::Table:
				if (prop.Descriptor)
				{
					node[prop.Name] = WriteTableValue(field, *prop.Descriptor);
					CollectTableAssetReferences(field, *prop.Descriptor, referencedAssets);
				}
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
				ReadValue(node, prop.Name, *static_cast<float*>(field));
				break;
			case EReflectPropertyType::Vector2Float:
				ReadVector2(node[prop.Name], *static_cast<Vector2*>(field));
				break;
			case EReflectPropertyType::RectFloat:
				ReadRect(node[prop.Name], *static_cast<Rect*>(field));
				break;
			case EReflectPropertyType::ColorFloat4:
				ReadColor4(node[prop.Name], *static_cast<Color*>(field));
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
					// 이름 문자열 우선(FromName). 실패하면(구 데이터=정수, 또는 미등록 enum) 정수 폴백.
					bool applied = false;
					if (prop.Enum && prop.Enum->FromName)
					{
						std::string name;
						if (ReadValue(node, prop.Name, name))
						{
							applied = prop.Enum->FromName(field, prop.Size, name.c_str());
						}
					}
					if (false == applied)
					{
						int val = 0;
						if (ReadValue(node, prop.Name, val))
							std::memcpy(field, &val, std::min(prop.Size, sizeof(int)));
					}
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
			case EReflectPropertyType::Array:
				if (prop.Descriptor)
				{
					ReadArrayValue(node[prop.Name], field, *prop.Descriptor);
				}
				break;
			case EReflectPropertyType::Table:
				if (prop.Descriptor)
				{
					ReadTableValue(node[prop.Name], field, *prop.Descriptor);
				}
				break;
			default:
				break;
			}
		}
	}

	// 타입 이름으로 ComponentTypeInfo를 가져옵니다. Engine.Reflection이 없으면 nullptr 반환.
	const ComponentTypeInfo* GetTypeInfo(const char* name)
	{
		return Script.Reflection ? Script.Reflection->FindComponentByName(name) : nullptr;
	}

	// ── 컴포넌트별 직렬화 (제네릭 + 예외 처리) ──────────────────────────────

	// Transform2D 는 더 이상 컴포넌트가 아니라 CGameObject 멤버 → 레지스트리 없이 직접 직렬화.
	YAML::Node WriteTransform(const Transform2D& transform)
	{
		YAML::Node node(YAML::NodeType::Map);
		node["Position"]        = WriteVector2(transform.Position);
		node["RotationRadians"] = transform.RotationRadians.Value;
		node["Scale"]           = WriteVector2(transform.Scale);
		return node;
	}

	void ReadTransform(const YAML::Node& node, Transform2D& transform)
	{
		if (!node) return;
		ReadVector2(node["Position"], transform.Position);
		ReadValue(node, "RotationRadians", transform.RotationRadians.Value);
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
		// 카메라는 이제 눈일 뿐이다 — 뷰포트 렉트(구 Position/Size/Viewport 키)와 ClearColor·
		// Priority 는 캔버스의 뷰포트 목록·배경색으로 이사했다. 구 캔버스의 그 키들은 읽지 않는다
		// (미등록 키는 리플렉션 읽기가 무시하므로 로드는 그대로 성공한다).
		const ComponentTypeInfo* ti = GetTypeInfo("Camera2D");
		if (ti) ReadComponentReflected(node, &camera, *ti);
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
		// 리플렉션은 등록 프로퍼티(AudioGuid/Volume...)만 직렬화 — 런타임 캐시 멤버는 무시.
		YAML::Node node = WriteComponentReflected(&audioPlayer, *ti, &referencedAssets);

		return node;
	}

	void ReadAudioPlayer(const YAML::Node& node, AudioPlayer& audioPlayer)
	{
		const ComponentTypeInfo* ti = GetTypeInfo("AudioPlayer");
		if (ti) ReadComponentReflected(node, &audioPlayer, *ti);

		// 하위호환: 구 단일 EffectGuid 키가 있으면 새 배열로 마이그레이션.
		if (!node["EffectGuids"] && node["EffectGuid"])
		{
			AssetGuid legacy(node["EffectGuid"].as<std::string>(std::string()));
			if (false == legacy.IsNull())
			{
				audioPlayer.EffectGuids.Add(legacy);
			}
		}
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
				continue;   // JPROP(NoSerialize) — 캔버스 파일에 저장하지 않는다.
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
				node[prop.Name] = *static_cast<const float*>(field);
				break;
			case EReflectPropertyType::Vector2Float:
				node[prop.Name] = WriteVector2(*static_cast<const Vector2*>(field));
				break;
			case EReflectPropertyType::RectFloat:
				node[prop.Name] = WriteRect(*static_cast<const Rect*>(field));
				break;
			case EReflectPropertyType::ColorFloat4:
				node[prop.Name] = WriteColor4(*static_cast<const Color*>(field));
				break;
			case EReflectPropertyType::Layout2D:
				node[prop.Name] = WriteLayout2D(*static_cast<const Layout2D*>(field));
				break;
			case EReflectPropertyType::Enum:
				{
					// 컴포넌트 경로와 같은 규칙 — 이름 우선, 메타가 없으면 정수 폴백.
					if (prop.Enum && prop.Enum->ToName)
					{
						if (const char* name = prop.Enum->ToName(field, prop.Size))
						{
							node[prop.Name] = std::string(name);
							break;
						}
					}
					int value = 0;
					std::memcpy(&value, field, std::min(prop.Size, sizeof(int)));
					node[prop.Name] = value;
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
			case EReflectPropertyType::Array:
				// 컴포넌트 경로(WriteReflectedFields)와 같은 헬퍼를 탄다 — 원소 타입 분기는
				// descriptor.Element 가 들고 있으므로 여기서 다시 나눌 필요가 없다.
				if (prop.Descriptor)
				{
					node[prop.Name] = WriteArrayValue(field, *prop.Descriptor);
					if (referencedAssets && prop.Descriptor->Element
						&& EReflectPropertyType::AssetGuid == prop.Descriptor->Element->Type
						&& prop.Descriptor->ArrayOps)
					{
						const std::size_t count = prop.Descriptor->ArrayOps->GetSize(field);
						for (std::size_t index = 0; index < count; ++index)
						{
							const void* element = prop.Descriptor->ArrayOps->GetConstElement(field, index);
							if (element) AddReferencedAsset(*referencedAssets, *static_cast<const AssetGuid*>(element));
						}
					}
				}
				break;
			case EReflectPropertyType::Table:
				if (prop.Descriptor)
				{
					node[prop.Name] = WriteTableValue(field, *prop.Descriptor);
					CollectTableAssetReferences(field, *prop.Descriptor, referencedAssets);
				}
				break;
			default:
				break;
			}
		}

		return node;
	}

	void ReadScriptFields(
		const YAML::Node& node,
		CGameScript& script,
		const ScriptTypeInfo& typeInfo,
		std::vector<AssetGuid>* referencedAssets = nullptr)
	{
		for (const ReflectPropertyInfo& prop : typeInfo.Properties)
		{
			if (false == prop.Serialize)
			{
				continue;   // JPROP(NoSerialize) — 캔버스에서 복원하지 않는다.
			}
			if (!node[prop.Name])
			{
				continue;
			}

			void* field = CReflectionRegistry::GetPropertyAddress(&script, prop);
			if (nullptr == field) continue;

			try
			{
				switch (prop.Type)
				{
				case EReflectPropertyType::Bool:
				{
					bool v = node[prop.Name].as<bool>(false);
					*static_cast<bool*>(field) = v;
					break;
				}
				case EReflectPropertyType::Int32:
				{
					std::int32_t v = node[prop.Name].as<std::int32_t>(0);
					*static_cast<std::int32_t*>(field) = v;
					break;
				}
				case EReflectPropertyType::Int64:
				{
					std::int64_t v = node[prop.Name].as<std::int64_t>(0);
					*static_cast<std::int64_t*>(field) = v;
					break;
				}
				case EReflectPropertyType::UInt32:
				{
					std::uint32_t v = node[prop.Name].as<std::uint32_t>(0);
					*static_cast<std::uint32_t*>(field) = v;
					break;
				}
				case EReflectPropertyType::UInt64:
				{
					std::uint64_t v = node[prop.Name].as<std::uint64_t>(0);
					*static_cast<std::uint64_t*>(field) = v;
					break;
				}
				case EReflectPropertyType::Float:
				case EReflectPropertyType::Degree:
				case EReflectPropertyType::Radian:
				{
					float v = node[prop.Name].as<float>(0.0f);
					*static_cast<float*>(field) = v;
					break;
				}
				case EReflectPropertyType::Vector2Float:
				{
					Vector2 v;
					ReadVector2(node[prop.Name], v);
					*static_cast<Vector2*>(field) = v;
					break;
				}
				case EReflectPropertyType::RectFloat:
				{
					Rect v;
					ReadRect(node[prop.Name], v);
					*static_cast<Rect*>(field) = v;
					break;
				}
				case EReflectPropertyType::ColorFloat4:
				{
					ReadColor4(node[prop.Name], *static_cast<Color*>(field));
					break;
				}
				case EReflectPropertyType::Layout2D:
				{
					Layout2D* layout = static_cast<Layout2D*>(field);
					*layout = ReadLayout2D(node[prop.Name], *layout);
					break;
				}
				case EReflectPropertyType::Enum:
				{
					bool applied = false;
					if (prop.Enum && prop.Enum->FromName)
					{
						std::string name;
						if (ReadValue(node, prop.Name, name))
						{
							applied = prop.Enum->FromName(field, prop.Size, name.c_str());
						}
					}
					if (false == applied)
					{
						int value = 0;
						if (ReadValue(node, prop.Name, value))
						{
							std::memcpy(field, &value, std::min(prop.Size, sizeof(int)));
						}
					}
					break;
				}
				case EReflectPropertyType::AssetGuid:
				{
					const std::string text = node[prop.Name].as<std::string>("");
					*static_cast<File::Guid*>(field) = File::Guid(text);
					if (referencedAssets)
					{
						AddReferencedAsset(*referencedAssets, File::Guid(text));
					}
					break;
				}
				case EReflectPropertyType::Ref:
				{
					const std::string text = node[prop.Name].as<std::string>("");
					static_cast<RefBase*>(field)->SetGuidText(text.c_str());
					if (referencedAssets && ERefCategory::Asset == prop.RefCategory)
					{
						AddReferencedAsset(*referencedAssets, File::Guid(text));
					}
					break;
				}
				case EReflectPropertyType::String:
				{
					*static_cast<std::string*>(field) = node[prop.Name].as<std::string>("");
					break;
				}
				case EReflectPropertyType::Array:
				{
					if (prop.Descriptor)
					{
						ReadArrayValue(node[prop.Name], field, *prop.Descriptor);
						if (referencedAssets && prop.Descriptor->Element
							&& EReflectPropertyType::AssetGuid == prop.Descriptor->Element->Type
							&& prop.Descriptor->ArrayOps)
						{
							const std::size_t count = prop.Descriptor->ArrayOps->GetSize(field);
							for (std::size_t index = 0; index < count; ++index)
							{
								const void* element = prop.Descriptor->ArrayOps->GetConstElement(field, index);
								if (element) AddReferencedAsset(*referencedAssets, *static_cast<const AssetGuid*>(element));
							}
						}
					}
					break;
				}
				case EReflectPropertyType::Table:
				{
					if (prop.Descriptor)
					{
						ReadTableValue(node[prop.Name], field, *prop.Descriptor);
						CollectTableAssetReferences(field, *prop.Descriptor, referencedAssets);
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
		}
	}

	// 스크립트 컴포넌트 1개를 YAML 노드로 직렬화한다(원본 writeScript 람다 → 함수화).
	// 공통 메타(Type/IsEnabled/InstanceGuid)는 호출부(WriteComponent)가 부착한다.
	YAML::Node WriteScriptNode(const CGameScript* script, std::vector<AssetGuid>& referencedAssets)
	{
		YAML::Node scriptNode(YAML::NodeType::Map);
		if (nullptr == script || INVALID_TYPE_ID == script->GetTypeId() || false == static_cast<bool>(Script.Reflection))
		{
			return scriptNode;
		}
		const ScriptTypeInfo* scriptInfo = Script.Reflection->FindScript(script->GetTypeId());
		if (scriptInfo && scriptInfo->Type.Name)
		{
			// 스크립트 타입명은 ScriptType 키에 둔다. 공통 "Type" 키는 디스패치용("Script"),
			// "IsEnabled"/"InstanceGuid" 도 호출부 공통 로직이 부착한다.
			scriptNode["ScriptType"] = scriptInfo->Type.Name;
			scriptNode["Fields"] = WriteScriptFields(script, *scriptInfo, &referencedAssets);
		}
		else
		{
			scriptNode["TypeId"] = script->GetTypeId();
		}
		return scriptNode;
	}

} // anonymous namespace

// ── 공개 API ────────────────────────────────────────────────────────────────

YAML::Node WriteComponent(const CComponent& component, std::vector<AssetGuid>* referencedAssets)
{
	std::vector<AssetGuid> localAssets;
	std::vector<AssetGuid>& assets = referencedAssets ? *referencedAssets : localAssets;

	CComponent* c = const_cast<CComponent*>(&component);
	const char* tn = c->GetTypeName();

	YAML::Node cn;
	const char* serializedType = tn;
	const CGameObject* owner = c->GetOwner().TryGet();
	const CGameCanvas* canvas = owner ? owner->GetCanvas() : nullptr;
	if (const CGameScript* script = canvas ? CCanvasRuntimeAccess::AsScript(*canvas, c) : nullptr)
	{
		cn = WriteScriptNode(script, assets);
		serializedType = "Script";
	}
	else if (0 == std::strcmp(tn, "SpriteRenderer2D")) cn = WriteSpriteRenderer(*static_cast<SpriteRenderer2D*>(c), assets);
	else if (0 == std::strcmp(tn, "Camera2D"))         cn = WriteCamera(*static_cast<Camera2D*>(c));
	else if (0 == std::strcmp(tn, "Light2D"))          cn = WriteLight(*static_cast<Light2D*>(c));
	else if (0 == std::strcmp(tn, "AudioPlayer"))      cn = WriteAudioPlayer(*static_cast<AudioPlayer*>(c), assets);
	else if (0 == std::strcmp(tn, "AudioListener"))    cn = WriteAudioListener(*static_cast<AudioListener*>(c));
	else if (0 == std::strcmp(tn, "Rigidbody2D"))      cn = WriteRigidbody(*static_cast<Rigidbody2D*>(c));
	else if (0 == std::strcmp(tn, "CircleCollider2D")) cn = WriteCircleCollider(*static_cast<CircleCollider2D*>(c));
	else if (0 == std::strcmp(tn, "PolygonCollider2D")) cn = WritePolygonCollider(*static_cast<PolygonCollider2D*>(c));
	else
	{
		const ComponentTypeInfo* ti = GetTypeInfo(tn);
		cn = ti ? WriteComponentReflected(c, *ti, &assets) : YAML::Node(YAML::NodeType::Map);
	}

	cn["Type"]      = serializedType;
	cn["IsEnabled"] = c->IsEnabled();
	if (false == c->GetInstanceGuid().IsNull())
	{
		cn["InstanceGuid"] = c->GetInstanceGuid().generic_string();
	}
	return cn;
}

CComponent* ReadComponentInto(CGameObject& object, const YAML::Node& node,
                              std::vector<AssetGuid>* referencedAssets)
{
	if (!node || false == node.IsMap())
	{
		return nullptr;
	}
	std::vector<AssetGuid> localAssets;
	std::vector<AssetGuid>& assets = referencedAssets ? *referencedAssets : localAssets;

	const std::string type = ReadValueOr<std::string>(node, "Type", "");
	if (type.empty())
	{
		return nullptr;
	}

	CComponent* added = nullptr;
	if (type == "SpriteRenderer2D")
	{
		if (SpriteRenderer2D* sprite = object.AddComponent<SpriteRenderer2D>())
		{
			ReadSpriteRenderer(node, *sprite);
			AddReferencedAsset(assets, sprite->SpriteGuid);
			AddReferencedAsset(assets, sprite->MaterialGuid);
			added = sprite;
		}
	}
	else if (type == "Camera2D")
	{
		if (Camera2D* camera = object.AddComponent<Camera2D>()) { ReadCamera(node, *camera); added = camera; }
	}
	else if (type == "Text2D")
	{
		if (Text2D* text = object.AddComponent<Text2D>())
		{
			if (const ComponentTypeInfo* ti = GetTypeInfo("Text2D"))
			{
				ReadComponentReflected(node, text, *ti);
			}
			text->FontSizePixels = std::max(1.0f, text->FontSizePixels);
			text->WidthPixels = std::max(0.0f, text->WidthPixels);
			text->HeightPixels = std::max(0.0f, text->HeightPixels);
			text->MinFontSizePixels = std::max(1.0f, text->MinFontSizePixels);
			text->MaxFontSizePixels = std::max(text->MinFontSizePixels, text->MaxFontSizePixels);
			text->LineSpacing = std::max(0.01f, text->LineSpacing);
			text->OutlineWidthPixels = std::max(0.0f, text->OutlineWidthPixels);
			AddReferencedAsset(assets, text->FontFamilyGuid);
			added = text;
		}
	}
	else if (type == "Square2D")
	{
		if (Square2D* shape = object.AddComponent<Square2D>())
		{
			if (const ComponentTypeInfo* ti = GetTypeInfo("Square2D"))
			{
				ReadComponentReflected(node, shape, *ti);
			}
			shape->Size.x = std::abs(shape->Size.x);
			shape->Size.y = std::abs(shape->Size.y);
			shape->OutlineWidth = std::max(0.0f, shape->OutlineWidth);
			added = shape;
		}
	}
	else if (type == "Circle2D")
	{
		if (Circle2D* shape = object.AddComponent<Circle2D>())
		{
			if (const ComponentTypeInfo* ti = GetTypeInfo("Circle2D"))
			{
				ReadComponentReflected(node, shape, *ti);
			}
			shape->Radius = std::abs(shape->Radius);
			shape->Segments = std::clamp(shape->Segments, 8u, 256u);
			shape->OutlineWidth = std::max(0.0f, shape->OutlineWidth);
			added = shape;
		}
	}
	else if (type == "Polygon2D")
	{
		if (Polygon2D* shape = object.AddComponent<Polygon2D>())
		{
			if (const ComponentTypeInfo* ti = GetTypeInfo("Polygon2D"))
			{
				ReadComponentReflected(node, shape, *ti);
			}
			shape->Radius = std::abs(shape->Radius);
			shape->VertexCount = std::clamp(shape->VertexCount, 3u, 256u);
			shape->OutlineWidth = std::max(0.0f, shape->OutlineWidth);
			added = shape;
		}
	}
	else if (type == "Light2D")
	{
		if (Light2D* light = object.AddComponent<Light2D>()) { ReadLight(node, *light); added = light; }
	}
	else if (type == "AudioPlayer")
	{
		if (AudioPlayer* audioPlayer = object.AddComponent<AudioPlayer>())
		{
			ReadAudioPlayer(node, *audioPlayer);
			AddReferencedAsset(assets, audioPlayer->AudioGuid);
			for (const AssetGuid& effectGuid : audioPlayer->EffectGuids)
			{
				AddReferencedAsset(assets, effectGuid);
			}
			added = audioPlayer;
		}
	}
	else if (type == "AudioListener")
	{
		if (AudioListener* audioListener = object.AddComponent<AudioListener>()) { ReadAudioListener(node, *audioListener); added = audioListener; }
	}
	else if (type == "Rigidbody2D")
	{
		if (Rigidbody2D* rigidbody = object.AddComponent<Rigidbody2D>()) { ReadRigidbody(node, *rigidbody); added = rigidbody; }
	}
	else if (type == "PolygonCollider2D")
	{
		if (PolygonCollider2D* collider = object.AddComponent<PolygonCollider2D>()) { ReadPolygonCollider(node, *collider); added = collider; }
	}
	else if (type == "CircleCollider2D")
	{
		if (CircleCollider2D* collider = object.AddComponent<CircleCollider2D>()) { ReadCircleCollider(node, *collider); added = collider; }
	}
	else if (type == "PrefabInstance")
	{
		if (PrefabInstance* prefab = object.AddComponent<PrefabInstance>())
		{
			prefab->SourcePrefabGuid = File::Guid(ReadValueOr<std::string>(node, "SourcePrefabGuid", ""));
			AddReferencedAsset(assets, prefab->SourcePrefabGuid);
			added = prefab;
		}
	}
	else if (type == "Script")
	{
		const std::string scriptTypeName = ReadValueOr<std::string>(node, "ScriptType", ReadValueOr<std::string>(node, "TypeName", ""));
		if (false == scriptTypeName.empty() && Script.Reflection && object.GetCanvas())
		{
			const ScriptTypeInfo* scriptInfo = Script.Reflection->FindScriptByName(scriptTypeName.c_str());
			if (scriptInfo)
			{
				CGameScript* script = CCanvasRuntimeAccess::AddScript(
					*object.GetCanvas(),
					object,
					scriptInfo->Type.Id,
					*Script.Reflection);
				if (script)
				{
					if (const YAML::Node fields = node["Fields"])
					{
						ReadScriptFields(fields, *script, *scriptInfo, &assets);
					}
					added = script;
				}
			}
		}
	}
	else
	{
		const ComponentTypeInfo* ti = GetTypeInfo(type.c_str());
		if (ti && Script.Reflection && Script.Reflection->AddComponent(*object.GetCanvas(), object, ti->Type.Id))
		{
			const std::vector<SafePtr<CComponent>>& components = object.GetComponents();
			added = components.empty() ? nullptr : components.back().TryGet();
			if (added)
			{
				ReadComponentReflected(node, added, *ti);
			}
		}
	}

	if (added)
	{
		added->SetEnabled(ReadValueOr<bool>(node, "IsEnabled", true));
		const std::string compGuid = ReadValueOr<std::string>(node, "InstanceGuid", "");
		if (false == compGuid.empty())
		{
			File::Guid restoredGuid(compGuid);
			if (CCanvasRuntimeAccess::FindComponentByGuid(object, restoredGuid))
			{
				restoredGuid = File::GenerateGuid();
			}
			CCanvasRuntimeAccess::SetComponentInstanceGuid(*added, restoredGuid);
		}
	}
	return added;
}

std::string SerializeComponent(const CComponent& component)
{
	const YAML::Node node = WriteComponent(component, nullptr);
	if (false == node.IsDefined())
	{
		return std::string();
	}
	YAML::Emitter emitter;
	emitter << node;
	return std::string(emitter.c_str());
}

bool DeserializeComponentInto(CGameObject& object, const char* text)
{
	if (nullptr == text)
	{
		return false;
	}
	YAML::Node node;
	try { node = YAML::Load(text); }
	catch (const YAML::Exception&) { return false; }
	return nullptr != ReadComponentInto(object, node, nullptr);
}

bool LooksLikeComponent(const char* text)
{
	if (nullptr == text)
	{
		return false;
	}
	YAML::Node node;
	try { node = YAML::Load(text); }
	catch (const YAML::Exception&) { return false; }
	// 단일 컴포넌트: Type 키 보유 + 오브젝트 컨테이너 키(Components) 없음.
	return node.IsMap() && node["Type"] && !node["Components"];
}

std::string SerializeReflectedPropertyValue(const void* field, const ReflectPropertyInfo& property)
{
	if (nullptr == field || nullptr == property.Descriptor)
	{
		return std::string();
	}
	YAML::Node node;
	if (EReflectPropertyType::Array == property.Type)
	{
		node = WriteArrayValue(field, *property.Descriptor);
	}
	else if (EReflectPropertyType::Table == property.Type)
	{
		node = WriteTableValue(field, *property.Descriptor);
	}
	else
	{
		node = WriteValueByDescriptor(field, *property.Descriptor);
	}
	if (false == node.IsDefined())
	{
		return std::string();
	}
	YAML::Emitter emitter;
	emitter << node;
	return std::string(emitter.c_str());
}

bool DeserializeReflectedPropertyValue(void* field, const ReflectPropertyInfo& property, const char* text)
{
	if (nullptr == field || nullptr == property.Descriptor || nullptr == text)
	{
		return false;
	}
	YAML::Node node;
	try
	{
		node = YAML::Load(text);
	}
	catch (const YAML::Exception&)
	{
		return false;
	}
	if (EReflectPropertyType::Array == property.Type)
	{
		return ReadArrayValue(node, field, *property.Descriptor);
	}
	if (EReflectPropertyType::Table == property.Type)
	{
		return ReadTableValue(node, field, *property.Descriptor);
	}
	return ReadValueByDescriptor(node, field, *property.Descriptor);
}

bool IsSerializedContainerProperty(const ReflectPropertyInfo& property)
{
	if (nullptr == property.Descriptor)
	{
		return false;
	}
	return EReflectPropertyType::Array == property.Type
		|| EReflectPropertyType::Table == property.Type;
}

} // namespace Serialization
