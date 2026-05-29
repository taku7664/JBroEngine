#pragma once

#include "Core/Asset/IAssetRegistry.h"

class CAssetRegistry final : public IAssetRegistry
{
public:
	bool RegisterAsset(const AssetMetaData& metaData) override;
	bool UnregisterAsset(const AssetGuid& guid) override;
	void Clear() override;
	void ClearNonPersistent() override;
	const AssetMetaData* FindAsset(const AssetGuid& guid) const override;
	const AssetMetaData* FindAssetByPath(const File::Path& path) const override;
	void BuildSnapshot(AssetRegistrySnapshot& outSnapshot) const override;

private:
	std::unordered_map<AssetGuid, AssetMetaData> m_assetTable;
	std::unordered_map<File::Path, AssetGuid> m_pathToGuidTable;
};
