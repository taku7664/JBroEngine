#pragma once

#include "Core/Asset/IAsset.h"
#include "Core/Asset/IAssetLoader.h"

#include <string>
#include <vector>

// ── Sprite slice 모드 ─────────────────────────────────────────────────────────
// None      : 슬라이스 없음 — 전체 이미지를 1 프레임으로 사용.
// Automatic : 알파 기반 자동 분할 (예약 — 구현 미정. 현재는 None 폴백).
// CellSize  : 셀의 너비/높이 픽셀 기준 균일 그리드 분할.
// CellCount : 행/열 개수 기준 균일 격자 분할.
enum class ESpriteSliceType : std::uint8_t
{
	None,
	Automatic,
	CellSize,
	CellCount
};

struct SpriteImportOptions
{
	ESpriteSliceType SliceType = ESpriteSliceType::None;

	// CellCount 모드 전용
	std::uint32_t RowCount    = 1;
	std::uint32_t ColumnCount = 1;

	// CellSize 모드 전용 (셀 픽셀 크기)
	std::uint32_t CellWidth   = 32;
	std::uint32_t CellHeight  = 32;

	// 공용 — 그리드 외곽 여백/내부 간격
	std::uint32_t MarginX = 0;
	std::uint32_t MarginY = 0;
	std::uint32_t GapX    = 0;
	std::uint32_t GapY    = 0;

	// 피벗 / PPU
	float PivotX        = 0.5f;
	float PivotY        = 0.5f;
	// 0 이면 프로젝트 세팅의 Default PPU 로 폴백(자산별 미지정). 0 보다 크면 그 값으로 오버라이드.
	float PixelsPerUnit = 0.0f;
};

struct SpriteFrame
{
	std::uint32_t X      = 0;
	std::uint32_t Y      = 0;
	std::uint32_t Width  = 0;
	std::uint32_t Height = 0;
	float         PivotX = 0.5f;
	float         PivotY = 0.5f;
};

class CSpriteImportOptions final
{
public:
	static SpriteImportOptions FromYaml(const std::string& yamlText);
	static std::string ToYaml(const SpriteImportOptions& options);

	// 텍스처 크기 + SliceType 기반 frame 목록 생성.
	// None / Automatic: 전체 1 프레임.
	// CellCount: 행×열.   CellSize: 픽셀 셀 크기.
	static std::vector<SpriteFrame> BuildFrames(std::uint32_t textureWidth,
	                                            std::uint32_t textureHeight,
	                                            const SpriteImportOptions& options);
};

// ── CSpriteAsset ──────────────────────────────────────────────────────────────
// 픽셀 + GPU 텍스처 + 슬라이스 정보를 모두 포함하는 단일 자산.
// 이전에 분리되어 있던 CTextureAsset 의 역할(픽셀/GPU)도 통합.
class CSpriteAsset final : public IAsset
{
public:
	CSpriteAsset(const AssetMetaData& metaData,
	             std::uint32_t width,
	             std::uint32_t height,
	             std::vector<std::uint8_t>&& pixels);
	// 빈 자산 (메타만) — 직렬화/디버그용
	explicit CSpriteAsset(const AssetMetaData& metaData);

	AssetGuid GetGuid() const override;
	EAssetType GetAssetType() const override;
	EAssetLoadState GetLoadState() const override;
	const AssetMetaData& GetMetaData() const override;
	void ApplyImportOptions(const std::string& importOptionsYaml) override;

	// 픽셀/크기
	std::uint32_t GetWidth()  const;
	std::uint32_t GetHeight() const;
	const std::vector<std::uint8_t>& GetPixels() const;

	// 픽셀 갱신 카운터. ReplacePixels 호출마다 증가.
	// 소비자(예: SpriteRenderSystem) 가 마지막으로 본 값과 비교해 변경을 감지한다.
	std::uint32_t GetPixelGeneration() const;

	// 슬라이스 정보
	const SpriteImportOptions&     GetImportOptions() const;
	const std::vector<SpriteFrame>& GetFrames() const;
	void SetImportData(const SpriteImportOptions& options, std::vector<SpriteFrame>&& frames);

	// raw 픽셀까지 in-place 교체. width/height/pixels 가 새 디스크 상태로 덮어씌워진다.
	// GPU 텍스처는 RenderResourceCache 가 따로 invalidate 해야 한다(이 자산은 GPU 를 소유하지 않음).
	void ReplacePixels(std::uint32_t width, std::uint32_t height, std::vector<std::uint8_t>&& pixels);

	// 자산이 실제 사용할 PPU. 임포트 옵션의 PixelsPerUnit 가 0 이면 fallback 사용.
	// fallback 은 보통 프로젝트의 Default PPU. 둘 다 0 이하면 1.0f 로 안전 폴백.
	float GetEffectivePixelsPerUnit(float fallback) const;

private:
	AssetMetaData               m_metaData;
	std::uint32_t               m_width  = 0;
	std::uint32_t               m_height = 0;
	std::vector<std::uint8_t>   m_pixels;
	SpriteImportOptions         m_importOptions;
	std::vector<SpriteFrame>    m_frames;
	std::uint32_t               m_pixelGeneration = 0;
	EAssetLoadState             m_loadState = EAssetLoadState::Loaded;
};

// 이미지 파일(.png/.jpg/.tga 등) 로더. CSpriteAsset 을 생성한다.
class CSpriteAssetLoader final : public IAssetLoader
{
public:
	EAssetType GetSupportedType() const override;
	bool CanLoad(const AssetLoadDesc& desc) const override;
	OwnerPtr<IAsset> Load(const AssetLoadDesc& desc) override;
	void Unload(IAsset& asset) override;
	// in-place reload: 메타 옵션 + raw 파일 재로드 모두 같은 자산 객체에 반영.
	bool ReloadInto(IAsset& existing, const AssetMetaData& metaData) override;
};
