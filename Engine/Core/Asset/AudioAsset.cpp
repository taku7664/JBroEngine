#include "pch.h"
#include "AudioAsset.h"

#include "yaml-cpp/yaml.h"

#if defined(JBRO_HAS_MINIAUDIO) && JBRO_HAS_MINIAUDIO
#include "ThirdParty/miniaudio/miniaudio.h"
#endif

#include <algorithm>
#include <cstring>

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
//  CAudioAsset / CAudioImportOptions / CAudioAssetLoader 구현
//
//  miniaudio 의 ma_decoder 직접 호출은 본 cpp 안에만 위치 — 헤더는 노출 X.
//  추후 저수준이 별도 라이브러리로 떨어져도 자산 모듈은 인터페이스만 의존.
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

// ── CAudioAsset ─────────────────────────────────────────────────────────────

CAudioAsset::CAudioAsset(const AssetMetaData& metaData,
                         AudioFormatInfo format,
                         std::vector<std::uint8_t>&& pcmData,
                         std::uint64_t totalFrames)
	: m_metaData(metaData)
	, m_format(format)
	, m_pcmData(std::move(pcmData))
	, m_totalFrames(totalFrames)
	, m_mode(EAudioImportMode::Decompressed)
{
}

CAudioAsset::CAudioAsset(const AssetMetaData& metaData,
                         AudioFormatInfo format,
                         File::Path streamPath,
                         std::uint64_t totalFrames)
	: m_metaData(metaData)
	, m_format(format)
	, m_streamPath(std::move(streamPath))
	, m_totalFrames(totalFrames)
	, m_mode(EAudioImportMode::Streaming)
{
}

CAudioAsset::CAudioAsset(const AssetMetaData& metaData)
	: m_metaData(metaData)
{
}

AssetGuid            CAudioAsset::GetGuid()      const { return m_metaData.Guid; }
EAssetType           CAudioAsset::GetAssetType() const { return m_metaData.Type; }
EAssetLoadState      CAudioAsset::GetLoadState() const { return m_loadState; }
const AssetMetaData& CAudioAsset::GetMetaData()  const { return m_metaData; }

const AudioImportOptions& CAudioAsset::GetImportOptions() const { return m_importOptions; }
void CAudioAsset::SetImportOptions(const AudioImportOptions& options) { m_importOptions = options; }

const AudioFormatInfo& CAudioAsset::GetFormat()           const { return m_format; }
std::uint64_t          CAudioAsset::GetTotalFrames()      const { return m_totalFrames; }

double CAudioAsset::GetDurationSeconds() const
{
	if (0 == m_format.SampleRate) return 0.0;
	return static_cast<double>(m_totalFrames) / static_cast<double>(m_format.SampleRate);
}

bool                              CAudioAsset::IsStreaming()    const { return EAudioImportMode::Streaming == m_mode; }
const std::vector<std::uint8_t>&  CAudioAsset::GetPcmData()     const { return m_pcmData; }
const File::Path&                 CAudioAsset::GetStreamPath()  const { return m_streamPath; }

// ── CAudioImportOptions — YAML ──────────────────────────────────────────────

namespace
{
	template<typename T>
	T ReadOption(const YAML::Node& node, const char* key, const T& defaultValue)
	{
		if (!node[key]) return defaultValue;
		try { return node[key].as<T>(); }
		catch (const YAML::Exception&) { return defaultValue; }
	}

	EAudioImportMode ParseImportMode(const std::string& s)
	{
		if (s == "Streaming") return EAudioImportMode::Streaming;
		return EAudioImportMode::Decompressed;
	}
	const char* ImportModeToString(EAudioImportMode m)
	{
		return EAudioImportMode::Streaming == m ? "Streaming" : "Decompressed";
	}

	EAudioBusKind ParseBusKind(const std::string& s)
	{
		if (s == "Master") return EAudioBusKind::Master;
		if (s == "Music")  return EAudioBusKind::Music;
		if (s == "Voice")  return EAudioBusKind::Voice;
		if (s == "UI")     return EAudioBusKind::UI;
		if (s == "Custom") return EAudioBusKind::Custom;
		return EAudioBusKind::SFX;
	}
	const char* BusKindToString(EAudioBusKind b)
	{
		switch (b)
		{
		case EAudioBusKind::Master: return "Master";
		case EAudioBusKind::Music:  return "Music";
		case EAudioBusKind::Voice:  return "Voice";
		case EAudioBusKind::UI:     return "UI";
		case EAudioBusKind::Custom: return "Custom";
		default:                    return "SFX";
		}
	}
}

AudioImportOptions CAudioImportOptions::FromYaml(const std::string& yamlText)
{
	AudioImportOptions options;
	if (yamlText.empty()) return options;

	YAML::Node root;
	try { root = YAML::Load(yamlText); }
	catch (const YAML::Exception&) { return options; }

	if (!root || false == root.IsMap()) return options;
	const YAML::Node audioNode = root["Audio"] ? root["Audio"] : root;

	options.Mode          = ParseImportMode(ReadOption<std::string>(audioNode, "Mode", "Decompressed"));
	options.DefaultVolume = ReadOption<float>(audioNode, "DefaultVolume", options.DefaultVolume);
	options.Loop          = ReadOption<bool> (audioNode, "Loop",          options.Loop);
	options.Is3D          = ReadOption<bool> (audioNode, "Is3D",          options.Is3D);
	options.MinDistance   = ReadOption<float>(audioNode, "MinDistance",   options.MinDistance);
	options.MaxDistance   = ReadOption<float>(audioNode, "MaxDistance",   options.MaxDistance);
	options.DefaultBus    = ParseBusKind(ReadOption<std::string>(audioNode, "DefaultBus", "SFX"));
	return options;
}

std::string CAudioImportOptions::ToYaml(const AudioImportOptions& options)
{
	YAML::Emitter emitter;
	emitter << YAML::BeginMap;
	emitter << YAML::Key << "Audio" << YAML::Value;
	emitter << YAML::BeginMap;
	emitter << YAML::Key << "Mode"          << YAML::Value << ImportModeToString(options.Mode);
	emitter << YAML::Key << "DefaultVolume" << YAML::Value << options.DefaultVolume;
	emitter << YAML::Key << "Loop"          << YAML::Value << options.Loop;
	emitter << YAML::Key << "Is3D"          << YAML::Value << options.Is3D;
	emitter << YAML::Key << "MinDistance"   << YAML::Value << options.MinDistance;
	emitter << YAML::Key << "MaxDistance"   << YAML::Value << options.MaxDistance;
	emitter << YAML::Key << "DefaultBus"    << YAML::Value << BusKindToString(options.DefaultBus);
	emitter << YAML::EndMap;
	emitter << YAML::EndMap;
	return emitter.c_str();
}

// ── CAudioAssetLoader ──────────────────────────────────────────────────────

namespace
{
	std::string LowerExtension(const File::Path& path)
	{
		std::string ext = path.extension().generic_string();
		std::transform(ext.begin(), ext.end(), ext.begin(),
			[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return ext;
	}

	bool IsSupportedAudioExtension(const File::Path& path)
	{
		const std::string ext = LowerExtension(path);
		return ext == ".wav" || ext == ".mp3" || ext == ".flac" || ext == ".ogg";
	}
}

EAssetType CAudioAssetLoader::GetSupportedType() const
{
	return EAssetType::Audio;
}

bool CAudioAssetLoader::CanLoad(const AssetLoadDesc& desc) const
{
	if (EAssetType::Audio != desc.Type || nullptr == desc.MetaData)
	{
		return false;
	}

	const AudioImportOptions importOpts = CAudioImportOptions::FromYaml(desc.MetaData->ImportOptionsYaml);
	if (desc.HasMemoryPayload())
	{
		return EAudioImportMode::Streaming != importOpts.Mode || false == desc.ResolvedPath.empty();
	}

	return EAssetType::Audio == desc.Type
	    && false == desc.ResolvedPath.empty()
	    && IsSupportedAudioExtension(desc.ResolvedPath);
}

OwnerPtr<IAsset> CAudioAssetLoader::Load(const AssetLoadDesc& desc)
{
	if (false == CanLoad(desc)) return nullptr;

	const AudioImportOptions importOpts =
		CAudioImportOptions::FromYaml(desc.MetaData->ImportOptionsYaml);

#if defined(JBRO_HAS_MINIAUDIO) && JBRO_HAS_MINIAUDIO

	// 디코더 설정 — F32 / 2ch / 48kHz 로 정규화. backend(ma_engine) 와 포맷 일치.
	ma_decoder_config decoderConfig = ma_decoder_config_init(ma_format_f32, 2, 48000);
	ma_decoder decoder;
	ma_result initResult = MA_ERROR;
	if (desc.HasMemoryPayload())
	{
		initResult = ma_decoder_init_memory(desc.MemoryPayload.data(), desc.MemoryPayload.size(), &decoderConfig, &decoder);
	}
	else
	{
		// 한글 사용자 경로 안전 처리 — wstring → UTF-8 → ma_decoder_init_file.
		// miniaudio 는 path 를 UTF-8 로 해석한다 (Windows 빌드는 자동으로 wchar 변환).
		const std::string utf8Path = desc.ResolvedPath.string();
		initResult = ma_decoder_init_file(utf8Path.c_str(), &decoderConfig, &decoder);
	}
	if (MA_SUCCESS != initResult)
	{
		return nullptr;
	}

	ma_uint64 totalFrames = 0;
	ma_decoder_get_length_in_pcm_frames(&decoder, &totalFrames);

	AudioFormatInfo format;
	format.Format     = EAudioFormat::PCM_F32;
	format.SampleRate = 48000;
	format.Channels   = 2;

	// Streaming 모드 — PCM 안 읽고 경로만 보관. 디코더는 즉시 해제.
	if (EAudioImportMode::Streaming == importOpts.Mode)
	{
		ma_decoder_uninit(&decoder);
		OwnerPtr<CAudioAsset> asset = MakeOwnerPtr<CAudioAsset>(
			*desc.MetaData,
			format,
			desc.ResolvedPath,
			static_cast<std::uint64_t>(totalFrames));
		if (asset) asset->SetImportOptions(importOpts);
		return asset;
	}

	// Decompressed — 전체를 메모리로 디코딩
	const std::size_t bytesPerFrame = format.Channels * sizeof(float);
	std::vector<std::uint8_t> pcm(static_cast<std::size_t>(totalFrames) * bytesPerFrame);

	ma_uint64 framesRead = 0;
	ma_decoder_read_pcm_frames(&decoder, pcm.data(), totalFrames, &framesRead);
	ma_decoder_uninit(&decoder);

	if (framesRead < totalFrames)
	{
		pcm.resize(static_cast<std::size_t>(framesRead) * bytesPerFrame);
	}

	OwnerPtr<CAudioAsset> asset = MakeOwnerPtr<CAudioAsset>(
		*desc.MetaData,
		format,
		std::move(pcm),
		static_cast<std::uint64_t>(framesRead));
	if (asset) asset->SetImportOptions(importOpts);
	return asset;

#else
	// miniaudio 없으면 빈 자산 — 메타만 보관해 직렬화/Editor UI 는 그대로 동작.
	OwnerPtr<CAudioAsset> asset = MakeOwnerPtr<CAudioAsset>(*desc.MetaData);
	if (asset) asset->SetImportOptions(importOpts);
	return asset;
#endif
}

void CAudioAssetLoader::Unload(IAsset& asset)
{
	(void)asset;   // CAudioAsset 의 PCM vector / 경로 string 은 소멸자 자동 해제
}
