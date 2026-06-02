#pragma once

#include "Core/Asset/IAsset.h"
#include "Core/Asset/IAssetLoader.h"
#include "Core/Audio/AudioTypes.h"

#include <string>
#include <vector>

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
//  AudioImportOptions / CAudioAsset / CAudioAssetLoader
//
//  CSpriteAsset 과 동일한 패턴.  디스크의 .wav/.mp3/.flac/.ogg 를 디코딩해
//  PCM 데이터를 보관하거나(Decompressed), 파일 경로만 보유하고 실제 디코딩은
//  재생 시점에 스트림으로 처리한다(Streaming).
//
//  이 헤더는 miniaudio 같은 백엔드 의존을 노출하지 않는다 — 추후 저수준 영역이
//  별도 라이브러리로 분리되어도 자산 측은 인터페이스(AudioTypes) 만 의존.
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

struct AudioImportOptions
{
	EAudioImportMode Mode          = EAudioImportMode::Decompressed;
	float            DefaultVolume = 1.0f;
	bool             Loop          = false;
	bool             Is3D          = false;
	float            MinDistance   = 1.0f;
	float             MaxDistance  = 50.0f;
	EAudioBusKind    DefaultBus    = EAudioBusKind::SFX;
};

// ── CAudioAsset ──────────────────────────────────────────────────────────────
class CAudioAsset final : public IAsset
{
public:
	// Decompressed 생성자 — 디코딩 끝난 PCM 보유.
	CAudioAsset(const AssetMetaData& metaData,
	            AudioFormatInfo format,
	            std::vector<std::uint8_t>&& pcmData,
	            std::uint64_t totalFrames);

	// Streaming 생성자 — 파일 경로만 보유. 재생 시 backend 가 직접 디코딩.
	CAudioAsset(const AssetMetaData& metaData,
	            AudioFormatInfo format,
	            File::Path streamPath,
	            std::uint64_t totalFrames);

	// 빈 자산 (메타만) — 직렬화/디버그용
	explicit CAudioAsset(const AssetMetaData& metaData);

	// IAsset
	AssetGuid            GetGuid()      const override;
	EAssetType           GetAssetType() const override;
	EAssetLoadState      GetLoadState() const override;
	const AssetMetaData& GetMetaData()  const override;
	void                 ApplyImportOptions(const std::string& importOptionsYaml) override;

	// 임포트 옵션
	const AudioImportOptions&        GetImportOptions() const;
	void                             SetImportOptions(const AudioImportOptions& options);

	// 포맷 / 길이
	const AudioFormatInfo&           GetFormat() const;
	std::uint64_t                    GetTotalFrames()      const;
	double                           GetDurationSeconds()  const;

	// 데이터
	bool                             IsStreaming()    const;
	const std::vector<std::uint8_t>& GetPcmData()     const;   // Decompressed 모드
	const File::Path&                GetStreamPath()  const;   // Streaming 모드

private:
	AssetMetaData             m_metaData;
	AudioFormatInfo           m_format;
	AudioImportOptions        m_importOptions;
	std::vector<std::uint8_t> m_pcmData;     // Decompressed 모드용
	File::Path                m_streamPath;  // Streaming 모드용
	std::uint64_t             m_totalFrames = 0;
	EAudioImportMode          m_mode = EAudioImportMode::Decompressed;
	EAssetLoadState           m_loadState = EAssetLoadState::Loaded;
};

// ── 임포트 옵션 직렬화 ─────────────────────────────────────────────────────
// CSpriteImportOptions 와 동일한 패턴 — yaml 텍스트로 .Jmeta 안에 저장.
class CAudioImportOptions final
{
public:
	static AudioImportOptions FromYaml(const std::string& yamlText);
	static std::string        ToYaml(const AudioImportOptions& options);
};

// ── 로더 ─────────────────────────────────────────────────────────────────────
// .wav / .mp3 / .flac / .ogg 를 디코딩해 CAudioAsset 생성.  miniaudio 의
// 내장 디코더 사용 — 별도 라이브러리 추가 필요 X.
class CAudioAssetLoader final : public IAssetLoader
{
public:
	EAssetType        GetSupportedType() const override;
	bool              CanLoad(const AssetLoadDesc& desc) const override;
	OwnerPtr<IAsset>  Load   (const AssetLoadDesc& desc) override;
	void              Unload (IAsset& asset) override;
	// in-place reload: 임포트 옵션만 갱신 (PCM/Stream raw 변경은 현재 미지원 — 향후 확장).
	bool              ReloadInto(IAsset& existing, const AssetMetaData& metaData) override;
};
