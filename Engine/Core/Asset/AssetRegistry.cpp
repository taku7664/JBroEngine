#include "pch.h"
#include "AssetRegistry.h"

#include "Core/Asset/AssetPath.h"

bool CAssetRegistry::RegisterAsset(const AssetMetaData& metaData)
{
	std::string normalizedPath;
	if (metaData.Guid.IsNull() || false == CAssetPath::NormalizeAssetKey(metaData.Path.generic_string().c_str(), normalizedPath))
	{
		return false;
	}

	std::lock_guard lock(m_mutex);

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
	std::lock_guard lock(m_mutex);

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
	std::lock_guard lock(m_mutex);
	m_assetTable.clear();
	m_pathToGuidTable.clear();
}

void CAssetRegistry::ClearNonPersistent()
{
	std::lock_guard lock(m_mutex);
	for (auto it = m_assetTable.begin(); it != m_assetTable.end(); )
	{
		if (it->second.IsPersistent)
		{
			++it;
			continue;
		}
		m_pathToGuidTable.erase(it->second.Path);
		it = m_assetTable.erase(it);
	}
}

const AssetMetaData* CAssetRegistry::FindAsset(const AssetGuid& guid) const
{
	std::lock_guard lock(m_mutex);
	auto it = m_assetTable.find(guid);
	if (it == m_assetTable.end())
	{
		return nullptr;
	}

	return &it->second;
}

const AssetMetaData* CAssetRegistry::FindAssetByPath(const File::Path& path) const
{
	std::string normalizedPath;
	if (false == CAssetPath::NormalizeAssetKey(path.generic_string().c_str(), normalizedPath))
	{
		return nullptr;
	}

	std::lock_guard lock(m_mutex);
	auto pathIt = m_pathToGuidTable.find(File::Path(normalizedPath));
	if (pathIt == m_pathToGuidTable.end())
	{
		return nullptr;
	}

	auto it = m_assetTable.find(pathIt->second);
	if (it == m_assetTable.end())
	{
		return nullptr;
	}
	return &it->second;
}

void CAssetRegistry::BuildSnapshot(AssetRegistrySnapshot& outSnapshot) const
{
	std::lock_guard lock(m_mutex);
	outSnapshot.Assets.clear();
	outSnapshot.Assets.reserve(m_assetTable.size());
	for (const auto& assetPair : m_assetTable)
	{
		outSnapshot.Assets.push_back(assetPair.second);
	}
}
