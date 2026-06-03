#include "pch.h"
#include "SceneSerializer.h"

#include "GameFramework/Serialization/ObjectSerializer.h"
#include "GameFramework/Object/GameObject.h"
#include "GameFramework/Scene/Scene.h"
#include "yaml-cpp/yaml.h"

#include <algorithm>
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
}

ESceneSerializeResult CSceneSerializer::SerializeToText(const CScene& scene, std::string& outText) const
{
	std::vector<AssetGuid> referencedAssets;

	// 활성 오브젝트를 순서대로 수집하고 인덱스 맵을 만든다(부모 인덱스 해석용).
	std::vector<CGameObject*> objectList;
	const_cast<CScene&>(scene).ForEachObject([&](CGameObject& obj) { objectList.push_back(&obj); });

	std::unordered_map<const CGameObject*, int> indexOf;
	for (std::size_t i = 0; i < objectList.size(); ++i)
	{
		indexOf[objectList[i]] = static_cast<int>(i);
	}

	YAML::Node objects(YAML::NodeType::Sequence);
	for (CGameObject* obj : objectList)
	{
		YAML::Node node = Serialization::WriteObject(*obj, &referencedAssets);

		// 계층은 씬 레벨 관심사 — 부모 인덱스를 오브젝트 노드에 덧붙인다.
		CGameObject* parent = obj->GetParent().TryGet();
		const auto parentIt = parent ? indexOf.find(parent) : indexOf.end();
		node["ParentIndex"] = (parentIt != indexOf.end()) ? parentIt->second : -1;

		objects.push_back(node);
	}

	// ReferencedAssets 를 상단(Version 뒤, Objects 앞)에 둔다 — 사람이 바로 확인 가능.
	YAML::Node root(YAML::NodeType::Map);
	root["Version"]          = SCENE_FILE_VERSION;
	root["ReferencedAssets"] = WriteReferencedAssets(referencedAssets);
	root["Objects"]          = objects;

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
	std::vector<AssetGuid> referencedAssets = ReadReferencedAssets(root["ReferencedAssets"]);

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
