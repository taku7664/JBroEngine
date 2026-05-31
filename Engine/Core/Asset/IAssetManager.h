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
	virtual bool SetAssetRootPath(const File::Path& assetRootPath) = 0;
	virtual const File::Path& GetAssetRootPath() const = 0;
	virtual bool ResolveAssetPath(const File::Path& path, File::Path& outResolvedPath) const = 0;

	virtual SafePtr<IAsset> FindLoadedAsset(const AssetGuid& guid) const = 0;
	virtual SafePtr<IAsset> LoadAsset(const AssetGuid& guid) = 0;
	virtual SafePtr<IAsset> LoadAssetByPath(const File::Path& path) = 0;
	virtual SafePtr<IAsset> ReloadAsset(const AssetGuid& guid) = 0;

	// ── path-only 등록 (.Jmeta 없이) ──────────────────────────────────────────
	// 자산을 path + type 만으로 in-memory registry 에 등록한다. .Jmeta 디스크 저장 X.
	// GUID 는 path 기반 deterministic — 같은 path = 같은 GUID, 재실행/재등록 시에도 안정.
	// isPersistent 는 라이프사이클 플래그(아래 SetAssetPersistent 와 직교).
	virtual bool RegisterAssetByPath(const File::Path& path, EAssetType type, bool isPersistent) = 0;

	// ── Persistent 플래그 토글 (라이프사이클 제어) ──────────────────────────
	// 이미 등록된 자산의 IsPersistent 를 켜거나 끈다.
	// 등록 방식(.Jmeta 유무) 과는 무관 — 일반 import 자산도 persistent 로 표시할 수 있다.
	virtual bool SetAssetPersistent(const AssetGuid& guid, bool isPersistent) = 0;

	// IsPersistent == false 인 자산만 unload + registry 에서 제거.
	// 프로젝트 닫힘 등 "한 작업 단위 끝" 시점에 호출. Persistent 자산은 보존된다.
	virtual void UnloadNonPersistentAssets() = 0;
	virtual SafePtr<IAsset> ReloadAssetByPath(const File::Path& path) = 0;
	virtual void UnloadAsset(const AssetGuid& guid) = 0;
	virtual bool UnregisterAssetByPath(const File::Path& path, bool unloadIfLoaded) = 0;

	// ── 자산 이동/이름변경 — 등록 경로만 교체 ────────────────────────────────
	// oldPath 로 등록된 자산의 경로를 newPath 로 갱신한다. GUID 와 이미 메모리에
	// 로드된 에셋 데이터(GPU 텍스처/오디오 버퍼 등)는 그대로 유지되므로, 그 GUID 를
	// 참조하던 스프라이트/오디오 등 라이브 핸들이 끊기지 않는다. (unload + reimport
	// 가 아니라 in-place 경로 교체.)  newPath 에 이미 다른 자산이 있으면 실패한다.
	virtual bool MoveAssetPath(const File::Path& oldPath, const File::Path& newPath) = 0;
	virtual bool BuildAssetPackage(const AssetPackageBuildDesc& desc) = 0;
	virtual bool LoadPackedAssetManifest(const File::Path& manifestPath) = 0;
};
