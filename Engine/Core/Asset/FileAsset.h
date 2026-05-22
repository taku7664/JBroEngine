#pragma once

#include "Core/Asset/IAsset.h"
#include "Core/Asset/IAssetLoader.h"

class CFileAsset final : public IAsset
{
public:
	CFileAsset(const AssetMetaData& metaData, std::vector<std::uint8_t>&& data);

	AssetGuid GetGuid() const override;
	EAssetType GetAssetType() const override;
	EAssetLoadState GetLoadState() const override;
	const AssetMetaData& GetMetaData() const override;

	const std::vector<std::uint8_t>& GetData() const;
	std::string_view GetText() const;

private:
	AssetMetaData m_metaData;
	std::vector<std::uint8_t> m_data;
	EAssetLoadState m_loadState = EAssetLoadState::Loaded;
};

class CFileAssetLoader final : public IAssetLoader
{
public:
	explicit CFileAssetLoader(EAssetType supportedType);

	EAssetType GetSupportedType() const override;
	bool CanLoad(const AssetLoadDesc& desc) const override;
	OwnerPtr<IAsset> Load(const AssetLoadDesc& desc) override;
	void Unload(IAsset& asset) override;

private:
	EAssetType m_supportedType = EAssetType::Unknown;
};
