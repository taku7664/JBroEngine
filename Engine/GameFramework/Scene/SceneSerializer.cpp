#include "pch.h"
#include "SceneSerializer.h"

#include "GameFramework/Serialization/ObjectSerializer.h"
#include "GameFramework/Object/GameObject.h"
#include "GameFramework/Reflection/ReflectionRegistry.h"
#include "GameFramework/Scene/GameLayer.h"
#include "GameFramework/Scene/Scene.h"
#include "GameFramework/Scene/SceneRuntimeAccess.h"
#include "Core/ScriptCore.h"
#include "yaml-cpp/yaml.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <vector>

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
//  SceneSerializer ─ 씬 파일(여러 오브젝트 + 메타) ↔ YAML (직렬화 3계층의 최상단)
//
//  · 오브젝트 1개 직렬화는 ObjectSerializer 에, 컴포넌트는 ComponentSerializer 에 위임.
//  · 여기서는 파일 레벨 관심사만 다룬다: Version, ReferencedAssets, 오브젝트 목록,
//    계층(ParentIndex).
//
//  ⚠ 호스트 전용(yaml-cpp) — 게임 DLL 에 노출하지 않는다.
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

namespace
{
	constexpr std::uint32_t SCENE_FILE_VERSION = 1;

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

	// 레이어 섹션 — 자기완결 노드(차후 레이어 파일 에셋 분리 대비, 씬 레벨 정보 미포함).
	// 순서 = 시퀀스 순서(컴포짓 아래→위).
	YAML::Node WriteLayers(const CGameScene& scene)
	{
		YAML::Node node(YAML::NodeType::Sequence);
		for (std::size_t i = 0; i < scene.GetLayerCount(); ++i)
		{
			const CGameLayer* layer = scene.GetLayerAt(i);
			if (nullptr == layer)
			{
				continue;
			}
			YAML::Node layerNode(YAML::NodeType::Map);
			layerNode["Name"]            = layer->Name;
			layerNode["Guid"]            = layer->GetInstanceGuid().generic_string();
			layerNode["Blend"]           = ToString(layer->BlendMode);
			layerNode["Opacity"]         = layer->Opacity;
			layerNode["Visible"]         = layer->Visible;
			layerNode["Static"]          = layer->Static;
			layerNode["ForceOwnTexture"] = layer->ForceOwnTexture;
			layerNode["Parallax"]        = layer->ParallaxFactor;
			node.push_back(layerNode);
		}
		return node;
	}

	// Layers 시퀀스를 씬에 재구성한다. 노드가 없거나 비면(구 포맷 마이그레이션) 기본
	// 레이어 1개를 만든다 — "레이어 0개 + 오브젝트 존재" 불허 불변식.
	// guid 복원은 씬 friend 권한이 필요해 호출측(CSceneSerializer 멤버)이 수행하도록
	// (레이어, 파일 guid) 쌍을 돌려준다.
	std::vector<std::pair<CGameLayer*, File::Guid>> ReadLayers(CGameScene& scene, const YAML::Node& node)
	{
		std::vector<std::pair<CGameLayer*, File::Guid>> layerGuids;
		if (node && node.IsSequence())
		{
			for (const YAML::Node& layerNode : node)
			{
				if (!layerNode || false == layerNode.IsMap())
				{
					continue;
				}
				const std::string name = layerNode["Name"] ? layerNode["Name"].as<std::string>("Layer") : "Layer";
				CGameLayer* layer = scene.CreateLayer(name.c_str());
				if (nullptr == layer)
				{
					continue;
				}
				try
				{
					if (layerNode["Guid"])
					{
						const File::Guid guid(layerNode["Guid"].as<std::string>(""));
						if (false == guid.IsNull())
						{
							layerGuids.emplace_back(layer, guid);
						}
					}
				}
				catch (const YAML::Exception&)
				{
				}
				const std::string blend = layerNode["Blend"] ? layerNode["Blend"].as<std::string>("Normal") : "Normal";
				layer->BlendMode       = LayerBlendModeFromString(blend.c_str());
				layer->Opacity         = layerNode["Opacity"]         ? layerNode["Opacity"].as<float>(1.0f)        : 1.0f;
				layer->Visible         = layerNode["Visible"]         ? layerNode["Visible"].as<bool>(true)         : true;
				layer->Static          = layerNode["Static"]          ? layerNode["Static"].as<bool>(false)         : false;
				layer->ForceOwnTexture = layerNode["ForceOwnTexture"] ? layerNode["ForceOwnTexture"].as<bool>(false): false;
				layer->ParallaxFactor  = layerNode["Parallax"]        ? layerNode["Parallax"].as<float>(1.0f)       : 1.0f;
			}
		}
		if (0 == scene.GetLayerCount())
		{
			scene.CreateLayer(nullptr);
		}
		return layerGuids;
	}

	std::vector<AssetGuid> ReadReferencedAssets(const YAML::Node& node)
	{
		std::vector<AssetGuid> assets;
		if (!node || false == node.IsSequence())
		{
			return assets;
		}
		for (const YAML::Node& assetNode : node)
		{
			try
			{
				const File::Guid guid(assetNode.as<std::string>());
				if (false == guid.IsNull() &&
				    std::find(assets.begin(), assets.end(), guid) == assets.end())
				{
					assets.push_back(guid);
				}
			}
			catch (const YAML::Exception&)
			{
			}
		}
		return assets;
	}

	void ReserveScenePools(CGameScene& scene, const YAML::Node& objectsNode)
	{
		if (false == static_cast<bool>(Script.Reflection))
		{
			return;
		}

		std::unordered_map<TypeId, std::size_t> componentCounts;
		std::unordered_map<TypeId, std::size_t> scriptCounts;
		for (const YAML::Node& objectNode : objectsNode)
		{
			const YAML::Node components = objectNode["Components"];
			if (!components || false == components.IsSequence())
			{
				continue;
			}
			for (const YAML::Node& componentNode : components)
			{
				const std::string typeName = componentNode["Type"] ? componentNode["Type"].as<std::string>("") : "";
				if ("Script" == typeName)
				{
					const std::string scriptName = componentNode["ScriptType"]
						? componentNode["ScriptType"].as<std::string>("")
						: componentNode["TypeName"].as<std::string>("");
					if (const ScriptTypeInfo* info = Script.Reflection->FindScriptByName(scriptName.c_str()))
					{
						++scriptCounts[info->Type.Id];
					}
					continue;
				}
				if (const ComponentTypeInfo* info = Script.Reflection->FindComponentByName(typeName.c_str()))
				{
					++componentCounts[info->Type.Id];
				}
			}
		}

		auto reserveCount = [](std::size_t count)
		{
			return static_cast<std::size_t>(std::ceil(static_cast<double>(count) * 1.5));
		};
		for (const auto& [typeId, count] : componentCounts)
		{
			Script.Reflection->ReserveComponentPool(scene, typeId, reserveCount(count));
		}
		for (const auto& [typeId, count] : scriptCounts)
		{
			if (const ScriptTypeInfo* info = Script.Reflection->FindScript(typeId))
			{
				CSceneRuntimeAccess::ReserveScriptMemory(
					scene,
					typeId,
					info->Type.Size,
					info->Type.Alignment,
					reserveCount(count));
			}
		}
	}
}

ESceneSerializeResult CSceneSerializer::SerializeToText(CGameScene& scene, std::string& outText) const
{
	std::vector<AssetGuid> referencedAssets;

	// 활성 오브젝트를 순서대로 수집하고 인덱스 맵을 만든다(부모 인덱스 해석용).
	std::vector<const CGameObject*> objectList;
	scene.ForEachObject([&](const CGameObject& obj) { objectList.push_back(&obj); });

	// 생성순서로 저장 → 로드 시 파일 순서대로 CreationOrder 가 재부여되어 표시 순서가 보존된다
	// (풀 슬롯 순회 순서는 생성순서와 무관).
	std::sort(objectList.begin(), objectList.end(),
		[](const CGameObject* a, const CGameObject* b) { return a->GetCreationOrder() < b->GetCreationOrder(); });

	std::unordered_map<const CGameObject*, int> indexOf;
	for (std::size_t i = 0; i < objectList.size(); ++i)
	{
		indexOf[objectList[i]] = static_cast<int>(i);
	}

	// 레이어 포인터 → 파일 내 레이어 인덱스(레이어 시퀀스 순서와 동일).
	std::unordered_map<const CGameLayer*, int> layerIndexOf;
	for (std::size_t i = 0; i < scene.GetLayerCount(); ++i)
	{
		layerIndexOf[scene.GetLayerAt(i)] = static_cast<int>(i);
	}

	YAML::Node objects(YAML::NodeType::Sequence);
	for (const CGameObject* obj : objectList)
	{
		YAML::Node node = Serialization::WriteObject(*obj, &referencedAssets);

		// 계층·레이어 소속은 씬 레벨 관심사 — 인덱스를 오브젝트 노드에 덧붙인다.
		const CGameObject* parent = obj->GetParent().TryGet();
		const auto parentIt = parent ? indexOf.find(parent) : indexOf.end();
		node["ParentIndex"] = (parentIt != indexOf.end()) ? parentIt->second : -1;

		const auto layerIt = layerIndexOf.find(obj->GetLayer().TryGet());
		node["LayerIndex"] = (layerIt != layerIndexOf.end()) ? layerIt->second : 0;

		objects.push_back(node);
	}

	// ReferencedAssets 를 상단(Version 뒤, Objects 앞)에 둔다 — 사람이 바로 확인 가능.
	YAML::Node root(YAML::NodeType::Map);
	root["Version"]          = SCENE_FILE_VERSION;
	root["ReferencedAssets"] = WriteReferencedAssets(referencedAssets);
	root["Layers"]           = WriteLayers(scene);
	root["Objects"]          = objects;

	YAML::Emitter emitter;
	emitter << root;
	outText = emitter.c_str();
	outText.push_back('\n');
	scene.SetReferencedAssets(std::move(referencedAssets));
	return ESceneSerializeResult::Success;
}

ESceneSerializeResult CSceneSerializer::DeserializeFromText(CGameScene& scene, const char* text) const
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

	const std::uint32_t version = root["Version"] ? root["Version"].as<std::uint32_t>(0) : 0;
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
	ReserveScenePools(scene, objectsNode);
	std::vector<AssetGuid> referencedAssets = ReadReferencedAssets(root["ReferencedAssets"]);

	// 레이어 먼저 재구성(오브젝트 배정 대상). Layers 키 없는 구 포맷은 기본 레이어 1개.
	const std::vector<std::pair<CGameLayer*, File::Guid>> layerGuids = ReadLayers(scene, root["Layers"]);
	for (const auto& [layer, guid] : layerGuids)
	{
		if (layer)
		{
			scene.SetLayerInstanceGuid(*layer, guid);
		}
	}

	std::vector<CGameObject*> objects;
	std::vector<int> parentIndices;
	for (const YAML::Node& objectNode : objectsNode)
	{
		if (!objectNode || false == objectNode.IsMap())
		{
			return ESceneSerializeResult::ParseError;
		}

		CGameObject* object = Serialization::ReadObjectInto(scene, objectNode, &referencedAssets);
		if (nullptr == object)
		{
			return ESceneSerializeResult::ParseError;
		}

		// 레이어 배정 — 부모 연결 전(전부 루트)이라 서브트리 전파 비용 없음.
		// LayerIndex 없는 구 포맷/범위 밖 인덱스는 기본(첫) 레이어 유지.
		const int layerIndex = objectNode["LayerIndex"] ? objectNode["LayerIndex"].as<int>(0) : 0;
		if (CGameLayer* layer = scene.GetLayerAt(static_cast<std::size_t>(std::max(layerIndex, 0))))
		{
			scene.MoveObjectToLayer(*object, *layer);
		}

		objects.push_back(object);
		parentIndices.push_back(objectNode["ParentIndex"] ? objectNode["ParentIndex"].as<int>(-1) : -1);
	}

	// 부모 연결(인덱스 → 오브젝트).
	for (std::size_t i = 0; i < objects.size(); ++i)
	{
		const int parentIndex = parentIndices[i];
		if (parentIndex < 0 || static_cast<std::size_t>(parentIndex) >= objects.size())
		{
			continue;
		}
		objects[i]->SetParent(*objects[static_cast<std::size_t>(parentIndex)]);
	}

	scene.SetReferencedAssets(std::move(referencedAssets));
	return ESceneSerializeResult::Success;
}

ESceneSerializeResult CSceneSerializer::SaveToFile(CGameScene& scene, const File::Path& path) const
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

ESceneSerializeResult CSceneSerializer::LoadFromFile(CGameScene& scene, const File::Path& path) const
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
