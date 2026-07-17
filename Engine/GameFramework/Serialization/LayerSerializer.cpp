#include "pch.h"
#include "GameFramework/Serialization/LayerSerializer.h"

#include "GameFramework/Object/GameObject.h"
#include "GameFramework/Canvas/GameLayer.h"
#include "GameFramework/Canvas/Canvas.h"
#include "GameFramework/Canvas/CanvasRuntimeAccess.h"
#include "GameFramework/Serialization/ObjectSerializer.h"

#include "yaml-cpp/yaml.h"

#include <algorithm>

namespace Serialization
{

namespace
{
	// 레이어 파일 포맷 버전. 캔버스 파일과 독립적으로 움직인다(레이어 노드 자체는 공유하지만
	// 파일 껍데기는 다르다).
	constexpr std::uint32_t LAYER_FILE_VERSION = 1;

	// 파일이 되살릴 오브젝트 guid 가 이미 캔버스에 살아 있는지. 하나라도 겹치면 로드를 거부한다.
	// 자식은 Children 에 중첩돼 있으므로 재귀로 훑는다.
	bool AnyInstanceGuidAlive(CGameCanvas& canvas, const YAML::Node& objectNode)
	{
		if (!objectNode || false == objectNode.IsMap())
		{
			return false;
		}

		if (const YAML::Node guidNode = objectNode["InstanceGuid"]; guidNode)
		{
			const std::string guidText = guidNode.as<std::string>("");
			if (false == guidText.empty())
			{
				const File::Guid guid(guidText);
				if (false == guid.IsNull() && canvas.FindByInstanceGuid(guid).IsValid())
				{
					return true;
				}
			}
		}

		if (const YAML::Node children = objectNode["Children"]; children && children.IsSequence())
		{
			for (const YAML::Node& childNode : children)
			{
				if (AnyInstanceGuidAlive(canvas, childNode))
				{
					return true;
				}
			}
		}
		return false;
	}
}

YAML::Node WriteLayerNode(const CGameLayer& layer, bool includeSourceAsset)
{
	YAML::Node node(YAML::NodeType::Map);
	node["Name"]            = layer.Name;
	node["Guid"]            = layer.GetInstanceGuid().generic_string();
	if (includeSourceAsset && false == layer.SourceAssetGuid.IsNull())
	{
		node["SourceAsset"] = layer.SourceAssetGuid.generic_string();
		// 승계 여부는 SourceAsset 이 있을 때만 뜻이 있다(인라인 레이어는 승계 대상이 아니다).
		node["KeepOnCanvasChange"] = layer.KeepOnCanvasChange;
	}
	node["Blend"]           = ToString(layer.BlendMode);
	node["Opacity"]         = layer.Opacity;
	node["Visible"]         = layer.Visible;
	node["Static"]          = layer.Static;
	node["ForceOwnTexture"] = layer.ForceOwnTexture;
	node["Parallax"]        = layer.ParallaxFactor;
	return node;
}

void ApplyLayerNodeProperties(CGameLayer& layer, const YAML::Node& node)
{
	if (!node || false == node.IsMap())
	{
		return;
	}

	layer.Name = node["Name"] ? node["Name"].as<std::string>("Layer") : "Layer";

	const std::string blend = node["Blend"] ? node["Blend"].as<std::string>("Normal") : "Normal";
	layer.BlendMode          = LayerBlendModeFromString(blend.c_str());
	layer.Opacity            = node["Opacity"]            ? node["Opacity"].as<float>(1.0f)            : 1.0f;
	layer.Visible            = node["Visible"]            ? node["Visible"].as<bool>(true)             : true;
	layer.Static             = node["Static"]             ? node["Static"].as<bool>(false)             : false;
	layer.ForceOwnTexture    = node["ForceOwnTexture"]    ? node["ForceOwnTexture"].as<bool>(false)    : false;
	layer.ParallaxFactor     = node["Parallax"]           ? node["Parallax"].as<float>(1.0f)           : 1.0f;
	layer.KeepOnCanvasChange = node["KeepOnCanvasChange"] ? node["KeepOnCanvasChange"].as<bool>(false) : false;
}

CGameLayer* ReadLayerNodeInto(CGameCanvas& canvas, const YAML::Node& node)
{
	if (!node || false == node.IsMap())
	{
		return nullptr;
	}

	const std::string name = node["Name"] ? node["Name"].as<std::string>("Layer") : "Layer";
	CGameLayer* layer = canvas.CreateLayer(name.c_str());
	if (nullptr == layer)
	{
		return nullptr;
	}

	try
	{
		if (node["Guid"])
		{
			const File::Guid guid(node["Guid"].as<std::string>(""));
			if (false == guid.IsNull())
			{
				CCanvasRuntimeAccess::SetLayerInstanceGuid(canvas, *layer, guid);
			}
		}
		if (node["SourceAsset"])
		{
			layer->SourceAssetGuid = File::Guid(node["SourceAsset"].as<std::string>(""));
		}
	}
	catch (const YAML::Exception&)
	{
	}

	ApplyLayerNodeProperties(*layer, node);
	return layer;
}

std::string SerializeLayer(const CGameCanvas& canvas, const CGameLayer& layer)
{
	// 소속 루트만 모은다 — 자식은 각 루트의 Children 에 중첩된다(자식=부모 레이어 불변식).
	std::vector<const CGameObject*> roots;
	const_cast<CGameCanvas&>(canvas).ForEachObject([&layer, &roots](CGameObject& object)
	{
		if (object.GetLayer().TryGet() == &layer && false == object.GetParent().IsValid())
		{
			roots.push_back(&object);
		}
	});

	// 저장 순서를 결정적으로: 생성 순서대로 써야 로드 후 표시 순서가 원본과 같다
	// (풀 슬롯 순회 순서는 생성 순서와 무관하다).
	std::sort(roots.begin(), roots.end(), [](const CGameObject* lhs, const CGameObject* rhs)
	{
		return lhs->GetCreationOrder() < rhs->GetCreationOrder();
	});

	std::vector<AssetGuid> referencedAssets;
	YAML::Node objects(YAML::NodeType::Sequence);
	for (const CGameObject* root : roots)
	{
		objects.push_back(WriteObject(*root, &referencedAssets, /*includeChildren*/ true));
	}

	YAML::Node referenced(YAML::NodeType::Sequence);
	for (const AssetGuid& guid : referencedAssets)
	{
		if (false == guid.IsNull())
		{
			referenced.push_back(guid.generic_string());
		}
	}

	YAML::Node root(YAML::NodeType::Map);
	root["Version"]          = LAYER_FILE_VERSION;
	root["ReferencedAssets"] = referenced;
	root["Layer"]            = WriteLayerNode(layer, /*includeSourceAsset*/ false);
	root["Objects"]          = objects;

	YAML::Emitter emitter;
	emitter << root;
	return std::string(emitter.c_str());
}

ELayerSerializeResult DeserializeLayer(CGameCanvas& canvas, const char* text, CGameLayer** outLayer)
{
	if (outLayer)
	{
		*outLayer = nullptr;
	}
	if (nullptr == text)
	{
		return ELayerSerializeResult::InvalidArgument;
	}

	YAML::Node root;
	try
	{
		root = YAML::Load(text);
	}
	catch (const YAML::Exception&)
	{
		return ELayerSerializeResult::ParseError;
	}

	if (!root || false == root.IsMap() || !root["Layer"])
	{
		return ELayerSerializeResult::ParseError;
	}

	const std::uint32_t version = root["Version"] ? root["Version"].as<std::uint32_t>(0) : 0;
	if (LAYER_FILE_VERSION != version)
	{
		return ELayerSerializeResult::ParseError;
	}

	const YAML::Node objectsNode = root["Objects"];

	// guid 충돌 검사는 무엇을 만들기 전에 — 중간에 거부하면 절반만 로드된 레이어가 남는다.
	if (objectsNode && objectsNode.IsSequence())
	{
		for (const YAML::Node& objectNode : objectsNode)
		{
			if (AnyInstanceGuidAlive(canvas, objectNode))
			{
				return ELayerSerializeResult::DuplicateInstance;
			}
		}
	}

	CGameLayer* layer = ReadLayerNodeInto(canvas, root["Layer"]);
	if (nullptr == layer)
	{
		return ELayerSerializeResult::ParseError;
	}

	if (objectsNode && objectsNode.IsSequence())
	{
		for (const YAML::Node& objectNode : objectsNode)
		{
			// 역직렬화는 기본 레이어에 만든다 — 방금 만든 레이어로 옮긴다.
			if (CGameObject* root_ = ReadObjectInto(canvas, objectNode, nullptr))
			{
				canvas.MoveObjectToLayer(*root_, *layer);
			}
		}
	}

	if (outLayer)
	{
		*outLayer = layer;
	}
	return ELayerSerializeResult::Success;
}

bool LooksLikeLayer(const char* text)
{
	if (nullptr == text)
	{
		return false;
	}
	YAML::Node node;
	try { node = YAML::Load(text); }
	catch (const YAML::Exception&) { return false; }
	return node.IsMap() && static_cast<bool>(node["Layer"]) && static_cast<bool>(node["Objects"]);
}

} // namespace Serialization
