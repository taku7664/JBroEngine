#pragma once

#include "Core/Asset/IAsset.h"
#include "Core/Asset/IAssetLoader.h"
#include "Core/Renderer/RendererTypes.h"

class CMaterialAsset final : public IAsset
{
public:
	explicit CMaterialAsset(const AssetMetaData& metaData);

	AssetGuid GetGuid() const override;
	EAssetType GetAssetType() const override;
	EAssetLoadState GetLoadState() const override;
	const AssetMetaData& GetMetaData() const override;

	ERenderQueue Queue = ERenderQueue::Transparent;

private:
	AssetMetaData m_metaData;
	EAssetLoadState m_loadState = EAssetLoadState::Loaded;
};

class CMaterialAssetLoader final : public IAssetLoader
{
public:
	EAssetType GetSupportedType() const override;
	bool CanLoad(const AssetLoadDesc& desc) const override;
	OwnerPtr<IAsset> Load(const AssetLoadDesc& desc) override;
	void Unload(IAsset& asset) override;
};
