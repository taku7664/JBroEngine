#pragma once

#include "Core/Asset/AssetTypes.h"

class IAssetRegistry
{
public:
	virtual ~IAssetRegistry() = default;

public:
	virtual bool RegisterAsset(const AssetMetaData& metaData) = 0;
	virtual bool UnregisterAsset(const AssetGuid& guid) = 0;
	virtual void Clear() = 0;
	// IsPersistent == false 항목만 제거. 프로젝트 자산 갱신 시 사용.
	virtual void ClearNonPersistent() = 0;
	virtual const AssetMetaData* FindAsset(const AssetGuid& guid) const = 0;
	virtual const AssetMetaData* FindAssetByPath(const File::Path& path) const = 0;
	virtual void BuildSnapshot(AssetRegistrySnapshot& outSnapshot) const = 0;
};
