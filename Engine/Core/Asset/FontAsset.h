#pragma once

#include "Core/Asset/IAsset.h"
#include "Core/Asset/IAssetLoader.h"

#include <cstdint>
#include <vector>

enum class EFontStyle : std::uint8_t
{
	Regular,
	Bold,
	Italic,
	BoldItalic
};

class CFontFaceAsset final : public IAsset
{
public:
	CFontFaceAsset(const AssetMetaData& metaData, std::vector<std::uint8_t>&& bytes, std::uint32_t faceIndex);

	AssetGuid GetGuid() const override;
	EAssetType GetAssetType() const override;
	EAssetLoadState GetLoadState() const override;
	const AssetMetaData& GetMetaData() const override;
	const std::vector<std::uint8_t>& GetBytes() const;
	std::uint32_t GetFaceIndex() const;
	std::uint32_t GetGeneration() const;

private:
	AssetMetaData m_metaData;
	std::vector<std::uint8_t> m_bytes;
	std::uint32_t m_faceIndex = 0;
	std::uint32_t m_generation = 1;
	EAssetLoadState m_loadState = EAssetLoadState::Loaded;
};

struct FontFamilyData
{
	AssetGuid Regular = INVALID_ASSET_GUID;
	AssetGuid Bold = INVALID_ASSET_GUID;
	AssetGuid Italic = INVALID_ASSET_GUID;
	AssetGuid BoldItalic = INVALID_ASSET_GUID;
	bool UseProjectFallbacks = true;
	std::vector<AssetGuid> FallbackFamilies;
};

class CFontFamilyAsset final : public IAsset
{
public:
	CFontFamilyAsset(const AssetMetaData& metaData, FontFamilyData data);

	AssetGuid GetGuid() const override;
	EAssetType GetAssetType() const override;
	EAssetLoadState GetLoadState() const override;
	const AssetMetaData& GetMetaData() const override;
	const FontFamilyData& GetData() const;
	void SetData(FontFamilyData data);
	AssetGuid ResolveFace(EFontStyle style, bool& usedRegularFallback) const;

private:
	AssetMetaData m_metaData;
	FontFamilyData m_data;
	EAssetLoadState m_loadState = EAssetLoadState::Loaded;
};

class CFontFaceAssetLoader final : public IAssetLoader
{
public:
	EAssetType GetSupportedType() const override;
	bool CanLoad(const AssetLoadDesc& desc) const override;
	OwnerPtr<IAsset> Load(const AssetLoadDesc& desc) override;
	void Unload(IAsset& asset) override;
};

class CFontFamilyAssetLoader final : public IAssetLoader
{
public:
	EAssetType GetSupportedType() const override;
	bool CanLoad(const AssetLoadDesc& desc) const override;
	OwnerPtr<IAsset> Load(const AssetLoadDesc& desc) override;
	void Unload(IAsset& asset) override;
	bool ReloadInto(IAsset& existing, const AssetMetaData& metaData) override;
};
