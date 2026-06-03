#pragma once

#include "Core/Asset/IAsset.h"
#include "Core/Asset/IAssetLoader.h"
#include "Core/Audio/AudioTypes.h"   // EAudioEffectKind

#include <map>
#include <string>

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
//  CAudioEffectAsset / CAudioEffectAssetLoader
//
//  사운드용 DSP 효과를 독립 에셋으로 표현한다 (스프라이트의 Material 패턴).
//  AudioPlayer 컴포넌트가 EffectGuid 로 참조하고, 재생 시 CAudioSystem 이
//  이 에셋의 Kind/파라미터로 IAudioEffect 노드를 구성해 체인에 삽입한다(후속 단계).
//
//  파라미터 모델은 공통 std::map<string,float> — 효과 종류를 추가해도 코드 변경
//  없이 키만 늘리면 되고, IAudioEffect::SetParameter(name,value) 와 직결된다.
//  키 표준(예): lpf=cutoff,q / delay=delay,decay,wet / reverb=roomSize,damping,wet,dry.
//
//  디스크 표현은 .jfx 소스 파일 안의 YAML (Material 의 .jmat 패턴).
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

struct AudioEffectData
{
	EAudioEffectKind            Kind = EAudioEffectKind::Reverb;
	std::map<std::string, float> Parameters;
};

class CAudioEffectAsset final : public IAsset
{
public:
	CAudioEffectAsset(const AssetMetaData& metaData, AudioEffectData data);

	// IAsset
	AssetGuid            GetGuid()      const override;
	EAssetType           GetAssetType() const override;
	EAssetLoadState      GetLoadState() const override;
	const AssetMetaData& GetMetaData()  const override;
	void                 ApplyImportOptions(const std::string& importOptionsYaml) override;

	// 데이터
	EAudioEffectKind                    GetKind() const;
	const std::map<std::string, float>& GetParameters() const;
	float                               GetParameter(const std::string& key, float defaultValue = 0.0f) const;

private:
	AssetMetaData   m_metaData;
	AudioEffectData m_data;
	EAssetLoadState m_loadState = EAssetLoadState::Loaded;
};

// ── YAML 직렬화 ─────────────────────────────────────────────────────────────
// .jfx 파일 본문 텍스트 ↔ AudioEffectData.
class CAudioEffectSerializer final
{
public:
	static AudioEffectData FromYaml(const std::string& yamlText);
	static std::string     ToYaml(const AudioEffectData& data);
};

// ── 로더 ─────────────────────────────────────────────────────────────────────
class CAudioEffectAssetLoader final : public IAssetLoader
{
public:
	EAssetType        GetSupportedType() const override;
	bool              CanLoad(const AssetLoadDesc& desc) const override;
	OwnerPtr<IAsset>  Load   (const AssetLoadDesc& desc) override;
	void              Unload (IAsset& asset) override;
	bool              ReloadInto(IAsset& existing, const AssetMetaData& metaData) override;
};
