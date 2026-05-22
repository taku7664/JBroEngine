#include "pch.h"
#include "FileAsset.h"

CFileAsset::CFileAsset(const AssetMetaData& metaData, std::vector<std::uint8_t>&& data)
	: m_metaData(metaData)
	, m_data(std::move(data))
{
}

AssetGuid CFileAsset::GetGuid() const
{
	return m_metaData.Guid;
}

EAssetType CFileAsset::GetAssetType() const
{
	return m_metaData.Type;
}

EAssetLoadState CFileAsset::GetLoadState() const
{
	return m_loadState;
}

const AssetMetaData& CFileAsset::GetMetaData() const
{
	return m_metaData;
}

const std::vector<std::uint8_t>& CFileAsset::GetData() const
{
	return m_data;
}

std::string_view CFileAsset::GetText() const
{
	if (m_data.empty())
	{
		return std::string_view();
	}

	return std::string_view(reinterpret_cast<const char*>(m_data.data()), m_data.size());
}

CFileAssetLoader::CFileAssetLoader(EAssetType supportedType)
	: m_supportedType(supportedType)
{
}

EAssetType CFileAssetLoader::GetSupportedType() const
{
	return m_supportedType;
}

bool CFileAssetLoader::CanLoad(const AssetLoadDesc& desc) const
{
	return EAssetType::Unknown != m_supportedType
		&& desc.Type == m_supportedType
		&& nullptr != desc.ResolvedPath
		&& nullptr != desc.MetaData;
}

OwnerPtr<IAsset> CFileAssetLoader::Load(const AssetLoadDesc& desc)
{
	if (false == CanLoad(desc))
	{
		return nullptr;
	}

	std::ifstream file(desc.ResolvedPath, std::ios::binary);
	if (false == file.is_open())
	{
		return nullptr;
	}

	file.seekg(0, std::ios::end);
	const std::streamoff size = file.tellg();
	if (size < 0)
	{
		return nullptr;
	}

	file.seekg(0, std::ios::beg);
	std::vector<std::uint8_t> data(static_cast<std::size_t>(size));
	if (false == data.empty())
	{
		file.read(reinterpret_cast<char*>(data.data()), size);
		if (false == file.good())
		{
			return nullptr;
		}
	}

	return MakeOwnerPtr<CFileAsset>(*desc.MetaData, std::move(data));
}

void CFileAssetLoader::Unload(IAsset& asset)
{
	(void)asset;
}
