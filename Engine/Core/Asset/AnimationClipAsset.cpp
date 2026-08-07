#include "pch.h"
#include "AnimationClipAsset.h"

#include "yaml-cpp/yaml.h"

#include <algorithm>
#include <fstream>
#include <sstream>

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
//  CAnimationClipAsset / CAnimationClipSerializer / CAnimationClipAssetLoader 구현
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

// ── CAnimationClipAsset ──────────────────────────────────────────────────────

CAnimationClipAsset::CAnimationClipAsset(const AssetMetaData& metaData, AnimationClipData data)
	: m_metaData(metaData)
	, m_data(std::move(data))
{
	RefreshEffectiveName();
}

AssetGuid            CAnimationClipAsset::GetGuid()      const { return m_metaData.Guid; }
EAssetType           CAnimationClipAsset::GetAssetType() const { return m_metaData.Type; }
EAssetLoadState      CAnimationClipAsset::GetLoadState() const { return m_loadState; }
const AssetMetaData& CAnimationClipAsset::GetMetaData()  const { return m_metaData; }

void CAnimationClipAsset::RefreshEffectiveName()
{
	if (false == m_data.Name.empty())
	{
		m_effectiveName = m_data.Name;
		return;
	}

	// 이름이 비었으면 파일 이름(확장자 제외). DisplayName 이 있으면 그쪽이 더 정확하다
	// — 리네임이 .jmeta 와 쌍으로 이뤄지므로 경로보다 먼저 갱신된다.
	if (false == m_metaData.DisplayName.empty())
	{
		m_effectiveName = m_metaData.DisplayName;
		return;
	}
	m_effectiveName = m_metaData.Path.stem().generic_string();
}

void CAnimationClipAsset::ApplyImportOptions(const std::string& importOptionsYaml)
{
	// 클립은 본체 데이터가 `.janimclip` 파일에 있다. AssetManager 의 옵션 갱신 경로로도
	// 같은 스키마를 받아 in-place 갱신한다 (빈 텍스트면 기본값).
	m_data = CAnimationClipSerializer::FromYaml(importOptionsYaml);
	RefreshEffectiveName();
	++m_generation;   // 재생 중인 애니메이터가 캐시를 버리도록 generation 증가.
}

// ── CAnimationClipSerializer ─────────────────────────────────────────────────

AnimationClipData CAnimationClipSerializer::FromYaml(const std::string& yamlText)
{
	AnimationClipData data;
	if (yamlText.empty()) return data;

	YAML::Node root;
	try { root = YAML::Load(yamlText); }
	catch (const YAML::Exception&) { return data; }

	if (!root || false == root.IsMap()) return data;
	const YAML::Node node = root["Clip"] ? root["Clip"] : root;

	try
	{
		if (node["Sprite"])          data.SpriteGuid      = AssetGuid(node["Sprite"].as<std::string>(""));
		if (node["Name"])            data.Name            = node["Name"].as<std::string>("");
		if (node["StartFrame"])      data.StartFrame      = node["StartFrame"].as<std::uint32_t>(0u);
		if (node["FrameCount"])      data.FrameCount      = node["FrameCount"].as<std::uint32_t>(0u);
		if (node["FramesPerSecond"]) data.FramesPerSecond = node["FramesPerSecond"].as<float>(12.0f);
		if (node["Loop"])            data.Loop            = node["Loop"].as<bool>(true);
	}
	catch (const YAML::Exception&) {}

	if (node["Events"] && node["Events"].IsSequence())
	{
		for (const YAML::Node& eventNode : node["Events"])
		{
			try
			{
				AnimationClipEvent clipEvent;
				clipEvent.Frame = eventNode["Frame"] ? eventNode["Frame"].as<std::uint32_t>(0u) : 0u;
				clipEvent.Name  = eventNode["Name"]  ? eventNode["Name"].as<std::string>("")    : std::string();
				// 이름 없는 이벤트는 스크립트가 구분할 방법이 없다 — 버린다.
				if (false == clipEvent.Name.empty())
				{
					data.Events.push_back(std::move(clipEvent));
				}
			}
			catch (const YAML::Exception&) {}
		}
	}

	// 이벤트는 프레임 오름차순이라고 가정하고 발화한다. 저작 순서를 믿지 않고 여기서 정렬한다.
	std::sort(data.Events.begin(), data.Events.end(),
		[](const AnimationClipEvent& a, const AnimationClipEvent& b) { return a.Frame < b.Frame; });

	return data;
}

std::string CAnimationClipSerializer::ToYaml(const AnimationClipData& data)
{
	YAML::Emitter emitter;
	emitter << YAML::BeginMap;
	emitter << YAML::Key << "Clip" << YAML::Value;
	emitter << YAML::BeginMap;
	emitter << YAML::Key << "Sprite"          << YAML::Value << data.SpriteGuid.generic_string();
	emitter << YAML::Key << "Name"            << YAML::Value << data.Name;
	emitter << YAML::Key << "StartFrame"      << YAML::Value << data.StartFrame;
	emitter << YAML::Key << "FrameCount"      << YAML::Value << data.FrameCount;
	emitter << YAML::Key << "FramesPerSecond" << YAML::Value << data.FramesPerSecond;
	emitter << YAML::Key << "Loop"            << YAML::Value << data.Loop;

	emitter << YAML::Key << "Events" << YAML::Value;
	emitter << YAML::BeginSeq;
	for (const AnimationClipEvent& clipEvent : data.Events)
	{
		emitter << YAML::BeginMap;
		emitter << YAML::Key << "Frame" << YAML::Value << clipEvent.Frame;
		emitter << YAML::Key << "Name"  << YAML::Value << clipEvent.Name;
		emitter << YAML::EndMap;
	}
	emitter << YAML::EndSeq;

	emitter << YAML::EndMap;
	emitter << YAML::EndMap;
	return emitter.c_str();
}

// ── CAnimationClipAssetLoader ────────────────────────────────────────────────

namespace
{
	// `.janimclip` 본문 텍스트를 얻는다 — 빌드 패키지의 메모리 페이로드 우선, 없으면 디스크.
	// (패키지 자산은 메모리 로드를 지원하는 로더만 허용된다.)
	bool ReadClipText(const AssetLoadDesc& desc, std::string& outText)
	{
		if (desc.HasMemoryPayload())
		{
			outText.assign(reinterpret_cast<const char*>(desc.MemoryPayload.data()), desc.MemoryPayload.size());
			return true;
		}
		if (desc.ResolvedPath.empty()) return false;

		std::ifstream stream(desc.ResolvedPath, std::ios::binary);
		if (false == stream.is_open()) return false;
		std::ostringstream ss;
		ss << stream.rdbuf();
		outText = ss.str();
		return true;
	}
}

EAssetType CAnimationClipAssetLoader::GetSupportedType() const
{
	return EAssetType::AnimationClip;
}

bool CAnimationClipAssetLoader::CanLoad(const AssetLoadDesc& desc) const
{
	return EAssetType::AnimationClip == desc.Type && nullptr != desc.MetaData;
}

OwnerPtr<IAsset> CAnimationClipAssetLoader::Load(const AssetLoadDesc& desc)
{
	if (false == CanLoad(desc)) return nullptr;

	// 본문을 못 읽어도 기본값 클립으로 폴백 — 에디터/직렬화는 그대로 동작.
	std::string text;
	ReadClipText(desc, text);
	AnimationClipData data = CAnimationClipSerializer::FromYaml(text);

	return MakeOwnerPtr<CAnimationClipAsset>(*desc.MetaData, std::move(data));
}

void CAnimationClipAssetLoader::Unload(IAsset& asset)
{
	(void)asset;   // vector/string 은 소멸자 자동 해제
}

bool CAnimationClipAssetLoader::ReloadInto(IAsset& existing, const AssetMetaData& metaData)
{
	if (EAssetType::AnimationClip != existing.GetAssetType()) return false;

	// 디스크 `.janimclip` 를 다시 읽어 in-place 갱신 — 외부 AssetRef 보존.
	AssetLoadDesc desc;
	desc.Type         = EAssetType::AnimationClip;
	desc.MetaData     = &metaData;
	desc.ResolvedPath = metaData.Path;

	std::string text;
	if (false == ReadClipText(desc, text)) return false;

	CAnimationClipAsset& clip = static_cast<CAnimationClipAsset&>(existing);
	clip.ApplyImportOptions(text);
	return true;
}
