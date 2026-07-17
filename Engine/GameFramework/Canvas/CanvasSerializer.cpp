#include "pch.h"
#include "CanvasSerializer.h"

#include "GameFramework/Serialization/LayerSerializer.h"
#include "GameFramework/Serialization/ObjectSerializer.h"
#include "GameFramework/Object/GameObject.h"
#include "GameFramework/Reflection/ReflectionRegistry.h"
#include "GameFramework/Canvas/GameLayer.h"
#include "GameFramework/Canvas/Canvas.h"
#include "GameFramework/Canvas/CanvasRuntimeAccess.h"
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
	constexpr std::uint32_t CANVAS_FILE_VERSION = 1;

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

	// 레이어 섹션 — 노드 자체는 LayerSerializer 가 정의한다(레이어 파일 `.jlayer` 와 공유).
	// 순서 = 시퀀스 순서(컴포짓 아래→위).
	YAML::Node WriteLayers(const CGameCanvas& scene)
	{
		YAML::Node node(YAML::NodeType::Sequence);
		for (std::size_t i = 0; i < scene.GetLayerCount(); ++i)
		{
			if (const CGameLayer* layer = scene.GetLayerAt(i))
			{
				node.push_back(Serialization::WriteLayerNode(*layer));
			}
		}
		return node;
	}

	// Layers 시퀀스를 씬에 재구성하고 노드 인덱스 → 레이어 대응을 돌려준다(오브젝트의
	// LayerIndex 와 같은 인덱스 공간이다). 노드가 없거나 비면(구 포맷 마이그레이션) 기본
	// 레이어 1개를 만든다 — "레이어 0개 + 오브젝트 존재" 불허 불변식.
	//
	// inherited[i] 가 nullptr 이 아니면 i 번 노드는 **이미 살아 있는 그 인스턴스**를 쓴다
	// (캔버스 전환의 승계). 새로 만들지 않고 컴포짓 속성만 덮는다 — 신원(guid)과 소속
	// 오브젝트는 살아 있는 쪽이 이긴다. 전체 교체 경로는 빈 목록을 넘긴다.
	std::vector<CGameLayer*> ReadLayers(CGameCanvas& scene, const YAML::Node& node,
	                                    const std::vector<CGameLayer*>& inherited)
	{
		std::vector<CGameLayer*> layers;
		if (node && node.IsSequence())
		{
			for (const YAML::Node& layerNode : node)
			{
				const std::size_t index = layers.size();
				CGameLayer* layer = index < inherited.size() ? inherited[index] : nullptr;
				if (nullptr != layer)
				{
					Serialization::ApplyLayerNodeProperties(*layer, layerNode);
				}
				else
				{
					layer = Serialization::ReadLayerNodeInto(scene, layerNode);
				}
				layers.push_back(layer);
			}
		}
		if (0 == scene.GetLayerCount())
		{
			layers.push_back(scene.CreateLayer(nullptr));
		}

		// 파일 순서 = 컴포짓 순서. 승계 레이어는 구 캔버스에서의 자리에 남아 있으므로 여기서
		// 제자리를 찾아준다 — 승계 레이어의 위치는 수신 캔버스가 저작한다(설계 12차: 이게
		// 이주 모델의 "순서를 어디에 둘지" 문제가 사라진 이유다).
		// 새로 만든 레이어만 있는 전체 교체 경로에선 이미 순서가 맞아 사실상 no-op 다.
		for (std::size_t index = 0; index < layers.size(); ++index)
		{
			if (nullptr != layers[index])
			{
				scene.MoveLayer(layers[index], index);
			}
		}
		return layers;
	}

	YAML::Node WriteLayout2D(const Layout2D& layout)
	{
		YAML::Node node(YAML::NodeType::Map);
		YAML::Node normalized(YAML::NodeType::Sequence);
		normalized.push_back(layout.Normalized.x);
		normalized.push_back(layout.Normalized.y);
		YAML::Node pixel(YAML::NodeType::Sequence);
		pixel.push_back(layout.Pixel.x);
		pixel.push_back(layout.Pixel.y);
		node["Normalized"] = normalized;
		node["Pixel"] = pixel;
		return node;
	}

	void ReadLayout2D(const YAML::Node& node, Layout2D& outLayout)
	{
		if (!node || false == node.IsMap())
		{
			return;
		}
		if (const YAML::Node normalized = node["Normalized"]; normalized && normalized.IsSequence() && normalized.size() >= 2)
		{
			outLayout.Normalized.x = normalized[0].as<float>(outLayout.Normalized.x);
			outLayout.Normalized.y = normalized[1].as<float>(outLayout.Normalized.y);
		}
		if (const YAML::Node pixel = node["Pixel"]; pixel && pixel.IsSequence() && pixel.size() >= 2)
		{
			outLayout.Pixel.x = pixel[0].as<float>(outLayout.Pixel.x);
			outLayout.Pixel.y = pixel[1].as<float>(outLayout.Pixel.y);
		}
	}

	// 뷰포트 섹션 — 캔버스 저작 데이터(카메라 Ref × 출력 렉트 × 레이어 필터).
	// 카메라·레이어는 guid 로 지목한다: 인덱스는 에디터에서 순서를 바꾸면 어긋난다.
	YAML::Node WriteViewports(const CGameCanvas& scene)
	{
		YAML::Node node(YAML::NodeType::Sequence);
		for (std::size_t i = 0; i < scene.GetViewportCount(); ++i)
		{
			const CanvasViewport* viewport = scene.GetViewportAt(i);
			if (nullptr == viewport)
			{
				continue;
			}
			YAML::Node viewportNode(YAML::NodeType::Map);
			viewportNode["Name"] = viewport->Name;
			viewportNode["Camera"] = viewport->CameraObjectGuid.IsNull()
				? std::string()
				: viewport->CameraObjectGuid.generic_string();
			viewportNode["Position"] = WriteLayout2D(viewport->Position);
			viewportNode["Size"] = WriteLayout2D(viewport->Size);
			viewportNode["Active"] = viewport->Active;

			YAML::Node filter(YAML::NodeType::Sequence);
			for (const File::Guid& layerGuid : viewport->LayerFilter)
			{
				if (false == layerGuid.IsNull())
				{
					filter.push_back(layerGuid.generic_string());
				}
			}
			viewportNode["LayerFilter"] = filter;
			node.push_back(viewportNode);
		}
		return node;
	}

	void ReadViewports(CGameCanvas& scene, const YAML::Node& node)
	{
		if (node && node.IsSequence())
		{
			for (const YAML::Node& viewportNode : node)
			{
				if (!viewportNode || false == viewportNode.IsMap())
				{
					continue;
				}
				const std::string name = viewportNode["Name"] ? viewportNode["Name"].as<std::string>("") : "";
				CanvasViewport* viewport = scene.CreateViewport(name.empty() ? nullptr : name.c_str());
				if (nullptr == viewport)
				{
					continue;
				}
				try
				{
					if (viewportNode["Camera"])
					{
						const std::string cameraGuid = viewportNode["Camera"].as<std::string>("");
						if (false == cameraGuid.empty())
						{
							viewport->CameraObjectGuid = File::Guid(cameraGuid);
						}
					}
					if (const YAML::Node filter = viewportNode["LayerFilter"]; filter && filter.IsSequence())
					{
						for (const YAML::Node& layerNode : filter)
						{
							const File::Guid layerGuid(layerNode.as<std::string>(""));
							if (false == layerGuid.IsNull())
							{
								viewport->LayerFilter.push_back(layerGuid);
							}
						}
					}
				}
				catch (const YAML::Exception&)
				{
				}
				ReadLayout2D(viewportNode["Position"], viewport->Position);
				ReadLayout2D(viewportNode["Size"], viewport->Size);
				viewport->Active = viewportNode["Active"] ? viewportNode["Active"].as<bool>(true) : true;
			}
		}
		// 뷰포트 없는 캔버스(구 포맷 포함)는 기본 풀스크린 뷰포트 1개로 — 카메라 미지정이라
		// 폴백(첫 활성 카메라)이 잡힌다. 즉 구 씬은 저작 없이 그대로 그려진다.
		scene.GetOrCreateDefaultViewport();
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

	void ReserveScenePools(CGameCanvas& scene, const YAML::Node& objectsNode)
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
				CCanvasRuntimeAccess::ReserveScriptMemory(
					scene,
					typeId,
					info->Type.Size,
					info->Type.Alignment,
					reserveCount(count));
			}
		}
	}
}

ECanvasSerializeResult CCanvasSerializer::SerializeToText(CGameCanvas& scene, std::string& outText) const
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
		const CGameLayer* layer = scene.GetLayerAt(i);
		layerIndexOf[layer] = static_cast<int>(i);

		// 파일에서 온 레이어는 캔버스가 그 `.jlayer` 를 참조하는 것이다 — ReferencedAssets 에
		// 올려야 빌드 수집기가 캔버스→레이어 파일 의존을 보고 패키지에 담는다.
		if (layer && false == layer->SourceAssetGuid.IsNull()
			&& std::find(referencedAssets.begin(), referencedAssets.end(), layer->SourceAssetGuid) == referencedAssets.end())
		{
			referencedAssets.push_back(layer->SourceAssetGuid);
		}
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
	YAML::Node backgroundColor(YAML::NodeType::Sequence);
	for (int i = 0; i < 4; ++i)
	{
		backgroundColor.push_back(scene.GetBackgroundColor()[i]);
	}

	YAML::Node root(YAML::NodeType::Map);
	root["Version"]          = CANVAS_FILE_VERSION;
	root["ReferencedAssets"] = WriteReferencedAssets(referencedAssets);
	root["BackgroundColor"]  = backgroundColor;
	root["Layers"]           = WriteLayers(scene);
	root["Viewports"]        = WriteViewports(scene);
	root["Objects"]          = objects;

	YAML::Emitter emitter;
	emitter << root;
	outText = emitter.c_str();
	outText.push_back('\n');
	scene.SetReferencedAssets(std::move(referencedAssets));
	return ECanvasSerializeResult::Success;
}

ECanvasSerializeResult CCanvasSerializer::DeserializeFromText(CGameCanvas& scene, const char* text) const
{
	YAML::Node root;
	const ECanvasSerializeResult parsed = ParseCanvasRoot(text, root);
	if (ECanvasSerializeResult::Success != parsed)
	{
		return parsed;
	}

	scene.ClearObjects();

	// 승계 없음 = 전부 새로 만든다. 전환 경로와 같은 리더를 쓴다 — 캔버스 포맷을 읽는 코드가
	// 두 벌이 되면 한쪽만 새 필드를 배워 같은 파일이 경로마다 다르게 읽힌다.
	std::vector<AssetGuid> referencedAssets;
	const ECanvasSerializeResult result = ReadCanvasBody(scene, root, {}, referencedAssets);
	scene.SetReferencedAssets(std::move(referencedAssets));
	return result;
}

ECanvasSerializeResult CCanvasSerializer::TransitionFromText(CGameCanvas& canvas,
                                                           const char* text,
                                                           std::vector<SafePtr<CGameLayer>>& outInheritedLayers) const
{
	outInheritedLayers.clear();

	YAML::Node root;
	const ECanvasSerializeResult parsed = ParseCanvasRoot(text, root);
	if (ECanvasSerializeResult::Success != parsed)
	{
		return parsed;
	}

	// 승계 판정은 무엇을 파괴하기 전에 — 판정 대상이 곧 파괴 후보다.
	const std::vector<CGameLayer*> inherited = ResolveInheritedLayers(canvas, root["Layers"]);
	if (std::none_of(inherited.begin(), inherited.end(), [](const CGameLayer* layer) { return nullptr != layer; }))
	{
		// 승계할 게 하나도 없으면 전환 = 전체 교체와 결과가 같다. 아래의 부분 파괴 경로를
		// 굳이 태우지 않는다(레이어를 전부 지우려 들면 DestroyLayer 의 "마지막 하나 거부"에
		// 걸리기도 한다).
		return DeserializeFromText(canvas, text);
	}

	// 비승계 레이어 파괴 — 승계가 최소 1개 살아남으므로 "마지막 하나 거부"는 걸리지 않는다.
	std::vector<CGameLayer*> doomed;
	for (std::size_t i = 0; i < canvas.GetLayerCount(); ++i)
	{
		CGameLayer* layer = canvas.GetLayerAt(i);
		if (nullptr == layer)
		{
			continue;
		}
		if (std::find(inherited.begin(), inherited.end(), layer) == inherited.end())
		{
			doomed.push_back(layer);
		}
	}
	for (CGameLayer* layer : doomed)
	{
		canvas.DestroyLayer(layer);
	}

	// **즉시** flush 한다. 지연 파괴로 남기면 아래에서 파일의 같은 guid 를 되살릴 때 아직
	// 살아있는 옛 오브젝트와 겹쳐, ObjectSerializer 가 충돌로 보고 새 guid 를 발급한다
	// (= Ref·뷰포트 카메라 전멸). 전환은 프레임 경계 밖에서 원자적으로 끝나야 한다.
	canvas.FlushPendingDestroys();
	// 뷰포트는 수신 캔버스가 통째로 저작한다 — 승계 대상이 아니다.
	canvas.ClearViewports();

	std::vector<AssetGuid> referencedAssets;
	const ECanvasSerializeResult result = ReadCanvasBody(canvas, root, inherited, referencedAssets);
	canvas.SetReferencedAssets(std::move(referencedAssets));

	// 승계 목록은 성공했을 때만 돌려준다 — 반쯤 전환된 캔버스에 훅을 보내면 스크립트가 새
	// 캔버스라고 믿고 자기 상태를 재구성하는데 정작 캔버스는 옛 것과 새 것이 섞여 있다.
	// `inherited` 는 파일의 레이어 노드와 자리를 맞춘 배열이라 승계 없는 칸이 nullptr 로 남는다.
	if (ECanvasSerializeResult::Success == result)
	{
		outInheritedLayers.reserve(inherited.size());
		for (CGameLayer* layer : inherited)
		{
			if (nullptr != layer)
			{
				outInheritedLayers.push_back(layer->SafeFromThis());
			}
		}
	}
	return result;
}

ECanvasSerializeResult CCanvasSerializer::ParseCanvasRoot(const char* text, YAML::Node& outRoot) const
{
	if (nullptr == text)
	{
		return ECanvasSerializeResult::InvalidArgument;
	}

	try
	{
		outRoot = YAML::Load(text);
	}
	catch (const YAML::Exception&)
	{
		return ECanvasSerializeResult::ParseError;
	}

	if (!outRoot || false == outRoot.IsMap())
	{
		return ECanvasSerializeResult::ParseError;
	}

	const std::uint32_t version = outRoot["Version"] ? outRoot["Version"].as<std::uint32_t>(0) : 0;
	if (CANVAS_FILE_VERSION != version)
	{
		return ECanvasSerializeResult::ParseError;
	}

	const YAML::Node objectsNode = outRoot["Objects"];
	if (!objectsNode || false == objectsNode.IsSequence())
	{
		return ECanvasSerializeResult::ParseError;
	}
	return ECanvasSerializeResult::Success;
}

std::vector<CGameLayer*> CCanvasSerializer::ResolveInheritedLayers(CGameCanvas& canvas, const YAML::Node& layersNode) const
{
	std::vector<CGameLayer*> inherited;
	if (!layersNode || false == layersNode.IsSequence())
	{
		return inherited;
	}

	inherited.reserve(layersNode.size());
	for (const YAML::Node& layerNode : layersNode)
	{
		CGameLayer* live = nullptr;
		if (layerNode && layerNode.IsMap() && layerNode["SourceAsset"]
			&& layerNode["KeepOnCanvasChange"] && layerNode["KeepOnCanvasChange"].as<bool>(false))
		{
			try
			{
				// 초대장 모델 — 수신 캔버스가 명시로 참조("유지")해야만 승계한다.
				// 인라인 레이어(SourceAsset 없음)는 애초에 판정에 들어오지 않는다.
				const File::Guid sourceAsset(layerNode["SourceAsset"].as<std::string>(""));
				live = canvas.FindLayerBySourceAsset(sourceAsset).TryGet();
			}
			catch (const YAML::Exception&)
			{
			}
		}
		inherited.push_back(live);
	}
	return inherited;
}

ECanvasSerializeResult CCanvasSerializer::ReadCanvasBody(CGameCanvas& scene,
                                                       const YAML::Node& root,
                                                       const std::vector<CGameLayer*>& inherited,
                                                       std::vector<AssetGuid>& outReferencedAssets) const
{
	const YAML::Node objectsNode = root["Objects"];
	ReserveScenePools(scene, objectsNode);
	outReferencedAssets = ReadReferencedAssets(root["ReferencedAssets"]);

	// 레이어 먼저 재구성(오브젝트 배정 대상). Layers 키 없는 구 포맷은 기본 레이어 1개.
	const std::vector<CGameLayer*> layers = ReadLayers(scene, root["Layers"], inherited);

	// 뷰포트는 레이어 필터가 레이어 guid 를 참조하므로 레이어 뒤에 읽는다.
	// 카메라 Ref 는 guid 로만 들고 있다가 첫 렌더 때 해석되므로 오브젝트보다 앞이어도 무방.
	ReadViewports(scene, root["Viewports"]);

	if (const YAML::Node background = root["BackgroundColor"]; background && background.IsSequence() && background.size() >= 4)
	{
		scene.SetBackgroundColor(
			background[0].as<float>(0.08f),
			background[1].as<float>(0.09f),
			background[2].as<float>(0.11f),
			background[3].as<float>(1.0f));
	}

	std::vector<CGameObject*> objects;
	std::vector<int> parentIndices;
	for (const YAML::Node& objectNode : objectsNode)
	{
		if (!objectNode || false == objectNode.IsMap())
		{
			return ECanvasSerializeResult::ParseError;
		}

		// LayerIndex 없는 구 포맷/범위 밖 인덱스는 기본(첫) 레이어.
		const int layerIndex = objectNode["LayerIndex"] ? objectNode["LayerIndex"].as<int>(0) : 0;
		const std::size_t layerSlot = static_cast<std::size_t>(std::max(layerIndex, 0));

		// 승계 레이어의 오브젝트는 이미 살아 있다 — 파일의 사본으로 다시 만들면 오브젝트가
		// 두 벌이 되고 guid 가 겹쳐 재발급된다. 자리는 비워 둔다(ParentIndex 가 파일 기준
		// 인덱스라 목록이 밀리면 안 된다). 자식도 같은 레이어라 함께 건너뛴다.
		if (layerSlot < inherited.size() && nullptr != inherited[layerSlot])
		{
			objects.push_back(nullptr);
			parentIndices.push_back(-1);
			continue;
		}

		CGameObject* object = Serialization::ReadObjectInto(scene, objectNode, &outReferencedAssets);
		if (nullptr == object)
		{
			return ECanvasSerializeResult::ParseError;
		}

		// 레이어 배정 — 부모 연결 전(전부 루트)이라 서브트리 전파 비용 없음.
		CGameLayer* layer = layerSlot < layers.size() ? layers[layerSlot] : nullptr;
		if (nullptr == layer)
		{
			layer = scene.GetDefaultLayer();
		}
		if (nullptr != layer)
		{
			scene.MoveObjectToLayer(*object, *layer);
		}

		objects.push_back(object);
		parentIndices.push_back(objectNode["ParentIndex"] ? objectNode["ParentIndex"].as<int>(-1) : -1);
	}

	// 부모 연결(인덱스 → 오브젝트). 건너뛴 자리는 nullptr 이다.
	for (std::size_t i = 0; i < objects.size(); ++i)
	{
		if (nullptr == objects[i])
		{
			continue;
		}
		const int parentIndex = parentIndices[i];
		if (parentIndex < 0 || static_cast<std::size_t>(parentIndex) >= objects.size())
		{
			continue;
		}
		CGameObject* parent = objects[static_cast<std::size_t>(parentIndex)];
		if (nullptr == parent)
		{
			continue;
		}
		objects[i]->SetParent(*parent);
	}

	return ECanvasSerializeResult::Success;
}

ECanvasSerializeResult CCanvasSerializer::SaveToFile(CGameCanvas& scene, const File::Path& path) const
{
	if (path.empty())
	{
		return ECanvasSerializeResult::InvalidArgument;
	}

	std::string text;
	SerializeToText(scene, text);

	std::ofstream file(path, std::ios::out | std::ios::trunc);
	if (false == file.is_open())
	{
		return ECanvasSerializeResult::IoError;
	}

	file << text;
	return ECanvasSerializeResult::Success;
}

ECanvasSerializeResult CCanvasSerializer::LoadFromFile(CGameCanvas& scene, const File::Path& path) const
{
	if (path.empty())
	{
		return ECanvasSerializeResult::InvalidArgument;
	}

	std::ifstream file(path);
	if (false == file.is_open())
	{
		return ECanvasSerializeResult::IoError;
	}

	std::stringstream buffer;
	buffer << file.rdbuf();
	return DeserializeFromText(scene, buffer.str().c_str());
}

std::vector<AssetGuid> CCanvasSerializer::ReadReferencedAssetsFromFile(const File::Path& path) const
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
