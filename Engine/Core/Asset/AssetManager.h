#pragma once

#include "Core/Asset/AssetRegistry.h"
#include "Core/Asset/IAssetManager.h"
#include "Core/Asset/IAssetLoader.h"
#include "Core/Asset/IAsset.h"

class CAssetManager final : public IAssetManager
{
public:
	bool Initialize(const AssetManagerDesc& desc) override;
	void Finalize() override;

	bool RegisterLoader(OwnerPtr<IAssetLoader> loader) override;
	IAssetRegistry& GetRegistry() override;
	const IAssetRegistry& GetRegistry() const override;

	bool ImportAsset(const AssetImportDesc& desc, AssetMetaData* outMetaData = nullptr) override;
	bool LoadRegistryFromMetaFiles() override;
	bool RefreshAssetRegistry() override;
	bool SetAssetRootPath(const char* assetRootPath) override;
	const char* GetAssetRootPath() const override;
	bool ResolveAssetPath(const char* path, std::string& outResolvedPath) const override;

	SafePtr<IAsset> FindLoadedAsset(const AssetGuid& guid) const override;
	SafePtr<IAsset> LoadAsset(const AssetGuid& guid) override;
	SafePtr<IAsset> LoadAssetByPath(const char* path) override;
	SafePtr<IAsset> ReloadAsset(const AssetGuid& guid) override;
	SafePtr<IAsset> ReloadAssetByPath(const char* path) override;
	void UnloadAsset(const AssetGuid& guid) override;
	bool BuildAssetPackage(const AssetPackageBuildDesc& desc) override;
	bool LoadPackedAssetManifest(const char* manifestPath) override;

private:
	IAssetLoader* FindLoader(EAssetType type) const;
	bool RegisterMetaData(const AssetMetaData& metaData);
	AssetGuid MakeUniqueAssetGuid() const;
	void UnloadAllAssets();
	static const File::Path& ResolvePathFromActiveRegistry(const File::Guid& guid);
	static const File::Guid& ResolveGuidFromActiveRegistry(const File::Path& path);

private:
	CAssetRegistry m_registry;
	std::unordered_map<EAssetType, OwnerPtr<IAssetLoader>> m_loaderTable;
	std::unordered_map<AssetGuid, OwnerPtr<IAsset>> m_loadedAssetTable;
	std::string m_assetRootPath = "Assets";
	bool m_isInitialized = false;
};
