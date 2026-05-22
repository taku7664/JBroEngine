#pragma once

#include "Core/Asset/IAsset.h"
#include "Core/Asset/IAssetLoader.h"

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
