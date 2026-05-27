#pragma once

#include "Core/Asset/AssetTypes.h"
#include "Utillity/SafePtr.h"

class IAsset;

class IAssetLoader : public EnableSafeFromThis<IAssetLoader>
{
public:
	virtual ~IAssetLoader() = default;

public:
	virtual EAssetType GetSupportedType() const = 0;
	virtual bool CanLoad(const AssetLoadDesc& desc) const = 0;
	virtual OwnerPtr<IAsset> Load(const AssetLoadDesc& desc) = 0;
	virtual void Unload(IAsset& asset) = 0;
};

