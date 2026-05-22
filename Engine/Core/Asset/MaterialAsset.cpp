#include "pch.h"
#include "MaterialAsset.h"

CMaterialAsset::CMaterialAsset(const AssetMetaData& metaData)
	: m_metaData(metaData)
{
}

AssetGuid CMaterialAsset::GetGuid() const
{
	return m_metaData.Guid;
}

EAssetType CMaterialAsset::GetAssetType() const
{
	return m_metaData.Type;
}

EAssetLoadState CMaterialAsset::GetLoadState() const
{
	return m_loadState;
}

const AssetMetaData& CMaterialAsset::GetMetaData() const
{
	return m_metaData;
}

EAssetType CMaterialAssetLoader::GetSupportedType() const
{
	return EAssetType::Material;
}

bool CMaterialAssetLoader::CanLoad(const AssetLoadDesc& desc) const
{
	return EAssetType::Material == desc.Type && nullptr != desc.MetaData;
}

OwnerPtr<IAsset> CMaterialAssetLoader::Load(const AssetLoadDesc& desc)
{
	if (false == CanLoad(desc))
	{
		return nullptr;
	}

	return MakeOwnerPtr<CMaterialAsset>(*desc.MetaData);
}

void CMaterialAssetLoader::Unload(IAsset& asset)
{
	(void)asset;
}
