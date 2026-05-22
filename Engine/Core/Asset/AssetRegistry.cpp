#include "pch.h"
#include "AssetRegistry.h"

#include "Core/Asset/AssetPath.h"

bool CAssetRegistry::RegisterAsset(const AssetMetaData& metaData)
{
	std::string normalizedPath;
	if (metaData.Guid.IsNull() || false == CAssetPath::NormalizeRelativePath(metaData.Path.generic_string().c_str(), normalizedPath))
	{
		return false;
	}

	if (m_assetTable.contains(metaData.Guid))
	{
		return false;
	}

	File::Path pathKey(normalizedPath);
	if (m_pathToGuidTable.contains(pathKey))
	{
		return false;
	}

	AssetMetaData normalizedMetaData = metaData;
	normalizedMetaData.Path = File::Path(normalizedPath);
	if (normalizedMetaData.MetaPath.empty())
	{
		normalizedMetaData.MetaPath = File::Path(CAssetPath::MakeMetaPath(normalizedMetaData.Path.generic_string().c_str()));
	}

	m_assetTable.emplace(normalizedMetaData.Guid, normalizedMetaData);
	m_pathToGuidTable.emplace(normalizedMetaData.Path, normalizedMetaData.Guid);
	return true;
}

bool CAssetRegistry::UnregisterAsset(const AssetGuid& guid)
{
	auto it = m_assetTable.find(guid);
	if (it == m_assetTable.end())
	{
		return false;
	}

	m_pathToGuidTable.erase(it->second.Path);
	m_assetTable.erase(it);
	return true;
}

void CAssetRegistry::Clear()
{
	m_assetTable.clear();
	m_pathToGuidTable.clear();
}

const AssetMetaData* CAssetRegistry::FindAsset(const AssetGuid& guid) const
{
	auto it = m_assetTable.find(guid);
	if (it == m_assetTable.end())
	{
		return nullptr;
	}

	return &it->second;
}

const AssetMetaData* CAssetRegistry::FindAssetByPath(const char* path) const
{
	std::string normalizedPath;
	if (false == CAssetPath::NormalizeRelativePath(path, normalizedPath))
	{
		return nullptr;
	}

	auto pathIt = m_pathToGuidTable.find(File::Path(normalizedPath));
	if (pathIt == m_pathToGuidTable.end())
	{
		return nullptr;
	}

	return FindAsset(pathIt->second);
}

void CAssetRegistry::BuildSnapshot(AssetRegistrySnapshot& outSnapshot) const
{
	outSnapshot.Assets.clear();
	outSnapshot.Assets.reserve(m_assetTable.size());
	for (const auto& assetPair : m_assetTable)
	{
		outSnapshot.Assets.push_back(assetPair.second);
	}
}
