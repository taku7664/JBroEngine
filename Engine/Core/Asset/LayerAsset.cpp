#include "pch.h"
#include "LayerAsset.h"

CLayerAssetLoader::CLayerAssetLoader()
	: CFileAssetLoader(EAssetType::Layer)
{
}

OwnerPtr<IAsset> CLayerAssetLoader::CreateAsset(const AssetMetaData& metaData, std::vector<std::uint8_t>&& data) const
{
	return MakeOwnerPtr<CLayerAsset>(metaData, std::move(data));
}
