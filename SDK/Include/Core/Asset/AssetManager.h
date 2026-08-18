#pragma once

#include "Core/Asset/AssetRegistry.h"
#include "Core/Asset/AssetPackage.h"
#include "Core/Asset/IAssetManager.h"
#include "Utillity/File/Guid128.h"
#include "Core/Asset/IAssetLoader.h"
#include "Core/Asset/IAsset.h"

#include <mutex>

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
	bool SetAssetRootPath(const File::Path& assetRootPath) override;
	const File::Path& GetAssetRootPath() const override;
	bool ResolveAssetPath(const File::Path& path, File::Path& outResolvedPath) const override;

	AssetRef<IAsset> FindLoadedAsset(const AssetGuid& guid) override;
	IAsset* FindOrLoadAssetByGuidText(const char* guidText) override;
	AssetRef<IAsset> LoadAsset(const AssetGuid& guid) override;
	AssetRef<IAsset> LoadAssetByPath(const File::Path& path) override;
	AssetRef<IAsset> ReloadAsset(const AssetGuid& guid) override;
	AssetRef<IAsset> ReloadAssetByPath(const File::Path& path) override;
	void UnloadAsset(const AssetGuid& guid) override;
	bool UnregisterAssetByPath(const File::Path& path, bool unloadIfLoaded) override;
	bool MoveAssetPath(const File::Path& oldPath, const File::Path& newPath) override;
	bool BuildAssetPackage(const AssetPackageBuildDesc& desc) override;
	bool LoadPackedAssetManifest(const File::Path& manifestPath) override;

	bool RegisterAssetByPath(const File::Path& path, EAssetType type, bool isPersistent) override;
	bool SetAssetPersistent(const AssetGuid& guid, bool isPersistent) override;
	void UnloadNonPersistentAssets() override;

	void AcquireAssetUseCount(const AssetGuid& guid) override;
	void ReleaseAssetUseCount(const AssetGuid& guid) override;

private:
	IAssetLoader* FindLoader(EAssetType type) const;
	bool RegisterMetaData(const AssetMetaData& metaData);
	OwnerPtr<IAsset> LoadAssetInternal(const AssetMetaData& metaData);
	AssetGuid MakeUniqueAssetGuid() const;
	// path 정규화 후 그것을 GUID 문자열로 사용 — 같은 path = 같은 GUID.
	// 일반 import GUID(UUID 형식)와 형태가 달라 충돌 없음.
	static AssetGuid MakePathBasedGuid(const File::Path& path);
	// 전체 unload — Finalize 에서만 사용.
	void UnloadAllAssets();
	static const File::Path& ResolvePathFromActiveRegistry(const File::Guid& guid);
	static const File::Guid& ResolveGuidFromActiveRegistry(const File::Path& path);

private:
	// 워커 스레드에서 ImportAsset/ReloadAsset 가 동시에 호출될 수 있으므로 protect.
	// ReloadAsset → UnloadAsset → LoadAsset 같은 재귀 진입을 허용하기 위해 recursive_mutex.
	mutable std::recursive_mutex m_mutex;
	CAssetRegistry m_registry;
	std::unordered_map<EAssetType, OwnerPtr<IAssetLoader>> m_loaderTable;
	// 로드된 자산 — 키는 Guid128(정수형)이다. AssetGuid(fs::path)를 키로 쓰면 조회마다
	// 비교/해시가 경로 연산이고, 무엇보다 스크립트의 Ref<Asset>::Get() 이 조회할 때마다
	// 문자열에서 키를 새로 만들어야 했다(UTF8→UTF16 트랜스코딩 + 힙 할당).
	// 원본 guid 는 값에 함께 둔다 — 레지스트리·use-count·렌더 캐시가 그걸 요구한다.
	struct LoadedAsset
	{
		AssetGuid        Guid;
		OwnerPtr<IAsset> Asset;
	};
	std::unordered_map<Guid128, LoadedAsset> m_loadedAssetTable;
	// AssetRef<T> 의 use-count. 0 보다 크면 UnloadAsset / UnloadNonPersistentAssets 에서 스킵.
	std::unordered_map<AssetGuid, std::size_t> m_useCountTable;
	OwnerPtr<CAssetPackReader> m_packReader;
	File::Path m_assetRootPath = "Assets";
	bool m_isInitialized = false;
};
