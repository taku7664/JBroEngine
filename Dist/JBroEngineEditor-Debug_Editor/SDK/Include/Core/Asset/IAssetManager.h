#pragma once

#include "Core/Asset/AssetTypes.h"
#include "Utillity/SafePtr.h"

class IAsset;
class IAssetLoader;
class IAssetRegistry;

class IAssetManager : public EnableSafeFromThis<IAssetManager>
{
public:
	virtual ~IAssetManager() = default;

public:
	virtual bool Initialize(const AssetManagerDesc& desc) = 0;
	virtual void Finalize() = 0;

	virtual bool RegisterLoader(OwnerPtr<IAssetLoader> loader) = 0;
	virtual IAssetRegistry& GetRegistry() = 0;
	virtual const IAssetRegistry& GetRegistry() const = 0;

	virtual bool ImportAsset(const AssetImportDesc& desc, AssetMetaData* outMetaData = nullptr) = 0;
	virtual bool LoadRegistryFromMetaFiles() = 0;
	virtual bool RefreshAssetRegistry() = 0;
	virtual bool SetAssetRootPath(const char* assetRootPath) = 0;
	virtual const char* GetAssetRootPath() const = 0;
	virtual bool ResolveAssetPath(const char* path, std::string& outResolvedPath) const = 0;

	virtual SafePtr<IAsset> FindLoadedAsset(const AssetGuid& guid) const = 0;
	virtual SafePtr<IAsset> LoadAsset(const AssetGuid& guid) = 0;
	virtual SafePtr<IAsset> LoadAssetByPath(const char* path) = 0;
	virtual SafePtr<IAsset> ReloadAsset(const AssetGuid& guid) = 0;
	virtual SafePtr<IAsset> ReloadAssetByPath(const char* path) = 0;
	virtual void UnloadAsset(const AssetGuid& guid) = 0;
	virtual bool BuildAssetPackage(const AssetPackageBuildDesc& desc) = 0;
	virtual bool LoadPackedAssetManifest(const char* manifestPath) = 0;
};
