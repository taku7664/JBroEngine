#include "pch.h"
#include "SpriteAsset.h"

#include <sstream>

CSpriteAsset::CSpriteAsset(const AssetMetaData& metaData)
	: m_metaData(metaData)
{
}

AssetGuid CSpriteAsset::GetGuid() const
{
	return m_metaData.Guid;
}

EAssetType CSpriteAsset::GetAssetType() const
{
	return m_metaData.Type;
}

EAssetLoadState CSpriteAsset::GetLoadState() const
{
	return m_loadState;
}

const AssetMetaData& CSpriteAsset::GetMetaData() const
{
	return m_metaData;
}

EAssetType CSpriteAssetLoader::GetSupportedType() const
{
	return EAssetType::Sprite;
}

bool CSpriteAssetLoader::CanLoad(const AssetLoadDesc& desc) const
{
	return EAssetType::Sprite == desc.Type && nullptr != desc.ResolvedPath && nullptr != desc.MetaData;
}

OwnerPtr<IAsset> CSpriteAssetLoader::Load(const AssetLoadDesc& desc)
{
	if (false == CanLoad(desc))
	{
		return nullptr;
	}

	std::ifstream file(desc.ResolvedPath);
	if (false == file.is_open())
	{
		return nullptr;
	}

	OwnerPtr<CSpriteAsset> sprite = MakeOwnerPtr<CSpriteAsset>(*desc.MetaData);
	std::string key;
	while (file >> key)
	{
		if (key == "TextureGuid")
		{
			std::string guid;
			file >> guid;
			sprite->TextureGuid = File::Guid(guid);
		}
		else if (key == "Pivot")
		{
			file >> sprite->PivotX >> sprite->PivotY;
		}
		else if (key == "PixelsPerUnit")
		{
			file >> sprite->PixelsPerUnit;
		}
	}

	return sprite;
}

void CSpriteAssetLoader::Unload(IAsset& asset)
{
	(void)asset;
}
