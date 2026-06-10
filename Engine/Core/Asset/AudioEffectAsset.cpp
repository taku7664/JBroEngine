#include "pch.h"
#include "AudioEffectAsset.h"

#include "yaml-cpp/yaml.h"

#include <fstream>
#include <sstream>

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
//  CAudioEffectAsset / CAudioEffectSerializer / CAudioEffectAssetLoader 구현
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

// ── CAudioEffectAsset ────────────────────────────────────────────────────────

CAudioEffectAsset::CAudioEffectAsset(const AssetMetaData& metaData, AudioEffectData data)
	: m_metaData(metaData)
	, m_data(std::move(data))
{
}

AssetGuid            CAudioEffectAsset::GetGuid()      const { return m_metaData.Guid; }
EAssetType           CAudioEffectAsset::GetAssetType() const { return m_metaData.Type; }
EAssetLoadState      CAudioEffectAsset::GetLoadState() const { return m_loadState; }
const AssetMetaData& CAudioEffectAsset::GetMetaData()  const { return m_metaData; }

EAudioEffectKind                    CAudioEffectAsset::GetKind()       const { return m_data.Kind; }
const std::map<std::string, float>& CAudioEffectAsset::GetParameters() const { return m_data.Parameters; }

float CAudioEffectAsset::GetParameter(const std::string& key, float defaultValue) const
{
	auto it = m_data.Parameters.find(key);
	return (it != m_data.Parameters.end()) ? it->second : defaultValue;
}

void CAudioEffectAsset::ApplyImportOptions(const std::string& importOptionsYaml)
{
	// 효과 에셋은 본체 데이터(Kind/파라미터)가 .jfx 파일에 있다. AssetManager 의 옵션
	// 갱신 경로로도 같은 스키마를 받아 in-place 갱신한다 (빈 텍스트면 기본값).
	m_data = CAudioEffectSerializer::FromYaml(importOptionsYaml);
	++m_generation;   // 런타임 효과 노드가 재적용 시점을 감지하도록 generation 증가.
}

// ── EAudioEffectKind ↔ string ───────────────────────────────────────────────

namespace
{
	EAudioEffectKind ParseEffectKind(const std::string& s)
	{
		if (s == "LowPass")    return EAudioEffectKind::LowPass;
		if (s == "HighPass")   return EAudioEffectKind::HighPass;
		if (s == "Echo")       return EAudioEffectKind::Echo;
		if (s == "Distortion") return EAudioEffectKind::Distortion;
		if (s == "Compressor") return EAudioEffectKind::Compressor;
		if (s == "Limiter")    return EAudioEffectKind::Limiter;
		return EAudioEffectKind::Reverb;
	}

	const char* EffectKindToString(EAudioEffectKind k)
	{
		switch (k)
		{
		case EAudioEffectKind::LowPass:    return "LowPass";
		case EAudioEffectKind::HighPass:   return "HighPass";
		case EAudioEffectKind::Echo:       return "Echo";
		case EAudioEffectKind::Distortion: return "Distortion";
		case EAudioEffectKind::Compressor: return "Compressor";
		case EAudioEffectKind::Limiter:    return "Limiter";
		default:                           return "Reverb";
		}
	}
}

// ── CAudioEffectSerializer ───────────────────────────────────────────────────

AudioEffectData CAudioEffectSerializer::FromYaml(const std::string& yamlText)
{
	AudioEffectData data;
	if (yamlText.empty()) return data;

	YAML::Node root;
	try { root = YAML::Load(yamlText); }
	catch (const YAML::Exception&) { return data; }

	if (!root || false == root.IsMap()) return data;
	const YAML::Node node = root["Effect"] ? root["Effect"] : root;

	if (node["Kind"])
	{
		try { data.Kind = ParseEffectKind(node["Kind"].as<std::string>()); }
		catch (const YAML::Exception&) {}
	}

	if (node["Parameters"] && node["Parameters"].IsMap())
	{
		for (const auto& kv : node["Parameters"])
		{
			try
			{
				data.Parameters[kv.first.as<std::string>()] = kv.second.as<float>();
			}
			catch (const YAML::Exception&) {}
		}
	}
	return data;
}

std::string CAudioEffectSerializer::ToYaml(const AudioEffectData& data)
{
	YAML::Emitter emitter;
	emitter << YAML::BeginMap;
	emitter << YAML::Key << "Effect" << YAML::Value;
	emitter << YAML::BeginMap;
	emitter << YAML::Key << "Kind" << YAML::Value << EffectKindToString(data.Kind);
	emitter << YAML::Key << "Parameters" << YAML::Value;
	emitter << YAML::BeginMap;
	for (const auto& kv : data.Parameters)
	{
		emitter << YAML::Key << kv.first << YAML::Value << kv.second;
	}
	emitter << YAML::EndMap;
	emitter << YAML::EndMap;
	emitter << YAML::EndMap;
	return emitter.c_str();
}

// ── CAudioEffectAssetLoader ──────────────────────────────────────────────────

namespace
{
	// .jfx 본문 텍스트를 얻는다 — 빌드 패키지의 메모리 페이로드 우선, 없으면 디스크.
	bool ReadEffectText(const AssetLoadDesc& desc, std::string& outText)
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

EAssetType CAudioEffectAssetLoader::GetSupportedType() const
{
	return EAssetType::AudioEffect;
}

bool CAudioEffectAssetLoader::CanLoad(const AssetLoadDesc& desc) const
{
	return EAssetType::AudioEffect == desc.Type && nullptr != desc.MetaData;
}

OwnerPtr<IAsset> CAudioEffectAssetLoader::Load(const AssetLoadDesc& desc)
{
	if (false == CanLoad(desc)) return nullptr;

	// 본문을 못 읽어도 기본값 효과 에셋으로 폴백 — 에디터/직렬화는 그대로 동작.
	std::string text;
	ReadEffectText(desc, text);
	AudioEffectData data = CAudioEffectSerializer::FromYaml(text);

	return MakeOwnerPtr<CAudioEffectAsset>(*desc.MetaData, std::move(data));
}

void CAudioEffectAssetLoader::Unload(IAsset& asset)
{
	(void)asset;   // map/string 은 소멸자 자동 해제
}

bool CAudioEffectAssetLoader::ReloadInto(IAsset& existing, const AssetMetaData& metaData)
{
	if (EAssetType::AudioEffect != existing.GetAssetType()) return false;

	// 디스크 .jfx 를 다시 읽어 in-place 갱신 — 외부 AssetRef 보존.
	AssetLoadDesc desc;
	desc.Type         = EAssetType::AudioEffect;
	desc.MetaData     = &metaData;
	desc.ResolvedPath = metaData.Path;

	std::string text;
	if (false == ReadEffectText(desc, text)) return false;

	CAudioEffectAsset& effect = static_cast<CAudioEffectAsset&>(existing);
	effect.ApplyImportOptions(text);
	return true;
}
