#include "pch.h"
#include "AssetManager.h"

#include "Core/Asset/AssetMetaFile.h"
#include "Core/Asset/AssetPath.h"

namespace
{
	CAssetManager* GActiveAssetManager = nullptr;
}

bool CAssetManager::Initialize(const AssetManagerDesc& desc)
{
	if (m_isInitialized)
	{
		return true;
	}

	if (nullptr != desc.AssetRootPath && '\0' != desc.AssetRootPath[0])
	{
		m_assetRootPath = std::filesystem::path(desc.AssetRootPath).generic_string();
	}

	m_isInitialized = true;
	GActiveAssetManager = this;
	File::SetPathGuidResolver(&CAssetManager::ResolvePathFromActiveRegistry, &CAssetManager::ResolveGuidFromActiveRegistry);
	LoadRegistryFromMetaFiles();
	return true;
}

void CAssetManager::Finalize()
{
	UnloadAllAssets();
	m_loaderTable.clear();
	m_registry.Clear();
	if (GActiveAssetManager == this)
	{
		GActiveAssetManager = nullptr;
		File::SetPathGuidResolver(nullptr, nullptr);
	}
	m_isInitialized = false;
}

bool CAssetManager::RegisterLoader(OwnerPtr<IAssetLoader> loader)
{
	if (!loader)
	{
		return false;
	}

	const EAssetType type = loader->GetSupportedType();
	if (EAssetType::Unknown == type || m_loaderTable.contains(type))
	{
		return false;
	}

	m_loaderTable.emplace(type, std::move(loader));
	return true;
}

IAssetRegistry& CAssetManager::GetRegistry()
{
	return m_registry;
}

const IAssetRegistry& CAssetManager::GetRegistry() const
{
	return m_registry;
}

bool CAssetManager::ImportAsset(const AssetImportDesc& desc, AssetMetaData* outMetaData)
{
#if JBRO_PLATFORM_WEB
	(void)desc;
	(void)outMetaData;
	return false;
#else
	std::string normalizedPath;
	if (EAssetType::Unknown == desc.Type || false == CAssetPath::NormalizeRelativePath(desc.Path, normalizedPath))
	{
		return false;
	}

	if (CAssetPath::IsMetaPath(normalizedPath.c_str()))
	{
		return false;
	}

	if (const AssetMetaData* registeredMetaData = m_registry.FindAssetByPath(normalizedPath.c_str()))
	{
		if (nullptr != outMetaData)
		{
			*outMetaData = *registeredMetaData;
		}
		return true;
	}

	AssetMetaData metaData;
	std::string resolvedMetaPath;
	const std::string metaPath = CAssetPath::MakeMetaPath(normalizedPath.c_str());
	const bool hasResolvedMetaPath = ResolveAssetPath(metaPath.c_str(), resolvedMetaPath);
	if (hasResolvedMetaPath && CAssetMetaFile::Load(resolvedMetaPath.c_str(), metaData))
	{
		metaData.Path = File::Path(normalizedPath);
		metaData.MetaPath = File::Path(metaPath);
		if (EAssetType::Unknown == metaData.Type)
		{
			metaData.Type = desc.Type;
		}
	}
	else
	{
		metaData.Guid = MakeUniqueAssetGuid();
		metaData.Type = desc.Type;
		metaData.Version = 1;
		metaData.Path = File::Path(normalizedPath);
		metaData.MetaPath = File::Path(metaPath);
		metaData.DisplayName = nullptr != desc.DisplayName ? desc.DisplayName : CAssetPath::GetDisplayNameFromPath(normalizedPath.c_str());
		metaData.Importer = nullptr != desc.Importer ? desc.Importer : "Default";
	}

	if (metaData.Guid.IsNull() || EAssetType::Unknown == metaData.Type)
	{
		return false;
	}

	if (metaData.DisplayName.empty())
	{
		metaData.DisplayName = CAssetPath::GetDisplayNameFromPath(normalizedPath.c_str());
	}

	if (metaData.Importer.empty())
	{
		metaData.Importer = nullptr != desc.Importer ? desc.Importer : "Default";
	}

	if (false == RegisterMetaData(metaData))
	{
		return false;
	}

	if (hasResolvedMetaPath)
	{
		CAssetMetaFile::Save(resolvedMetaPath.c_str(), metaData);
	}

	if (nullptr != outMetaData)
	{
		*outMetaData = metaData;
	}
	return true;
#endif
}

bool CAssetManager::LoadRegistryFromMetaFiles()
{
#if JBRO_PLATFORM_WEB
	return true;
#else
	m_registry.Clear();

	std::error_code errorCode;
	const std::filesystem::path rootPath(m_assetRootPath);
	if (false == std::filesystem::exists(rootPath, errorCode))
	{
		return true;
	}

	if (false == std::filesystem::is_directory(rootPath, errorCode))
	{
		return false;
	}

	for (const std::filesystem::directory_entry& entry : std::filesystem::recursive_directory_iterator(rootPath, errorCode))
	{
		if (errorCode)
		{
			return false;
		}

		if (false == entry.is_regular_file(errorCode))
		{
			continue;
		}

		const std::filesystem::path filePath = entry.path();
		std::filesystem::path relativePath = std::filesystem::relative(filePath, rootPath, errorCode);
		if (errorCode)
		{
			errorCode.clear();
			continue;
		}

		std::string relativeMetaPath = relativePath.generic_string();
		std::string normalizedMetaPath;
		if (false == CAssetPath::NormalizeRelativePath(relativeMetaPath.c_str(), normalizedMetaPath))
		{
			continue;
		}

		if (false == CAssetPath::IsMetaPath(normalizedMetaPath.c_str()))
		{
			continue;
		}

		AssetMetaData metaData;
		if (false == CAssetMetaFile::Load(filePath.string().c_str(), metaData))
		{
			continue;
		}

		std::string assetPath = CAssetPath::StripMetaExtension(normalizedMetaPath.c_str());
		metaData.Path = File::Path(assetPath);
		metaData.MetaPath = File::Path(CAssetPath::MakeMetaPath(assetPath.c_str()));
		RegisterMetaData(metaData);
	}

	return true;
#endif
}

bool CAssetManager::RefreshAssetRegistry()
{
	UnloadAllAssets();
	return LoadRegistryFromMetaFiles();
}

bool CAssetManager::SetAssetRootPath(const char* assetRootPath)
{
	if (nullptr == assetRootPath || '\0' == assetRootPath[0])
	{
		return false;
	}

	m_assetRootPath = std::filesystem::path(assetRootPath).generic_string();
	return RefreshAssetRegistry();
}

const char* CAssetManager::GetAssetRootPath() const
{
	return m_assetRootPath.c_str();
}

bool CAssetManager::ResolveAssetPath(const char* path, std::string& outResolvedPath) const
{
	std::string normalizedPath;
	if (false == CAssetPath::NormalizeRelativePath(path, normalizedPath))
	{
		return false;
	}

	std::filesystem::path resolvedPath(m_assetRootPath);
	resolvedPath /= std::filesystem::path(normalizedPath);
	outResolvedPath = resolvedPath.generic_string();
	return true;
}

SafePtr<IAsset> CAssetManager::FindLoadedAsset(const AssetGuid& guid) const
{
	auto it = m_loadedAssetTable.find(guid);
	if (it == m_loadedAssetTable.end())
	{
		return nullptr;
	}

	return it->second.GetSafePtr();
}

SafePtr<IAsset> CAssetManager::LoadAsset(const AssetGuid& guid)
{
	if (SafePtr<IAsset> loadedAsset = FindLoadedAsset(guid))
	{
		return loadedAsset;
	}

	const AssetMetaData* metaData = m_registry.FindAsset(guid);
	if (nullptr == metaData)
	{
		return nullptr;
	}

	IAssetLoader* loader = FindLoader(metaData->Type);
	if (nullptr == loader)
	{
		return nullptr;
	}

	AssetLoadDesc desc;
	desc.Guid = metaData->Guid;
	desc.Type = metaData->Type;
	const std::string pathString = metaData->Path.generic_string();
	desc.Path = pathString.c_str();
	desc.MetaData = metaData;

	std::string resolvedPath;
	if (false == ResolveAssetPath(metaData->Path.generic_string().c_str(), resolvedPath))
	{
		return nullptr;
	}
	desc.ResolvedPath = resolvedPath.c_str();

	if (!loader->CanLoad(desc))
	{
		return nullptr;
	}

	OwnerPtr<IAsset> asset = loader->Load(desc);
	if (!asset)
	{
		return nullptr;
	}

	SafePtr<IAsset> safeAsset = asset.GetSafePtr();
	m_loadedAssetTable.emplace(guid, std::move(asset));
	return safeAsset;
}

SafePtr<IAsset> CAssetManager::LoadAssetByPath(const char* path)
{
	const AssetMetaData* metaData = m_registry.FindAssetByPath(path);
	if (nullptr == metaData)
	{
		return nullptr;
	}

	return LoadAsset(metaData->Guid);
}

SafePtr<IAsset> CAssetManager::ReloadAsset(const AssetGuid& guid)
{
	UnloadAsset(guid);
	return LoadAsset(guid);
}

SafePtr<IAsset> CAssetManager::ReloadAssetByPath(const char* path)
{
	const AssetMetaData* metaData = m_registry.FindAssetByPath(path);
	if (nullptr == metaData)
	{
		return nullptr;
	}

	return ReloadAsset(metaData->Guid);
}

void CAssetManager::UnloadAsset(const AssetGuid& guid)
{
	auto assetIt = m_loadedAssetTable.find(guid);
	if (assetIt == m_loadedAssetTable.end())
	{
		return;
	}

	if (IAssetLoader* loader = FindLoader(assetIt->second->GetAssetType()))
	{
		loader->Unload(*assetIt->second);
	}

	m_loadedAssetTable.erase(assetIt);
}

bool CAssetManager::BuildAssetPackage(const AssetPackageBuildDesc& desc)
{
#if JBRO_PLATFORM_WEB
	(void)desc;
	return false;
#else
	if (nullptr == desc.OutputManifestPath || '\0' == desc.OutputManifestPath[0])
	{
		return false;
	}

	AssetRegistrySnapshot snapshot;
	m_registry.BuildSnapshot(snapshot);

	std::ofstream manifest(desc.OutputManifestPath, std::ios::binary);
	if (false == manifest.is_open())
	{
		return false;
	}

	manifest << "version: 1\n";
	manifest << "assets:\n";
	for (const AssetMetaData& metaData : snapshot.Assets)
	{
		manifest << "  - guid: " << metaData.Guid.generic_string() << "\n";
		manifest << "    type: " << static_cast<int>(metaData.Type) << "\n";
		manifest << "    path: " << metaData.Path.generic_string() << "\n";
	}

	(void)desc.OutputBlobPath;
	return true;
#endif
}

bool CAssetManager::LoadPackedAssetManifest(const char* manifestPath)
{
	(void)manifestPath;
	return true;
}

IAssetLoader* CAssetManager::FindLoader(EAssetType type) const
{
	auto loaderIt = m_loaderTable.find(type);
	if (loaderIt == m_loaderTable.end())
	{
		return nullptr;
	}

	return loaderIt->second.Get();
}

bool CAssetManager::RegisterMetaData(const AssetMetaData& metaData)
{
	if (metaData.Guid.IsNull())
	{
		return false;
	}

	if (nullptr != m_registry.FindAsset(metaData.Guid) || nullptr != m_registry.FindAssetByPath(metaData.Path.generic_string().c_str()))
	{
		return false;
	}

	return m_registry.RegisterAsset(metaData);
}

AssetGuid CAssetManager::MakeUniqueAssetGuid() const
{
	while (true)
	{
		const AssetGuid guid = CAssetPath::GenerateAssetGuid();
		if (nullptr == m_registry.FindAsset(guid))
		{
			return guid;
		}
	}
}

void CAssetManager::UnloadAllAssets()
{
	for (auto& assetPair : m_loadedAssetTable)
	{
		if (IAssetLoader* loader = FindLoader(assetPair.second->GetAssetType()))
		{
			loader->Unload(*assetPair.second);
		}
	}

	m_loadedAssetTable.clear();
}

const File::Path& CAssetManager::ResolvePathFromActiveRegistry(const File::Guid& guid)
{
	if (nullptr == GActiveAssetManager)
	{
		return File::NULL_PATH;
	}

	const AssetMetaData* metaData = GActiveAssetManager->m_registry.FindAsset(guid);
	return metaData ? metaData->Path : File::NULL_PATH;
}

const File::Guid& CAssetManager::ResolveGuidFromActiveRegistry(const File::Path& path)
{
	if (nullptr == GActiveAssetManager)
	{
		return File::NULL_GUID;
	}

	const AssetMetaData* metaData = GActiveAssetManager->m_registry.FindAssetByPath(path.generic_string().c_str());
	return metaData ? metaData->Guid : File::NULL_GUID;
}
