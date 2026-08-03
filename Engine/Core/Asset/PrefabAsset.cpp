#include "pch.h"
#include "PrefabAsset.h"

CPrefabAssetLoader::CPrefabAssetLoader()
	: CFileAssetLoader(EAssetType::Prefab)
{
}

OwnerPtr<IAsset> CPrefabAssetLoader::CreateAsset(const AssetMetaData& metaData, std::vector<std::uint8_t>&& data) const
{
	return MakeOwnerPtr<CPrefabAsset>(metaData, std::move(data));
}
