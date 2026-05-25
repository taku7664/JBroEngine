#pragma once

#include "Core/Asset/IAsset.h"
#include "Core/Asset/IAssetLoader.h"

#include <string>
#include <vector>

struct SpriteImportOptions
{
	std::uint32_t RowCount = 1;
	std::uint32_t ColumnCount = 1;
	std::uint32_t MarginX = 0;
	std::uint32_t MarginY = 0;
	std::uint32_t GapX = 0;
	std::uint32_t GapY = 0;
	float PivotX = 0.5f;
	float PivotY = 0.5f;
	float PixelsPerUnit = 100.0f;
};

struct SpriteFrame
{
	std::uint32_t X = 0;
	std::uint32_t Y = 0;
	std::uint32_t Width = 0;
	std::uint32_t Height = 0;
	float PivotX = 0.5f;
	float PivotY = 0.5f;
};

class CSpriteImportOptions final
{
public:
	static SpriteImportOptions FromYaml(const std::string& yamlText);
	static std::string ToYaml(const SpriteImportOptions& options);
	static std::vector<SpriteFrame> BuildFrames(std::uint32_t textureWidth, std::uint32_t textureHeight, const SpriteImportOptions& options);
};

class CSpriteAsset final : public IAsset
{
public:
	explicit CSpriteAsset(const AssetMetaData& metaData);

	AssetGuid GetGuid() const override;
	EAssetType GetAssetType() const override;
	EAssetLoadState GetLoadState() const override;
	const AssetMetaData& GetMetaData() const override;

	AssetGuid TextureGuid = INVALID_ASSET_GUID;
	float PivotX = 0.5f;
	float PivotY = 0.5f;
	float PixelsPerUnit = 100.0f;
	SpriteImportOptions ImportOptions;
	std::vector<SpriteFrame> Frames;

private:
	AssetMetaData m_metaData;
	EAssetLoadState m_loadState = EAssetLoadState::Loaded;
};

class CSpriteAssetLoader final : public IAssetLoader
{
public:
	EAssetType GetSupportedType() const override;
	bool CanLoad(const AssetLoadDesc& desc) const override;
	OwnerPtr<IAsset> Load(const AssetLoadDesc& desc) override;
	void Unload(IAsset& asset) override;
};
