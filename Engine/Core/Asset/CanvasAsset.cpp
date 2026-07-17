#include "pch.h"
#include "CanvasAsset.h"

CCanvasAssetLoader::CCanvasAssetLoader()
	: CFileAssetLoader(EAssetType::Canvas)
{
}

OwnerPtr<IAsset> CCanvasAssetLoader::CreateAsset(const AssetMetaData& metaData, std::vector<std::uint8_t>&& data) const
{
	return MakeOwnerPtr<CCanvasAsset>(metaData, std::move(data));
}
